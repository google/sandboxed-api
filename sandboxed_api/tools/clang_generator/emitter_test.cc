// Copyright 2020 Google LLC
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

#include "sandboxed_api/tools/clang_generator/emitter.h"

#include <initializer_list>
#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sapi {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::MatchesRegex;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::StrNe;

class EmitterForTesting : public Emitter {
 public:
  std::vector<std::string> SpellingsForNS(const std::string& ns_name) {
    std::vector<std::string> result;
    for (const RenderedType* rt : rendered_types_ordered_) {
      if (rt->ns_name == ns_name) {
        result.push_back(rt->spelling);
      }
    }
    return result;
  }

  const std::vector<std::string>& GetRenderedFunctions() {
    return rendered_functions_ordered_;
  }
};

class EmitterTest : public FrontendActionTest {};

TEST_F(EmitterTest, BasicFunctionality) {
  GeneratorOptions options;
  options.set_function_names<std::initializer_list<std::string>>(
      {"ExposedFunction"});

  EmitterForTesting emitter;
  ASSERT_THAT(
      RunFrontendAction(R"(extern "C" void ExposedFunction() {})",
                        std::make_unique<GeneratorAction>(emitter, options)),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  absl::StatusOr<std::string> header = emitter.EmitHeader(options);
  EXPECT_THAT(header, IsOk());
}

TEST_F(EmitterTest, RelatedTypes) {
  EmitterForTesting emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"(namespace std {
             using size_t = unsigned long;
             }  // namespace std
             using std::size_t;
             typedef enum { kRed, kGreen, kBlue } Color;
             struct Channel {
               Color color;
               size_t width;
               size_t height;
             };
             extern "C" void Colorize(Channel* chan);

             typedef struct { int member; } MyStruct;
             extern "C" void Structize(MyStruct* s);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(2));

  // Types from "std" should be skipped
  EXPECT_THAT(emitter.SpellingsForNS("std"), IsEmpty());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("typedef enum { kRed, kGreen, kBlue } Color",
                          "struct Channel {"
                          " Color color;"
                          " size_t width;"
                          " size_t height; }",
                          "typedef struct { int member; } MyStruct"));
}

TEST_F(EmitterTest, CollectFunctionPointer) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(typedef void (callback_t)(void*);
             struct HandlerData {
               int member;
               callback_t* cb;
             };
             extern "C" int Structize(HandlerData*);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(
      UglifyAll(emitter.SpellingsForNS("")),
      ElementsAre("typedef void (callback_t)(void *)",
                  "struct HandlerData { int member; callback_t *cb; }"));
}

TEST_F(EmitterTest, TypedefNames) {
  EmitterForTesting emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"(typedef enum { kNone, kSome } E;
             struct A { E member; };
             typedef struct { int member; } B;
             typedef struct tagC { int member; } C;
             extern "C" void Colorize(A*, B*, C*);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());

  EXPECT_THAT(
      UglifyAll(emitter.SpellingsForNS("")),
      ElementsAre("typedef enum { kNone, kSome } E", "struct A { E member; }",
                  "typedef struct { int member; } B",
                  "struct tagC { int member; }", "typedef struct tagC C"));
}

TEST_F(EmitterTest, NestedStruct) {
  EmitterForTesting emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"(struct A {
               struct B { int number; };
               B b;
               int data;
             };
             extern "C" void Structize(A* s);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A {"
                          " struct B { int number; };"
                          " B b;"
                          " int data; }"));
}

TEST_F(EmitterTest, NestedAnonymousStruct) {
  EmitterForTesting emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"(struct A {
               struct { int number; } b;
               int data;
             };
             extern "C" void Structize(A* s);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A {"
                          " struct { int number; } b;"
                          " int data; }"));
}

TEST_F(EmitterTest, ParentNotCollected) {
  EmitterForTesting emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"(struct A {
               struct B { int number; };
               B b;
               int data;
             };
             extern "C" void Structize(A::B* s);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A {"
                          " struct B { int number; };"
                          " B b;"
                          " int data; }"));
}

TEST_F(EmitterTest, RemoveQualifiers) {
  EmitterForTesting emitter;
  ASSERT_THAT(
      RunFrontendAction(
          R"(struct A { int data; };
             extern "C" void Structize(const A* in, A* out);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A { int data; }"));
}

TEST_F(EmitterTest, StructByValueSkipsFunction) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(struct A { int data; };
             extern "C" int Structize(A a);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), IsEmpty());
}

