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
#include "oneflow/core/ndarray/ndarray_assign_core.h"
#include "oneflow/core/device/cuda_util.h"
#include "oneflow/core/kernel/kernel_util.h"

namespace oneflow {

namespace {

template<typename T, int NDIMS>
__global__ void NdarrayAssignGpu(XpuVarNdarray<T> y, const XpuReducedNdarray<T, NDIMS> reduced) {
  NdarrayAssignCore<T, NDIMS>::Assign(y, reduced);
}

}  // namespace

template<typename T, int NDIMS>
struct NdarrayAssignCoreWrapper<DeviceType::kGPU, T, NDIMS> final {
  static void Assign(ep::Stream* ctx, const XpuVarNdarray<T>& y,
                     const XpuReducedNdarray<T, NDIMS>& reduced) {
    size_t n = y.host_shape().HostElemNum();
    if (n == 0) { return; }
    RUN_CUDA_KERNEL((NdarrayAssignGpu<T, NDIMS>), ctx, n, y, reduced);
  }
};

#define INSTANTIATE_NDARRAY_ASSIGN(dtype_pair, NDIMS) \
  template struct NdarrayAssignCoreWrapper<DeviceType::kGPU, OF_PP_PAIR_FIRST(dtype_pair), NDIMS>;
OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(
    INSTANTIATE_NDARRAY_ASSIGN,
    ARITHMETIC_DATA_TYPE_SEQ HALF_DATA_TYPE_SEQ UNSIGNED_INT_DATA_TYPE_SEQ, DIM_SEQ);

}  // namespace oneflow
