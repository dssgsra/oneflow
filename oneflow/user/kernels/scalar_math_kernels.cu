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
#include "oneflow/user/kernels/scalar_math_kernels.h"
#include "oneflow/user/kernels/elementwise_xpu_kernel.cuh"
#include "oneflow/core/kernel/util/cuda_half_util.h"

namespace oneflow {

template<template<typename> typename Op, typename T>
struct UnaryByScalarFunctor {
  __host__ __device__ explicit UnaryByScalarFunctor(T scalar) : scalar(scalar) {}
  __device__ T operator()(T a) const { return Op<T>::Invoke(a, scalar); }
  const T scalar;
};

template<template<typename> typename Op>
struct UnaryByScalarFunctor<Op, float16> {
  __host__ __device__ explicit UnaryByScalarFunctor(half scalar) : scalar(scalar) {}
  __device__ half operator()(half a) const { return Op<half>::Invoke(a, scalar); }
  const half scalar;
};

template<template<typename> class BIN_OP, typename T>
struct ScalarMathFunctor<DeviceType::kGPU, BIN_OP, T> final {
  void operator()(DeviceCtx* ctx, const int64_t elem_cnt, const T scalar, const T* in, T* out) {
    OF_CUDA_CHECK(cuda::elementwise::Unary(UnaryByScalarFunctor<BIN_OP, T>(scalar), elem_cnt, out,
                                           in, ctx->cuda_stream()));
  }
};

template<template<typename> class BIN_OP>
struct ScalarMathFunctor<DeviceType::kGPU, BIN_OP, float16> final {
  void operator()(DeviceCtx* ctx, const int64_t elem_cnt, float16 scalar, const float16* in,
                  float16* out) {
    OF_CUDA_CHECK(cuda::elementwise::Unary(
        UnaryByScalarFunctor<BIN_OP, float16>(float16_2half(scalar)), elem_cnt,
        reinterpret_cast<half*>(out), reinterpret_cast<const half*>(in), ctx->cuda_stream()));
  }
};

INSTANTIATE_SCALAR_MATH_FUNCTORS(DeviceType::kGPU, BinaryFuncAdd);
INSTANTIATE_SCALAR_MATH_FUNCTORS(DeviceType::kGPU, BinaryFuncFloorDiv);
INSTANTIATE_SCALAR_MATH_FUNCTORS(DeviceType::kGPU, BinaryFuncFMod);
INSTANTIATE_SCALAR_MATH_FUNCTORS(DeviceType::kGPU, BinaryFuncMul);
INSTANTIATE_SCALAR_MATH_FUNCTORS(DeviceType::kGPU, BinaryFuncPow);

template<typename T>
struct ScalarPowGradFunctor {
  OF_DEVICE_FUNC explicit ScalarPowGradFunctor(T exponent) : exponent(exponent) {}
  __device__ T operator()(T x, T dy) const {
    return exponent * (pow(x, exponent - static_cast<T>(1.0))) * dy;
  }
  const T exponent;
};

template<>
struct ScalarPowGradFunctor<half> {
  OF_DEVICE_FUNC explicit ScalarPowGradFunctor(half exponent) : exponent(exponent) {}
  __device__ half operator()(half x, half dy) const {
    return __float2half(
        __half2float(exponent)
        * (__powf(__half2float(x), __half2float(exponent) - static_cast<float>(1.0)))
        * __half2float(dy));
  }
  const half exponent;
};

template<DeviceType device_type, typename T>
class GpuScalarPowGradKernel final : public user_op::OpKernel {
 public:
  GpuScalarPowGradKernel() = default;
  ~GpuScalarPowGradKernel() = default;

 private:
  using user_op::OpKernel::Compute;
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* x_tensor = ctx->Tensor4ArgNameAndIndex("x", 0);
    const user_op::Tensor* dy_tensor = ctx->Tensor4ArgNameAndIndex("dy", 0);
    user_op::Tensor* dx_tensor = ctx->Tensor4ArgNameAndIndex("dx", 0);
    const T* x_ptr = x_tensor->dptr<T>();
    const T* dy_ptr = dy_tensor->dptr<T>();
    T* dx_ptr = dx_tensor->mut_dptr<T>();
    T scalar_operand = static_cast<T>(0);
    if (ctx->Attr<bool>("has_int_operand")) {
      scalar_operand = static_cast<T>(ctx->Attr<int64_t>("int_operand"));
    } else if (ctx->Attr<bool>("has_float_operand")) {
      scalar_operand = static_cast<T>(ctx->Attr<double>("float_operand"));
    } else {
      UNIMPLEMENTED();
    }
    const int32_t elem_cnt = x_tensor->shape().elem_cnt();
    OF_CUDA_CHECK((oneflow::cuda::elementwise::Binary(ScalarPowGradFunctor<T>(scalar_operand),
                                                      elem_cnt, dx_ptr, x_ptr, dy_ptr,
                                                      ctx->device_ctx()->cuda_stream())));
  }
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

#define REGISTER_GPU_SCALAR_POW_BACKWARD_KERNEL(device, dtype) \
  REGISTER_USER_KERNEL("scalar_pow_grad")                      \
      .SetCreateFn<GpuScalarPowGradKernel<device, dtype>>()    \
      .SetIsMatchedHob((user_op::HobDeviceType() == device)    \
                       && (user_op::HobDataType("dx", 0) == GetDataType<dtype>::value));

REGISTER_GPU_SCALAR_POW_BACKWARD_KERNEL(DeviceType::kGPU, uint8_t);
REGISTER_GPU_SCALAR_POW_BACKWARD_KERNEL(DeviceType::kGPU, int8_t);
REGISTER_GPU_SCALAR_POW_BACKWARD_KERNEL(DeviceType::kGPU, int32_t);
REGISTER_GPU_SCALAR_POW_BACKWARD_KERNEL(DeviceType::kGPU, int64_t);
REGISTER_GPU_SCALAR_POW_BACKWARD_KERNEL(DeviceType::kGPU, float);
REGISTER_GPU_SCALAR_POW_BACKWARD_KERNEL(DeviceType::kGPU, double);

}  // namespace oneflow
