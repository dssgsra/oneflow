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
#include "oneflow/core/framework/framework.h"
#include "oneflow/core/cuda/atomic.cuh"
#include "oneflow/user/kernels/model_update_kernel_util.h"
#include <cub/cub.cuh>
#include "oneflow/core/cuda/atomic.cuh"

namespace oneflow {

namespace {

template<typename T, typename G>
__global__ void SGDUpdateGpu(int64_t n, T scale, float l1, float l2, float weight_decay,
                             float learning_rate_val, const float* learning_rate,
                             const T* scale_by_ptr, const int64_t* skip_if, const G* model_diff,
                             T* model) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  if (learning_rate != nullptr) { learning_rate_val = *learning_rate; }
  if (scale_by_ptr != nullptr) { scale *= *scale_by_ptr; }
  CUDA_1D_KERNEL_LOOP(i, n) {
    SGDUpdateFunctor<T, G>()(model_diff + i, model + i, scale, l1, l2, weight_decay,
                             learning_rate_val);
  }
}

template<typename T, typename K, typename IDX>
__global__ void IndexedSlicesSGDUpdateGpu(float weight_decay, const IDX feature_size,
                                          const int64_t lower_bound, const int64_t upper_bound,
                                          const IDX* num_unique_instance,
                                          const float* learning_rate, const K* indices,
                                          const T* values, T* model) {
  const int64_t n = *num_unique_instance * feature_size;
  const T lr = *learning_rate;
  CUDA_1D_KERNEL_LOOP_T(IDX, i, n) {
    const IDX indices_idx = i / feature_size;
    const IDX inner_idx = i - indices_idx * feature_size;
    const IDX instance_id = indices[indices_idx];
    if (instance_id >= lower_bound && instance_id < upper_bound) {
      const IDX model_idx = (instance_id - lower_bound) * feature_size + inner_idx;
      SGDUpdateFunctor<T, T>()(values + i, model + model_idx, static_cast<T>(1), 0.0, 0.0,
                               weight_decay, lr);
    }
  }
}

template<typename T>
__global__ void SumSquares2(int64_t n, const T* src0, T* dst0, const T* src1, T* dst1) {
  T t_sum0 = 0;
  T t_sum1 = 0;
  CUDA_1D_KERNEL_LOOP(i, n) {
    t_sum0 += src0[i] * src0[i];
    t_sum1 += src1[i] * src1[i];
  }
  typedef cub::BlockReduce<T, kCudaThreadsNumPerBlock> BlockReduce;
  __shared__ typename BlockReduce::TempStorage temp_storage0;
  __shared__ typename BlockReduce::TempStorage temp_storage1;
  T b_sum0 = BlockReduce(temp_storage0).Sum(t_sum0);
  T b_sum1 = BlockReduce(temp_storage1).Sum(t_sum1);
  if (threadIdx.x == 0) {
    cuda::atomic::Add(dst0, b_sum0);
    cuda::atomic::Add(dst1, b_sum1);
  }
}

}  // namespace

template<typename T, typename G>
struct SGDUpdateKernelUtil<DeviceType::kGPU, T, G> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float weight_decay,
                     float learning_rate_val, const float* learning_rate, const T* scale_by_ptr,
                     const int64_t* skip_if, const G* model_diff, T* model);
};

template<typename T, typename G>
void SGDUpdateKernelUtil<DeviceType::kGPU, T, G>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float weight_decay,
    float learning_rate_val, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const G* model_diff, T* model) {
  SGDUpdateGpu<T, G><<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      n, scale, l1, l2, weight_decay, learning_rate_val, learning_rate, scale_by_ptr, skip_if,
      model_diff, model);
}

template<typename T>
struct SGDUpdateKernelUtil<DeviceType::kGPU, T, float16> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float weight_decay,
                     float learning_rate_val, const float* learning_rate, const T* scale_by_ptr,
                     const int64_t* skip_if, const float16* model_diff, T* model);
};

template<typename T>
void SGDUpdateKernelUtil<DeviceType::kGPU, T, float16>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float weight_decay,
    float learning_rate_val, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const float16* model_diff, T* model) {
  SGDUpdateKernelUtil<DeviceType::kGPU, T, half>::Update(
      ctx, n, scale, l1, l2, weight_decay, learning_rate_val, learning_rate, scale_by_ptr, skip_if,
      reinterpret_cast<const half*>(model_diff), model);
}

template struct SGDUpdateKernelUtil<DeviceType::kGPU, double, double>;
template struct SGDUpdateKernelUtil<DeviceType::kGPU, float, float>;
template struct SGDUpdateKernelUtil<DeviceType::kGPU, float, float16>;

