// Copyright 2021 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xls/contrib/xlscc/translator.h"

#include <cstdio>
#include <memory>
#include <ostream>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "xls/common/file/temp_file.h"
#include "xls/common/status/matchers.h"
#include "xls/common/status/status_macros.h"
#include "xls/contrib/xlscc/hls_block.pb.h"
#include "xls/contrib/xlscc/metadata_output.pb.h"
#include "xls/contrib/xlscc/unit_test.h"
#include "xls/interpreter/function_interpreter.h"
#include "xls/ir/bits.h"
#include "xls/ir/ir_test_base.h"
#include "xls/ir/nodes.h"
#include "xls/ir/value.h"

using xls::status_testing::IsOkAndHolds;

// TODO(seanhaskell): Reimplement unsequenced assignment detection
#define UNSEQUENCED_TESTS 0

namespace xlscc {
namespace {

using xls::status_testing::IsOkAndHolds;

class TranslatorTest : public XlsccTestBase {
 public:
};

TEST_F(TranslatorTest, IntConst) {
  const std::string content = R"(
    int my_package(int a) {
      return 123;
    })";
  Run({{"a", 100}}, 123, content);
}

TEST_F(TranslatorTest, LongConst) {
  const std::string content = R"(
      int my_package(int a) {
        return 123L;
      })";

  Run({{"a", 100}}, 123, content);
}

TEST_F(TranslatorTest, LongLongConst) {
  const std::string content = R"(
      long long my_package(long long a) {
        return 123L;
      })";

  Run({{"a", 100}}, 123, content);
}

TEST_F(TranslatorTest, LongLongTrueConst) {
  const std::string content = R"(
      long long my_package(long long a) {
        return 123LL;
      })";

  Run({{"a", 100}}, 123, content);
}

TEST_F(TranslatorTest, SyntaxError) {
  const std::string content = R"(
      int my_package(int a) {
        return a+
      })";

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kFailedPrecondition,
                  testing::HasSubstr("Unable to parse text")));
}

TEST_F(TranslatorTest, Assignment) {
  {
    const std::string content = R"(
        int my_package(int a) {
          a = 5;
          return a;
        })";

    Run({{"a", 1000}}, 5, content);
  }
  {
    const std::string content = R"(
        int my_package(int a) {
          a = 5;
          return a = 10;
        })";

    Run({{"a", 1000}}, 10, content);
  }
}

TEST_F(TranslatorTest, ChainedAssignment) {
  const std::string content = R"(
      int my_package(int a) {
        a += 5;
        a += 10;
        return a;
      })";

  Run({{"a", 1000}}, 1015, content);
}

TEST_F(TranslatorTest, UnsignedChar) {
  const std::string content = R"(
      unsigned char my_package(unsigned char a) {
        return a+5;
      })";

  Run({{"a", 100}}, 105, content);
}

TEST_F(TranslatorTest, SignedChar) {
  const std::string content = R"(
      bool my_package(signed char a) {
        return a < 1;
      })";

  Run({{"a", 0xff}}, static_cast<int64_t>(true), content);
  Run({{"a", 2}}, static_cast<int64_t>(false), content);
}

TEST_F(TranslatorTest, Bool) {
  const std::string content = R"(
      int my_package(long long a) {
        return bool(a);
      })";

  Run({{"a", 1000}}, 1, content);
  Run({{"a", 0}}, 0, content);
  Run({{"a", -1}}, 1, content);
}

TEST_F(TranslatorTest, DeclGroup) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        long long aa=a, bb=b;
        return aa+bb;
      })";

  Run({{"a", 10}, {"b", 20}}, 30, content);
}

TEST_F(TranslatorTest, Short) {
  const std::string content = R"(
      short my_package(short a, short b) {
        return a+b;
      })";

  Run({{"a", 100}, {"b", 200}}, 300, content);
}

TEST_F(TranslatorTest, UShort) {
  const std::string content = R"(
      unsigned short my_package(unsigned short a, unsigned short b) {
        return a+b;
      })";

  Run({{"a", 100}, {"b", 200}}, 300, content);
}

TEST_F(TranslatorTest, Typedef) {
  const std::string content = R"(
      typedef long long my_int;
      my_int my_package(my_int a) {
        return a*10;
      })";

  Run({{"a", 4}}, 40, content);
}

TEST_F(TranslatorTest, IrAsm) {
  const std::string content = R"(
      long long my_package(long long a) {
       int asm_out;
       asm (
           "fn (fid)(x: bits[i]) -> bits[r] { "
           "   ret op_(aid): bits[r] = bit_slice(x, start=s, width=r) }"
         : "=r" (asm_out)
         : "i" (64), "s" (1), "r" (32), "param0" (a));
       return asm_out;
      })";

  Run({{"a", 1000}}, 500, content);
}

TEST_F(TranslatorTest, ArrayParam) {
  const std::string content = R"(
       long long my_package(const long long arr[2]) {
         return arr[0]+arr[1];
       })";
  XLS_ASSERT_OK_AND_ASSIGN(std::string ir_src, SourceToIr(content));
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<xls::Package> package,
                           ParsePackage(ir_src));
  XLS_ASSERT_OK(package->SetTopByName("my_package"));
  std::vector<uint64_t> in_vals = {55, 20};
  XLS_ASSERT_OK_AND_ASSIGN(xls::Value in_arr,
                           xls::Value::UBitsArray(in_vals, 64));
  absl::flat_hash_map<std::string, xls::Value> args;
  args["arr"] = in_arr;
  XLS_ASSERT_OK_AND_ASSIGN(xls::Function * entry, package->GetTopAsFunction());

  auto x = DropInterpreterEvents(xls::InterpretFunctionKwargs(
      entry, {{"arr", xls::Value::UBitsArray({55, 20}, 64).value()}}));

  ASSERT_THAT(x, IsOkAndHolds(xls::Value(xls::UBits(75, 64))));
}

TEST_F(TranslatorTest, ArraySet) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         long long arr[4];
         arr[0] = a;
         arr[1] = b;
         return arr[0]+arr[1];
       })";

  Run({{"a", 11}, {"b", 50}}, 61, content);
}

TEST_F(TranslatorTest, IncrementInArrayIndex1) {
  const std::string content = R"(
       long long my_package(long long a) {
         int arr[4];
         arr[a++] = 5;
         return a;
       })";

  Run({{"a", 11}}, 12, content);
}

TEST_F(TranslatorTest, IncrementInArrayIndex2) {
  const std::string content = R"(
       struct Blah {
         int operator=(int x) {
           return 0;
         }
       };
       long long my_package(long long a) {
         Blah arr[4];
         arr[a++] = 5;
         return a;
       })";

  Run({{"a", 11}}, 12, content);
}

TEST_F(TranslatorTest, Array2D) {
  const std::string content = R"(
       int my_package(int a, int b) {
         int x[2][2] = {{b,b}, {b,b}};
         x[1][0] += a;
         return x[1][0];
       })";

  Run({{"a", 55}, {"b", 100}}, 155, content);
}

TEST_F(TranslatorTest, Array2DParam) {
  const std::string content = R"(
       int access_it(int x[2][2]) {
         return x[1][0];
       }
       int my_package(int a, int b) {
         int x[2][2] = {{b,b}, {b,b}};
         x[1][0] += a;
         return access_it(x);
       })";

  Run({{"a", 55}, {"b", 100}}, 155, content);
}

TEST_F(TranslatorTest, Array2DConstParam) {
  const std::string content = R"(
       int access_it(const int x[2][2]) {
         return x[1][0];
       }
       int my_package(int a, int b) {
         int x[2][2] = {{b,b}, {b,b}};
         x[1][0] += a;
         return access_it(x);
       })";

  Run({{"a", 55}, {"b", 100}}, 155, content);
}

TEST_F(TranslatorTest, Array2DInit) {
  const std::string content = R"(
       struct ts {
         ts(int v) : x(v) { };
         operator int () const { return x; }
         ts operator += (int v) { x += v; return (*this); }
         int x;
       };
       int my_package(int a, int b) {
         int x[2][2] = {{b,b}, {b,b}};
         x[1][0] += a;
         return x[1][0];
       })";
  Run({{"a", 55}, {"b", 100}}, 155, content);
}

TEST_F(TranslatorTest, Array2DClass) {
  const std::string content = R"(
       struct ts {
         ts(int v) : x(v) { };
         operator int () const { return x; }
         ts operator += (int v) { x += v; return (*this); }
         int x;
       };
       int my_package(int a, int b) {
         ts x[2][2] = {{b,b}, {b,b}};
         x[1][0] += a;
         return x[1][0];
       })";
  Run({{"a", 55}, {"b", 100}}, 155, content);
}

TEST_F(TranslatorTest, ArrayInitList) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         long long arr[2] = {10, 20};
         arr[0] += a;
         arr[1] += b;
         return arr[0]+arr[1];
       })";
  Run({{"a", 11}, {"b", 50}}, 91, content);
}

TEST_F(TranslatorTest, ArrayRefParam) {
  const std::string content = R"(
       void asd(int b[2]) {
         b[0] += 5;
       }
       int my_package(int a) {
         int arr[2] = {a, 3*a};
         asd(arr);
         return arr[0] + arr[1];
       })";
  Run({{"a", 11}}, 11 + 5 + 3 * 11, content);
}

TEST_F(TranslatorTest, ArrayInitListWrongSize) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         long long arr[4] = {10, 20};
         return a;
       })";
  ASSERT_FALSE(SourceToIr(content).ok());
}

TEST_F(TranslatorTest, ArrayTooManyInitListValues) {
  const std::string content = R"(
       long long my_package() {
         long long arr[1] = {4, 5};
         return arr[0];
       })";
  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kFailedPrecondition,
                  testing::HasSubstr("Unable to parse text")));
}

TEST_F(TranslatorTest, ArrayInitListMismatchedSizeZeros) {
  const std::string content = R"(
       long long my_package() {
         long long arr[4] = {};
         return arr[3];
       })";
  ASSERT_FALSE(SourceToIr(content).ok());
}

TEST_F(TranslatorTest, ArrayInitListMismatchedSizeMultipleZeros) {
  const std::string content = R"(
       long long my_package() {
         long long arr[4] = {0, 0};
         return arr[3];
       })";
  ASSERT_FALSE(SourceToIr(content).ok());
}

TEST_F(TranslatorTest, ArrayInitListMismatchedSizeOneNonZeros) {
  const std::string content = R"(
       long long my_package() {
         long long arr[4] = {9};
         return arr[3];
       })";
  ASSERT_FALSE(SourceToIr(content).ok());
}

TEST_F(TranslatorTest, ArrayInitListMismatchedSizeOneWithZeros) {
  const std::string content = R"(
       long long my_package() {
         long long arr[4] = {0};
         return arr[3];
       })";
  Run({}, 0, content);
}

