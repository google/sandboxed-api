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
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tools/clang_generator/emitter_base.h"
#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/symbol_list_emitter.h"
#include "sandboxed_api/tools/clang_generator/types.h"

namespace sapi {
namespace {

using ::absl_testing::IsOk;
using ::testing::ContainsRegex;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::MatchesRegex;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::StrNe;

class EmitterForTesting : public Emitter {
 public:
  explicit EmitterForTesting(const GeneratorOptions* options)
      : Emitter(options) {}

  // Returns the spellings of all rendered_types_ordered_ that have the given
  // namespace name.
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

// Tests that the generator only emits the requested function and ignores the
// others.
TEST_F(EmitterTest, SpecificFunctionRequested) {
  GeneratorOptions options;
  options.set_function_names<std::initializer_list<std::string>>(
      {"ExposedFunction"});

  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendActionOnFile(
                  "simple_functions.cc",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  absl::StatusOr<std::string> header = emitter.EmitHeader();
  ASSERT_THAT(header, IsOk());
  EXPECT_THAT(*header, HasSubstr("ExposedFunction"));
}

// Tests that the generator emits all functions if no specific functions are
// requested.
TEST_F(EmitterTest, AllFunctionsSuccess) {
  constexpr absl::string_view kInputFile = "simple_functions.cc";
  GeneratorOptions options;
  options.set_function_names<std::initializer_list<std::string>>({});
  options.in_files = {"simple_functions.cc"};

  EmitterForTesting emitter(&options);
  ASSERT_THAT(
      RunFrontendActionOnFile(
          kInputFile, std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(2));

  absl::StatusOr<std::string> header = emitter.EmitHeader();
  ASSERT_THAT(header, IsOk());
  EXPECT_THAT(*header, HasSubstr("ExposedFunction"));
  EXPECT_THAT(*header, HasSubstr("OtherFunction"));
}

// Tests that the generator emits all functions if no specific functions are
// requested, and the input file is not provided.
TEST_F(EmitterTest, AllFunctionsNoInputFiles) {
  GeneratorOptions options;
  options.set_function_names<std::initializer_list<std::string>>({});

  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendActionOnFile(
                  "simple_functions.cc",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(2));

  absl::StatusOr<std::string> header = emitter.EmitHeader();
  ASSERT_THAT(header, IsOk());
  EXPECT_THAT(*header, HasSubstr("ExposedFunction"));
  EXPECT_THAT(*header, HasSubstr("OtherFunction"));
}

// Tests that the generator emits all functions if no specific functions are
// requested, the input file is provided, and the limit scan depth is enabled.
TEST_F(EmitterTest, AllFunctionsLimitScanDepthSuccess) {
  constexpr absl::string_view kInputFile = "simple_functions.cc";
  GeneratorOptions options;
  options.set_function_names<std::initializer_list<std::string>>({});
  options.limit_scan_depth = true;
  options.in_files.emplace(kInputFile);

  EmitterForTesting emitter(&options);
  ASSERT_THAT(
      RunFrontendActionOnFile(
          kInputFile, std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(2));

  absl::StatusOr<std::string> header = emitter.EmitHeader();
  ASSERT_THAT(header, IsOk());
  EXPECT_THAT(*header, HasSubstr("ExposedFunction"));
  EXPECT_THAT(*header, HasSubstr("OtherFunction"));
}

// Tests that the generator fails to emit all functions if no specific functions
// are requested, the input file is not provided, and the limit scan depth is
// enabled.
TEST_F(EmitterTest, AllFunctionsLimitScanDepthFailure) {
  GeneratorOptions options;
  options.set_function_names<std::initializer_list<std::string>>({});
  options.limit_scan_depth = true;

  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendActionOnFile(
                  "simple_functions.cc",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), IsEmpty());
}

TEST_F(EmitterTest, RelatedTypes) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
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
                  std::make_unique<GeneratorAction>(&emitter, &options)),
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
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(typedef void (callback_t)(void*);
             struct HandlerData {
               int member;
               callback_t* cb;
             };
             extern "C" int Structize(HandlerData*);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(
      UglifyAll(emitter.SpellingsForNS("")),
      ElementsAre("typedef void (callback_t)(void *)",
                  "struct HandlerData { int member; callback_t *cb; }"));
}

TEST_F(EmitterTest, TypedefNames) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(typedef enum { kNone, kSome } E;
             struct A { E member; };
             typedef struct { int member; } B;
             typedef struct tagC { int member; } C;
             extern "C" void Colorize(A*, B*, C*);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(
      UglifyAll(emitter.SpellingsForNS("")),
      ElementsAre("typedef enum { kNone, kSome } E", "struct A { E member; }",
                  "typedef struct { int member; } B",
                  "struct tagC { int member; }", "typedef struct tagC C"));
}

TEST_F(EmitterTest, TypedefAnonymousWithFieldStructure) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A { int number; };
             typedef struct { A member; } B;
             extern "C" void Foo(B*);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A { int number; }",
                          "typedef struct { A member; } B"));
}

TEST_F(EmitterTest, NamedEnumWithoutTypedef) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(enum Color { kRed, kGreen, kBlue };
             typedef struct { enum Color member; } B;
             extern "C" void Foo(B*);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("enum Color { kRed, kGreen, kBlue }",
                          "typedef struct { enum Color member; } B"));
}