template<typename T, typename K, typename IDX>
struct IndexedSlicesSGDUpdateKernelUtil<DeviceType::kGPU, T, K, IDX> {
  static void Update(DeviceCtx* ctx, float weight_decay, int64_t num_indices, int64_t feature_size,
                     int64_t lower_bound, int64_t upper_bound, const IDX* num_unique_instance,
                     const float* learning_rate, const K* indices, const T* values, T* model);
};

template<typename T, typename K, typename IDX>
void IndexedSlicesSGDUpdateKernelUtil<DeviceType::kGPU, T, K, IDX>::Update(
    DeviceCtx* ctx, float weight_decay, int64_t num_indices, int64_t feature_size,
    int64_t lower_bound, int64_t upper_bound, const IDX* num_unique_instance,
    const float* learning_rate, const K* indices, const T* values, T* model) {
  IndexedSlicesSGDUpdateGpu<T, K, IDX>
      <<<BlocksNum4ThreadsNum(num_indices * feature_size), kCudaThreadsNumPerBlock, 0,
         ctx->cuda_stream()>>>(weight_decay, feature_size, lower_bound, upper_bound,
                               num_unique_instance, learning_rate, indices, values, model);
}

#define INITIATE_INDEXED_SLICES_SGD_UPDATE_KERNEL_UTIL_GPU(val_type_pair, key_type_pair,  \
                                                           idx_type_pair)                 \
  template struct IndexedSlicesSGDUpdateKernelUtil<                                       \
      DeviceType::kGPU, OF_PP_PAIR_FIRST(val_type_pair), OF_PP_PAIR_FIRST(key_type_pair), \
      OF_PP_PAIR_FIRST(idx_type_pair)>;
OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(INITIATE_INDEXED_SLICES_SGD_UPDATE_KERNEL_UTIL_GPU,
                                 FLOATING_DATA_TYPE_SEQ, INDEX_DATA_TYPE_SEQ, INT_DATA_TYPE_SEQ);
#undef INITIATE_INDEXED_SLICES_SGD_UPDATE_KERNEL_UTIL_GPU

namespace {

template<typename T, typename G>
__global__ void MomentumUpdateGpu(int64_t n, T scale, float l1, float l2, float beta,
                                  float weight_decay, float learning_rate_val,
                                  const float* learning_rate, const T* scale_by_ptr,
                                  const int64_t* skip_if, const G* model_diff, T* model,
                                  T* momentum) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  if (learning_rate != nullptr) { learning_rate_val = *learning_rate; }
  if (scale_by_ptr != nullptr) { scale *= *scale_by_ptr; }
  CUDA_1D_KERNEL_LOOP(i, n) {
    MomentumUpdateFunctor<T, G>()(model_diff + i, model + i, momentum + i, scale, l1, l2, beta,
                                  weight_decay, learning_rate_val);
  }
}

template<typename T, typename K, typename IDX>
__global__ void IndexedSlicesMomentumUpdateGpu(T beta, float weight_decay, int64_t feature_size,
                                               int64_t lower_bound, int64_t upper_bound,
                                               const IDX* num_unique_instance,
                                               const float* learning_rate, const K* indices,
                                               const T* values, T* model, T* momentum) {
  const int64_t n = *num_unique_instance * feature_size;
  const T lr = *learning_rate;
  CUDA_1D_KERNEL_LOOP(i, n) {
    const IDX indices_idx = i / feature_size;
    const IDX inner_idx = i - indices_idx * feature_size;
    const IDX instance_id = indices[indices_idx];
    if (instance_id >= lower_bound && instance_id < upper_bound) {
      const IDX model_idx = (instance_id - lower_bound) * feature_size + inner_idx;
      MomentumUpdateFunctor<T, T>()(values + i, model + model_idx, momentum + model_idx,
                                    static_cast<T>(1), 0.0, 0.0, beta, weight_decay, lr);
    }
  }
}

}  // namespace

template<typename T, typename G>
struct MomentumUpdateKernelUtil<DeviceType::kGPU, T, G> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta,
                     float weight_decay, float learning_rate_val, const float* learning_rate,
                     const T* scale_by_ptr, const int64_t* skip_if, const G* model_diff, T* model,
                     T* momentum);
};

template<typename T, typename G>
void MomentumUpdateKernelUtil<DeviceType::kGPU, T, G>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta, float weight_decay,
    float learning_rate_val, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const G* model_diff, T* model, T* momentum) {
  MomentumUpdateGpu<T, G>
      <<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
          n, scale, l1, l2, beta, weight_decay, learning_rate_val, learning_rate, scale_by_ptr,
          skip_if, model_diff, model, momentum);
}

template<typename T>
struct MomentumUpdateKernelUtil<DeviceType::kGPU, T, float16> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta,
                     float weight_decay, float learning_rate_val, const float* learning_rate,
                     const T* scale_by_ptr, const int64_t* skip_if, const float16* model_diff,
                     T* model, T* momentum);
};