TEST_F(TranslatorTest, ArrayInitListMismatchedSizeOneWithDefaultStruct) {
  const std::string content = R"(
       struct x {
         int a;
       };
       long long my_package() {
         x arr[4] = {{}};
         return arr[3].a;
       })";
  Run({}, 0, content);
}

TEST_F(TranslatorTest, ConstInitListExpr) {
  const std::string content = R"(
    int my_package(int a) {
      const int test_arr[][6] = {
          {  10,  0,  0,  0,  0,  0 },
          {  a,  20,  0,  0,  0,  0 }
      };
      return test_arr[0][0] + test_arr[1][1] + test_arr[1][0];
    })";
  Run({{"a", 3}}, 33, content);
}

TEST_F(TranslatorTest, InitListExpr) {
  const std::string content = R"(
    int my_package(int a) {
      int test_arr[][6] = {
          {  10,  0,  0,  0,  0,  0 },
          {  a,  20,  0,  0,  0,  0 }
      };
      return test_arr[0][0] + test_arr[1][1] + test_arr[1][0];
    })";
  Run({{"a", 3}}, 33, content);
}

TEST_F(TranslatorTest, ArrayInitLoop) {
  const std::string content = R"(
       struct tss {
         tss() : ss(15) {}
         tss(const tss &o) : ss(o.ss) {}
         int ss;
       };
       struct ts { tss vv[4]; };
       long long my_package(long long a) {
         ts x;
         x.vv[0].ss = a;
         ts y = x;
         return y.vv[0].ss;
       })";
  Run({{"a", 110}}, 110, content);
}

TEST_F(TranslatorTest, StringConstantArray) {
  const std::string content = R"(
       long long my_package(long long a) {
         const char foo[] = "A";
         return a+foo[0];
       })";
  Run({{"a", 11}}, 11 + 'A', content);
}

TEST_F(TranslatorTest, GlobalInt) {
  const std::string content = R"(
       const int off = 60;
       int foo() {
         // Reference it from another function to test context management
         return off;
       }
       long long my_package(long long a) {
         // Reference it twice to test global value re-use
         long long ret = a+foo();
         // Check context pop
         {
           ret += off;
         }
         ret += off;
         return ret;
       })";
  Run({{"a", 11}}, 11 + 60 + 60 + 60, content);
}

TEST_F(TranslatorTest, GlobalEnum) {
  const std::string content = R"(
       enum BlahE {
         A=2,B,C
       };
       long long my_package(long long a) {
         return a+B+B;
       })";
  Run({{"a", 11}}, 11 + 3 + 3, content);
}

TEST_F(TranslatorTest, MaximumEnum) {
  const std::string content = R"(
       enum BlahE {
         A=2,
         B=0xFFFFFFFF
       };
       long long my_package(long long a) {
         return a+B+B;
       })";
  Run({{"a", 11}}, 11L + 0xFFFFFFFFL + 0xFFFFFFFFL, content);
}

TEST_F(TranslatorTest, SetGlobal) {
  const std::string content = R"(
       int off = 60;
       long long my_package(long long a) {
         off = 5;
         long long ret = a;
         ret += off;
         return ret;
       })";
  ASSERT_FALSE(SourceToIr(content).ok());
}

TEST_F(TranslatorTest, UnsequencedAssign) {
  const std::string content = R"(
      int my_package(int a) {
        return (a=7)+a;
      })";
  auto ret = SourceToIr(content);

  // Clang catches this one and fails parsing
  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kFailedPrecondition,
                                    testing::HasSubstr("parse")));
}

#if UNSEQUENCED_TESTS

TEST_F(TranslatorTest, UnsequencedRefParam) {
  const std::string content = R"(
      int make7(int &a) {
        return a=7;
      }
      int my_package(int a) {
        return make7(a)+a;
      })";

  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                            testing::HasSubstr("unsequenced")));
}
TEST_F(TranslatorTest, UnsequencedRefParam2) {
  const std::string content = R"(
      int make7(int &a) {
        return a=7;
      }
      int my_package(int a) {
        return a+make7(a);
      })";

  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                            testing::HasSubstr("unsequenced")));
}

TEST_F(TranslatorTest, UnsequencedRefParam3) {
  const std::string content = R"(
      int make7(int &a) {
        return a=7;
      }
      int my_package(int a) {
        return make7(a)+a;
      })";

  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                            testing::HasSubstr("unsequenced")));
}

TEST_F(TranslatorTest, UnsequencedRefParam4) {
  const std::string content = R"(
      int my_package(int a) {
        return (a=7)?a:11;
      })";
  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                            testing::HasSubstr("unsequenced")));
}
TEST_F(TranslatorTest, UnsequencedRefParam5) {
  const std::string content = R"(
      int my_package(int a) {
        return a?a:(a=7);
      })";
  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                            testing::HasSubstr("unsequenced")));
}

// Okay with one parameter
TEST_F(TranslatorTest, AvoidUnsequencedRefParamUnary) {
  const std::string content = R"(
      long long nop(long long a) {
        return a;
      }
      long long my_package(long long a) {
        return -nop(a=10);
      };)";

  Run({{"a", 100}}, -10, content);
}

TEST_F(TranslatorTest, UnsequencedRefParamBinary) {
  const std::string content = R"(
      int nop(int a, int b) {
        return a;
      }
      int my_package(int a) {
        return -nop(a=10, 100);
      })";
  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                            testing::HasSubstr("unsequenced")));
}

#endif  // UNSEQUENCED_TESTS

TEST_F(TranslatorTest, OpAssignmentResult) {
  const std::string content = R"(
      int my_package(int a) {
        return a+=5;
      })";

  Run({{"a", 100}}, 105, content);
}

TEST_F(TranslatorTest, UndefinedConditionalAssign) {
  const std::string content = R"(
      int my_package(int a) {
        int ret;
        if(a) {
          ret = 11;
        }
        return ret;
      })";

  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kFailedPrecondition,
                                    testing::HasSubstr("Unable to parse")));
}

TEST_F(TranslatorTest, IfStmt) {
  const std::string content = R"(
      long long my_package(long long a) {
        if(a<-100) a = 1;
        else if(a<-10) a += 3;
        else { a *= 2; }
        return a;
      })";

  Run({{"a", 60}}, 120, content);
  Run({{"a", -50}}, -47, content);
  Run({{"a", -150}}, 1, content);
}

TEST_F(TranslatorTest, IfAssignOverrideCondition) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        if(a>1000) {
          if(b)
            a=55;
          a=1234;
        }
        return a;
      })";

  Run({{"a", 60}, {"b", 0}}, 60, content);
  Run({{"a", 1001}, {"b", 0}}, 1234, content);
  Run({{"a", 1001}, {"b", 1}}, 1234, content);
}

TEST_F(TranslatorTest, SwitchStmt) {
  const std::string content = R"(
       long long my_package(long long a) {
         long long ret;
         switch(a) {
           case 1:
             ret = 100;
             break;
           case 2:
             ret = 200;
             break;
           default:
             ret = 300;
             break;
         }
         return ret;
       })";

  Run({{"a", 1}}, 100, content);
  Run({{"a", 2}}, 200, content);
  Run({{"a", 3}}, 300, content);
}

TEST_F(TranslatorTest, SwitchConditionalBreak) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         long long ret;
         switch(a) {
           case 1:
             ret = 100;
             break;
           case 2:
             ret = 200;
             if(b) break;
           default:
             ret = 300;
             break;
         }
         return ret;
       })";

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kUnimplemented,
                  testing::HasSubstr("Conditional breaks are not supported")));
}

TEST_F(TranslatorTest, SwitchStmtDefaultTop) {
  const std::string content = R"(
       long long my_package(long long a) {
         long long ret;
         switch(a) {
           default:
             ret = 300;
             break;
           case 1: {
             ret = 100;
             break;
           } case 2:
             ret = 200;
             break;
         }
         return ret;
       })";

  Run({{"a", 1}}, 100, content);
  Run({{"a", 2}}, 200, content);
  Run({{"a", 3}}, 300, content);
}

TEST_F(TranslatorTest, SwitchMultiCaseMultiLine) {
  const std::string content = R"(
       long long my_package(long long a) {
         long long ret=0;
         switch(a) {
           case 1:
             ret += 300;
             ret += 2;
           case 2:
             ret += 5;
             ret += 100;
             break;
         }
         return ret;
       })";

  Run({{"a", 1}}, 407, content);
  Run({{"a", 2}}, 105, content);
  Run({{"a", 3}}, 0, content);
}

TEST_F(TranslatorTest, SwitchMultiCaseMultiLineBrace) {
  const std::string content = R"(
       long long my_package(long long a) {
         long long ret=0;
         switch(a) {
           case 1:
             ret += 300;
             ret += 2;
           case 2: {
             ret += 5;
             ret += 100;
             break;
           }
         }
         return ret;
       })";

  Run({{"a", 1}}, 407, content);
  Run({{"a", 2}}, 105, content);
  Run({{"a", 3}}, 0, content);
}

TEST_F(TranslatorTest, SwitchDoubleBreak) {
  const std::string content = R"(
       long long my_package(long long a) {
         long long ret=0;
         switch(a) {
           case 1:
             ret += 300;
             ret += 2;
             break;
             break;
           case 2: {
             ret += 5;
             ret += 100;
             break;
             break;
           }
         }
         return ret;
       })";

  Run({{"a", 1}}, 302, content);
  Run({{"a", 2}}, 105, content);
  Run({{"a", 3}}, 0, content);
}

TEST_F(TranslatorTest, SwitchMultiCase) {
  const std::string content = R"(
       long long my_package(long long a) {
         long long ret=0;
         switch(a) {
           case 1:
             ret += 300;
           case 2:
             ret += 100;
             break;
         }
         return ret;
       })";

  Run({{"a", 1}}, 400, content);
  Run({{"a", 2}}, 100, content);
  Run({{"a", 3}}, 0, content);
}

TEST_F(TranslatorTest, SwitchReturnStmt) {
  const std::string content = R"(
       long long my_package(long long a) {
         switch(a) {
           case 1:
             return 100;
           case 2:
             return 200;
           default:
             return 300;
         }
       })";

  Run({{"a", 1}}, 100, content);
  Run({{"a", 2}}, 200, content);
  Run({{"a", 3}}, 300, content);
}

TEST_F(TranslatorTest, SwitchDeepFlatten) {
  const std::string content = R"(
       long long my_package(long long a) {
         switch(a) {
           case 1:
           case 2:
           default:
             return 300;
         }
       })";

  Run({{"a", 1}}, 300, content);
  Run({{"a", 2}}, 300, content);
  Run({{"a", 3}}, 300, content);
}