TEST_F(EmitterTest, TypedefOpaqueStruct) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(typedef struct png_control* png_controlp;
             extern "C" void Structize(png_controlp);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct png_control",
                          "typedef struct png_control *png_controlp"));
}

TEST_F(EmitterTest, TypedefAnonymousStructAndPointer) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(typedef struct {
               void*        opaque;
             } png_image, *png_imagep;
             extern "C" void Structize(png_imagep);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("typedef struct { void *opaque; } png_image",
                          "typedef png_image *png_imagep"));
}

TEST_F(EmitterTest, NestedStruct) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A {
               struct B { int number; };
               B b;
               int data;
             };
             extern "C" void Structize(A* s);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A {"
                          " struct B { int number; };"
                          " B b;"
                          " int data; }"));
}

TEST_F(EmitterTest, NestedAnonymousStruct) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A {
               struct { int number; } b;
               int data;
             };
             extern "C" void Structize(A* s);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A {"
                          " struct { int number; } b;"
                          " int data; }"));
}

TEST_F(EmitterTest, ParentNotCollected) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A {
               struct B { int number; };
               B b;
               int data;
             };
             extern "C" void Structize(A::B* s);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A {"
                          " struct B { int number; };"
                          " B b;"
                          " int data; }"));
}

TEST_F(EmitterTest, StructForwardDecl) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A;
             extern "C" void UsingForwardDeclaredStruct(A* s);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")), ElementsAre("struct A"));
}

TEST_F(EmitterTest, AggregateStructWithDefaultedMembers) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A {
               int a = 0;
               int b = 42;
             };
             extern "C" void AggregateStruct(A* s);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A {"
                          " int a = 0;"
                          " int b = 42; }"));
}

TEST_F(EmitterTest, AggregateStructWithMethods) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A {
               int a = 0;
               int b = 42;
               int my_mem_fn();
             };
             extern "C" void AggregateStruct(A* s);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  // Expect a forward decl in this case
  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")), ElementsAre("struct A"));
}

TEST_F(EmitterTest, RemoveQualifiers) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  ASSERT_THAT(RunFrontendAction(
                  R"(struct A { int data; };
             extern "C" void Structize(const A* in, A* out);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct A { int data; }"));
}

TEST_F(EmitterTest, StructByValueSkipsFunction) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(struct A { int data; };
             extern "C" int Structize(A a);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), IsEmpty());
}

TEST_F(EmitterTest, ReturnStructByValueSkipsFunction) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(struct A { int data; };
             extern "C" A Structize();)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), IsEmpty());
}

TEST_F(EmitterTest, TypedefStructByValueSkipsFunction) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(typedef struct { int data; } A;
             extern "C" int Structize(A a);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), IsEmpty());
}

TEST_F(EmitterTest, CollectTypedefPointerType) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(typedef struct _KernelProfileRecord {
               int member;
             }* KernelProfileRecord;
             extern "C" const KernelProfileRecord*
             GetOpenCLKernelProfileRecords(const int, long long int*);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(
      UglifyAll(emitter.SpellingsForNS("")),
      ElementsAre("struct _KernelProfileRecord { int member; }",
                  "typedef struct _KernelProfileRecord *KernelProfileRecord"));
}

TEST_F(EmitterTest, TypedefTypeDependencies) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(typedef bool some_other_unused;
             using size_t = long long int;
             typedef struct _Image Image;
             typedef size_t (*StreamHandler)(Image*, void*, size_t);
             enum unrelated_unused { NONE, SOME };
             struct _Image {
               StreamHandler stream;
               int size;
             };
             extern "C" void Process(StreamHandler handler);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(
      UglifyAll(emitter.SpellingsForNS("")),
      ElementsAre("using size_t = long long", "struct _Image",
                  "typedef struct _Image Image",
                  "typedef size_t (*StreamHandler)(Image *, void *, size_t)",
                  "struct _Image { StreamHandler stream; int size; }"));
}

