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

#include "xls/jit/type_layout.h"

#include "xls/ir/value_helpers.h"

namespace xls {

static bool IsLeafValue(const Value& value) {
  return value.IsBits() || value.IsToken();
}

static void LeafValueToNativeLayout(const Value& value,
                                    const ElementLayout& element_layout,
                                    uint8_t* buffer) {
  uint8_t* element_buffer = buffer + element_layout.offset;
  if (value.IsBits()) {
    // Write the bytes from the Bits object into the buffer.
    value.bits().ToBytes(
        absl::MakeSpan(element_buffer, element_layout.data_size));
    // Clear any padding bytes.
    std::memset(element_buffer + element_layout.data_size, 0,
                element_layout.padded_size - element_layout.data_size);
    return;
  }
  XLS_CHECK(value.IsToken());
  std::memset(buffer, 0, element_layout.padded_size);
}

void TypeLayout::ValueToNativeLayout(const Value& value,
                                     uint8_t* buffer) const {
  XLS_DCHECK(ValueConformsToType(value, type())) << absl::StreamFormat(
      "Value `%s` is not of type `%s`", value.ToString(), type()->ToString());

  if (IsLeafValue(value)) {
    return LeafValueToNativeLayout(value, elements_.front(), buffer);
  }

  // At this point, `value` is a compound type. To avoid the expense of
  // recursive calls enumerating the type elements, manually keep a stack to
  // perform the enumeration.
  struct Frame {
    // Type being handled. Necessarily is a compound type (array or tuple).
    const Value* value;
    // The index of the current element of `value` being handled.
    int64_t index;
    // The number of elements in `value`.
    int64_t limit;
  };
  absl::InlinedVector<Frame, 5> stack;
  // Adds the compound type value to the stack for enumeration through its
  // elements.
  auto push_frame = [&](const Value& v) {
    stack.push_back(Frame{.value = &v, .index = 0, .limit = v.size()});
  };
  push_frame(value);

  // The linear index of the current leaf element being handled.
  int64_t leaf_index = 0;
  while (!stack.empty()) {
    Frame& frame = stack.back();
    if (frame.index == frame.limit) {
      stack.pop_back();
      continue;
    }
    const Value& value_element = frame.value->element(frame.index);
    if (IsLeafValue(value_element)) {
      LeafValueToNativeLayout(value_element, elements_.at(leaf_index), buffer);
      ++frame.index;
      ++leaf_index;
    } else {
      ++frame.index;
      push_frame(value_element);
    }
  }
  XLS_CHECK_EQ(leaf_index, elements_.size());
}

Value TypeLayout::NativeLayoutToValueInternal(Type* element_type,
                                              const uint8_t* buffer,
                                              int64_t* leaf_index) const {
  if (element_type->IsBits()) {
    int64_t bit_count = element_type->AsBitsOrDie()->bit_count();
    const ElementLayout& element_layout = elements_.at(*leaf_index);
    ++(*leaf_index);
    return Value(
        Bits::FromBytes(absl::MakeSpan(buffer + element_layout.offset,
                                       CeilOfRatio(bit_count, int64_t{8})),
                        bit_count));
  }
  if (element_type->IsToken()) {
    ++(*leaf_index);
    return Value::Token();
  }
  if (element_type->IsTuple()) {
    TupleType* tuple_type = element_type->AsTupleOrDie();
    std::vector<Value> elements;
    for (int64_t i = 0; i < tuple_type->size(); ++i) {
      elements.push_back(NativeLayoutToValueInternal(
          tuple_type->element_type(i), buffer, leaf_index));
    }
    return Value::TupleOwned(std::move(elements));
  }

  XLS_CHECK(element_type->IsArray());
  ArrayType* array_type = element_type->AsArrayOrDie();
  std::vector<Value> elements;
  for (int64_t i = 0; i < array_type->size(); ++i) {
    elements.push_back(NativeLayoutToValueInternal(array_type->element_type(),
                                                   buffer, leaf_index));
  }
  return Value::ArrayOwned(std::move(elements));
}

Value TypeLayout::NativeLayoutToValue(const uint8_t* buffer) const {
  int64_t leaf_index = 0;
  return NativeLayoutToValueInternal(type_, buffer, &leaf_index);
}

std::string TypeLayout::ToString() const {
  std::vector<std::string> lines;
  lines.push_back(absl::StrFormat("TypeLayout {"));
  lines.push_back(absl::StrFormat("  type = %s", type_->ToString()));
  lines.push_back(absl::StrFormat("  size = %d", size()));
  lines.push_back("  elements = {");
  for (const ElementLayout& el : elements_) {
    lines.push_back(absl::StrFormat(
        "    ElementLayout{.offset=%d, .data_size=%d, .padded_size=%d}",
        el.offset, el.data_size, el.padded_size));
  }
  lines.push_back("  }");
  lines.push_back("}");
  return absl::StrJoin(lines, "\n");
}

}  // namespace xls
