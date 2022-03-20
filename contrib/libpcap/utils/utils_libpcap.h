// Copyright 2022 Google LLC
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

#ifndef CONTRIB_LIBPCAP_UTILS_UTILS_LIBPCAP_H_
#define CONTRIB_LIBPCAP_UTILS_UTILS_LIBPCAP_H_

#include <fcntl.h>

#include "contrib/libpcap/sandboxed.h"

class LibPcapPacket {
 public:
  LibPcapPacket(sapi::v::GenericPtr* context, LibpcapApi* api) {
    init_status_ = FetchPacket(context, api);
  }
  absl::Status GetInitStatus() const;

  absl::StatusOr<absl::Span<const uint8_t>> GetData() const;
  absl::StatusOr<long int> GetSec() const;
  absl::StatusOr<long int> GetUsec() const;

  bool Finished() const;

  friend std::ostream& operator<<(std::ostream& os, const LibPcapPacket& pkg);

 private:
  absl::Status FetchPacket(sapi::v::GenericPtr* context, LibpcapApi* libpcap);

  absl::Status init_status_;
  struct pcap_pkthdr packet_header_;
  std::vector<uint8_t> buffer_;
  bool finished_ = false;
};

class LibPcap {
 public:
  LibPcap(LibpcapSapiSandbox* sandbox, const char* pcap_filename)
      : sandbox_(CHECK_NOTNULL(sandbox)),
        api_(sandbox_),
        pcap_filename_(CHECK_NOTNULL(pcap_filename)),
        fd_(open(pcap_filename, O_RDONLY)) {
    init_status_ = OpenRemote();
  }

  ~LibPcap();

  bool IsInit();
  absl::Status CheckIsInit();
  absl::Status GetInitStatus();

  absl::StatusOr<LibPcapPacket> Next();

  absl::Status SetFilter(const std::string& filter, int optimize = 0,
                         uint32_t netmask = 0xffffffff);

 private:
  absl::Status OpenRemote();

  LibpcapSapiSandbox* sandbox_;
  LibpcapApi api_;
  std::string pcap_filename_;
  absl::Status init_status_;
  sapi::v::Fd fd_;
  sapi::v::GenericPtr sapi_pcap_context_;
};

#endif  // CONTRIB_LIBPCAP_UTILS_UTILS_LIBPCAP_H_