TEST_F(EmitterTest, TypedefArrays) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(typedef short JCOEF;
                     typedef JCOEF JBLOCK[64];
                     typedef JBLOCK *JBLOCKROW;
                     typedef JBLOCKROW *JBLOCKARRAY;
                     extern "C" void Array(JBLOCKARRAY);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("typedef short JCOEF", "typedef JCOEF JBLOCK[64]",
                          "typedef JBLOCK *JBLOCKROW",
                          "typedef JBLOCKROW *JBLOCKARRAY"));
}

TEST_F(EmitterTest, MacroTypes) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(# define __intN_t(N, MODE) \
      typedef int int##N##_t __attribute__ ((__mode__ (MODE)))
    # define __u_intN_t(N, MODE) \
      typedef unsigned int u_int##N##_t __attribute__ ((__mode__ (MODE)))
    # ifndef __int8_t_defined
    #  define __int8_t_defined
    __intN_t (64, __DI__);
    __intN_t (8, __QI__);
    # endif

                     extern "C" void Foo(int8_t*);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("typedef int int8_t __attribute__((mode(__QI__)))"));
}

TEST_F(EmitterTest, UseRecordDefiniton) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(struct Foo {
                      struct Bar* bar;
                     };
                     typedef struct Bar Bar;
                     struct Bar {
                      int x;
                     };
                     extern "C" void Baz(Bar* bar);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("struct Bar", "typedef struct Bar Bar",
                          "struct Bar { int x; }"));
}

TEST_F(EmitterTest, OmitDependentTypes) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(template <typename T>
             struct Callback {
               typedef void (T::*MemberSignature)();
               MemberSignature pointer;
             };
             struct S : public Callback<S> {
               void Callable() {}
             };
             extern "C" void Invoke(S::MemberSignature* cb);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")), IsEmpty());
}

TEST_F(EmitterTest, SkipAbseilInternals) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(namespace absl::internal {
               typedef int Int;
             }
             extern "C" void TakesAnInternalInt(absl::internal::Int);
             extern "C" void AbslInternalTakingAnInt(int);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")), IsEmpty());
}

TEST_F(EmitterTest, SkipProtobufMessagesInternals) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(RunFrontendAction(
                  R"(namespace google::protobuf {
                       class Message {};
                     }
                     class MySpecialType {
                       int x;
                     };
                     class MyMessage : public google::protobuf::Message {
                       MySpecialType member;
                     };
                     extern "C" void TakesAMessage(MyMessage*);)",
                  std::make_unique<GeneratorAction>(&emitter, &options)),
              IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("")),
              ElementsAre("class MyMessage"));
}

TEST_F(EmitterTest, Namespaced) {
  GeneratorOptions options;
  EmitterForTesting emitter(&options);
  EXPECT_THAT(
      RunFrontendAction(R"(namespace sandboxed {
                           struct S { int member; };
                           }  // namespace sandboxed
                           extern "C" sandboxed::S* Structize();)",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("sandboxed")),
              ElementsAre("struct S { int member; }"));

  SAPI_ASSERT_OK_AND_ASSIGN(std::string header, emitter.EmitHeader());
  // Expect the namespace to be preserved.
  EXPECT_THAT(header, HasSubstr("::absl::StatusOr<sandboxed::S*> Structize()"));
}

TEST_F(EmitterTest, StripNamespacePrefix) {
  GeneratorOptions options;
  options.namespace_name = "sandboxed";
  EmitterForTesting emitter(&options);
  EXPECT_THAT(
      RunFrontendAction(R"(namespace sandboxed {
                           struct S { int member; };
                           }  // namespace sandboxed
                           extern "C" sandboxed::S* Structize();)",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("sandboxed")),
              ElementsAre("struct S { int member; }"));

  SAPI_ASSERT_OK_AND_ASSIGN(std::string header, emitter.EmitHeader());
  // Expect the namespace prefix to be stripped, as `Structize` will also be
  // emitted in the `sandboxed` namespace.
  EXPECT_THAT(header, HasSubstr("::absl::StatusOr<S*> Structize()"));
}

TEST_F(EmitterTest, KeepTextualNamespacePrefix) {
  GeneratorOptions options;
  options.namespace_name = "sandboxed";
  EmitterForTesting emitter(&options);
  EXPECT_THAT(
      RunFrontendAction(R"(namespace sandboxed_ns {
                           struct S { int member; };
                           }  // namespace sandboxed_ns
                           extern "C" sandboxed_ns::S* Structize();)",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("sandboxed_ns")),
              ElementsAre("struct S { int member; }"));

  SAPI_ASSERT_OK_AND_ASSIGN(std::string header, emitter.EmitHeader());
  // Keep the namespace prefix, `sandboxed` and `sandboxed_ns` are different
  // namespaces.
  EXPECT_THAT(header,
              HasSubstr("::absl::StatusOr<sandboxed_ns::S*> Structize()"));
}

