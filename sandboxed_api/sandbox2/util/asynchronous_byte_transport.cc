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
//
//  +---------------------------------------------------------------+
//  |                ASYNCHRONOUS BYTE TRANSPORT                    |
//  |                                                               |
//  |   +-----------+         Shared Memory         +-----------+   |
//  |   |           | <===========================> |           |   |
//  |   |   HOST    |   +-----------------------+   | SANDBOXEE |   |
//  |   |           |   |  Header (H2S, S2H)    |   |           |   |
//  |   +-----------+   +-----------------------+   +-----------+   |
//  |                   |   H2S Data Payload    |                   |
//  |                   +-----------------------+                   |
//  |                   |   S2H Data Payload    |                   |
//  |                   +-----------------------+                   |
//  +---------------------------------------------------------------+
//
// AsynchronousByteTransport implements a high-performance, full-duplex IPC
// transport using shared memory between two endpoints (Host and Sandboxee).
// This class is mostly meant to be used in a cross-process scenario. Note that
// the class is not thread-safe (and using it across threads can lead to
// inconsistent state), except for `Terminate()` which can be called
// concurrently. This IPC mechanism does not work as a ring buffer, which has
// the advantage of using only a few pages and lowers the amount of used memory.
// The reader is responsible for resetting the writing index, which often
// happens after reading all the data from its channel, but might also happen
// after a partial read. In order for this implementation to be full-duplex,
// the shared memory is divided into two equal halves, one for each direction.
// This means that both endpoints can send and receive data at the same time.
// This transport does not have limitations on the size of the payload, as it
// keeps writing to the shared memory until the peer has read the data.
//
// 1. Structure of the Shared Memory Region
// ----------------------------------------
// The transport operates on a contiguous memory buffer which maps to a
// `Header` followed by a raw byte buffer `data[]` divided into two equal
// halves. The `Header` contains:
// - `synchronization_type`: Enum declaring the synchronization mechanism
//   (Futex or FutexWithSwap).
// - `h2s`: A `ChannelHeader` for Host to Sandboxee communication.
// - `s2h`: A `ChannelHeader` for Sandboxee to Host communication.
// - `data[]`: The usable buffer space for payload serialization. Divided
// equally into H2S and S2H data buffers.
//
// Each `ChannelHeader` contains:
// - `state`: A 32-bit atomic field containing both control bits and the current
//   write index. Serves as the futex for the channel.
//
// 2. Technical Details of Synchronization
// ----------------------------------------
// Synchronization is driven by atomic operations on the `state` variable of
// each channel.
//
// State Bitmask Layout:
// - Bits 0-28: The Write Index (Mask: `kWriteIndexMask`). Denotes the end of
//   unread data in the channel.
// - Bit 29: `kConnectionClosedBit`. Set when `Terminate()` is called to cease
//   I/O.
// - Bit 30: `kWaitForWritingBit`. Set by a sender when attempting to Write but
//   missing buffer space.
// - Bit 31: `kWaitForReadingBit`. Set by a receiver when attempting to Read but
//   lacking unread bytes.
//
// Waiting and Signalling Realization:
// The system encapsulates sleep and wakeup sequences depending on
// `SynchronizationType`:
// - Futex Mode: Standard Linux `FUTEX_WAIT` blocks caller on `state`.
//   `FUTEX_WAKE` wakes the peer.
//
// Given this class might be used in an unstrusted sandboxee scenario, a
// `termination_state_` is added to make sure that once a side terminates the
// connection, no further I/O is possible on that side. The
// `kConnectionClosedBit` in the channel state is used to indicate that the
// connection is closed on the other side. Of course, in the event of an
// attacker controlled sandboxee, this might not be respected, but this is
// already handled by our monitoring logic and the sandboxee should be killed
// eventually. `termination_state_` mostly ensures that the host will terminate
// the connection if `Terminate()` is called.
//
// 3. Security Considerations
// ----------------------------------------
// The security considerations are as follows. Note that we are comparing to the
// only other IPC mechanism in sandbox2, which is using a socket:
// - Denial Of Service (DoS): An untrusted peer can either continually signal
//   futexes or leave the counterpart waiting indefinitely. However, the
//   Terminate function can be called to unblock any pending operations with an
//   error status. Besides, DoS is also possible with the socket based IPC, by
//   keeping the connection open and never writing back from the socket.
// - Crash Immunity: A malicious endpoint cannot trigger crashes, out-of-bounds
//   reads/writes, or memory corruption within the caller because all access to
//   the shared memory is bounds-checked and operated with local copies of the
//   data contained in the shared memory. This also means that TOCTOU attacks
//   are not possible.
// - Misc: An untrusted peer can write arbitrary data to the shared memory,
//   which means that the host should never trust the data coming from the
//   sandboxee, but this is also true for the socket based IPC, as an attacker
//   can send arbitrary data over the socket as well.

