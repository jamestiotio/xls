// Copyright 2022 The XLS Authors
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xls/common/status/matchers.h"
#include "xls/ir/bits.h"
#include "xls/ir/value.h"
#include "xls/modules/fp/fp32_add_2_cc.h"
#include "xls/modules/fp/fp32_fma_cc.h"

// Rather than do a pattern-matching unit test of aot_compile.cc's output, this
// "unit test" focuses on using libraries compiled from such.

namespace xls {
namespace {

Value F32Value(bool sign, uint8_t exp, uint32_t frac) {
  return Value::Tuple({Value(UBits(static_cast<uint64_t>(sign), 1)),
                       Value(UBits(exp, 8)), Value(UBits(frac, 23))});
}

// Tests straightforward usage of a straightforward library/AOT-compiled module.
TEST(AotCompileTest, BasicUsage) {
  Value f32_one = F32Value(false, 0x7f, 0);
  Value f32_two = F32Value(false, 0x80, 0);
  Value f32_three = F32Value(false, 0x80, 0x400000);
  XLS_ASSERT_OK_AND_ASSIGN(Value result, fp32_add_2(f32_one, f32_two));
  EXPECT_EQ(result, f32_three);
}

// Another basic test, just for some more mileage.
TEST(AotCompileTest, AnotherBasicUsage) {
  Value f32_one = F32Value(false, 0x7f, 0);
  Value f32_two = F32Value(false, 0x80, 0);
  Value f32_three = F32Value(false, 0x80, 0x400000);
  Value f32_five = F32Value(false, 0x81, 0x200000);
  XLS_ASSERT_OK_AND_ASSIGN(Value result, fp32_fma(f32_one, f32_two, f32_three));
  EXPECT_EQ(result, f32_five);
}

}  // namespace
}  // namespace xls