TEST_F(EmitterTest, StripNamespacePrefixNested) {
  GeneratorOptions options;
  options.namespace_name = "sandboxed";
  EmitterForTesting emitter(&options);
  EXPECT_THAT(
      RunFrontendAction(R"(namespace sandboxed::nested {
                           struct S { int member; };
                           }  // namespace sandboxed::nested
                           extern "C" sandboxed::nested::S* Structize();)",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());
  EXPECT_THAT(emitter.GetRenderedFunctions(), SizeIs(1));

  EXPECT_THAT(UglifyAll(emitter.SpellingsForNS("sandboxed::nested")),
              ElementsAre("struct S { int member; }"));

  SAPI_ASSERT_OK_AND_ASSIGN(std::string header, emitter.EmitHeader());
  // Expect the namespace prefix to be stripped, similar to the
  // StripNamespacePrefix test.
  EXPECT_THAT(header, HasSubstr("::absl::StatusOr<nested::S*> Structize()"));
}

TEST_F(EmitterTest, SymbolListTest) {
  constexpr absl::string_view kInputFile = "simple_functions.cc";
  GeneratorOptions options;
  options.set_function_names<std::initializer_list<std::string>>({});
  options.in_files = {"simple_functions.cc"};
  options.symbol_list_gen = true;

  SymbolListEmitter emitter;
  ASSERT_THAT(
      RunFrontendActionOnFile(
          kInputFile, std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  absl::StatusOr<std::string> header = emitter.Emit(options);
  ASSERT_THAT(header, IsOk());
  EXPECT_EQ(*header, "ExposedFunction\nOtherFunction\n_Z11CppFunctionv\n");
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

class SandboxeeTest : public EmitterTest {};

TEST_F(SandboxeeTest, PrototypesAreCorrectlyGenerated) {
  GeneratorOptions options;
  options.namespace_name = "sandboxed";
  options.sandboxee_src_out = "sandboxee_src.cc";
  EmitterForTesting emitter(&options);
  EXPECT_THAT(
      RunFrontendAction(R"(namespace sandboxed {
                           struct S { int member; };
                           }  // namespace sandboxed
                           enum E { A, B, C };
                           typedef unsigned long long MyType;
                           extern "C" sandboxed::S* Structize();
                           extern "C" MyType Add(MyType a, MyType b);
                           extern "C" void Enumify(E e);)",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string sandboxee_src,
                            emitter.EmitSandboxeeSrc());

  sandboxee_src = UglifyAll({sandboxee_src})[0];

  EXPECT_THAT(sandboxee_src,
              HasSubstr("extern \"C\" { "
                        "void* Structize(); "
                        "unsigned long long Add(unsigned long long, unsigned "
                        "long long); "
                        "void Enumify(unsigned int); "
                        "}"));
}

TEST_F(SandboxeeTest, StubsAreCallingSandboxedFunctions) {
  GeneratorOptions options;
  options.namespace_name = "sandboxed";
  options.sandboxee_src_out = "sandboxee_src.cc";
  EmitterForTesting emitter(&options);
  EXPECT_THAT(
      RunFrontendAction(R"(namespace sandboxed {
                           struct S { int member; };
                           }  // namespace sandboxed
                           enum E { A, B, C };
                           typedef unsigned long long MyType;
                           extern "C" sandboxed::S* Structize();
                           extern "C" MyType Add(MyType a, MyType b);
                           extern "C" void Enumify(E e);)",
                        std::make_unique<GeneratorAction>(&emitter, &options)),
      IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string sandboxee_src,
                            emitter.EmitSandboxeeSrc());

  sandboxee_src = UglifyAll({sandboxee_src})[0];

  EXPECT_THAT(
      sandboxee_src,
      ContainsRegex("FuncRet FuncHandlerStructize.*{.*Structize\\(.*\\);.*}"));

  EXPECT_THAT(sandboxee_src,
              ContainsRegex("FuncRet FuncHandlerAdd.*{.*Add\\(.*\\);.*}"));

  EXPECT_THAT(sandboxee_src, ContainsRegex("FuncRet FuncHandlerEnumify"
                                           ".*{.*Enumify\\(.*\\);.*}"));
}

}  // namespace
}  // namespace sapi