template<typename T>
void MomentumUpdateKernelUtil<DeviceType::kGPU, T, float16>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta, float weight_decay,
    float learning_rate_val, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const float16* model_diff, T* model, T* momentum) {
  MomentumUpdateKernelUtil<DeviceType::kGPU, T, half>::Update(
      ctx, n, scale, l1, l2, beta, weight_decay, learning_rate_val, learning_rate, scale_by_ptr,
      skip_if, reinterpret_cast<const half*>(model_diff), model, momentum);
}

template struct MomentumUpdateKernelUtil<DeviceType::kGPU, double, double>;
template struct MomentumUpdateKernelUtil<DeviceType::kGPU, float, float>;
template struct MomentumUpdateKernelUtil<DeviceType::kGPU, float, float16>;

template<typename T, typename K, typename IDX>
struct IndexedSlicesMomentumMdUpdateKernelUtil<DeviceType::kGPU, T, K, IDX> {
  static void Update(DeviceCtx* ctx, T beta, float weight_decay, int64_t num_instance,
                     int64_t feature_size, int64_t lower_bound, int64_t upper_bound,
                     const IDX* num_unique_instance, const float* learning_rate, const K* indices,
                     const T* values, T* model, T* momentum);
};

template<typename T, typename K, typename IDX>
void IndexedSlicesMomentumMdUpdateKernelUtil<DeviceType::kGPU, T, K, IDX>::Update(
    DeviceCtx* ctx, T beta, float weight_decay, int64_t num_instance, int64_t feature_size,
    int64_t lower_bound, int64_t upper_bound, const IDX* num_unique_instance,
    const float* learning_rate, const K* indices, const T* values, T* model, T* momentum) {
  IndexedSlicesMomentumUpdateGpu<T, K, IDX><<<BlocksNum4ThreadsNum(num_instance * feature_size),
                                              kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      beta, weight_decay, feature_size, lower_bound, upper_bound, num_unique_instance,
      learning_rate, indices, values, model, momentum);
}

#define INSTANTIATE_INDEXED_SLICES_MOMENTUM_MODEL_UPDATE_KERNEL_UTIL_GPU(                 \
    val_type_pair, key_type_pair, idx_type_pair)                                          \
  template struct IndexedSlicesMomentumMdUpdateKernelUtil<                                \
      DeviceType::kGPU, OF_PP_PAIR_FIRST(val_type_pair), OF_PP_PAIR_FIRST(key_type_pair), \
      OF_PP_PAIR_FIRST(idx_type_pair)>;
OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(INSTANTIATE_INDEXED_SLICES_MOMENTUM_MODEL_UPDATE_KERNEL_UTIL_GPU,
                                 FLOATING_DATA_TYPE_SEQ, INDEX_DATA_TYPE_SEQ, INT_DATA_TYPE_SEQ);
#undef INSTANTIATE_INDEXED_SLICES_MOMENTUM_MODEL_UPDATE_KERNEL_UTIL_GPU

namespace {

__global__ void BiasCorrectionFactorKernelGpu(float beta, const int64_t* train_step, float* out) {
  const auto exponent = static_cast<double>(*train_step + 1);
  const float bias_correction_factor = 1.0 - static_cast<float>(pow(beta, exponent));
  *out = bias_correction_factor;
}

template<typename T, typename G>
__global__ void AdamUpdateGpu(int64_t n, T scale, float l1, float l2, float beta1, float beta2,
                              float epsilon, float weight_decay, bool amsgrad,
                              bool do_bias_correction, float learning_rate_val,
                              float bias_correction1_val, float bias_correction2_val,
                              const float* learning_rate, const T* scale_by_ptr,
                              const int64_t* skip_if, const float* bias_correction1_ptr,
                              const float* bias_correction2_ptr, const G* model_diff, T* model,
                              T* m, T* v, T* max_v) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  if (learning_rate != nullptr) { learning_rate_val = *learning_rate; }
  if (scale_by_ptr != nullptr) { scale *= *scale_by_ptr; }
  if (bias_correction1_ptr != nullptr) { bias_correction1_val = *bias_correction1_ptr; }
  if (bias_correction2_ptr != nullptr) { bias_correction2_val = *bias_correction2_ptr; }

  CUDA_1D_KERNEL_LOOP(i, n) {
    AdamUpdateFunctor<T, G>()(model_diff + i, model + i, m + i, v + i, max_v + i, scale, l1, l2,
                              beta1, beta2, epsilon, weight_decay, amsgrad, bias_correction1_val,
                              bias_correction2_val, learning_rate_val);
  }
}

template<typename T>
__global__ void AdamUpdateBetaTGpu(const T beta1, const T beta2, const int64_t* skip_if, T* beta1_t,
                                   T* beta2_t) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  *beta1_t *= beta1;
  *beta2_t *= beta2;
}