#include "sandboxed_api/sandbox2/util/asynchronous_byte_transport.h"

#include <linux/futex.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "sandboxed_api/sandbox2/buffer.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

namespace {

int FutexWait(std::atomic<uint32_t>* futex, uint32_t val) {
  return TEMP_FAILURE_RETRY(util::Syscall(
      __NR_futex, reinterpret_cast<intptr_t>(futex), FUTEX_WAIT, val, 0));
}
int FutexWake(std::atomic<uint32_t>* futex, uint32_t count) {
  return TEMP_FAILURE_RETRY(util::Syscall(__NR_futex,
                                          reinterpret_cast<intptr_t>(futex),
                                          FUTEX_WAKE, count, 0, 0, 0));
}

// 8 KiB chunk size. This is an arbitrary value that is used to split the
// payload into smaller chunks for easier and faster synchronization.
static constexpr size_t kChunkSize = 8 << 10;

static constexpr uint32_t kWakeAllCount = std::numeric_limits<uint32_t>::max();

}  // namespace

absl::StatusOr<std::unique_ptr<AsynchronousByteTransport>>
AsynchronousByteTransport::CreateHostSide(
    std::unique_ptr<sandbox2::Buffer> buffer,
    SynchronizationType synchronization_type) {
  // No chance we can send and receive in this shared memory, so fail early.
  if (buffer->size() < sizeof(Header) + kChunkSize * 2) {
    return absl::InvalidArgumentError("Buffer is too small");
  }

  std::unique_ptr<AsynchronousByteTransport> transport =
      absl::WrapUnique(new AsynchronousByteTransport(
          std::move(buffer), ClientType::kHost, synchronization_type));
  return transport;
}

absl::StatusOr<std::unique_ptr<AsynchronousByteTransport>>
AsynchronousByteTransport::CreateSandboxeeSide(
    std::unique_ptr<sandbox2::Buffer> buffer) {
  // No chance we can send and receive in this shared memory, so fail early.
  if (buffer->size() < sizeof(Header) + kChunkSize * 2) {
    return absl::InvalidArgumentError("Buffer is too small");
  }

  return absl::WrapUnique(
      new AsynchronousByteTransport(std::move(buffer), ClientType::kSandboxee,
                                    SynchronizationType::kInvalid));
}

AsynchronousByteTransport::AsynchronousByteTransport(
    std::unique_ptr<sandbox2::Buffer> buffer, ClientType client_type,
    SynchronizationType synchronization_type)
    : buffer_(std::move(buffer)),
      client_type_(client_type),
      synchronization_type_(client_type_ == ClientType::kHost
                                ? synchronization_type
                                : GetHeader()->synchronization_type),
      write_channel_(client_type_ == ClientType::kHost ? &GetHeader()->h2s
                                                       : &GetHeader()->s2h),
      write_data_(client_type_ == ClientType::kHost
                      ? absl::MakeSpan(GetHeader()->data, GetDataSize())
                      : absl::MakeSpan(GetHeader()->data + GetDataSize(),
                                       GetDataSize())),
      read_channel_(client_type_ == ClientType::kHost ? &GetHeader()->s2h
                                                      : &GetHeader()->h2s),
      read_data_(
          client_type_ == ClientType::kHost
              ? absl::MakeSpan(GetHeader()->data + GetDataSize(), GetDataSize())
              : absl::MakeSpan(GetHeader()->data, GetDataSize())) {
  if (client_type_ == ClientType::kHost) {
    GetHeader()->h2s.state.store(0, std::memory_order_release);
    GetHeader()->s2h.state.store(0, std::memory_order_release);
    GetHeader()->synchronization_type = synchronization_type;
    GetHeader()->sandboxee_read_index = 0;
  }
}

const AsynchronousByteTransport::Header* AsynchronousByteTransport::GetHeader()
    const {
  return reinterpret_cast<const Header*>(buffer_->data());
}

AsynchronousByteTransport::Header* AsynchronousByteTransport::GetHeader() {
  return reinterpret_cast<Header*>(buffer_->data());
}

size_t AsynchronousByteTransport::GetDataSize() const {
  return std::min(kUsableDataSize, (buffer_->size() - sizeof(Header)) / 2);
}

