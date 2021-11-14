/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#ifndef ONEFLOW_CORE_PRIMITIVE_CPU_TYPE_SEQ_H_
#define ONEFLOW_CORE_PRIMITIVE_CPU_TYPE_SEQ_H_

#include "oneflow/core/common/preprocessor.h"
#include "oneflow/core/common/data_type.h"
#include <half.hpp>
#include "oneapi/dnnl/dnnl.hpp"

#define CPU_PRIMITIVE_CHAR_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(char, DataType::kChar)
#define CPU_PRIMITIVE_INT8_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(int8_t, DataType::kInt8)
#define CPU_PRIMITIVE_UINT8_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(uint8_t, DataType::kUInt8)
#define CPU_PRIMITIVE_INT32_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(int32_t, DataType::kInt32)
#define CPU_PRIMITIVE_INT64_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(int64_t, DataType::kInt64)
#define CPU_PRIMITIVE_FLOAT_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(float, DataType::kFloat)
#define CPU_PRIMITIVE_DOUBLE_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(double, DataType::kDouble)
#define CPU_PRIMITIVE_FLOAT16_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(float16, DataType::kFloat16)

#define CPU_PRIMITIVE_CALCULATE_ONEFLOW_TYPE  0
#define CPU_PRIMITIVE_CALCULATE_ONEDNN_TYPE   1

#define CPU_PRIMITIVE_ONEDNN_INT8_TYPE_SEQ    OF_PP_MAKE_TUPLE_SEQ(int8_t, DataType::kInt8, dnnl::memory::data_type::s8)
#define CPU_PRIMITIVE_ONEDNN_UINT8_TYPE_SEQ   OF_PP_MAKE_TUPLE_SEQ(uint8_t, DataType::kUInt8, dnnl::memory::data_type::u8)
#define CPU_PRIMITIVE_ONEDNN_INT32_TYPE_SEQ   OF_PP_MAKE_TUPLE_SEQ(int32_t, DataType::kInt32, dnnl::memory::data_type::s32)
#define CPU_PRIMITIVE_ONEDNN_FLOAT_TYPE_SEQ   OF_PP_MAKE_TUPLE_SEQ(float, DataType::kFloat, dnnl::memory::data_type::f32)
#define CPU_PRIMITIVE_ONEDNN_FLOAT16_TYPE_SEQ OF_PP_MAKE_TUPLE_SEQ(float16, DataType::kFloat16, dnnl::memory::data_type::f16)

#define CPU_PRIMITIVE_NATIVE_TYPE_SEQ \
  CPU_PRIMITIVE_CHAR_TYPE_SEQ         \
  CPU_PRIMITIVE_INT8_TYPE_SEQ         \
  CPU_PRIMITIVE_UINT8_TYPE_SEQ        \
  CPU_PRIMITIVE_INT32_TYPE_SEQ        \
  CPU_PRIMITIVE_INT64_TYPE_SEQ        \
  CPU_PRIMITIVE_FLOAT_TYPE_SEQ        \
  CPU_PRIMITIVE_DOUBLE_TYPE_SEQ

#define CPU_PRIMITIVE_ONEDNN_NATIVE_TYPE_SEQ \
  CPU_PRIMITIVE_ONEDNN_INT8_TYPE_SEQ         \
  CPU_PRIMITIVE_ONEDNN_UINT8_TYPE_SEQ        \
  CPU_PRIMITIVE_ONEDNN_INT32_TYPE_SEQ        \
  CPU_PRIMITIVE_ONEDNN_FLOAT_TYPE_SEQ        \
  CPU_PRIMITIVE_ONEDNN_FLOAT16_TYPE_SEQ      \

#define CPU_PRIMITIVE_ALL_TYPE_SEQ \
  CPU_PRIMITIVE_NATIVE_TYPE_SEQ    \
  CPU_PRIMITIVE_FLOAT16_TYPE_SEQ

#define CPU_PRIMITIVE_FLOATING_TYPE_SEQ \
  CPU_PRIMITIVE_FLOAT_TYPE_SEQ          \
  CPU_PRIMITIVE_DOUBLE_TYPE_SEQ

#endif  // ONEFLOW_CORE_PRIMITIVE_CPU_TYPE_SEQ_H_