template<typename T, typename K, typename IDX>
__global__ void IndexedSlicesAdamUpdateGpu(
    float beta1, float beta2, float epsilon, float weight_decay, bool amsgrad,
    bool do_bias_correction, float lr, int64_t feature_size, int64_t lower_bound,
    int64_t upper_bound, const IDX* num_unique_instance, const float* learning_rate,
    const float* bias_correction1_ptr, const float* bias_correction2_ptr, const K* indices,
    const T* values, T* model, T* m, T* v, T* max_v) {
  if (learning_rate != nullptr) { lr = *learning_rate; }
  float bias_correction1 = 1.0;
  float bias_correction2 = 1.0;
  if (bias_correction1_ptr != nullptr) { bias_correction1 = *bias_correction1_ptr; }
  if (bias_correction2_ptr != nullptr) { bias_correction2 = *bias_correction2_ptr; }

  const int64_t n = *num_unique_instance * feature_size;
  CUDA_1D_KERNEL_LOOP(i, n) {
    const IDX indices_idx = i / feature_size;
    const IDX inner_idx = i - indices_idx * feature_size;
    const IDX instance_id = indices[indices_idx];
    if (instance_id >= lower_bound && instance_id < upper_bound) {
      const IDX model_idx = (instance_id - lower_bound) * feature_size + inner_idx;
      AdamUpdateFunctor<T, T>()(values + i, model + model_idx, m + model_idx, v + model_idx,
                                max_v + i, static_cast<T>(1), 0, 0, beta1, beta2, epsilon,
                                weight_decay, amsgrad, bias_correction1, bias_correction2, lr);
    }
  }
}

template<typename T, typename G>
__global__ void LambGradGpu(int64_t n, T scale, float l1, float l2, float beta1, float beta2,
                            float epsilon, const T* beta1_t, const T* beta2_t,
                            const T* scale_by_ptr, const int64_t* skip_if, const G* model_diff,
                            T* adam_diff, T* model, T* m, T* v) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  if (scale_by_ptr != nullptr) { scale *= *scale_by_ptr; }
  CUDA_1D_KERNEL_LOOP(i, n) {
    LambGradFunctor<T, G>()(beta1_t, beta2_t, model_diff + i, adam_diff + i, model + i, m + i,
                            v + i, scale, l1, l2, beta1, beta2, epsilon);
  }
}

template<typename T>
__global__ void LambUpdateGpu(int64_t n, float weight_decay, const float* learning_rate,
                              const int64_t* skip_if, const T* w_norm_2, const T* g_norm_2,
                              const T* beta1_t, const T* beta2_t, const T* adam_diff, T* model) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  const float lr = LambLRFunctor<T>()(*learning_rate, w_norm_2, g_norm_2);
  CUDA_1D_KERNEL_LOOP(i, n) { LambUpdateFunctor<T>()(lr, weight_decay, adam_diff + i, model + i); }
}

}  // namespace

template<typename T, typename G>
struct AdamUpdateKernelUtil<DeviceType::kGPU, T, G> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta1,
                     float beta2, float epsilon, float weight_decay, bool amsgrad,
                     bool do_bias_correction, float learning_rate_val, float bias_correction1_val,
                     float bias_correction2_val, const float* learning_rate, const T* scale_by_ptr,
                     const int64_t* skip_if, const float* bias_correction1_ptr,
                     const float* bias_correction2_ptr, const G* model_diff, T* model, T* m, T* v,
                     T* max_v);
};

template<typename T, typename G>
void AdamUpdateKernelUtil<DeviceType::kGPU, T, G>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta1, float beta2, float epsilon,
    float weight_decay, bool amsgrad, bool do_bias_correction, float learning_rate_val,
    float bias_correction1_val, float bias_correction2_val, const float* learning_rate,
    const T* scale_by_ptr, const int64_t* skip_if, const float* bias_correction1_ptr,
    const float* bias_correction2_ptr, const G* model_diff, T* model, T* m, T* v, T* max_v) {
  AdamUpdateGpu<T, G><<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      n, scale, l1, l2, beta1, beta2, epsilon, weight_decay, amsgrad, do_bias_correction,
      learning_rate_val, bias_correction1_val, bias_correction2_val, learning_rate, scale_by_ptr,
      skip_if, bias_correction1_ptr, bias_correction2_ptr, model_diff, model, m, v, max_v);
}

template<typename T>
struct AdamUpdateKernelUtil<DeviceType::kGPU, T, float16> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta1,
                     float beta2, float epsilon, float weight_decay, bool amsgrad,
                     bool do_bias_correction, float learning_rate_val, float bias_correction1_val,
                     float bias_correction2_val, const float* learning_rate, const T* scale_by_ptr,
                     const int64_t* skip_if, const float* bias_correction1_ptr,
                     const float* bias_correction2_ptr, const float16* model_diff, T* model, T* m,
                     T* v, T* max_v);
};