TEST_F(TranslatorTest, SwitchReturnStmt2) {
  const std::string content = R"(
       long long my_package(long long a) {
         switch(a) {
           case 1:
             return 100;
           case 2:
             a+=10;
             break;
         }
         return a;
       })";

  Run({{"a", 1}}, 100, content);
  Run({{"a", 2}}, 12, content);
  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, SwitchDefaultPlusCase) {
  const std::string content = R"(
       long long my_package(long long a) {
         switch(a) {
           default:
           case 1:
             return 100;
           case 2:
             a+=10;
             break;
         }
         return a;
       })";

  Run({{"a", 1}}, 100, content);
  Run({{"a", 2}}, 12, content);
  Run({{"a", 3}}, 100, content);
}

TEST_F(TranslatorTest, SwitchInFor) {
  const std::string content = R"(
       long long my_package(long long a) {
         #pragma hls_unroll yes
         for(int i=0;i<2;++i) {
           switch(i) {
             case 0:
               a += 300;
               break;
             case 1:
               a += 100;
               break;
           }
         }
         return a;
       })";

  Run({{"a", 1}}, 401, content);
}

TEST_F(TranslatorTest, SwitchBreakAfterReturn) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         long long ret=0;
         switch(a) {
           case 1:
             if(b > 0) {return -1000;};
             ret += b;
             break;
         }
         return ret;
       })";

  Run({{"a", 5}, {"b", 1}}, 0, content);
  Run({{"a", 1}, {"b", 1}}, -1000, content);
  Run({{"a", 1}, {"b", -10}}, -10, content);
}

TEST_F(TranslatorTest, ForInSwitch) {
  const std::string content = R"(
       long long my_package(long long a) {
         switch(a) {
           case 0:
             #pragma hls_unroll yes
             for(int i=0;i<3;++i) {
               a+=10;
               if(a > 110) {
                 break;
               }
             }
             break;
           case 1:
             a += 100;
             break;
         }
         return a;
       })";

  Run({{"a", 0}}, 30, content);
  Run({{"a", 1}}, 101, content);
  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, ForUnroll) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        for(int i=1;i<=10;++i) {
          a += b;
          a += 2*b;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, ForUnrollLabel) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        label:
        for(int i=1;i<=10;++i) {
          a += b;
          a += 2*b;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, WhileUnroll) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        int i=1;
        #pragma hls_unroll yes
        while(i<=10) {
          a += b;
          a += 2*b;
          ++i;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, DoWhileUnroll) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        int i=1;
        #pragma hls_unroll yes
        do {
          a += b;
          ++i;
        } while(i<=10 && a<10);
        return a;
      })";
  Run({{"a", 100}, {"b", 20}}, 120, content);
  Run({{"a", 3}, {"b", 2}}, 11, content);
  Run({{"a", 3}, {"b", 0}}, 3, content);
}

TEST_F(TranslatorTest, WhileUnrollShortCircuit) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        int i=1;
        #pragma hls_unroll yes
        while(i<=10) {
          a += b;
          ++i;
          if(a > 60) {
            break;
          }
        }
        return (i*10) + a;
      })";
  Run({{"a", 11}, {"b", 20}}, 111, content);
}

TEST_F(TranslatorTest, WhileUnrollFalseCond) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        int i=1;
        #pragma hls_unroll yes
        while(i<1) {
          a += b;
          a += 2*b;
          ++i;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 11, content);
}

TEST_F(TranslatorTest, WhileUnrollFalseCond2) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        int i=1;
        #pragma hls_unroll yes
        while(i<=10 && a<0) {
          a += b;
          a += 2*b;
          ++i;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 11, content);
}

TEST_F(TranslatorTest, WhileUnrollFalseCond3) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        int i=1;
        #pragma hls_unroll yes
        for(;i<=10 && a<0;++i) {
          a += b;
          a += 2*b;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 11, content);
}

TEST_F(TranslatorTest, ForUnrollShortCircuit) {
  const std::string content = R"(

        long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<10 && a<=100;++i) {
           a += b;
          }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 111, content);
}

TEST_F(TranslatorTest, ForUnrollShortCircuit2) {
  const std::string content = R"(

        long long my_package(long long a, long long b) {
        int i=0;
         #pragma hls_unroll yes
         for(;i<10 && a<=100;++i) {
           a += b;
          }
         return a+i;
       })";
  Run({{"a", 11}, {"b", 20}}, 111 + 5, content);
}

// Check that continue doesn't skip loop increment
TEST_F(TranslatorTest, ForUnrollShortCircuit2A) {
  const std::string content = R"(
        long long my_package(long long a, long long b) {
         int i=0;
         #pragma hls_unroll yes
         for(;i<10;++i) {
           if(a>100) continue;
           a += b;
          }
         return a+i;
       })";
  Run({{"a", 11}, {"b", 20}}, 111 + 10, content);
}

TEST_F(TranslatorTest, ForUnrollShortCircuit3) {
  const std::string content = R"(
        template<int N>
        long sum(long in[N], int n) {
          long sum = 0;
          #pragma hls_unroll yes
          for (int i = 0; i < N && i < n; ++i) sum += in[i];
          return sum;
        }

        long long my_package(long long a, long long b) {
          long in[4] = {0,a,b,100};
          return sum<4>(in, 3);
       })";
  Run({{"a", 11}, {"b", 20}}, 31, content);
}

TEST_F(TranslatorTest, ForUnrollShortCircuit4) {
  const std::string content = R"(
         struct TestInt {
           TestInt(long long v) : x(v) { }
           int operator[](int i)const {
             return (x >> i)&1;
           }
           TestInt operator ++() {
             ++x;
             return *this;
           }
           bool operator <(int v)const {
             return x < v;
           }
           operator int() const {
             return x;
           }
           int x;
         };

        template <int W>
        long Ctz(TestInt in) {
          TestInt lz = 0;
          #pragma hls_unroll yes
          while (lz < W && in[lz] == 0) {
            ++lz;
          }
          return lz;
        }

        long long my_package(long long a) {
          return Ctz<8>(a);
       })";
  Run({{"a", 0b00001000}}, 3, content);
}

TEST_F(TranslatorTest, ForUnrollShortCircuitClass) {
  const std::string content = R"(
       struct TestInt {
         TestInt(int v) : x(v) { }
         operator int()const {
           return x;
         }
         TestInt operator ++() {
           ++x;
           return *this;
         }
         bool operator <=(int v)const {
           return x <= v;
         }
         int x;
       };
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(TestInt i=1;i<=10 && a<1000;++i) {
           a += b;
           a += 2*b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, ForUnrollMultiCondBreak) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        for(int i=1;i<10;++i) {
          if(a<20) {
            break;
          }
          if(a>100) {
            break;
          }
          a += b;
        }
        return a;
      })";
  Run({{"a", 21}, {"b", 20}}, 101, content);
  Run({{"a", 11}, {"b", 20}}, 11, content);
}

TEST_F(TranslatorTest, ForUnrollNestedCondBreak) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        for(int i=1;i<10;++i) {
          if(a>50) {
            if(i<4) {
              break;
            }
          }
          a += b;
        }
        return a;
      })";
  Run({{"a", 51}, {"b", 20}}, 51, content);
  Run({{"a", 43}, {"b", 1}}, 52, content);
  Run({{"a", 0}, {"b", 3}}, 27, content);
}

TEST_F(TranslatorTest, ForUnrollClass) {
  const std::string content = R"(
       struct TestInt {
         TestInt(int v) : x(v) { }
         operator int()const {
           return x;
         }
         TestInt operator ++() {
           ++x;
           return *this;
         }
         bool operator <=(int v) {
           return x <= v;
         }
         int x;
       };
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(TestInt i=1;i<=10;++i) {
           a += b;
           a += 2*b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, ForUnrollConditionallyAssignLoopVar) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<10;++i) {
           a += b;
           if(a > 40) {
             ++i;
           }
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 131, content);
}

TEST_F(TranslatorTest, ForUnrollNoInit) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        int i=1;
        #pragma hls_unroll yes
        for(;i<=10;++i) {
          a += b;
          a += 2*b;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, ForUnrollNoInc) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        for(int i=1;i<=10;) {
          a += b;
          a += 2*b;
          ++i;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, ForUnrollNoCond) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        for(int i=1;;++i) {
          a += b;
          a += 2*b;
        }
        return a;
      })";
  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kResourceExhausted,
                                    testing::HasSubstr("maximum")));
}

TEST_F(TranslatorTest, ForUnrollNoCondBreakInBody) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        for(int i=1;;++i) {
          if(i>10) {
            break;
          }
          a += b;
          a += 2*b;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, ForUnrollNoPragma) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        for(int i=1;i<=10;++i) {
          a += b;
          a += 2*b;
        }
        return a;
      })";
  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kUnimplemented,
                  testing::HasSubstr("loop missing #pragma")));
}

TEST_F(TranslatorTest, ForNestedUnroll) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        #pragma hls_unroll yes
        for(int i=1;i<=10;++i) {
          #pragma hls_unroll yes
          for(int j=0;j<4;++j) {
            int l = b;
            a += l;
          }
        }
        return a;
      })";
  Run({{"a", 200}, {"b", 20}}, 1000, content);
}

TEST_F(TranslatorTest, ForUnrollInfinite) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=1;i<=10;--i) {
           a += b;
           a += 2*b;
         }
         return a;
       })";
  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kResourceExhausted,
                                    testing::HasSubstr("maximum")));
}

TEST_F(TranslatorTest, ForUnrollBreak) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<9;++i) {
           if(a > 100) {
             break;
           }
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 111, content);
}

// Only one break condition is true, not all conditions after
TEST_F(TranslatorTest, ForUnrollBreakOnEquals) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<9;++i) {
           if(a == 31) {
             break;
           }
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 31, content);
}

TEST_F(TranslatorTest, ForUnrollBreakAfterAssignNested) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<4;++i) {
           a += b;
           if(a > 100) {
             if(b > 0) {
               break;
             }
           }
         }
         return a;
       })";
  Run({{"a", 101}, {"b", 20}}, 121, content);
}

TEST_F(TranslatorTest, ForUnrollContinueNested) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<6;++i) {
           if(i == 1) {
             if(b > 0) {
               continue;
             }
           }
           a += b;
         }
         return a;
       })";
  Run({{"a", 101}, {"b", 20}}, 201, content);
  Run({{"a", 100}, {"b", 20}}, 200, content);
  Run({{"a", 10}, {"b", 20}}, 110, content);
  Run({{"a", 10}, {"b", -1}}, 4, content);
}

TEST_F(TranslatorTest, ForUnrollBreakAfterAssign) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<3;++i) {
           a += b;
           if(a > 100) {
             break;
           }
         }
         return a;
       })";
  Run({{"a", 80}, {"b", 20}}, 120, content);
}

