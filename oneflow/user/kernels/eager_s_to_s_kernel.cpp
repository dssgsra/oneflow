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
#include "oneflow/user/kernels/communicate_util.h"
#include "oneflow/core/device/nccl_util.h"
#include "oneflow/core/common/container_util.h"
#include "oneflow/core/framework/framework.h"
#include "oneflow/core/kernel/new_kernel_util.h"
#include "oneflow/core/ccl/ccl.h"
#include "oneflow/core/job/parallel_desc.h"
#include "oneflow/core/job/nd_sbp_util.h"
#include "oneflow/core/register/tensor_slice_copier.h"
#include "oneflow/core/control/global_process_ctx.h"

namespace oneflow {

namespace {

bool ContainsEmptySlice(const std::vector<TensorSliceView>& slices) {
  return std::any_of(slices.cbegin(), slices.cend(),
                     [](const TensorSliceView& slice) { return slice.IsEmpty(); });
}

Maybe<Symbol<cfg::NdSbp>> GetAllSplitNdSbp(int64_t axis, int64_t ndim) {
  cfg::NdSbp split_nd_sbp;
  for (int64_t i = 0; i < ndim; ++i) {
    split_nd_sbp.mutable_sbp_parallel()->Add()->mutable_split_parallel()->set_axis(axis);
  }
  return SymbolOf(split_nd_sbp);
}

auto* CachedGetAllSplitNdSbp = DECORATE(&GetAllSplitNdSbp, ThreadLocal);

class EagerNaiveSToSOpKernelState final : public user_op::OpKernelState {
 public:
  explicit EagerNaiveSToSOpKernelState(user_op::KernelInitContext* ctx) { Init(ctx); }
  ~EagerNaiveSToSOpKernelState() override = default;

  const std::vector<std::pair<int64_t, std::shared_ptr<TensorSliceCopier>>>&
  sorted_elem_cnt2in_tensor_slice_copier_pair() const {
    return sorted_elem_cnt2in_tensor_slice_copier_pair_;
  }

  const std::vector<std::pair<int64_t, std::shared_ptr<TensorSliceCopier>>>&
  sorted_elem_cnt2out_tensor_slice_copier_pair() const {
    return sorted_elem_cnt2out_tensor_slice_copier_pair_;
  }

  const std::vector<std::pair<int64_t, int64_t>>& sorted_p2p_pair() const {
    return sorted_p2p_pair_;
  }

 private:
  void Init(user_op::KernelInitContext* ctx) {
    const std::string& in_parallel_conf_txt = ctx->Attr<std::string>("in_parallel_conf");
    const std::string& out_parallel_conf_txt = ctx->Attr<std::string>("out_parallel_conf");
    const int64_t in_split_axis = ctx->Attr<int64_t>("in_split_axis");
    const int64_t out_split_axis = ctx->Attr<int64_t>("out_split_axis");
    const Shape& shape = ctx->Attr<Shape>("shape");
    DeviceType device_type = ctx->device_type();
    DataType data_type = ctx->TensorDesc4ArgNameAndIndex("in", 0)->data_type();
    Symbol<ParallelDesc> in_parallel_desc = CHECK_JUST(TxtStringToPlacement(in_parallel_conf_txt));
    Symbol<ParallelDesc> out_parallel_desc =
        CHECK_JUST(TxtStringToPlacement(out_parallel_conf_txt));
    int64_t in_parallel_num = in_parallel_desc->parallel_num();
    int64_t out_parallel_num = out_parallel_desc->parallel_num();

    const std::vector<TensorSliceView> in_slices =
        GetTensorSliceView(*in_parallel_desc->hierarchy(),
                           *CHECK_JUST(CachedGetAllSplitNdSbp(
                               in_split_axis, in_parallel_desc->hierarchy()->NumAxes())),
                           shape);
    CHECK(!ContainsEmptySlice(in_slices));
    const std::vector<TensorSliceView> out_slices =
        GetTensorSliceView(*out_parallel_desc->hierarchy(),
                           *CHECK_JUST(CachedGetAllSplitNdSbp(
                               out_split_axis, out_parallel_desc->hierarchy()->NumAxes())),
                           shape);
    CHECK(!ContainsEmptySlice(out_slices));

    for (int64_t i = 0; i < out_parallel_num; ++i) {
      const TensorSliceView& out_slice = out_slices.at(i);
      for (int64_t j = 0; j < in_parallel_num; ++j) {
        const TensorSliceView& in_slice = in_slices.at(j);
        const TensorSliceView& intersection = out_slice.Intersect(in_slice);
        if (intersection.IsEmpty()) { continue; }
        int64_t src = CHECK_JUST(in_parallel_desc->MachineId4ParallelId(j));
        int64_t dst = CHECK_JUST(out_parallel_desc->MachineId4ParallelId(i));
        sorted_p2p_pair_.emplace_back(std::make_pair(src, dst));
        sorted_elem_cnt2in_tensor_slice_copier_pair_.emplace_back(std::make_pair(
            intersection.shape().elem_cnt(),
            std::make_shared<TensorSliceCopier>(intersection, in_slice, data_type, device_type)));
        sorted_elem_cnt2out_tensor_slice_copier_pair_.emplace_back(std::make_pair(
            intersection.shape().elem_cnt(),
            std::make_shared<TensorSliceCopier>(out_slice, intersection, data_type, device_type)));
      }
    }
  }

