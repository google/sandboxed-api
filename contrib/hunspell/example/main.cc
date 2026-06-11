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

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "contrib/hunspell/sandboxed.h"

absl::Status PrintSuggest(HunspellApi& api, sapi::v::RemotePtr& hunspellrp,
                          sapi::v::ConstCStr& word) {
  sapi::v::GenericPtr outptr;

  SAPI_ASSIGN_OR_RETURN(
      int nlist,
      api.Hunspell_suggest(&hunspellrp, outptr.PtrAfter(), word.PtrBefore()));

  if (nlist == 0) {
    std::cout << "No suggestions.\n";
    return absl::OkStatus();
  }

  sapi::v::Array<char*> ptr_list(nlist);
  ptr_list.SetRemote(reinterpret_cast<void*>(outptr.GetValue()));

  SAPI_RETURN_IF_ERROR(api.GetSandbox()->TransferFromSandboxee(&ptr_list));

  std::cout << "Suggestions:\n";
  for (int i = 0; i < nlist; i++) {
    sapi::v::RemotePtr sugrp(ptr_list[i]);
    SAPI_ASSIGN_OR_RETURN(std::string sugestion,
                          api.GetSandbox()->GetCString(sugrp));
    std::cout << sugestion[i] << "\n";
  }

  api.Hunspell_free_list(&hunspellrp, ptr_list.PtrNone(), nlist).IgnoreError();

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (argc != 4) {
    std::cerr << "Usage:\n  " << argv[0];
    std::cerr << " AFFIX_FILE FICTIONARY_FILE WORDS_TO_CHECK_FILE\n";
    return EXIT_FAILURE;
  }

  sapi::v::ConstCStr affix_file_name(argv[1]);
  sapi::v::ConstCStr dictionary_file_name(argv[2]);

  HunspellSapiSandbox sandbox(affix_file_name.GetData(),
                              dictionary_file_name.GetData());
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  HunspellApi api(&sandbox);
  absl::StatusOr<Hunhandle*> hunspell = api.Hunspell_create(
      affix_file_name.PtrBefore(), dictionary_file_name.PtrBefore());
  if (!hunspell.ok()) {
    std::cerr << "Could not initialize hunsepll\n";
    return EXIT_FAILURE;
  }
  sapi::v::RemotePtr hunspellrp(*hunspell);

  std::ifstream word_to_check_list(argv[3], std::ios_base::in);
  if (!word_to_check_list.is_open()) {
    std::cerr << "Could not open file of words to check\n";
    return EXIT_FAILURE;
  }

  std::string buf;
  while (std::getline(word_to_check_list, buf)) {
    sapi::v::ConstCStr cbuf(buf.c_str());
    absl::StatusOr<int> result =
        api.Hunspell_spell(&hunspellrp, cbuf.PtrBefore());
    if (!result.ok()) {
      std::cerr << "Could not check word\n" << result.status() << std::endl;
      return EXIT_FAILURE;
    }

    if (*result) {
      std::cout << "Word " << buf << " is ok\n";
    } else {
      std::cout << "Word " << buf << " is incorrect\n";
      absl::Status status = PrintSuggest(api, hunspellrp, cbuf);
      if (!status.ok()) {
        std::cerr << "Unable to get all suggestion\n" << status << std::endl;
      }
    }
  }

  api.Hunspell_destroy(&hunspellrp).IgnoreError();

  return EXIT_SUCCESS;
}