TEST_F(TranslatorTest, ForUnrollBreakAfterAssign2) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<2;++i) {
           a += b;
           if(a > 100) {
             break;
           }
         }
         return a;
       })";
  Run({{"a", 101}, {"b", 20}}, 121, content);
}

TEST_F(TranslatorTest, ForUnrollBreakTest) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<3;++i) {
           a += b;
           if(a > 100) {
             break;
           }
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 71, content);
  Run({{"a", 101}, {"b", 20}}, 121, content);
}

TEST_F(TranslatorTest, ForUnrollBreak2) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<50;++i) {
           if(i==3) break;
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 71, content);
}

TEST_F(TranslatorTest, ForUnrollBreak2Nested) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<50;++i) {
           if(a < 1000) {
             if(i==3) break;
           }
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 71, content);
}

TEST_F(TranslatorTest, ForUnrollBreak3) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<50;++i) {
           a += b;
           if(i==3) break;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 91, content);
}

TEST_F(TranslatorTest, ForUnrollBreak4) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<50;++i) {
           a += b;
           break;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 31, content);
}

TEST_F(TranslatorTest, ForUnrollContinue) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           a += b;
           continue;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 231, content);
}

TEST_F(TranslatorTest, ForUnrollContinue2) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           continue;
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 11, content);
}

TEST_F(TranslatorTest, ForUnrollContinue3) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           break;
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 11, content);
}

TEST_F(TranslatorTest, ForUnrollContinue4) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           if(a>155) {
             continue;
           }
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 171, content);
}

TEST_F(TranslatorTest, ForUnrollContinue5) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           a += b;
           if(a>155) {
             continue;
           }
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 231, content);
}

TEST_F(TranslatorTest, ForUnrollContinue6) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           {
             continue;
           }
           a += b;
         }
         return a;
       })";
  Run({{"a", 11}, {"b", 20}}, 11, content);
}

TEST_F(TranslatorTest, ReturnFromFor) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           return a;
           a += b;
         }
         return 0;
       })";
  Run({{"a", 233}, {"b", 0}}, 233, content);
}

TEST_F(TranslatorTest, ReturnFromFor2) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<11;++i) {
           a += b;
           return a;
         }
         return 0;
       })";
  Run({{"a", 233}, {"b", 20}}, 253, content);
}

TEST_F(TranslatorTest, ReturnFromFor3) {
  const std::string content = R"(
       long long my_package(long long a, long long b) {
         #pragma hls_unroll yes
         for(int i=0;i<10;++i) {
           a += b;
           if(a>500) return a;
         }
         return 0;
       })";
  Run({{"a", 140}, {"b", 55}}, 525, content);
}

TEST_F(TranslatorTest, ConditionalReturnStmt) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        if(b) {
          if(a<200) return 2200;
          if(a<500) return 5500;
        }
        return a;
      })";

  Run({{"a", 505}, {"b", 1}}, 505, content);
  Run({{"a", 455}, {"b", 1}}, 5500, content);
  Run({{"a", 101}, {"b", 1}}, 2200, content);
  Run({{"a", 505}, {"b", 0}}, 505, content);
  Run({{"a", 455}, {"b", 0}}, 455, content);
  Run({{"a", 101}, {"b", 0}}, 101, content);
}

TEST_F(TranslatorTest, DoubleReturn) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        if(b) {
          return b;
          return a;
        }
        return a;
        return b;
      })";

  Run({{"a", 11}, {"b", 0}}, 11, content);
  Run({{"a", 11}, {"b", 3}}, 3, content);
}

TEST_F(TranslatorTest, TripleReturn) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        return 66;
        return 66;
        return a;
      })";

  Run({{"a", 11}, {"b", 0}}, 66, content);
  Run({{"a", 11}, {"b", 3}}, 66, content);
}

TEST_F(TranslatorTest, VoidReturn) {
  const std::string content = R"(
      void my_package(int &a) {
        a = 22;
      })";

  Run({{"a", 1000}}, 22, content);
  Run({{"a", 221}}, 22, content);
}

TEST_F(TranslatorTest, AssignAfterReturn) {
  const std::string content = R"(
      void my_package(int &a) {
        return;
        a = 22;
      })";

  Run({{"a", 1000}}, 1000, content);
}

TEST_F(TranslatorTest, AssignAfterReturnInIf) {
  const std::string content = R"(
      void my_package(int &a) {
        if(a == 5) {
          return;
        }
        a = 22;
      })";

  Run({{"a", 5}}, 5, content);
  Run({{"a", 10}}, 22, content);
  Run({{"a", 100}}, 22, content);
}

TEST_F(TranslatorTest, AssignAfterReturn3) {
  const std::string content = R"(
      void ff(int x[8]) {
       x[4] = x[2];
       return;
       x[3] = x[4];
      };
      #pragma hls_top
      int my_package(int a, int b,int c,int d,int e,int f,int g,int h) {
          int arr[8] = {a,b,c,d,e,f,g,h};
          ff(arr);
          return arr[4]+arr[3]+arr[5];
      })";
  Run({{"a", 3},
       {"b", 4},
       {"c", 5},
       {"d", 6},
       {"e", 7},
       {"f", 8},
       {"g", 9},
       {"h", 10}},
      19, content);
}

TEST_F(TranslatorTest, CapitalizeFirstLetter) {
  const std::string content = R"(
       class State {
        public:
           State()
            : last_was_space_(true) {
          }
           unsigned char process(unsigned char c) {
           unsigned char ret = c;
           if(last_was_space_ && (c >= 'a') && (c <= 'z'))
             ret -= ('a' - 'A');
           last_was_space_ = (c == ' ');
           return ret;
         }
        private:
          bool last_was_space_;
       };
       unsigned char my_package(State &st, unsigned char c) {
         return st.process(c);
       })";
  XLS_ASSERT_OK_AND_ASSIGN(std::string ir_src, SourceToIr(content));
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<xls::Package> package,
                           ParsePackage(ir_src));
  XLS_ASSERT_OK(package->SetTopByName("my_package"));

  auto state =
      xls::Value(xls::Value::TupleOwned({xls::Value(xls::UBits(1, 1))}));

  const char* input = "hello world";
  std::string output = "";
  for (; *input != 0u; ++input) {
    const char inc = *input;
    XLS_ASSERT_OK_AND_ASSIGN(xls::Function * entry,
                             package->GetTopAsFunction());
    absl::flat_hash_map<std::string, xls::Value> args;
    args["st"] = state;
    args["c"] = xls::Value(xls::UBits(inc, 8));
    XLS_ASSERT_OK_AND_ASSIGN(
        xls::Value actual,
        DropInterpreterEvents(xls::InterpretFunctionKwargs(entry, args)));
    XLS_ASSERT_OK_AND_ASSIGN(std::vector<xls::Value> returns,
                             actual.GetElements());
    ASSERT_EQ(returns.size(), 2);
    XLS_ASSERT_OK_AND_ASSIGN(char outc, returns[0].bits().ToUint64());

    state = returns[1];
    output += outc;
  }

  ASSERT_EQ(output, "Hello World");
}

TEST_F(TranslatorTest, AssignmentInBlock) {
  const std::string content = R"(
      int my_package(int a) {
        int r = a;
        {
          r = 55;
        }
        return r;
      })";

  Run({{"a", 100}}, 55, content);
}

TEST_F(TranslatorTest, AssignmentInParens) {
  const std::string content = R"(
      int my_package(int a) {
        int r = a;
        (r) = 55;
        return r;
      })";

  Run({{"a", 100}}, 55, content);
}

TEST_F(TranslatorTest, ShadowAssigment) {
  const std::string content = R"(
      int my_package(int a) {
        int r = a;
        {
          int r = 22;
          r = 55;
          (void)r;
        }
        return r;
      })";

  Run({{"a", 100}}, 100, content);
}

TEST_F(TranslatorTest, CompoundStructAccess) {
  const std::string content = R"(
       struct TestX {
         int x;
       };
       struct TestY {
         TestX tx;
       };
       int my_package(int a) {
         TestY y;
         y.tx.x = a;
         return y.tx.x;
       })";
  Run({{"a", 56}}, 56, content);
}

TEST_F(TranslatorTest, SubstTemplateType) {
  constexpr const char* content = R"(
       struct TestR {
         int f()const {
           return 10;
         }
       };
       struct TestW {
         int f()const {
           return 11;
         }
       };
       template<typename T>
       int do_something(T a) {
         return a.f();
       }
       int my_package(int a) {
         %s t;
         return do_something(t);
       })";
  Run({{"a", 3}}, 10, absl::StrFormat(content, "TestR"));
  Run({{"a", 3}}, 11, absl::StrFormat(content, "TestW"));
}

TEST_F(TranslatorTest, TemplateStruct) {
  const std::string content = R"(
       template<typename T>
       struct TestX {
         T x;
       };
       int my_package(int a) {
         TestX<int> x;
         x.x = a;
         return x.x;
       })";
  Run({{"a", 56}}, 56, content);
}

TEST_F(TranslatorTest, ArrayOfStructsAccess) {
  const std::string content = R"(
       struct TestX {
         int x;
       };
       struct TestY {
         TestX tx;
       };
       int my_package(int a) {
         TestY y[3];
         y[2].tx.x = a;
         return y[2].tx.x;
       })";
  Run({{"a", 56}}, 56, content);
}

TEST_F(TranslatorTest, StructWithArrayAccess) {
  const std::string content = R"(
       struct TestX {
         int x[3];
       };
       struct TestY {
         TestX tx;
       };
       int my_package(int a) {
         TestY y;
         y.tx.x[2] = a;
         return y.tx.x[2];
       })";
  Run({{"a", 56}}, 56, content);
}

TEST_F(TranslatorTest, StructInitList) {
  const std::string content = R"(
       struct Test {
         long long x;
         long long y;
         long long z;
       };
       long long my_package(long long a, long long b) {
         Test ret = {a,5,b};
         return ret.x+ret.y+ret.z;
       })";
  Run({{"a", 11}, {"b", 50}}, 66, content);
}

TEST_F(TranslatorTest, StructConditionalAssign) {
  const std::string content = R"(
       struct Test {
         long long x;
         long long y;
         long long z;
       };
       long long my_package(long long a, long long b) {
         Test ret = {a,5,b};
         if(a>11) {
           ret.y = 100;
         }
         return ret.x+ret.y+ret.z;
       })";
  Run({{"a", 11}, {"b", 50}}, 11 + 5 + 50, content);
  Run({{"a", 12}, {"b", 50}}, 12 + 100 + 50, content);
}

TEST_F(TranslatorTest, StructInitListWrongCount) {
  const std::string content = R"(
       struct Test {
         long long x;
         long long y;
         long long z;
       };
       long long my_package(long long a, long long b) {
         Test ret = {a,b};
         return ret.x+ret.y+ret.z;
       })";
  Run({{"a", 11}, {"b", 50}}, 61, content);
}

