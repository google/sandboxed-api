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

#include "contrib/libpcap/utils/utils_libpcap.h"

#include <fstream>
#include <iostream>
#include <string>

#include "contrib/libpcap/sandboxed.h"

constexpr size_t kMaxPacket = 1024 * 1024 * 128;  // 128MB

absl::Status LibPcap::OpenRemote() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  SAPI_RETURN_IF_ERROR(sandbox_->TransferToSandboxee(&fd_));

  sapi::v::CStr mode("r");
  SAPI_ASSIGN_OR_RETURN(void* pfile,
                        api_.sapi_fdopen(fd_.GetRemoteFd(), mode.PtrBefore()));
  sapi::v::RemotePtr pcap_pfile(pfile);

  sapi::v::GenericPtr sapi_pcap_errmsg;
  SAPI_ASSIGN_OR_RETURN(
      pcap_t* pcap_context,
      api_.pcap_fopen_offline(&pcap_pfile, sapi_pcap_errmsg.PtrAfter()));

  if (pcap_context == nullptr) {
    api_.sapi_fclose(&pcap_pfile).IgnoreError();
    sapi::v::RemotePtr remote_ptr_error(sapi_pcap_errmsg.GetRemote());
    SAPI_ASSIGN_OR_RETURN(std::string errmsg,
                          sandbox_->GetCString(remote_ptr_error));
    return absl::UnavailableError(errmsg);
  }

  sapi_pcap_context_.SetRemote(pcap_context);
  return absl::OkStatus();
}

LibPcap::~LibPcap() {
  if (sapi_pcap_context_.GetRemote() != nullptr) {
    api_.pcap_close(sapi_pcap_context_.PtrNone()).IgnoreError();
  }
}

bool LibPcap::IsInit() { return CheckIsInit().ok(); }

absl::Status LibPcap::CheckIsInit() {
  if (fd_.GetValue() < 0) {
    return absl::UnavailableError("PCAP file not opened");
  }
  return init_status_;
}

absl::Status LibPcap::GetInitStatus() { return init_status_; }

absl::StatusOr<LibPcapPacket> LibPcap::Next() {
  SAPI_RETURN_IF_ERROR(CheckIsInit());

  LibPcapPacket packet(&sapi_pcap_context_, &api_);
  if (!packet.GetInitStatus().ok()) {
    return packet.GetInitStatus();
  }

  return packet;
}

absl::Status LibPcap::SetFilter(const std::string& filter, int optimize,
                                uint32_t netmask) {
  sapi::v::Struct<struct bpf_program> sapi_bpf_program;
  sapi::v::ConstCStr sapi_filter(filter.c_str());

  int ret;
  SAPI_ASSIGN_OR_RETURN(
      ret, api_.pcap_compile(sapi_pcap_context_.PtrNone(),
                             sapi_bpf_program.PtrAfter(),
                             sapi_filter.PtrBefore(), optimize, netmask));
  if (ret == -1) {
    return absl::UnavailableError("Unable to compile filter");
  }

  SAPI_ASSIGN_OR_RETURN(ret, api_.pcap_setfilter(sapi_pcap_context_.PtrNone(),
                                                 sapi_bpf_program.PtrNone()));
  if (ret == -1) {
    return absl::UnavailableError("Unable to set filter");
  }

  return absl::OkStatus();
}

absl::Status LibPcapPacket::FetchPacket(sapi::v::GenericPtr* context,
                                        LibpcapApi* api) {
  sapi::v::Struct<struct pcap_pkthdr> sapi_packet_header;
  SAPI_ASSIGN_OR_RETURN(
      uint8_t* package,
      api->pcap_next(context->PtrNone(), sapi_packet_header.PtrAfter()));

  if (package == nullptr) {
    finished_ = true;
    return absl::OkStatus();
  }

  packet_header_ = sapi_packet_header.data();

  size_t bufsize = packet_header_.caplen;
  if (bufsize > kMaxPacket) {
    return absl::UnavailableError("Package to large");
  }

  buffer_.resize(bufsize);
  sapi::v::Array<uint8_t> sapi_buffer(buffer_.data(), buffer_.size());
  sapi_buffer.SetRemote(package);
  SAPI_RETURN_IF_ERROR(api->GetSandbox()->TransferFromSandboxee(&sapi_buffer));

  return absl::OkStatus();
}

absl::Status LibPcapPacket::GetInitStatus() const { return init_status_; }

absl::StatusOr<absl::Span<const uint8_t>> LibPcapPacket::GetData() const {
  SAPI_RETURN_IF_ERROR(GetInitStatus());
  return absl::Span<const uint8_t>(buffer_.data(), buffer_.size());
}

absl::StatusOr<long int> LibPcapPacket::GetSec() const {
  SAPI_RETURN_IF_ERROR(GetInitStatus());
  return packet_header_.ts.tv_sec;
}

absl::StatusOr<long int> LibPcapPacket::GetUsec() const {
  SAPI_RETURN_IF_ERROR(GetInitStatus());
  return packet_header_.ts.tv_usec;
}

bool LibPcapPacket::Finished() const { return finished_; }

std::ostream& operator<<(std::ostream& os, const LibPcapPacket& pkg) {
  if (!pkg.GetInitStatus().ok()) {
    return os << "Class not initialized";
  }
  os << "[" << std::dec << pkg.GetSec().value();
  os << "." << std::dec << pkg.GetUsec().value() << "] ";
  absl::Span<const uint8_t> data = pkg.GetData().value();
  os << "(" << std::dec << data.size() << ") ";
  for (int i = 0; i < std::min(data.size(), 40UL); i++) {
    os << std::hex << data[i] << " ";
  }
  os << "...";
  return os;
}