template<typename T>
void AdamUpdateKernelUtil<DeviceType::kGPU, T, float16>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float beta1, float beta2, float epsilon,
    float weight_decay, bool amsgrad, bool do_bias_correction, float learning_rate_val,
    float bias_correction1_val, float bias_correction2_val, const float* learning_rate,
    const T* scale_by_ptr, const int64_t* skip_if, const float* bias_correction1_ptr,
    const float* bias_correction2_ptr, const float16* model_diff, T* model, T* m, T* v, T* max_v) {
  AdamUpdateKernelUtil<DeviceType::kGPU, T, half>::Update(
      ctx, n, scale, l1, l2, beta1, beta2, epsilon, weight_decay, amsgrad, do_bias_correction,
      learning_rate_val, bias_correction1_val, bias_correction2_val, learning_rate, scale_by_ptr,
      skip_if, bias_correction1_ptr, bias_correction2_ptr,
      reinterpret_cast<const half*>(model_diff), model, m, v, max_v);
}

template struct AdamUpdateKernelUtil<DeviceType::kGPU, float, float>;
template struct AdamUpdateKernelUtil<DeviceType::kGPU, double, double>;
template struct AdamUpdateKernelUtil<DeviceType::kGPU, float, float16>;

template<typename T, typename G>
__global__ void AdagradUpdateGpu(int64_t n, T scale, float l1, float l2, float lr_decay,
                                 float epsilon, float weight_decay, float learning_rate_val,
                                 int64_t train_step, const float* learning_rate,
                                 const int64_t* train_step_ptr, const T* scale_by_ptr,
                                 const int64_t* skip_if, const G* model_diff, T* model, T* sum) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  if (learning_rate != nullptr) { learning_rate_val = *learning_rate; }
  if (train_step_ptr != nullptr) {
    train_step = *train_step_ptr + 1;
  }  // train_step_ptr start from zero.
  if (scale_by_ptr != nullptr) { scale *= *scale_by_ptr; }
  learning_rate_val = learning_rate_val / (1 + (train_step - 1) * lr_decay);

  CUDA_1D_KERNEL_LOOP(i, n) {
    AdagradUpdateFunctor<T, G>()(model_diff + i, model + i, sum + i, scale, l1, l2, epsilon,
                                 weight_decay, learning_rate_val);
  }
}

template<typename T, typename G>
struct AdagradUpdateKernelUtil<DeviceType::kGPU, T, G> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float lr_decay,
                     float epsilon, float weight_decay, float learning_rate_val, int64_t train_step,
                     const float* learning_rate, const int64_t* train_step_ptr,
                     const T* scale_by_ptr, const int64_t* skip_if, const G* model_diff, T* model,
                     T* sum);
};

template<typename T, typename G>
void AdagradUpdateKernelUtil<DeviceType::kGPU, T, G>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float lr_decay, float epsilon,
    float weight_decay, float learning_rate_val, int64_t train_step, const float* learning_rate,
    const int64_t* train_step_ptr, const T* scale_by_ptr, const int64_t* skip_if,
    const G* model_diff, T* model, T* sum) {
  AdagradUpdateGpu<T, G>
      <<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
          n, scale, l1, l2, lr_decay, epsilon, weight_decay, learning_rate_val, train_step,
          learning_rate, train_step_ptr, scale_by_ptr, skip_if, model_diff, model, sum);
}

template struct AdagradUpdateKernelUtil<DeviceType::kGPU, float, float>;
template struct AdagradUpdateKernelUtil<DeviceType::kGPU, double, double>;

template<typename T, typename G>
struct LambUpdateKernelUtil<DeviceType::kGPU, T, G> {
  static void Update(DeviceCtx* ctx, int64_t n, float scale, float l1, float l2, float beta1,
                     float beta2, float epsilon, float weight_decay, const float* learning_rate,
                     const T* scale_by_ptr, const int64_t* skip_if, const G* model_diff,
                     T* adam_diff, T* model, T* m, T* v, T* norm_buffer, T* beta1_t, T* beta2_t);
};