TEST_F(TranslatorTest, StructInitListWithDefaultnt) {
  const std::string content = R"(
       struct Test {
         long long x;
         long long y;
         long long z = 100;
       };
       long long my_package(long long a, long long b) {
         Test ret = {a,b,10};
         return ret.x+ret.y+ret.z;
       })";
  Run({{"a", 11}, {"b", 50}}, 71, content);
}

TEST_F(TranslatorTest, StructInitListWithDefaultWrongCount) {
  const std::string content = R"(
       struct Test {
         long long x;
         long long y;
         long long z = 100;
       };
       long long my_package(long long a, long long b) {
         Test ret = {a,b};
         return ret.x+ret.y+ret.z;
       })";
  Run({{"a", 11}, {"b", 50}}, 161, content);
}

TEST_F(TranslatorTest, NoTupleStruct) {
  const std::string content = R"(
       #pragma hls_no_tuple
       struct Test {
         int x;
       };
       Test my_package(int a) {
         Test s;
         s.x=a;
         return s;
       })";
  Run({{"a", 311}}, 311, content);
}

TEST_F(TranslatorTest, NoTupleMultiField) {
  const std::string content = R"(
       #pragma hls_no_tuple
       struct Test {
         int x;
         int y;
       };
       Test my_package(int a) {
         Test s;
         s.x=a;
         return s;
       })";
  auto ret = SourceToIr(content);

  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kFailedPrecondition,
                                    testing::HasSubstr("only 1 field")));
}

TEST_F(TranslatorTest, NoTupleMultiFieldLineComment) {
  const std::string content = R"(
       //#pragma hls_no_tuple
       struct Test {
         int x;
         int y;
       };
       int my_package(int a) {
         Test s;
         s.x=a;
         return s.x;
       })";
  Run({{"a", 311}}, 311, content);
}

TEST_F(TranslatorTest, NoTupleMultiFieldBlockComment) {
  const std::string content = R"(
       /*
       #pragma hls_no_tuple*/
       struct Test {
         int x;
         int y;
       };
       int my_package(int a) {
         Test s;
         s.x=a;
         return s.x;
       })";
  Run({{"a", 311}}, 311, content);
}

TEST_F(TranslatorTest, StructMemberOrder) {
  const std::string content = R"(
       struct Test {
         int x;
         long y;
       };
       Test my_package(int a, int b) {
         Test s;
         s.x=a;
         s.y=b;
         return s;
       })";

  xls::Value tuple_values[2] = {xls::Value(xls::SBits(50, 64)),
                                xls::Value(xls::SBits(311, 32))};
  xls::Value expected = xls::Value::Tuple(tuple_values);

  absl::flat_hash_map<std::string, xls::Value> args = {
      {"a", xls::Value(xls::SBits(311, 32))},
      {"b", xls::Value(xls::SBits(50, 32))}};

  Run(args, expected, content);
}

TEST_F(TranslatorTest, ImplicitConversion) {
  const std::string content = R"(
       struct Test {
         Test(int v) : x(v) {
           this->y = 10;
         }
         operator int()const {
           return x+y;
         }
         int x;
         int y;
       };
       int my_package(int a) {
         Test s(a);
         return s;
       })";
  Run({{"a", 3}}, 13, content);
}

TEST_F(TranslatorTest, OperatorOverload) {
  const std::string content = R"(
       struct Test {
         Test(int v) : x(v) {
           this->y = 10;
         }
         Test operator+=(Test const&o) {
           x *= o.y;
           return *this;
         }
         Test operator+(Test const&o) {
           return x-o.x;
         }
         int x;
         int y;
       };
       int my_package(int a) {
         Test s1(a);
         Test s2(a);
         s1 += s2; // s1.x = a * 10
         return (s1 + s2).x; // Return (a*10)-a
       })";
  Run({{"a", 3}}, 27, content);
}

TEST_F(TranslatorTest, OperatorOnBuiltin) {
  const std::string content = R"(
       struct Test {
         Test(int v) : x(v) {
         }
         int x;
       };
       Test operator+(int a, Test b) {
         return Test(a+b.x);
       }
       int my_package(int a) {
         Test s1(a);
         return (10+s1).x;
       })";
  Run({{"a", 3}}, 13, content);
}

TEST_F(TranslatorTest, UnaryOperatorAvoidUnsequencedError2) {
  const std::string content = R"(
       struct Test {
         Test(int v) : x(v) {
           this->y = 10;
         }
         Test(const Test &o) : x(o.x) {
           this->y = 10;
         }
         Test operator +(Test o) const {
           return Test(x + o.x);
         }
         operator int () const {
           return x;
         }
         int x;
         int y;
       };
       int my_package(int a) {
         Test s1(a);
         Test s2(0);
         s2 = s1 + Test(1);
         return s2;
       })";
  Run({{"a", 3}}, 4, content);
}

TEST_F(TranslatorTest, UnaryOperatorAvoidUnsequencedError3) {
  const std::string content = R"(
       struct Test {
         Test(int v) : x(v) {
           this->y = 10;
         }
         Test(const Test &o) : x(o.x) {
           this->y = 10;
         }
         Test operator ++() {
           x = x + 1;
           return (*this);
         }
         operator int () const {
           return x;
         }
         int x;
         int y;
       };
       int my_package(int a) {
         Test s1(a);
         Test s2(0);
         s2 = ++s1;
         return s2;
       })";
  Run({{"a", 3}}, 4, content);
}

TEST_F(TranslatorTest, TypedefStruct) {
  const std::string content = R"(
       typedef struct {
         int x;
         int y;
       }Test;
       int my_package(int a) {
         Test s;
         s.x = a;
         s.y = a*10;
         return s.x+s.y;
       })";
  Run({{"a", 3}}, 33, content);
}

TEST_F(TranslatorTest, ConvertToVoid) {
  const std::string content = R"(
       struct ts {int x;};
       long long my_package(long long a) {
         ts t;
         (void)t;
         return a;
       })";
  Run({{"a", 10}}, 10, content);
}

TEST_F(TranslatorTest, AvoidDoubleAssignmentFromBackwardsEval) {
  const std::string content = R"(
       struct Test {
         Test(int v) : x(v) {
           this->y = 10;
         }
         Test(const Test &o) : x(o.x) {
           this->y = 10;
         }
         Test operator ++() {
           x = x + 1;
           return (*this);
         }
         operator int () const {
           return x;
         }
         int x;
         int y;
       };
       int my_package(int a) {
         Test s1(a);
         Test s2(0);
         s2 = ++s1;
         return s1;
       })";
  Run({{"a", 3}}, 4, content);
}

TEST_F(TranslatorTest, CompoundAvoidUnsequenced) {
  const std::string content = R"(
       struct Test {
         int x;
       };
       int my_package(int a) {
         Test s1;
         s1.x = a;
         s1.x = ++s1.x;
         return s1.x;
       })";
  Run({{"a", 3}}, 4, content);
}

TEST_F(TranslatorTest, CompoundAvoidUnsequenced2) {
  const std::string content = R"(
       int my_package(int a) {
         int s1[2] = {a, a};
         s1[0] = ++s1[1];
         return s1[0];
       })";
  Run({{"a", 3}}, 4, content);
}

TEST_F(TranslatorTest, DefaultValues) {
  const std::string content = R"(
       struct Test {
         int x;
         int y;
       };
       int my_package(int a) {
         Test s;
         return s.x+s.y+a;
       })";
  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, StructMemberReferenceParameter) {
  const std::string content = R"(
       struct Test {
         int p;
       };
       int do_something(Test &x, int a) {
         x.p += a;
         return x.p;
       }
       int my_package(int a) {
         Test ta;
         ta.p = a;
         do_something(ta, 5);
         return do_something(ta, 10);
       })";
  Run({{"a", 3}}, 3 + 5 + 10, content);
}

TEST_F(TranslatorTest, AnonStruct) {
  const std::string content = R"(
       int my_package(int a) {
         struct {
           int x;
           int y;
         } s;
         s.x = a;
         s.y = a*10;
         return s.x+s.y;
       })";
  // Not implemented, expect graceful failure
  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kUnimplemented,
                  testing::HasSubstr("DeclStmt other than Var")));
}

TEST_F(TranslatorTest, Inheritance) {
  const std::string content = R"(
       struct Base {
         int x;
       };
       struct Derived : public Base {
         int foo()const {
           return x;
         }
       };
       int my_package(int x) {
         Derived b;
         b.x = x;
         return b.foo();
       })";
  Run({{"x", 47}}, 47, content);
}

TEST_F(TranslatorTest, BaseConstructor) {
  const std::string content = R"(
       struct Base {
         Base() : x(88) { }
          int x;
       };
       struct Derived : public Base {
       };
       int my_package(int x) {
         Derived b;
         return x + b.x;
       })";
  Run({{"x", 15}}, 103, content);
}

TEST_F(TranslatorTest, BaseConstructorNoTuple) {
  const std::string content = R"(
       #pragma hls_no_tuple
       struct Base {
         Base() : x(88) { }
          int x;
       };
       #pragma hls_no_tuple
       struct Derived : public Base {
       };
       int my_package(int x) {
         Derived b;
         return x + b.x;
       })";
  Run({{"x", 15}}, 103, content);
}

TEST_F(TranslatorTest, InheritanceNoTuple) {
  const std::string content = R"(
       struct Base {
         int x;
       };
       #pragma hls_no_tuple
       struct Derived : public Base {
         int foo()const {
           return x;
         }
       };
       int my_package(int x) {
         Derived b;
         b.x = x;
         return b.foo();
       })";
  Run({{"x", 47}}, 47, content);
}

TEST_F(TranslatorTest, InheritanceNoTuple2) {
  const std::string content = R"(
       #pragma hls_no_tuple
       struct Base {
         int x;
       };
       #pragma hls_no_tuple
       struct Derived : public Base {
         int foo()const {
           return x;
         }
       };
       int my_package(int x) {
         Derived b;
         b.x = x;
         return b.foo();
       })";
  Run({{"x", 47}}, 47, content);
}

TEST_F(TranslatorTest, InheritanceNoTuple4) {
  const std::string content = R"(
       #pragma hls_no_tuple
       struct Base {
         int x;
         void set(int v) { x=v; }
         int get()const { return x; }
       };
       #pragma hls_no_tuple
       struct Derived : public Base {
         void setd(int v) { x=v; }
         int getd()const { return x; }
       };
       int my_package(int x) {
         Derived d;
         d.setd(x);
         d.setd(d.getd()*3);
         d.set(d.get()*5);
         return d.x;
       })";
  Run({{"x", 10}}, 150, content);
}