TEST_F(EmitterTest, ReturnStructByValueSkipsFunction) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(struct A { int data; };
             extern "C" A Structize();)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), IsEmpty());
}

TEST_F(EmitterTest, TypedefStructByValueSkipsFunction) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(typedef struct { int data; } A;
             extern "C" int Structize(A a);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), IsEmpty());
}

TEST_F(EmitterTest, CollectTypedefPointerType) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(typedef struct _KernelProfileRecord {
               int member;
             }* KernelProfileRecord;
             extern "C" const KernelProfileRecord*
             GetOpenCLKernelProfileRecords(const int, long long int*);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(
      UglifyAll(emitter.SpellingsForNS("")),
      ElementsAre("struct _KernelProfileRecord { int member; }",
                  "typedef struct _KernelProfileRecord *KernelProfileRecord"));
}

TEST_F(EmitterTest, TypedefTypeDependencies) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(typedef bool some_other_unused;
             using size_t = long long int;
             typedef struct _Image Image;
             typedef size_t (*StreamHandler)(const Image*, const void*,
                                             const size_t);
             enum unrelated_unused { NONE, SOME };
             struct _Image {
               StreamHandler stream;
               int size;
             };
             extern "C" void Process(StreamHandler handler);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("using size_t = long long", "struct _Image",
                          "typedef struct _Image Image",
                          "typedef size_t (*StreamHandler)(const Image *, "
                          "const void *, const size_t)",
                          "struct _Image {"
                          " StreamHandler stream;"
                          " int size; }"));
}

TEST_F(EmitterTest, OmitDependentTypes) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(template <typename T>
             struct Callback {
               typedef void (T::*MemberSignature)();
               MemberSignature pointer;
             };
             struct S : public Callback<S> {
               void Callable() {}
             };
             extern "C" void Invoke(S::MemberSignature* cb);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")), IsEmpty());
}

TEST_F(EmitterTest, SkipAbseilInternals) {
  EmitterForTesting emitter;
  EXPECT_THAT(
      RunFrontendAction(
          R"(namespace absl::internal {
               typedef int Int;
             }
             extern "C" void TakesAnInternalInt(absl::internal::Int);
             extern "C" void AbslInternalTakingAnInt(int);)",
          std::make_unique<GeneratorAction>(emitter, GeneratorOptions())),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")), IsEmpty());
}

TEST(IncludeGuard, CreatesRandomizedGuardForEmptyFilename) {
  // Copybara will transform the string. This is intentional.
  constexpr absl::string_view kGeneratedHeaderPrefix =
      "SANDBOXED_API_GENERATED_HEADER_";

  const std::string include_guard = GetIncludeGuard("");
  EXPECT_THAT(include_guard, MatchesRegex(absl::StrCat(kGeneratedHeaderPrefix,
                                                       R"([0-9A-F]+_)")));

  EXPECT_THAT(GetIncludeGuard(""), StrNe(include_guard));
}

TEST(IncludeGuard, BasicFunctionality) {
  EXPECT_THAT(GetIncludeGuard("boost/graph/compressed_sparse_row_graph.hpp"),
              StrEq("BOOST_GRAPH_COMPRESSED_SPARSE_ROW_GRAPH_HPP_"));

  // "SAPI_" prefix is there to avoid generating guards starting with "_"
  EXPECT_THAT(GetIncludeGuard("/usr/include/unistd.h"),
              StrEq("SAPI_USR_INCLUDE_UNISTD_H_"));
}

TEST(IncludeGuard, AvoidReservedIdentifiers) {
  EXPECT_THAT(GetIncludeGuard("9p.h"), StrEq("SAPI_9P_H_"));
  EXPECT_THAT(GetIncludeGuard("double__under.h"), StrEq("DOUBLE_UNDER_H_"));
  EXPECT_THAT(GetIncludeGuard("_single.h"), StrEq("SAPI_SINGLE_H_"));
  EXPECT_THAT(GetIncludeGuard("__double.h"), StrEq("SAPI_DOUBLE_H_"));
}

}  // namespace
}  // namespace sapi