template<typename T, typename G>
void LambUpdateKernelUtil<DeviceType::kGPU, T, G>::Update(
    DeviceCtx* ctx, int64_t n, float scale, float l1, float l2, float beta1, float beta2,
    float epsilon, float weight_decay, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const G* model_diff, T* adam_diff, T* model, T* m, T* v, T* norm_buffer,
    T* beta1_t, T* beta2_t) {
  AdamUpdateBetaTGpu<T><<<1, 1, 0, ctx->cuda_stream()>>>(beta1, beta2, skip_if, beta1_t, beta2_t);
  LambGradGpu<T, G><<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      n, scale, l1, l2, beta1, beta2, epsilon, beta1_t, beta2_t, scale_by_ptr, skip_if, model_diff,
      adam_diff, model, m, v);
  T* w_norm_2 = norm_buffer;
  T* g_norm_2 = norm_buffer + 1;
  Memset<DeviceType::kGPU>(ctx, norm_buffer, 0, 2 * sizeof(T));
  SumSquares2<T><<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      n, model, w_norm_2, adam_diff, g_norm_2);
  LambUpdateGpu<T><<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      n, weight_decay, learning_rate, skip_if, w_norm_2, g_norm_2, beta1_t, beta2_t, adam_diff,
      model);
}

template<typename T>
struct LambUpdateKernelUtil<DeviceType::kGPU, T, float16> {
  static void Update(DeviceCtx* ctx, int64_t n, float scale, float l1, float l2, float beta1,
                     float beta2, float epsilon, float weight_decay, const float* learning_rate,
                     const T* scale_by_ptr, const int64_t* skip_if, const float16* model_diff,
                     T* adam_diff, T* model, T* m, T* v, T* norm_buffer, T* beta1_t, T* beta2_t);
};

template<typename T>
void LambUpdateKernelUtil<DeviceType::kGPU, T, float16>::Update(
    DeviceCtx* ctx, int64_t n, float scale, float l1, float l2, float beta1, float beta2,
    float epsilon, float weight_decay, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const float16* model_diff, T* adam_diff, T* model, T* m, T* v,
    T* norm_buffer, T* beta1_t, T* beta2_t) {
  LambUpdateKernelUtil<DeviceType::kGPU, T, half>::Update(
      ctx, n, scale, l1, l2, beta1, beta2, epsilon, weight_decay, learning_rate, scale_by_ptr,
      skip_if, reinterpret_cast<const half*>(model_diff), adam_diff, model, m, v, norm_buffer,
      beta1_t, beta2_t);
}

template struct LambUpdateKernelUtil<DeviceType::kGPU, float, float>;
template struct LambUpdateKernelUtil<DeviceType::kGPU, double, double>;
template struct LambUpdateKernelUtil<DeviceType::kGPU, float, float16>;

template<typename T, typename K, typename IDX>
struct IndexedSlicesAdamMdUpdateKernelUtil<DeviceType::kGPU, T, K, IDX> {
  static void Update(DeviceCtx* ctx, float beta1, float beta2, float epsilon, float weight_decay,
                     bool amsgrad, bool do_bias_correction, float lr, int64_t num_instance,
                     int64_t feature_size, int64_t lower_bound, int64_t upper_bound,
                     const IDX* num_unique_instance, const float* learning_rate,
                     const float* bias_correction1_ptr, const float* bias_correction2_ptr,
                     const K* indices, const T* values, T* model, T* m, T* v, T* max_v);
};

template<typename T, typename K, typename IDX>
void IndexedSlicesAdamMdUpdateKernelUtil<DeviceType::kGPU, T, K, IDX>::Update(
    DeviceCtx* ctx, float beta1, float beta2, float epsilon, float weight_decay, bool amsgrad,
    bool do_bias_correction, float lr, int64_t num_instance, int64_t feature_size,
    int64_t lower_bound, int64_t upper_bound, const IDX* num_unique_instance,
    const float* learning_rate, const float* bias_correction1_ptr,
    const float* bias_correction2_ptr, const K* indices, const T* values, T* model, T* m, T* v,
    T* max_v) {
  IndexedSlicesAdamUpdateGpu<T, K, IDX><<<BlocksNum4ThreadsNum(num_instance * feature_size),
                                          kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      beta1, beta2, epsilon, weight_decay, amsgrad, do_bias_correction, lr, feature_size,
      lower_bound, upper_bound, num_unique_instance, learning_rate, bias_correction1_ptr,
      bias_correction2_ptr, indices, values, model, m, v, max_v);
}

#define INSTANTIATE_INDEXED_SLICES_ADAM_MODEL_UPDATE_KERNEL_UTIL_GPU(val_type_pair, key_type_pair, \
                                                                     idx_type_pair)                \
  template struct IndexedSlicesAdamMdUpdateKernelUtil<                                             \
      DeviceType::kGPU, OF_PP_PAIR_FIRST(val_type_pair), OF_PP_PAIR_FIRST(key_type_pair),          \
      OF_PP_PAIR_FIRST(idx_type_pair)>;
OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(INSTANTIATE_INDEXED_SLICES_ADAM_MODEL_UPDATE_KERNEL_UTIL_GPU,
                                 FLOATING_DATA_TYPE_SEQ, INDEX_DATA_TYPE_SEQ, INT_DATA_TYPE_SEQ);