absl::Status AsynchronousByteTransport::Write(absl::Span<const uint8_t> data,
                                              bool will_then_read) {
  ChannelHeader* channel = write_channel_;
  uint8_t* data_ptr = write_data_.data();

  while (!data.empty()) {
    absl::Span<const uint8_t> chunk = data.subspan(0, kChunkSize);
    uint32_t state = channel->state.load(std::memory_order_acquire);
    uint32_t termination_state =
        termination_state_.load(std::memory_order_relaxed);
    if (termination_state & kConnectionClosedBit ||
        state & kConnectionClosedBit) {
      return absl::AbortedError("Connection closed");
    }
    uint32_t write_index = state & kWriteIndexMask;

    if (write_index > GetDataSize()) {
      return absl::AbortedError("Write index out of bounds");
    }

    if (chunk.size() > GetDataSize() - write_index) {
      WaitForWriting(channel, state);
      continue;
    }

    memcpy(data_ptr + write_index, chunk.data(), chunk.size());

    if (!UpdateWriteIndex(channel, state, write_index + chunk.size(),
                          will_then_read)) {
      continue;
    }

    data.remove_prefix(chunk.size());
  }

  return absl::OkStatus();
}

absl::Status AsynchronousByteTransport::Read(absl::Span<uint8_t> data) {
  ChannelHeader* channel = read_channel_;

  if (last_data_was_read_) {
    return absl::AbortedError("Connection closed");
  }

  uint32_t state;
  while (!data.empty()) {
    state = channel->state.load(std::memory_order_acquire);
    uint32_t write_index = state & kWriteIndexMask;

    if (write_index > read_data_.size()) {
      return absl::AbortedError("Write index out of bounds");
    }

    uint32_t& read_index = GetReadIndex();
    if (read_index > write_index) {
      return absl::AbortedError("Read index out of bounds");
    }

    absl::Span<const uint8_t> read_buffer =
        read_data_.subspan(read_index, write_index);
    if (read_buffer.empty()) {
      uint32_t termination_state =
          termination_state_.load(std::memory_order_relaxed);
      if (termination_state & kConnectionClosedBit ||
          state & kConnectionClosedBit) {
        return absl::AbortedError("Connection closed");
      }
      WaitForReading(channel, state);
      continue;
    }

    size_t chunk_size =
        std::min(kChunkSize, static_cast<size_t>(write_index - read_index));
    absl::Span<uint8_t> chunk = data.subspan(0, chunk_size);

    memcpy(chunk.data(), read_buffer.data(), chunk.size());
    read_index += chunk.size();
    data.remove_prefix(chunk.size());

    if (read_index == write_index) {
      // If the connection is closed, we will not read any more data at this
      // point, but we will still acknowledge the last bit of data that was sent
      // from the other side. Note that in the event of an attacker controlled
      // client, we will read from read_index up to the size of the buffer
      // until we acknowledge the connection is closed. This is needed so that
      // we can support legitimate scenarios where the client sends size_of_buf
      // - 1 bytes and then closes the connection. In this case, the host still
      // needs to read the last chunk of data.
      if (state & kConnectionClosedBit ||
          termination_state_.load(std::memory_order_relaxed) &
              kConnectionClosedBit) {
        last_data_was_read_ = true;
      }
      ResetWriteIndex(channel, state);
    }
  }

  return absl::OkStatus();
}

bool AsynchronousByteTransport::ResetWriteIndex(ChannelHeader* channel,
                                                uint32_t state) {
  // Keeping the connection closed bit is necessary because this implementation
  // keeps reading the shared memory buffer even after the connection is closed.
  // Once we reach the end of the buffer, we acknowledge the connection is
  // closed.
  uint32_t new_state = (state & (kConnectionClosedBit));
  uint32_t state_copy = state;
  if (!channel->state.compare_exchange_strong(state_copy, new_state,
                                              std::memory_order_release)) {
    // It is necessary to keep this second CAS here because he do not want to
    // miss the window where the writer has written and is waiting for writing
    // again. Otherwise, we might just never reset the write index and enter a
    // deadlock.
    state |= (state_copy & (kWaitForWritingBit | kConnectionClosedBit));
    if (state != state_copy ||
        !channel->state.compare_exchange_strong(state, new_state,
                                                std::memory_order_release)) {
      return false;
    }
  }
  if (state & kWaitForWritingBit) {
    WakeInternal(channel);
  }
  uint32_t& read_index = GetReadIndex();
  read_index = 0;
  return true;
}

