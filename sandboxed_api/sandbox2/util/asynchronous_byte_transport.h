// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SANDBOXED_API_SANDBOX2_UTIL_ASYNCHRONOUS_BYTE_TRANSPORT_H_
#define SANDBOXED_API_SANDBOX2_UTIL_ASYNCHRONOUS_BYTE_TRANSPORT_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/buffer.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {

// AsynchronousByteTransport implements an efficient, low-latency IPC byte
// stream mechanism between two endpoints (e.g., a sandboxed process and its
// supervisor) using a shared memory buffer and futex-based synchronization.
//
// IMPORTANT: This class is not thread-safe. Concurrent read/write operations
// without external synchronization will result in undefined behavior and data
// corruption. The only thread-safe method is `Terminate()`, which may be
// invoked concurrently from a separate thread to unblock any pending I/O
// operations.
//
// Example Usage:
// ```cpp
// // 1. Create a shared buffer
// SAPI_ASSERT_OK_AND_ASSIGN(auto buffer, sandbox2::Buffer::CreateWithSize(64 <<
// 10));
//
// // 2. Create the host side transport
// SAPI_ASSERT_OK_AND_ASSIGN(auto host_transport,
//                           AsynchronousByteTransport::CreateHostSide(std::move(buffer)));
//
// // 3. Create the sandboxee side transport (typically in a separate process)
// SAPI_ASSERT_OK_AND_ASSIGN(
//     auto sandboxee_transport,
//     AsynchronousByteTransport::CreateSandboxeeSide(std::move(sandboxee_buffer)));
//
// // 4. Send and receive data
// // Host process:
// absl::Status status = host_transport->Send(data_payload);
//
// // Sandboxee process:
// absl::Status status =
// sandboxee_transport->Recv(absl::MakeSpan(receive_buffer));
// ```
class AsynchronousByteTransport {
 public:
  enum SynchronizationType {
    kInvalid,
    kFutex,
  };

  AsynchronousByteTransport(AsynchronousByteTransport&&) = default;
  AsynchronousByteTransport& operator=(AsynchronousByteTransport&&) = default;

  // Creates the host side of the transport. There should only be one host side
  // and one sandboxee side. If `synchronization_type` is not specified,
  // `kFutex` will be used as the default synchronization type. If the buffer is
  // too small, returns an error.
  static absl::StatusOr<std::unique_ptr<AsynchronousByteTransport>>
  CreateHostSide(std::unique_ptr<sandbox2::Buffer> buffer,
                 SynchronizationType synchronization_type = kFutex);

  // Creates the sandboxee side of the transport. There should only be one host
  // side and one sandboxee side. If the buffer is too small, returns an error.
  static absl::StatusOr<std::unique_ptr<AsynchronousByteTransport>>
  CreateSandboxeeSide(std::unique_ptr<sandbox2::Buffer> buffer);

  // Sends data to the transport. This call might block until there is enough
  // space in the transport or the transport is terminated.
  absl::Status Send(absl::Span<const uint8_t> data) {
    return Write(data, /*will_then_read=*/false);
  }

  // Receives data from the transport. This call might block until enough
  // data is available or the transport is terminated.
  absl::Status Recv(absl::Span<uint8_t> data) { return Read(data); }

  // Sends data to the transport and then receives data from the transport.
  // This call might block until there is enough space in the transport to send
  // the data, then until enough data is available to receive, or the transport
  // is terminated.
  absl::Status Exchange(absl::Span<const uint8_t> data_to_send,
                        absl::Span<uint8_t> data_to_recv) {
    SAPI_RETURN_IF_ERROR(Write(data_to_send, /*will_then_read=*/true));
    return Read(data_to_recv);
  }

  // Marks the transport as terminated. This will cause any pending read/write
  // operations to return with an error. This is the only operation that can be
  // called concurrently with other operations.
  void Terminate();

 private:
  static constexpr size_t kUsableDataSize = (1ULL << 29) - 1;
  static constexpr uint32_t kWaitForReadingBit = 1 << 31;
  static constexpr uint32_t kWaitForWritingBit = 1 << 30;
  static constexpr uint32_t kConnectionClosedBit = 1 << 29;
  static constexpr uint32_t kWriteIndexMask =
      ~(kWaitForReadingBit | kWaitForWritingBit | kConnectionClosedBit);

  static_assert(std::atomic<uint32_t>::is_always_lock_free,
                "std::atomic<uint32_t> should always be lock-free");
  struct ChannelHeader {
    std::atomic<uint32_t> state;  // Also our futex.
  };

  struct Header {
    SynchronizationType synchronization_type;
    uint32_t sandboxee_read_index;
    ChannelHeader h2s;  // Host to Sandboxee
    ChannelHeader s2h;  // Sandboxee to Host
    // Must be last.
    uint8_t data[];
  };

  enum class ClientType { kSandboxee, kHost };

  AsynchronousByteTransport(std::unique_ptr<sandbox2::Buffer> buffer,
                            ClientType client_type,
                            SynchronizationType synchronization_type);

  const Header* GetHeader() const;

  Header* GetHeader();

  size_t GetDataSize() const;

  absl::Status Write(absl::Span<const uint8_t> data,
                     bool will_then_read = false);

  absl::Status Read(absl::Span<uint8_t> data);

  bool ResetWriteIndex(ChannelHeader* channel, uint32_t state);
  bool UpdateWriteIndex(ChannelHeader* channel, uint32_t state,
                        uint32_t new_write_index, bool will_then_read);

  void WaitForReading(ChannelHeader* channel, uint32_t state);

  void WaitForWriting(ChannelHeader* channel, uint32_t state);

  int WaitInternal(ChannelHeader* channel, uint32_t state);

  void WakeInternal(ChannelHeader* channel, uint32_t count = 1);

  int TransferInternal(ChannelHeader* write_channel,
                       ChannelHeader* read_channel);

  uint32_t& GetReadIndex() {
    return client_type_ == ClientType::kHost
               ? read_index_
               : GetHeader()->sandboxee_read_index;
  }

  std::unique_ptr<sandbox2::Buffer> buffer_;
  ClientType client_type_;
  uint32_t read_index_ = 0;
  SynchronizationType synchronization_type_;
  ChannelHeader* write_channel_;
  absl::Span<uint8_t> write_data_;
  ChannelHeader* read_channel_;
  absl::Span<const uint8_t> read_data_;

  constexpr static uint32_t kWaitingOnChannel = 1 << 0;
  std::atomic<uint32_t> termination_state_ = 0;

  // If the last data was read after the connection was closed, we can
  // immediately return an error on a new read.
  bool last_data_was_read_ = false;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_ASYNCHRONOUS_BYTE_TRANSPORT_H_
