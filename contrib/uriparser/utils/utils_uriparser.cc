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

#include "contrib/uriparser/utils/utils_uriparser.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <fstream>
#include <iostream>
#include <string>

#include "absl/cleanup/cleanup.h"

UriParser::~UriParser() {
  if (GetStatus().ok()) {
    api_.uriFreeUriMembersA(uri_.PtrBefore()).IgnoreError();
  }
}

absl::Status UriParser::GetStatus() { return status_; }

absl::Status UriParser::ParseUri() {
  SAPI_RETURN_IF_ERROR(sandbox_->Allocate(&uri_, true));

  sapi::v::Struct<UriParserStateA> state;
  state.mutable_data()->uri = reinterpret_cast<UriUriA*>(uri_.GetRemote());
  SAPI_ASSIGN_OR_RETURN(
      int ret, api_.uriParseUriA(state.PtrBefore(), c_uri_.PtrBefore()));
  if (ret != 0) {
    return absl::UnavailableError("Unable to parse uri");
  }

  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&uri_));

  return absl::OkStatus();
}

absl::StatusOr<std::string> UriParser::FetchUriText(UriTextRangeA* ptr) {
  if (ptr == nullptr) {
    return "";
  }
  if (ptr->first == nullptr) {
    return "";
  }

  size_t size = ptr->afterLast - ptr->first;

  // Sometimes uriparser dosen't allocate new memory
  // and sometimes it does.
  SAPI_ASSIGN_OR_RETURN(
      std::string uri,
      sandbox_->GetCString(sapi::v::RemotePtr(const_cast<char*>(ptr->first))));

  return uri.substr(0, size);
}

absl::StatusOr<std::string> UriParser::GetScheme() {
  return FetchUriText(&uri_.mutable_data()->scheme);
}

absl::StatusOr<std::string> UriParser::GetUserInfo() {
  return FetchUriText(&uri_.mutable_data()->userInfo);
}

absl::StatusOr<std::string> UriParser::GetHostText() {
  return FetchUriText(&uri_.mutable_data()->hostText);
}

absl::StatusOr<std::string> UriParser::GetHostIP() {
  char ipstr[INET6_ADDRSTRLEN] = "";

  UriHostDataA* data = &uri_.mutable_data()->hostData;

  if (uri_.mutable_data()->hostData.ip4) {
    sapi::v::Struct<UriIp4> ip4;
    ip4.SetRemote(uri_.mutable_data()->hostData.ip4);
    SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&ip4));
    inet_ntop(AF_INET, ip4.mutable_data()->data, ipstr, sizeof(ipstr));
  } else if (uri_.mutable_data()->hostData.ip6) {
    sapi::v::Struct<UriIp6> ip6;
    ip6.SetRemote(uri_.mutable_data()->hostData.ip6);
    SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&ip6));
    inet_ntop(AF_INET6, ip6.mutable_data()->data, ipstr, sizeof(ipstr));
  }

  return std::string(ipstr);
}

absl::StatusOr<std::string> UriParser::GetPortText() {
  return FetchUriText(&uri_.mutable_data()->portText);
}

absl::StatusOr<std::string> UriParser::GetQuery() {
  return FetchUriText(&uri_.mutable_data()->query);
}

absl::StatusOr<std::string> UriParser::GetFragment() {
  return FetchUriText(&uri_.mutable_data()->fragment);
}

absl::StatusOr<std::vector<std::string>> UriParser::GetPath() {
  std::vector<std::string> ret;

  UriPathSegmentA* pathHead = uri_.mutable_data()->pathHead;
  if (pathHead == nullptr) {
    return ret;
  }

  sapi::v::Struct<UriPathSegmentA> path_segment;
  path_segment.SetRemote(pathHead);

  while (path_segment.GetRemote() != nullptr) {
    SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&path_segment));

    SAPI_ASSIGN_OR_RETURN(std::string seg,
                          FetchUriText(&path_segment.mutable_data()->text));
    if (!seg.empty()) {
      ret.push_back(seg);
    }

    path_segment.SetRemote(path_segment.mutable_data()->next);
  }

  return ret;
}

absl::Status UriParser::NormalizeSyntax() {
  SAPI_ASSIGN_OR_RETURN(int dirty_parts,
                        api_.uriNormalizeSyntaxMaskRequiredA(uri_.PtrNone()));

  return NormalizeSyntax(dirty_parts);
}

absl::Status UriParser::NormalizeSyntax(int norm_mask) {
  SAPI_ASSIGN_OR_RETURN(int ret,
                        api_.uriNormalizeSyntaxExA(uri_.PtrAfter(), norm_mask));

  if (ret != 0) {
    return absl::UnavailableError("Unable to normalize");
  }

  return absl::OkStatus();
}

absl::StatusOr<std::string> UriParser::GetUri() { return GetUri(&uri_); }