TEST_F(TranslatorTest, InheritanceTuple) {
  const std::string content = R"(
       struct Base {
         int x;
         void set(int v) { x=v; }
         int get()const { return x; }
       };
       struct Derived : public Base {
         void setd(int v) { x=v; }
         int getd()const { return x; }
       };
       int my_package(int x) {
         Derived d;
         d.setd(x);
         d.setd(d.getd()*3);
         d.set(d.get()*5);
         return d.x;
       })";
  Run({{"x", 10}}, 150, content);
}

TEST_F(TranslatorTest, Constructor) {
  const std::string content = R"(
      struct Test {
        Test() : x(5) {
          y = 10;
        }
        int x;
        int y;
      };
      int my_package(int a) {
        Test s;
        return s.x+s.y;
      })";
  Run({{"a", 3}}, 15, content);
}

TEST_F(TranslatorTest, Destructor) {
  const std::string content = R"(
      struct Test {
        Test() : x(5) {
          y = 10;
        }
        ~Test() {

        }
        int x;
        int y;
      };
      int my_package(int a) {
        Test s;
        return s.x+s.y;
      })";
  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                    testing::HasSubstr("aren't yet called")));
}

TEST_F(TranslatorTest, ConstructorWithArg) {
  const std::string content = R"(
      struct Test {
        Test(int v) : x(v) {
          y = 10;
        }
        int x;
        int y;
      };
      int my_package(int a) {
        Test s(a);
        return s.x+s.y;
      })";
  Run({{"a", 3}}, 13, content);
}

TEST_F(TranslatorTest, ConstructorWithThis) {
  const std::string content = R"(
      struct Test {
        Test(int v) : x(v) {
          this->y = 10;
        }
        int x;
        int y;
      };
      int my_package(int a) {
        Test s(a);
        return s.x+s.y;
      })";
  Run({{"a", 3}}, 13, content);
}

TEST_F(TranslatorTest, ExplicitDefaultConstructor) {
  const std::string content = R"(
         struct TestR {
           int bb;
         };
         #pragma hls_top
         int my_package(int a) {
            TestR b = TestR();
           return b.bb + a;
         })";
  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, ConditionallyAssignThis) {
  const std::string content = R"(
       struct ts {
         void blah() {
           return;
           v = v | 1;
         }
         int v;
       };
       #pragma hls_top
       int my_package(int a) {
         ts t;
         t.v = a;
         t.blah();
         return t.v;
       })";
  Run({{"a", 6}}, 6, content);
}

TEST_F(TranslatorTest, SetMemberInnerContext) {
  const std::string content = R"(
       struct Test {
         void set_x(int v) {
           { x = v; }
         }
         int x;
         int y;
       };
       int my_package(int a) {
         Test s;
         s.set_x(a);
         s.y = 11;
         return s.x+s.y;
       })";
  Run({{"a", 3}}, 14, content);
}

TEST_F(TranslatorTest, StaticMethod) {
  const std::string content = R"(
       struct Test {
          static int foo(int a) {
            return a+5;
          }
       };
       int my_package(int a) {
         return Test::foo(a);
       })";
  Run({{"a", 3}}, 8, content);
}

TEST_F(TranslatorTest, SignExtend) {
  {
    const std::string content = R"(
        unsigned long long my_package(long long a) {
          return long(a);
        })";

    Run({{"a", 3}}, 3, content);
  }
  {
    const std::string content = R"(
        long long my_package(long long a) {
          return (unsigned long)a;
        })";

    Run({{"a", 3}}, 3, content);
    Run({{"a", -3}}, 18446744073709551613ull, content);
  }
}

TEST_F(TranslatorTest, TopFunctionByName) {
  const std::string content = R"(
      int my_package(int a) {
        return a + 1;
      })";

  Run({{"a", 3}}, 4, content);
}

TEST_F(TranslatorTest, TopFunctionPragma) {
  const std::string content = R"(
      #pragma hls_top
      int asdf(int a) {
        return a + 1;
      })";

  Run({{"a", 3}}, 4, content);
}

TEST_F(TranslatorTest, TopFunctionNoPragma) {
  const std::string content = R"(
      int asdf(int a) {
        return a + 1;
      })";
  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kNotFound,
                  testing::HasSubstr("No top function found")));
}

TEST_F(TranslatorTest, Function) {
  const std::string content = R"(
      int do_something(int a) {
        return a;
      }
      int my_package(int a) {
        return do_something(a);
      })";

  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, FunctionNoOutputs) {
  const std::string content = R"(
      void do_nothing(int a) {
        (void)a;
      }
      int my_package(int a) {
        do_nothing(a);
        return a;
      })";

  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, TopFunctionNoOutputs) {
  const std::string content = R"(
      void my_package(int a) {
        (void)a;
      })";

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(absl::StatusCode::kInvalidArgument,
                                            testing::HasSubstr("no outputs")));
}

TEST_F(TranslatorTest, DefaultArg) {
  const std::string content = R"(
      int do_something(int a, int b=2) {
        return a+b;
      }
      int my_package(int a) {
        return do_something(a);
      })";

  Run({{"a", 3}}, 5, content);
}

TEST_F(TranslatorTest, FunctionInline) {
  const std::string content = R"(
      inline int do_something(int a) {
        return a;
      }
      int my_package(int a) {
        return do_something(a);
      })";

  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, TemplateFunction) {
  const std::string content = R"(
      template<int N>
      int do_something(int a) {
        return a+N;
      }
      int my_package(int a) {
        return do_something<5>(a);
      })";

  Run({{"a", 3}}, 8, content);
}

TEST_F(TranslatorTest, TemplateFunctionBool) {
  constexpr const char* content = R"(
      template<bool C>
      int do_something(int a) {
        return C?a:15;
      }
      int my_package(int a) {
        return do_something<%s>(a);
      })";
  Run({{"a", 3}}, 3, absl::StrFormat(content, "true"));
  Run({{"a", 3}}, 15, absl::StrFormat(content, "false"));
}

TEST_F(TranslatorTest, FunctionDeclOrder) {
  const std::string content = R"(
      int do_something(int a);
      int my_package(int a) {
        return do_something(a);
      }
      int do_something(int a) {
        return a;
      })";

  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, FunctionDeclMissing) {
  const std::string content = R"(
      int do_something(int a);
      int my_package(int a) {
        return do_something(a);
      })";

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kNotFound,
                  testing::HasSubstr("do_something used but has no body")));
}

TEST_F(TranslatorTest, ReferenceParameter) {
  const std::string content = R"(
      int do_something(int &x, int a) {
        x += a;
        return x;
      }
      int my_package(int a) {
        do_something(a, 5);
        return do_something(a, 10);
      })";

  Run({{"a", 3}}, 3 + 5 + 10, content);
}

TEST_F(TranslatorTest, Namespace) {
  const std::string content = R"(
      namespace test {
      int do_something(int a) {
        return a;
      }
      }
      int my_package(int a) {
        return test::do_something(a);
      })";

  Run({{"a", 3}}, 3, content);
}

TEST_F(TranslatorTest, NamespaceFailure) {
  const std::string content = R"(
      namespace test {
      int do_something(int a) {
        return a;
      }
      }
      int my_package(int a) {
        return do_something(a);
      })";
  auto ret = SourceToIr(content);

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kFailedPrecondition,
                  testing::HasSubstr("Unable to parse text")));
}

TEST_F(TranslatorTest, Ternary) {
  const std::string content = R"(
      int my_package(int a) {
        return a ? a : 11;
      })";

  Run({{"a", 3}}, 3, content);
  Run({{"a", 0}}, 11, content);
}

// This is here mainly to check for graceful exit with no memory leaks
TEST_F(TranslatorTest, ParseFailure) {
  const std::string content = "int my_package(int a) {";

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kFailedPrecondition,
                  testing::HasSubstr("Unable to parse text")));
}

std::string NativeOperatorTestIr(std::string op) {
  return absl::StrFormat(R"(
      long long my_package(long long a, long long b) {
        return a %s b;
      })",
                         op);
}

std::string NativeOperatorTestIrEq(std::string op) {
  return absl::StrFormat(R"(
      long long my_package(long long a, long long b) {
        a %s= b;
        return a;
      })",
                         op);
}

TEST_F(TranslatorTest, NativeOperatorAdd) {
  const std::string op = "+";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 3}, {"b", 10}}, 13, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 11}, {"b", 22}}, 33, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorSub) {
  const std::string op = "-";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 8}, {"b", 3}}, 5, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 30}, {"b", 11}}, 19, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorMul) {
  const std::string op = "*";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 3}, {"b", 10}}, 30, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 11}, {"b", 2}}, 22, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorDiv) {
  const std::string op = "/";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 55}, {"b", 3}}, 18, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", -1800}, {"b", 18}}, -100, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorRem) {
  const std::string op = "%";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 55}, {"b", 3}}, 1, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", -1800}, {"b", 18}}, 0, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorAnd) {
  const std::string op = "&";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 0b1001}, {"b", 0b0110}}, 0b0000, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 0b1001}, {"b", 0b1110}}, 0b1000, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorOr) {
  const std::string op = "|";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 0b1001}, {"b", 0b0110}}, 0b1111, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 0b1001}, {"b", 0b1110}}, 0b1111, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 0b1000}, {"b", 0b1110}}, 0b1110, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorXor) {
  const std::string op = "^";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 0b1001}, {"b", 0b0110}}, 0b1111, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 0b1001}, {"b", 0b1110}}, 0b0111, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 0b1000}, {"b", 0b1110}}, 0b0110, content);
  }
}

TEST_F(TranslatorTest, NativeOperatorNot) {
  const std::string content = R"(
      long long my_package(unsigned long long a) {
        return (long long)(~a);
      })";

  Run({{"a", 0b000}}, ~static_cast<uint64_t>(0b000), content);
  Run({{"a", 0b111}}, ~static_cast<uint64_t>(0b111), content);
  Run({{"a", 0b101}}, ~static_cast<uint64_t>(0b101), content);
}

TEST_F(TranslatorTest, NativeOperatorNeg) {
  const std::string content = R"(
      long long my_package(long long a) {
        return (long long)(-a);
      })";

  Run({{"a", 11}}, -11, content);
  Run({{"a", 0}}, 0, content);
  Run({{"a", -1000}}, 1000, content);
}

TEST_F(TranslatorTest, NativeOperatorPlus) {
  const std::string content = R"(
      long long my_package(long long a) {
        return (long long)(+a);
      })";

  Run({{"a", 11}}, 11, content);
  Run({{"a", 0}}, 0, content);
  Run({{"a", -1000}}, -1000, content);
}