  std::vector<std::pair<int64_t, std::shared_ptr<TensorSliceCopier>>>
      sorted_elem_cnt2in_tensor_slice_copier_pair_;
  std::vector<std::pair<int64_t, std::shared_ptr<TensorSliceCopier>>>
      sorted_elem_cnt2out_tensor_slice_copier_pair_;
  std::vector<std::pair<int64_t, int64_t>> sorted_p2p_pair_;
};

size_t InferNaiveSToSKernelTmpBufferSize(user_op::InferContext* ctx) {
  const user_op::TensorDesc& in_tensor = ctx->InputTensorDesc("in", 0);
  const Shape& shape = ctx->Attr<Shape>("shape");
  const std::string& out_parallel_conf_txt = ctx->Attr<std::string>("out_parallel_conf");
  Symbol<ParallelDesc> out_parallel_desc = CHECK_JUST(TxtStringToPlacement(out_parallel_conf_txt));
  const std::string& in_parallel_conf_txt = ctx->Attr<std::string>("in_parallel_conf");
  Symbol<ParallelDesc> in_parallel_desc = CHECK_JUST(TxtStringToPlacement(in_parallel_conf_txt));
  int64_t maximum_parallel_num =
      out_parallel_desc->parallel_num() > in_parallel_desc->parallel_num()
          ? out_parallel_desc->parallel_num()
          : in_parallel_desc->parallel_num();
  size_t tensor_byte_size =
      shape.elem_cnt() / maximum_parallel_num * GetSizeOfDataType(in_tensor.data_type());
  return tensor_byte_size;
}

}  // namespace

template<DeviceType device_type>
class EagerNaiveSToSKernel final : public user_op::OpKernel {
 public:
  EagerNaiveSToSKernel() = default;
  ~EagerNaiveSToSKernel() override = default;

  std::shared_ptr<user_op::OpKernelState> CreateOpKernelState(
      user_op::KernelInitContext* ctx) const override {
    return std::make_shared<EagerNaiveSToSOpKernelState>(ctx);
  }

 private:
  void Compute(user_op::KernelComputeContext* ctx, user_op::OpKernelState* state) const override {
    auto* kernel_state = dynamic_cast<EagerNaiveSToSOpKernelState*>(state);
    CHECK(kernel_state != nullptr);
    const user_op::Tensor* in = ctx->Tensor4ArgNameAndIndex("in", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);
    user_op::Tensor* tmp_buffer = ctx->Tensor4ArgNameAndIndex("tmp_buffer", 0);
    const void* in_ptr = in->dptr();
    void* out_ptr = out->mut_dptr();
    void* tmp_buffer_ptr = tmp_buffer->mut_dptr();

    const auto& sorted_elem_cnt2in_tensor_slice_copier_pair =
        kernel_state->sorted_elem_cnt2in_tensor_slice_copier_pair();
    const auto& sorted_elem_cnt2out_tensor_slice_copier_pair =
        kernel_state->sorted_elem_cnt2out_tensor_slice_copier_pair();
    const auto& sorted_p2p_pair = kernel_state->sorted_p2p_pair();
    CHECK_EQ(sorted_elem_cnt2in_tensor_slice_copier_pair.size(), sorted_p2p_pair.size());
    CHECK_EQ(sorted_elem_cnt2out_tensor_slice_copier_pair.size(), sorted_p2p_pair.size());

    for (int64_t i = 0; i < sorted_p2p_pair.size(); ++i) {
      const auto& p2p_pair = sorted_p2p_pair.at(i);
      int64_t src = p2p_pair.first;
      int64_t dst = p2p_pair.second;
      if (GlobalProcessCtx::Rank() == src) {
        const auto& elem_cnt2tensor_slice_copier_pair =
            sorted_elem_cnt2in_tensor_slice_copier_pair.at(i);
        const auto& elem_cnt = elem_cnt2tensor_slice_copier_pair.first;
        const auto& tensor_slice_copier = elem_cnt2tensor_slice_copier_pair.second;
        tensor_slice_copier->Copy(ctx->stream(), tmp_buffer_ptr, in_ptr);
        CHECK_JUST(Send<device_type>(reinterpret_cast<const void*>(tmp_buffer_ptr), elem_cnt,
                                     in->data_type(), dst, ctx->device_ctx()));
      }
      if (GlobalProcessCtx::Rank() == dst) {
        const auto& elem_cnt2tensor_slice_copier_pair =
            sorted_elem_cnt2out_tensor_slice_copier_pair.at(i);
        const auto& elem_cnt = elem_cnt2tensor_slice_copier_pair.first;
        const auto& tensor_slice_copier = elem_cnt2tensor_slice_copier_pair.second;
        CHECK_JUST(
            Recv<device_type>(tmp_buffer_ptr, elem_cnt, out->data_type(), src, ctx->device_ctx()));
        tensor_slice_copier->Copy(ctx->stream(), out_ptr,
                                  reinterpret_cast<const void*>(tmp_buffer_ptr));
      }
    }
  }
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

#define REGISTER_EAGER_NAIVE_S_TO_S_KERNEL(device)         \
  REGISTER_USER_KERNEL("eager_naive_s_to_s")               \
      .SetCreateFn<EagerNaiveSToSKernel<device>>()         \
      .SetIsMatchedHob(user_op::HobDeviceType() == device) \
      .SetInferTmpSizeFn(InferNaiveSToSKernelTmpBufferSize);

REGISTER_EAGER_NAIVE_S_TO_S_KERNEL(DeviceType::kCPU)

#if defined(WITH_CUDA) && HAS_GPU_SEND_RECV
REGISTER_EAGER_NAIVE_S_TO_S_KERNEL(DeviceType::kGPU)
#endif

}  // namespace oneflow