absl::StatusOr<std::string> UriParser::GetUri(sapi::v::Struct<UriUriA>* uri) {
  sapi::v::Int size;
  int ret;

  SAPI_ASSIGN_OR_RETURN(
      ret, api_.uriToStringCharsRequiredA(uri->PtrNone(), size.PtrAfter()));
  if (ret != 0) {
    return absl::UnavailableError("Unable to get size");
  }

  sapi::v::Array<char> buf(size.GetValue() + 1);
  sapi::v::NullPtr null_ptr;

  SAPI_ASSIGN_OR_RETURN(ret, api_.uriToStringA(buf.PtrAfter(), uri->PtrNone(),
                                               buf.GetSize(), &null_ptr));
  if (ret != 0) {
    return absl::UnavailableError("Unable to Recomposing URI");
  }

  return std::string(buf.GetData());
}

absl::StatusOr<std::string> UriParser::GetUriEscaped(bool space_to_plus,
                                                     bool normalize_breaks) {
  SAPI_ASSIGN_OR_RETURN(std::string uri, GetUri());

  // Be sure to allocate *6 times* the space of the input buffer for
  // *6 times* for _normalizeBreaks == URI_TRUE_
  int space = uri.length() * 6 + 1;

  sapi::v::Array<char> bufout(space);
  sapi::v::ConstCStr bufin(uri.c_str());

  SAPI_RETURN_IF_ERROR(api_.uriEscapeA(bufin.PtrBefore(), bufout.PtrAfter(),
                                       space_to_plus, normalize_breaks));

  return std::string(bufout.GetData());
}

absl::StatusOr<std::string> UriParser::GetUriWithBase(const std::string& base) {
  UriParser base_uri(sandbox_, base);

  sapi::v::Struct<UriUriA> newuri;
  SAPI_ASSIGN_OR_RETURN(int ret,
                        api_.uriAddBaseUriA(newuri.PtrAfter(), uri_.PtrNone(),
                                            base_uri.uri_.PtrBefore()));
  if (ret != 0) {
    return absl::UnavailableError("Unable to add base");
  }
  absl::Cleanup newuri_cleanup = [this, &newuri] {
    api_.uriFreeUriMembersA(newuri.PtrNone()).IgnoreError();
  };

  return GetUri(&newuri);
}

absl::StatusOr<std::string> UriParser::GetUriWithoutBase(
    const std::string& base, bool domain_root_mode) {
  UriParser base_uri(sandbox_, base);

  sapi::v::Struct<UriUriA> newuri;
  SAPI_ASSIGN_OR_RETURN(
      int ret,
      api_.uriRemoveBaseUriA(newuri.PtrAfter(), uri_.PtrNone(),
                             base_uri.uri_.PtrBefore(), domain_root_mode));
  if (ret != 0) {
    return absl::UnavailableError("Unable to remove base");
  }
  absl::Cleanup newuri_cleanup = [this, &newuri] {
    api_.uriFreeUriMembersA(newuri.PtrNone()).IgnoreError();
  };

  return GetUri(&newuri);
}

absl::StatusOr<absl::btree_map<std::string, std::string>>
UriParser::GetQueryElements() {
  absl::btree_map<std::string, std::string> outquery;

  if (uri_.mutable_data()->query.first == nullptr) {
    return outquery;
  }

  sapi::v::Array<void*> query_ptr(1);
  sapi::v::Int query_count;
  sapi::v::RemotePtr first(const_cast<char*>(uri_.mutable_data()->query.first));
  sapi::v::RemotePtr afterLast(
      const_cast<char*>(uri_.mutable_data()->query.afterLast));

  SAPI_ASSIGN_OR_RETURN(
      int ret,
      api_.uriDissectQueryMallocA(query_ptr.PtrAfter(), query_count.PtrAfter(),
                                  &first, &afterLast));
  if (ret != 0) {
    return absl::UnavailableError("Unable to get query list");
  }
  absl::Cleanup query_list_cleanup = [this, &query_ptr] {
    sapi::v::RemotePtr rptr(query_ptr[0]);
    api_.uriFreeQueryListA(&rptr).IgnoreError();
  };

  sapi::v::Struct<UriQueryListA> obj;
  obj.SetRemote(query_ptr[0]);

  for (int i = 0; i < query_count.GetValue(); ++i) {
    SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&obj));
    obj.SetRemote(nullptr);

    void* key_p = const_cast<char*>(obj.mutable_data()->key);
    void* value_p = const_cast<char*>(obj.mutable_data()->value);

    SAPI_ASSIGN_OR_RETURN(std::string key_s,
                          sandbox_->GetCString(sapi::v::RemotePtr(key_p)));
    std::string value_s;
    if (value_p != nullptr) {
      SAPI_ASSIGN_OR_RETURN(value_s,
                            sandbox_->GetCString(sapi::v::RemotePtr(value_p)));
    } else {
      value_s = "";
    }
    outquery[key_s] = value_s;
    obj.SetRemote(obj.mutable_data()->next);
  }
  obj.SetRemote(nullptr);

  return outquery;
}