TEST_F(TranslatorTest, NativeOperatorShrSigned) {
  const std::string op = ">>";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 10}, {"b", 1}}, 5, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", -20}, {"b", 2}}, -5, content);
  }
}
TEST_F(TranslatorTest, NativeOperatorShrUnsigned) {
  const std::string content = R"(
      unsigned long long my_package(unsigned long long a, unsigned long long b)
      {
        return a >> b;
      })";
  { Run({{"a", 10}, {"b", 1}}, 5, content); }
  { Run({{"a", -20}, {"b", 2}}, 4611686018427387899L, content); }
}
TEST_F(TranslatorTest, NativeOperatorShl) {
  const std::string op = "<<";
  {
    const std::string content = NativeOperatorTestIr(op);

    Run({{"a", 16}, {"b", 1}}, 32, content);
  }
  {
    const std::string content = NativeOperatorTestIrEq(op);

    Run({{"a", 13}, {"b", 2}}, 52, content);
  }
}
TEST_F(TranslatorTest, NativeOperatorPreInc) {
  {
    const std::string content = R"(
        int my_package(int a) {
          return ++a;
        })";

    Run({{"a", 10}}, 11, content);
  }
  {
    const std::string content = R"(
        int my_package(int a) {
          ++a;
          return a;
        })";

    Run({{"a", 50}}, 51, content);
  }
}
TEST_F(TranslatorTest, NativeOperatorPostInc) {
  {
    const std::string content = R"(
        int my_package(int a) {
          return a++;
        })";

    Run({{"a", 10}}, 10, content);
  }
  {
    const std::string content = R"(
        int my_package(int a) {
          a++;
          return a;
        })";

    Run({{"a", 50}}, 51, content);
  }
}
TEST_F(TranslatorTest, NativeOperatorPreDec) {
  {
    const std::string content = R"(
        int my_package(int a) {
          return --a;
        })";

    Run({{"a", 10}}, 9, content);
  }
  {
    const std::string content = R"(
        int my_package(int a) {
          --a;
          return a;
        })";

    Run({{"a", 50}}, 49, content);
  }
}
TEST_F(TranslatorTest, NativeOperatorPostDec) {
  {
    const std::string content = R"(
        int my_package(int a) {
          return a--;
        })";

    Run({{"a", 10}}, 10, content);
  }
  {
    const std::string content = R"(
        int my_package(int a) {
          a--;
          return a;
        })";

    Run({{"a", 50}}, 49, content);
  }
}

std::string NativeBoolOperatorTestIr(std::string op) {
  return absl::StrFormat(R"(
      long long my_package(long long a, long long b) {
        return (long long)(a %s b);
      })",
                         op);
}

std::string NativeUnsignedBoolOperatorTestIr(std::string op) {
  return absl::StrFormat(R"(
      long long my_package(unsigned long long a, unsigned long long b) {
        return (long long)(a %s b);
      })",
                         op);
}

TEST_F(TranslatorTest, NativeOperatorEq) {
  const std::string op = "==";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", 3}, {"b", 3}}, 1, content);
  Run({{"a", 11}, {"b", 10}}, 0, content);
}

TEST_F(TranslatorTest, NativeOperatorNe) {
  const std::string op = "!=";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", 3}, {"b", 3}}, 0, content);
  Run({{"a", 11}, {"b", 10}}, 1, content);
}

TEST_F(TranslatorTest, NativeOperatorGt) {
  const std::string op = ">";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 0, content);
  Run({{"a", 2}, {"b", 3}}, 0, content);
  Run({{"a", 3}, {"b", 3}}, 0, content);
  Run({{"a", 11}, {"b", 10}}, 1, content);
}

TEST_F(TranslatorTest, NativeOperatorGtU) {
  const std::string op = ">";
  const std::string content = NativeUnsignedBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 1, content);
  Run({{"a", 2}, {"b", 3}}, 0, content);
  Run({{"a", 3}, {"b", 3}}, 0, content);
  Run({{"a", 11}, {"b", 10}}, 1, content);
}

TEST_F(TranslatorTest, NativeOperatorGte) {
  const std::string op = ">=";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 0, content);
  Run({{"a", 2}, {"b", 3}}, 0, content);
  Run({{"a", 3}, {"b", 3}}, 1, content);
  Run({{"a", 11}, {"b", 10}}, 1, content);
}

TEST_F(TranslatorTest, NativeOperatorGteU) {
  const std::string op = ">=";
  const std::string content = NativeUnsignedBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 1, content);
  Run({{"a", 2}, {"b", 3}}, 0, content);
  Run({{"a", 3}, {"b", 3}}, 1, content);
  Run({{"a", 11}, {"b", 10}}, 1, content);
}

TEST_F(TranslatorTest, NativeOperatorLt) {
  const std::string op = "<";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 1, content);
  Run({{"a", 2}, {"b", 3}}, 1, content);
  Run({{"a", 3}, {"b", 3}}, 0, content);
  Run({{"a", 11}, {"b", 10}}, 0, content);
}

TEST_F(TranslatorTest, NativeOperatorLtU) {
  const std::string op = "<";
  const std::string content = NativeUnsignedBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 0, content);
  Run({{"a", 2}, {"b", 3}}, 1, content);
  Run({{"a", 3}, {"b", 3}}, 0, content);
  Run({{"a", 11}, {"b", 10}}, 0, content);
}

TEST_F(TranslatorTest, NativeOperatorLte) {
  const std::string op = "<=";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 1, content);
  Run({{"a", 2}, {"b", 3}}, 1, content);
  Run({{"a", 3}, {"b", 3}}, 1, content);
  Run({{"a", 11}, {"b", 10}}, 0, content);
}

TEST_F(TranslatorTest, NativeOperatorLteU) {
  const std::string op = "<=";
  const std::string content = NativeUnsignedBoolOperatorTestIr(op);

  Run({{"a", -2}, {"b", 3}}, 0, content);
  Run({{"a", 2}, {"b", 3}}, 1, content);
  Run({{"a", 3}, {"b", 3}}, 1, content);
  Run({{"a", 11}, {"b", 10}}, 0, content);
}

TEST_F(TranslatorTest, NativeOperatorLAnd) {
  const std::string op = "&&";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", 0b111}, {"b", 0b111}}, 1, content);
  Run({{"a", 0b001}, {"b", 0b100}}, 1, content);
  Run({{"a", 0b111}, {"b", 0}}, 0, content);
  Run({{"a", 0}, {"b", 0}}, 0, content);
}

TEST_F(TranslatorTest, NativeOperatorLOr) {
  const std::string op = "||";
  const std::string content = NativeBoolOperatorTestIr(op);

  Run({{"a", 0b111}, {"b", 0b111}}, 1, content);
  Run({{"a", 0b001}, {"b", 0b100}}, 1, content);
  Run({{"a", 0b111}, {"b", 0}}, 1, content);
  Run({{"a", 0}, {"b", 0}}, 0, content);
}

TEST_F(TranslatorTest, NativeOperatorLNot) {
  const std::string content = R"(
      long long my_package(unsigned long long a) {
        return (long long)(!a);
      })";

  Run({{"a", 0}}, 1, content);
  Run({{"a", 11}}, 0, content);
  Run({{"a", -11}}, 0, content);
}

TEST_F(TranslatorTest, SizeOf) {
  const std::string content = R"(
      int my_package() {
        short foo[3] = {2,3,4};
        return sizeof(foo);
      })";

  Run({}, 3 * 16, content);
}

TEST_F(TranslatorTest, SizeOfExpr) {
  const std::string content = R"(
      int my_package() {
        short foo[3] = {2,3,4};
        return sizeof(foo) / sizeof(foo[0]);
      })";

  Run({}, 3, content);
}

TEST_F(TranslatorTest, SizeOfDeep) {
  const std::string content = R"(
      struct TestInner {
        short foo;
      };
      struct Test {
        TestInner x[5];
      };
      int my_package() {
        Test b;
        return sizeof(b);
      })";

  Run({}, 5 * 16, content);
}

TEST_F(TranslatorTest, ParenType) {
  const std::string content = R"(
    int thing(const int (&arr)[2]) {
      return arr[0] + arr[1];
    }
    int my_package(int a) {
      int arr[2] = {a, 1+a};
      return thing(arr);
    })";

  Run({{"a", 10}}, 21, content);
}

TEST_F(TranslatorTest, DefaultArrayInit) {
  const std::string content = R"(
    struct Val {
      Val() : v(0) { }
      Val(const Val&o) : v(o.v) {
      }
      int v;
    };
    struct Foo {
      Val a[16];
    };

    int my_package(int a) {
      Foo x;
      x.a[1].v = a;
      Foo y;
      y = x;
      return 1+y.a[1].v;
    })";

  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kNotFound,
                                    testing::HasSubstr("__builtin_memcpy")));
}

TEST_F(TranslatorTest, ZeroIterationForLoop) {
  const std::string content = R"(
    long long my_package(long long a) {
      for(;0;) {}
      return a;
    })";
  Run({{"a", 1}}, 1, content);
}

TEST_F(TranslatorTest, OnlyUnrolledLoops) {
  const std::string content = R"(
    long long my_package(long long a) {
      for(;1;) {}
      return a;
    })";

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kUnimplemented,
                  testing::HasSubstr("loop missing #pragma")));
}

TEST_F(TranslatorTest, InvalidUnrolledLoop) {
  const std::string content = R"(
    long long my_package(long long a) {
      #pragma hls_unroll yes
      for(;1;) {}
      return a;
    })";

  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kResourceExhausted,
                                    testing::HasSubstr("maximum")));
}

TEST_F(TranslatorTest, NonPragmaNestedLoop) {
  const std::string content = R"(
    long long my_package(long long a) {
      #pragma hls_unroll yes
      for(int i = 0; i < 10; i++) {
        for(int j = 0; j < 10; j++) {
          a++;
        }
      }
      return a;
    })";

  ASSERT_THAT(SourceToIr(content).status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kUnimplemented,
                  testing::HasSubstr("loop missing #pragma")));
}

TEST_F(TranslatorTest, Label) {
  const std::string content = R"(
      long long my_package(long long a, long long b) {
        this_loop:
        #pragma hls_unroll yes
        for(int i=1;i<=10;++i) {
          a += b;
          a += 2*b;
        }
        return a;
      })";
  Run({{"a", 11}, {"b", 20}}, 611, content);
}

TEST_F(TranslatorTest, DisallowUsed) {
  const std::string content = R"(
      #include "/xls_builtin.h"

      int foo(int a) {
        (void)__xlscc_unimplemented();
        return a+3;
      }
  
      int my_package(int a) {
        return foo(a);
      })";
  auto ret = SourceToIr(content);

  ASSERT_THAT(
      SourceToIr(content).status(),
      xls::status_testing::StatusIs(absl::StatusCode::kUnimplemented,
                                    testing::HasSubstr("Unimplemented")));
}

