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
/*
 * libxslt_tutorial.c: demo program for the XSL Transformation 1.0 engine
 *
 * based on xsltproc.c, by Daniel.Veillard@imag.fr
 * by John Fleck
 *
 * Licence for libxslt except libexslt
 * ----------------------------------------------------------------------
 *  Copyright (C) 2001-2002 Daniel Veillard.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is fur-
 * nished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * DANIEL VEILLARD BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CON-
 * NECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Daniel Veillard shall not
 * be used in advertising or otherwise to promote the sale, use or other deal-
 * ings in this Software without prior written authorization from him.
 *
 * ----------------------------------------------------------------------
 *
 * Licence for libexslt
 * ----------------------------------------------------------------------
 *  Copyright (C) 2001-2002 Thomas Broyer, Charlie Bozeman and Daniel Veillard.
 *  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is fur-
 * nished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CON-
 * NECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the authors shall not
 * be used in advertising or otherwise to promote the sale, use or other deal-
 * ings in this Software without prior written authorization from him.
 * ----------------------------------------------------------------------
 */

#include <err.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

#include <libxml/catalog.h>
#include <libxml/debugXML.h>
#include <libxml/parser.h>
#include <libxml/xinclude.h>
#include <libxml/xmlIO.h>
#include <libxml/xmlmemory.h>
#include <libxslt/transform.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/xsltutils.h>

#include "libxslt_sapi.h"

namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::StrEq;

class LibXSLTSandboxTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_THAT(getenv("TEST_FILES_DIR"), NotNull());
    sandbox_ = new sapi::contrib::libxslt::LibXsltSapiSandbox();
    ASSERT_THAT(sandbox_->Init(), IsOk());
    api_ = new sapi::contrib::libxslt::LibXSLTApi(sandbox_);
  }
  static void TearDownTestSuite() {
    delete api_;
    delete sandbox_;
  }
  static sapi::contrib::libxslt::LibXSLTApi* api_;
  static sapi::contrib::libxslt::LibXsltSapiSandbox* sandbox_;
};

sapi::contrib::libxslt::LibXSLTApi* LibXSLTSandboxTest::api_;
sapi::contrib::libxslt::LibXsltSapiSandbox* LibXSLTSandboxTest::sandbox_;

std::string GetTestFilePath(const std::string& filename) {
  return sapi::file::JoinPath(getenv("TEST_FILES_DIR"), filename);
}

TEST_F(LibXSLTSandboxTest, Simple) {
  const char* const params[] = { NULL };
  sapi::v::Array ptrs(params, 1);
  ASSERT_THAT(sandbox_->Init(), IsOk());
  ASSERT_THAT(api_->xmlInitParser(), IsOk());
  std::string stylesheetName = GetTestFilePath("bad.xml"),
              documentName = GetTestFilePath("bad2.xml");
  int stylesheetFd = open(stylesheetName.data(), O_CLOEXEC | O_NOCTTY | O_RDONLY, 0);
  ASSERT_NE(stylesheetFd, -1);
  int documentFd = open(documentName.data(), O_CLOEXEC | O_NOCTTY | O_RDONLY, 0);

  ASSERT_NE(documentFd, -1);
  sapi::v::Fd fStylesheet{stylesheetFd}, fDocument{documentFd}, fStdout{0};

  documentFd = stylesheetFd = -1;

  ASSERT_THAT(sandbox_->TransferToSandboxee(&fStylesheet), IsOk());
  ASSERT_THAT(sandbox_->TransferToSandboxee(&fDocument), IsOk());
  ASSERT_THAT(sandbox_->TransferToSandboxee(&fStdout), IsOk());
  ::sapi::v::ConstCStr encoding{"UTF-8"};
  ::sapi::v::NullPtr null;
  auto vDocumentRaw = api_->xmlReadFd(fDocument.GetRemoteFd(), &null, encoding.PtrBefore(), 0);
  ASSERT_THAT(vDocumentRaw, IsOk()) << "Could not call xmlReadFd()";
  ASSERT_THAT(*vDocumentRaw, NotNull()) << "xmlReadFd() failed for document";
  auto vStylesheetRaw = api_->xmlReadFd(fStylesheet.GetRemoteFd(), &null, encoding.PtrBefore(), 0);
  ASSERT_THAT(vStylesheetRaw, IsOk()) << "Could not call xmlReadFd()";
  ASSERT_THAT(*vStylesheetRaw, NotNull()) << "xmlReadFd() failed for stylesheet";
  sapi::v::RemotePtr vStylesheet{*vStylesheetRaw}, vDocument{*vDocumentRaw};
  auto vTransformedRaw = api_->sapi_xsltParseStylesheetDoc(&vStylesheet, &vDocument, ptrs.PtrBefore());
  ASSERT_THAT(vTransformedRaw, IsOk()) << "Could not call sapi_xsltParseStylesheetDoc()";
  ASSERT_THAT(*vTransformedRaw, NotNull()) << "sapi_xsltParseStylesheetDoc() failed";
  sapi::v::RemotePtr vTransformed{*vTransformedRaw};
  auto saveCtx = api_->xmlSaveToFd(fStdout.GetRemoteFd(), encoding.PtrBefore(), 0);
  ASSERT_THAT(saveCtx, IsOk());
  ASSERT_THAT(*saveCtx, NotNull());
  sapi::v::RemotePtr vSaveCtx{*saveCtx};
  auto saveResult = api_->xmlSaveDoc(&vSaveCtx, &vTransformed);
  ASSERT_THAT(saveResult, IsOk());
  ASSERT_GE(*saveResult, 0);
  ASSERT_THAT((saveResult = api_->xmlSaveClose(&vSaveCtx)), IsOk());
  ASSERT_GE(*saveResult, 0);
}
}  // namespace

int main(int argc, char* argv[]) {
  ::google::InitGoogleLogging(program_invocation_short_name);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