#undef INSTANTIATE_INDEXED_SLICES_ADAM_MODEL_UPDATE_KERNEL_UTIL_GPU

template<>
struct BiasCorrectionFactorKernelUtil<DeviceType::kGPU> {
  static void BiasCorrectionFactorCompute(DeviceCtx* ctx, float beta, const int64_t* train_step,
                                          float* out);
};

void BiasCorrectionFactorKernelUtil<DeviceType::kGPU>::BiasCorrectionFactorCompute(
    DeviceCtx* ctx, float beta, const int64_t* train_step, float* out) {
  BiasCorrectionFactorKernelGpu<<<1, 1, 0, ctx->cuda_stream()>>>(beta, train_step, out);
}

namespace {

template<typename T, typename G, bool centered>
__global__ void RmsPropUpdateGpu(int64_t n, T scale, float l1, float l2, T* mean_square,
                                 T* mean_gradient, float epsilon, float weight_decay,
                                 float decay_rate, float learning_rate_val,
                                 const float* learning_rate, const T* scale_by_ptr,
                                 const int64_t* skip_if, const G* model_diff, T* model) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  if (learning_rate != nullptr) { learning_rate_val = *learning_rate; }
  if (scale_by_ptr != nullptr) { scale *= *scale_by_ptr; }
  CUDA_1D_KERNEL_LOOP(i, n) {
    RmsPropUpdateFunctor<T, G, centered>()(model_diff + i, model + i, n, scale, l1, l2,
                                           mean_square + i,
                                           (centered ? mean_gradient + i : nullptr), epsilon,
                                           weight_decay, decay_rate, learning_rate_val);
  }
}

}  // namespace

template<typename T, typename G>
struct RmsPropUpdateKernelUtil<DeviceType::kGPU, T, G> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, bool centered,
                     float epsilon, float weight_decay, float decay_rate, float learning_rate_val,
                     const float* learning_rate, const T* scale_by_ptr, const int64_t* skip_if,
                     const G* model_diff, T* model, T* mean_square, T* mean_gradient);
};

template<typename T, typename G>
void RmsPropUpdateKernelUtil<DeviceType::kGPU, T, G>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, bool centered, float epsilon,
    float weight_decay, float decay_rate, float learning_rate_val, const float* learning_rate,
    const T* scale_by_ptr, const int64_t* skip_if, const G* model_diff, T* model, T* mean_square,
    T* mean_gradient) {
  if (centered) {
    RmsPropUpdateGpu<T, G, true>
        <<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
            n, scale, l1, l2, mean_square, mean_gradient, epsilon, weight_decay, decay_rate,
            learning_rate_val, learning_rate, scale_by_ptr, skip_if, model_diff, model);
  } else {
    RmsPropUpdateGpu<T, G, false>
        <<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
            n, scale, l1, l2, mean_square, mean_gradient, epsilon, weight_decay, decay_rate,
            learning_rate_val, learning_rate, scale_by_ptr, skip_if, model_diff, model);
  }
}

template<typename T>
struct RmsPropUpdateKernelUtil<DeviceType::kGPU, T, float16> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, bool centered,
                     float epsilon, float weight_decay, float decay_rate, float learning_rate_val,
                     const float* learning_rate, const T* scale_by_ptr, const int64_t* skip_if,
                     const float16* model_diff, T* model, T* mean_square, T* mean_gradient);
};

template<typename T>
void RmsPropUpdateKernelUtil<DeviceType::kGPU, T, float16>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, bool centered, float epsilon,
    float weight_decay, float decay_rate, float learning_rate_val, const float* learning_rate,
    const T* scale_by_ptr, const int64_t* skip_if, const float16* model_diff, T* model,
    T* mean_square, T* mean_gradient) {
  RmsPropUpdateKernelUtil<DeviceType::kGPU, T, half>::Update(
      ctx, n, scale, l1, l2, centered, epsilon, weight_decay, decay_rate, learning_rate_val,
      learning_rate, scale_by_ptr, skip_if, reinterpret_cast<const half*>(model_diff), model,
      mean_square, mean_gradient);
}

template struct RmsPropUpdateKernelUtil<DeviceType::kGPU, float, float>;
template struct RmsPropUpdateKernelUtil<DeviceType::kGPU, double, double>;
template struct RmsPropUpdateKernelUtil<DeviceType::kGPU, float, float16>;

namespace {

template<typename T, typename G>
__global__ void LarsScaleModelDiffGpu(int64_t n, T scale, float l1, float l2, const T* scale_by_ptr,
                                      const int64_t* skip_if, const G* model_diff, T* model,
                                      T* model_diff_tmp) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  if (scale_by_ptr != nullptr) { scale *= *scale_by_ptr; }
  CUDA_1D_KERNEL_LOOP(i, n) {
    model_diff_tmp[i] =
        CastScaleRegularizeGradientFunctor<T, G>()(model_diff[i], model[i], scale, l1, l2);
  }
}