TEST_F(TranslatorTest, DisallowUnused) {
  const std::string content = R"(
      #include "/xls_builtin.h"

      int foo(int a) {
        __xlscc_unimplemented();
        return a+3;
      }
  
      int my_package(int a) {
        return a+5;
      })";
  Run({{"a", 11}}, 16, content);
}

// Checks that unnecessary conditions aren't applied to assignments
// Variables don't need conditions from before their declarations
// This won't cause logical incorrectness, but it overcomplicates the IR
// and can cause loop unrolling to fail.
TEST_F(TranslatorTest, ConditionsPerVariable) {
  const std::string content = R"(
    #pragma hls_top
    int my_package(int a, int b) {
      if(b) {
        int ret;
        ret = a;
        return ret;
      }
      return -1;
    })";

  xlscc::GeneratedFunction* pfunc = nullptr;
  XLS_ASSERT_OK(SourceToIr(content, &pfunc).status());
  ASSERT_NE(pfunc, nullptr);
  xls::Node* return_node = pfunc->xls_func->return_value();
  ASSERT_NE(return_node, nullptr);
  xls::Select* select_node = return_node->As<xls::Select>();
  ASSERT_NE(select_node, nullptr);
  const absl::Span<xls::Node* const>& cases = select_node->cases();
  ASSERT_EQ(cases.size(), 2);
  // Check for direct reference to parameter
  EXPECT_TRUE(cases[0]->Is<xls::Param>() || cases[1]->Is<xls::Param>());
}

TEST_F(TranslatorTest, FileNumbersIncluded) {
  const std::string content = R"(
  #include "/xls_builtin.h"

  #pragma hls_top
  void st(__xls_channel<int>& in,
           __xls_channel<int>& out) {
    const int ctrl = in.read();
    out.write(ctrl + 1);
  })";
  XLS_ASSERT_OK_AND_ASSIGN(xls::TempFile temp,
                           xls::TempFile::CreateWithContent(content, ".cc"));
  XLS_ASSERT_OK_AND_ASSIGN(std::string ir, SourceToIr(temp));

  ASSERT_TRUE(absl::StrContains(ir, temp.path().string()));
}

TEST_F(TranslatorTest, BooleanOrAssign) {
  const std::string content = R"(
  #pragma hls_top
  bool st(bool b) {
    b |= false;
    return b;
  })";
  Run({{"b", false}}, static_cast<int64_t>(false), content);
  Run({{"b", true}}, static_cast<int64_t>(true), content);
}

TEST_F(TranslatorTest, BooleanAndAssign) {
  const std::string content = R"(
  #pragma hls_top
  bool st(bool b) {
    b &= true;
    return b;
  })";
  Run({{"b", true}}, static_cast<int64_t>(true), content);
  Run({{"b", false}}, static_cast<int64_t>(false), content);
}

TEST_F(TranslatorTest, EnumConstant) {
  const std::string content = R"(
  struct Values{
   enum { zero = 2 };
  };
  #pragma hls_top
  int st(int a) {
    Values v;
    const int val = a * v.zero;
    return val;
  })";
  Run({{"a", 5}}, 10, content);
}

TEST_F(TranslatorTest, NonInitEnumConstant) {
  const std::string content = R"(
  struct Values{
   enum { zero, one };
  };
  #pragma hls_top
  int st(int a) {
    Values v;
    const int val = a * v.zero;
    return val;
  })";
  Run({{"a", 5}}, 0, content);
}

TEST_F(TranslatorTest, NonInitOneEnumConstant) {
  const std::string content = R"(
  struct Values{
   enum { zero, one };
  };
  #pragma hls_top
  int st(int a) {
    Values v;
    const int val = a * v.one;
    return val;
  })";
  Run({{"a", 5}}, 5, content);
}

TEST_F(TranslatorTest, TopMemberAccess) {
  const std::string content = R"(
      #include "/xls_builtin.h"

      struct Test {
        int foo = 0;

        #pragma hls_top
        void test(__xls_channel<int>& in,
                  __xls_channel<int> &out) {
          const int ctrl = in.read() + foo;
          out.write(ctrl);
        }
      };)";

  HLSBlock block_spec;
  {
    block_spec.set_name("foo");

    HLSChannel* ch_in = block_spec.add_channels();
    ch_in->set_name("in");
    ch_in->set_is_input(true);
    ch_in->set_type(FIFO);

    HLSChannel* ch_out1 = block_spec.add_channels();
    ch_out1->set_name("out");
    ch_out1->set_is_input(false);
    ch_out1->set_type(FIFO);
  }

  XLS_ASSERT_OK(ScanFile(content));
  package_.reset(new xls::Package("my_package"));
  ASSERT_THAT(
      translator_->GenerateIR_Block(package_.get(), block_spec).status(),
      xls::status_testing::StatusIs(
          absl::StatusCode::kUnimplemented,
          testing::HasSubstr("top level methods are not supported")));
}

TEST_F(TranslatorTest, ParameterPack) {
  const std::string content = R"(
      int inner(int a, int b, int c) {
          return a+2*b+5*c;
      }

      template<typename...Ranges>
      int Test(Ranges... b) {
          return inner(b...);
      }

      #pragma hls_top
      int sum(int a, int b, int c) {
          return Test<int,int,int>(a,b,c);
      })";
  Run({{"a", 1}, {"b", 2}, {"c", 3}}, 20, content);
}

TEST_F(TranslatorTest, FunctionEnum) {
  const std::string content = R"(
    #pragma hls_top
    int sum(int a) {
        enum b { B0 = 0, B1 = 1 };
        return B1 + a;
    }
    )";
  Run({{"a", 5}}, 6, content);
}

TEST_F(TranslatorTest, UnusedTemplate) {
  const std::string content = R"(
      template<typename T, int N>
      struct StructVal { T data[N]; };

      #pragma hls_top
      int my_package(StructVal<long, 2> a) {
        return 0;
      }
    )";
  XLS_ASSERT_OK_AND_ASSIGN(std::string ir_src, SourceToIr(content));
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<xls::Package> package,
                           ParsePackage(ir_src));
  XLS_ASSERT_OK(package->SetTopByName("my_package"));
  std::vector<uint64_t> in_vals = {55, 20};
  XLS_ASSERT_OK_AND_ASSIGN(xls::Value in_arr,
                           xls::Value::UBitsArray(in_vals, 64));
  xls::Value in_tuple = xls::Value::Tuple({in_arr});
  absl::flat_hash_map<std::string, xls::Value> args;
  args["a"] = in_tuple;
  XLS_ASSERT_OK_AND_ASSIGN(xls::Function * entry, package->GetTopAsFunction());

  auto x = DropInterpreterEvents(
      xls::InterpretFunctionKwargs(entry, {{"a", in_tuple}}));

  ASSERT_THAT(x, IsOkAndHolds(xls::Value(xls::UBits(0, 32))));
}

TEST_F(TranslatorTest, BinaryOpComma) {
  const std::string content = R"(
      #pragma hls_top
      int test(int a) {
          return (a=5,a+1);
      })";
  Run({{"a", 1}}, 6, content);
}

// Check that hls_array_allow_default_pad pragma fills with 0's
TEST_F(TranslatorTest, ArrayZeroExtendFillsZeros) {
  const std::string content = R"(
        #pragma hls_top
        int my_package(int a) {
         #pragma hls_array_allow_default_pad
         int x[4] = {1,2};
         return x[2];
       })";
  Run({{"a", 5}}, 0, content);
}

// Check that hls_array_allow_default_pad pragma maintains supplied values
TEST_F(TranslatorTest, ArrayZeroExtendMaintainsValues) {
  const std::string content = R"(
        #pragma hls_top
        int my_package(int a) {
         #pragma hls_array_allow_default_pad
         int x[4] = {1,2};
         return x[1];
       })";
  Run({{"a", 5}}, 2, content);
}

// Check that hls_array_allow_default_pad pragma maintains supplied values
TEST_F(TranslatorTest, ArrayZeroExtendStruct) {
  const std::string content = R"(
        struct y {
          int z;
        };
        #pragma hls_top
        int my_package(int a) {
         #pragma hls_array_allow_default_pad
         y x[4] = {{1},{2}};
         return x[1].z;
       })";
  Run({{"a", 5}}, 2, content);
}

// Check that hls_array_allow_default_pad pragma
// works on structs with constructors
TEST_F(TranslatorTest, ArrayZeroExtendStructWithConstructor) {
  const std::string content = R"(
        struct y {
          int z;
          y() : z(7) {}
          y(int val) : z(val) {}
        };
        #pragma hls_top
        int my_package(int a) {
         #pragma hls_array_allow_default_pad
         y x[4] = {{1},{2}};
         return x[2].z;
       })";
  Run({{"a", 5}}, 7, content);
}

// Check that enum array initializers function properly
TEST_F(TranslatorTest, EnumArrayInitializer) {
  const std::string content = R"(
        typedef enum {
          VAL_1,
          VAL_2
        } enum_val;
        #pragma hls_top
        bool my_package(int a) {
         enum_val x[3] = { VAL_1, VAL_1, VAL_2 };
         return x[2] == VAL_2;
       })";
  Run({{"a", 2}}, static_cast<int64_t>(true), content);
}

TEST_F(TranslatorTest, ChannelTemplateType) {
  const std::string content = R"(
      #include "/xls_builtin.h"
      #pragma hls_top
      void test(int& in, int& out) {
        const int ctrl = in;
        out = ctrl;
      })";

  HLSBlock block_spec;
  {
    block_spec.set_name("foo");

    HLSChannel* ch_in = block_spec.add_channels();
    ch_in->set_name("in");
    ch_in->set_is_input(true);
    ch_in->set_type(FIFO);

    HLSChannel* ch_out1 = block_spec.add_channels();
    ch_out1->set_name("out");
    ch_out1->set_is_input(false);
    ch_out1->set_type(FIFO);
  }
  XLS_ASSERT_OK_AND_ASSIGN(xls::TempFile temp,
                           xls::TempFile::CreateWithContent(content, ".cc"));
  XLS_ASSERT_OK(ScanFile(temp));
  package_.reset(new xls::Package("my_package"));
  ASSERT_THAT(translator_
                  ->GenerateIR_Block(package_.get(), block_spec,
                                     /*top_level_init_interval=*/1)
                  .status(),
              xls::status_testing::StatusIs(
                  absl::StatusCode::kUnimplemented,
                  testing::HasSubstr(
                      "Channel type should be a template specialization")));
}

TEST_F(TranslatorTest, ClassMemberInit) {
  const std::string content = R"(
    struct Foo {
      int x = 10;
    };
    int my_package(int a) {
      Foo foo;
      foo.x += a;
      return foo.x;
    })";
  Run({{"a", 100}}, 110, content);
}

}  // namespace

}  // namespace xlscc