bool AsynchronousByteTransport::UpdateWriteIndex(ChannelHeader* channel,
                                                 uint32_t state,
                                                 uint32_t new_write_index,
                                                 bool will_then_read) {
  // In this function, we do not need to keep the connection closed bit because
  // a failure in updating the write index means we will try again and
  // eventually acknowledge the connection is closed.
  uint32_t new_state = new_write_index;
  uint32_t state_copy = state;
  if (!channel->state.compare_exchange_strong(state_copy, new_state,
                                              std::memory_order_release)) {
    // This second CAS is not necessary, but it is a nice performance
    // optimization that avoids us from having to memcpy the current data chunk
    // to the same memory location only because the reader started to wait for
    // reading.
    state |= kWaitForReadingBit;
    if (state != state_copy ||
        !channel->state.compare_exchange_strong(state, new_state,
                                                std::memory_order_release)) {
      return false;
    }
  }
  if (state & kWaitForReadingBit) {
    if (will_then_read) {
      TransferInternal(channel, read_channel_);
      return true;
    }
    WakeInternal(channel);
  }
  return true;
}

void AsynchronousByteTransport::WaitForReading(ChannelHeader* channel,
                                               uint32_t state) {
  if (channel->state.compare_exchange_strong(state, state | kWaitForReadingBit,
                                             std::memory_order_relaxed)) {
    state |= kWaitForReadingBit;
    WaitInternal(channel, state);
  }
}

void AsynchronousByteTransport::WaitForWriting(ChannelHeader* channel,
                                               uint32_t state) {
  if (channel->state.compare_exchange_strong(state, state | kWaitForWritingBit,
                                             std::memory_order_relaxed)) {
    WaitInternal(channel, state | kWaitForWritingBit);
  }
}

int AsynchronousByteTransport::WaitInternal(ChannelHeader* channel,
                                            uint32_t state) {
  uint32_t expected_termination_state = 0;
  if (!termination_state_.compare_exchange_strong(expected_termination_state,
                                                  kWaitingOnChannel,
                                                  std::memory_order_release)) {
    return -1;
  }
  uint32_t termination_state =
      termination_state_.fetch_or(kWaitingOnChannel, std::memory_order_relaxed);
  if (termination_state & kConnectionClosedBit) {
    termination_state_.fetch_and(~kWaitingOnChannel, std::memory_order_relaxed);
    return -1;
  }
  int res = FutexWait(&channel->state, state);
  SAPI_RAW_PCHECK(res != -1 || errno == EAGAIN,
                  "FutexWait failed with unrecoverable error");
  termination_state_.fetch_and(~kWaitingOnChannel, std::memory_order_relaxed);
  return res;
}

void AsynchronousByteTransport::WakeInternal(ChannelHeader* channel,
                                             uint32_t count) {
  SAPI_RAW_PCHECK(FutexWake(&channel->state, count) != -1,
                  "FutexWake failed with unrecoverable error");
}

int AsynchronousByteTransport::TransferInternal(ChannelHeader* write_channel,
                                                ChannelHeader* read_channel) {
  uint32_t read_state = GetReadIndex();
  // If the counterpart has already written data and is waiting for reading,
  // we just need to wake them up and get back to reading.
  if (!read_channel->state.compare_exchange_strong(
          read_state, read_state | kWaitForReadingBit,
          std::memory_order_relaxed)) {
    WakeInternal(write_channel);
    return 0;
  }

  read_state |= kWaitForReadingBit;

  WakeInternal(write_channel);
  return WaitInternal(read_channel, read_state);
}

void AsynchronousByteTransport::Terminate() {
  write_channel_->state.fetch_or(kConnectionClosedBit,
                                 std::memory_order_relaxed);
  read_channel_->state.fetch_or(kConnectionClosedBit,
                                std::memory_order_relaxed);
  termination_state_.fetch_or(kConnectionClosedBit, std::memory_order_relaxed);

  WakeInternal(write_channel_, kWakeAllCount);
  WakeInternal(read_channel_, kWakeAllCount);

  // It's fine to wait indefinitely, because we know that the other side will
  // eventually stop waiting and proceed, as the connection is now closed.
  while (termination_state_.load(std::memory_order_relaxed) &
         kWaitingOnChannel) {
    WakeInternal(write_channel_, kWakeAllCount);
    WakeInternal(read_channel_, kWakeAllCount);
    absl::SleepFor(absl::Milliseconds(100));
  }
}

}  // namespace sandbox2