template<typename T>
__global__ void LarsGetLocalLearningRateGpu(const float* learning_rate, T weight_decay, T epsilon,
                                            T lars_coefficient, const int64_t* skip_if,
                                            T* data_tmp) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  T* model_norm = &data_tmp[0];
  T* model_diff_norm = &data_tmp[1];
  T* local_learning_rate = &data_tmp[2];
  *model_norm = std::sqrt(*model_norm);
  *model_diff_norm = std::sqrt(*model_diff_norm);
  T lars = static_cast<T>(1);
  if (*model_norm > 0 && *model_diff_norm > 0) {
    lars = lars_coefficient * (*model_norm)
           / (epsilon + (*model_diff_norm) + weight_decay * (*model_norm));
  }
  *local_learning_rate = *learning_rate * lars;
}

template<typename T>
__global__ void LarsUpdateGpu(int64_t n, float momentum_beta, T* momentum, float weight_decay,
                              const int64_t* skip_if, T* local_learning_rate, T* model_diff_tmp,
                              T* model) {
  if (skip_if != nullptr && *skip_if != 0) { return; }
  CUDA_1D_KERNEL_LOOP(i, n) {
    LarsUpdateFunctor<T>()(model_diff_tmp + i, model + i, momentum_beta, momentum + i, weight_decay,
                           *local_learning_rate);
  }
}
}  // namespace

template<typename T, typename G>
struct LarsUpdateKernelUtil<DeviceType::kGPU, T, G> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float momentum_beta,
                     float epsilon, float lars_coefficient, float weight_decay,
                     const float* learning_rate, const T* scale_by_ptr, const int64_t* skip_if,
                     const G* model_diff, T* model, T* momentum, T* data_tmp, T* model_diff_tmp);
};

template<typename T, typename G>
void LarsUpdateKernelUtil<DeviceType::kGPU, T, G>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float momentum_beta, float epsilon,
    float lars_coefficient, float weight_decay, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const G* model_diff, T* model, T* momentum, T* data_tmp,
    T* model_diff_tmp) {
  LarsScaleModelDiffGpu<T, G>
      <<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
          n, scale, l1, l2, scale_by_ptr, skip_if, model_diff, model, model_diff_tmp);
  T* model_norm = data_tmp;
  T* model_diff_norm = data_tmp + 1;
  T* local_learning_rate = data_tmp + 2;
  Memset<DeviceType::kGPU>(ctx, data_tmp, 0, 2 * sizeof(T));
  SumSquares2<T><<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      n, model, model_norm, model_diff_tmp, model_diff_norm);
  LarsGetLocalLearningRateGpu<T><<<1, 1, 0, ctx->cuda_stream()>>>(
      learning_rate, weight_decay, epsilon, lars_coefficient, skip_if, data_tmp);
  LarsUpdateGpu<T><<<BlocksNum4ThreadsNum(n), kCudaThreadsNumPerBlock, 0, ctx->cuda_stream()>>>(
      n, momentum_beta, momentum, weight_decay, skip_if, local_learning_rate, model_diff_tmp,
      model);
}

template<typename T>
struct LarsUpdateKernelUtil<DeviceType::kGPU, T, float16> {
  static void Update(DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float momentum_beta,
                     float epsilon, float lars_coefficient, float weight_decay,
                     const float* learning_rate, const T* scale_by_ptr, const int64_t* skip_if,
                     const float16* model_diff, T* model, T* momentum, T* data_tmp,
                     T* model_diff_tmp);
};

template<typename T>
void LarsUpdateKernelUtil<DeviceType::kGPU, T, float16>::Update(
    DeviceCtx* ctx, int64_t n, T scale, float l1, float l2, float momentum_beta, float epsilon,
    float lars_coefficient, float weight_decay, const float* learning_rate, const T* scale_by_ptr,
    const int64_t* skip_if, const float16* model_diff, T* model, T* momentum, T* data_tmp,
    T* model_diff_tmp) {
  LarsUpdateKernelUtil<DeviceType::kGPU, T, half>::Update(
      ctx, n, scale, l1, l2, momentum_beta, epsilon, lars_coefficient, weight_decay, learning_rate,
      scale_by_ptr, skip_if, reinterpret_cast<const half*>(model_diff), model, momentum, data_tmp,
      model_diff_tmp);
}

template struct LarsUpdateKernelUtil<DeviceType::kGPU, float, float>;
template struct LarsUpdateKernelUtil<DeviceType::kGPU, double, double>;
template struct LarsUpdateKernelUtil<DeviceType::kGPU, float, float16>;

}  // namespace oneflow
