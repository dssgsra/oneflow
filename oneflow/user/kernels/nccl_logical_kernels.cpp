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
#include "oneflow/core/device/nccl_util.h"
#include "oneflow/core/job/eager_nccl_comm_manager.h"
#include "oneflow/core/job/parallel_desc.h"
#include "oneflow/core/ep/include/primitive/permute.h"

#if defined(WITH_CUDA) && NCCL_VERSION_CODE > 2700

namespace oneflow {

namespace {

class NcclLogicalKernelCommState final : public user_op::OpKernelState {
 public:
  explicit NcclLogicalKernelCommState(user_op::KernelInitContext* ctx)
      : is_init_(false),
        has_independent_stream_(ctx->op_conf().has_stream_name_hint()),
        stream_name_(""),
        parallel_desc_(ctx->parallel_desc()) {
    if (has_independent_stream_) { stream_name_ = ctx->op_conf().stream_name_hint(); }
  }
  ~NcclLogicalKernelCommState() = default;

  ncclComm_t comm() {
    if (!is_init_) {
      std::set<std::pair<int64_t, int64_t>> device_set;
      FOR_RANGE(int64_t, parallel_id, 0, parallel_desc_.parallel_num()) {
        int64_t machine_id = CHECK_JUST(parallel_desc_.MachineId4ParallelId(parallel_id));
        int64_t device_id = CHECK_JUST(parallel_desc_.DeviceId4ParallelId(parallel_id));
        device_set.emplace(std::make_pair(machine_id, device_id));
      }
      EagerNcclCommMgr* comm_mgr = CHECK_NOTNULL(Global<EagerNcclCommMgr>::Get());
      if (has_independent_stream_) {
        comm_ = comm_mgr->GetCommForDeviceAndStreamName(device_set, stream_name_);
      } else {
        comm_ = comm_mgr->GetCommForDevice(device_set);
      }
      is_init_ = true;
    }
    return comm_;
  }

 private:
  bool is_init_;
  bool has_independent_stream_;
  std::string stream_name_;
  ParallelDesc parallel_desc_;
  ncclComm_t comm_{};
};

class NcclLogicalAllReduceKernel final : public user_op::OpKernel {
 public:
  NcclLogicalAllReduceKernel() = default;
  ~NcclLogicalAllReduceKernel() override = default;

  std::shared_ptr<user_op::OpKernelState> CreateOpKernelState(
      user_op::KernelInitContext* ctx) const override {
    return std::make_shared<NcclLogicalKernelCommState>(ctx);
  }

 private:
  void Compute(user_op::KernelComputeContext* ctx, user_op::OpKernelState* state) const override {
    auto* nccl_comm = dynamic_cast<NcclLogicalKernelCommState*>(state);
    CHECK(nccl_comm != nullptr);
    const user_op::Tensor* in = ctx->Tensor4ArgNameAndIndex("in", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);
    CHECK_EQ(in->shape(), out->shape());
    CHECK_EQ(in->data_type(), out->data_type());
    OF_NCCL_CHECK(ncclAllReduce(in->dptr(), out->mut_dptr(), in->shape().elem_cnt(),
                                GetNcclDataType(in->data_type()), ncclRedOp_t::ncclSum,
                                nccl_comm->comm(), ctx->device_ctx()->cuda_stream()));
  };
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

class NcclLogicalReduceScatterKernel final : public user_op::OpKernel {
 public:
  NcclLogicalReduceScatterKernel() = default;
  ~NcclLogicalReduceScatterKernel() override = default;

  std::shared_ptr<user_op::OpKernelState> CreateOpKernelState(
      user_op::KernelInitContext* ctx) const override {
    return std::make_shared<NcclLogicalKernelCommState>(ctx);
  }

 private:
  void Compute(user_op::KernelComputeContext* ctx, user_op::OpKernelState* state) const override {
    auto* nccl_comm = dynamic_cast<NcclLogicalKernelCommState*>(state);
    CHECK(nccl_comm != nullptr);
    const user_op::Tensor* in = ctx->Tensor4ArgNameAndIndex("in", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);
    CHECK_EQ(in->data_type(), out->data_type());
    const int64_t num_ranks = ctx->parallel_ctx().parallel_num();
    CHECK_EQ(in->shape().elem_cnt(), out->shape().elem_cnt() * num_ranks);
    OF_NCCL_CHECK(ncclReduceScatter(in->dptr(), out->mut_dptr(), out->shape().elem_cnt(),
                                    GetNcclDataType(in->data_type()), ncclRedOp_t::ncclSum,
                                    nccl_comm->comm(), ctx->device_ctx()->cuda_stream()));
  };
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

class NcclLogicalAllGatherKernel final : public user_op::OpKernel {
 public:
  NcclLogicalAllGatherKernel() = default;
  ~NcclLogicalAllGatherKernel() override = default;

  std::shared_ptr<user_op::OpKernelState> CreateOpKernelState(
      user_op::KernelInitContext* ctx) const override {
    return std::make_shared<NcclLogicalKernelCommState>(ctx);
  }

 private:
  void Compute(user_op::KernelComputeContext* ctx, user_op::OpKernelState* state) const override {
    auto* nccl_comm = dynamic_cast<NcclLogicalKernelCommState*>(state);
    CHECK(nccl_comm != nullptr);
    const user_op::Tensor* in = ctx->Tensor4ArgNameAndIndex("in", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);
    CHECK_EQ(in->data_type(), out->data_type());
    const int64_t num_ranks = ctx->parallel_ctx().parallel_num();
    CHECK_EQ(in->shape().elem_cnt() * num_ranks, out->shape().elem_cnt());
    OF_NCCL_CHECK(ncclAllGather(in->dptr(), out->mut_dptr(), in->shape().elem_cnt(),
                                GetNcclDataType(in->data_type()), nccl_comm->comm(),
                                ctx->device_ctx()->cuda_stream()));
  };
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

template<typename T>
class NcclLogicalAllGatherNoncontinuous final : public user_op::OpKernel {
 public:
  NcclLogicalAllGatherNoncontinuous() = default;
  ~NcclLogicalAllGatherNoncontinuous() override = default;

  std::shared_ptr<user_op::OpKernelState> CreateOpKernelState(
      user_op::KernelInitContext* ctx) const override {
    return std::make_shared<NcclLogicalKernelCommState>(ctx);
  }

 private:
  void Compute(user_op::KernelComputeContext* ctx, user_op::OpKernelState* state) const override {
    auto* nccl_comm = dynamic_cast<NcclLogicalKernelCommState*>(state);
    CHECK(nccl_comm != nullptr);
    const user_op::Tensor* in = ctx->Tensor4ArgNameAndIndex("in", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);
    user_op::Tensor* tmp_buffer = ctx->Tensor4ArgNameAndIndex("tmp_buffer", 0);
    const int64_t dtype_size = GetSizeOfDataType(in->data_type());
    int64_t data_size = GetCudaAlignedSize(out->shape().elem_cnt() * dtype_size);
    void* unpack_from_ptr = tmp_buffer->mut_dptr();
    CHECK_EQ(tmp_buffer->shape().elem_cnt(), data_size);

    CHECK_EQ(in->data_type(), out->data_type());
    const int64_t num_ranks = ctx->parallel_ctx().parallel_num();
    const int64_t in_split_axis = ctx->Attr<int64_t>("in_split_axis");

    DimVector logical_shape_dim_vec;
    in->shape().ToDimVector(&logical_shape_dim_vec);
    logical_shape_dim_vec[in_split_axis] = logical_shape_dim_vec.at(in_split_axis) * num_ranks;

    // NOTE(chengcheng): Do AllGather
    CHECK_EQ(in->shape().elem_cnt() * num_ranks, out->shape().elem_cnt());
    OF_NCCL_CHECK(ncclAllGather(in->dptr(), unpack_from_ptr, in->shape().elem_cnt(),
                                GetNcclDataType(in->data_type()), nccl_comm->comm(),
                                ctx->device_ctx()->cuda_stream()));

    CHECK_GT(in_split_axis, 0);
    // NOTE(chengcheng): Do unpack.
    DimVector unpack_from_dim_vec = logical_shape_dim_vec;
    CHECK_EQ(unpack_from_dim_vec.at(in_split_axis) % num_ranks, 0);
    unpack_from_dim_vec[in_split_axis] = unpack_from_dim_vec.at(in_split_axis) / num_ranks;
    unpack_from_dim_vec.insert(unpack_from_dim_vec.begin(), num_ranks);
    std::vector<int32_t> perm;
    FOR_RANGE(int64_t, i, 1, unpack_from_dim_vec.size()) { perm.push_back(i); }
    perm.insert(perm.begin() + in_split_axis, 0);
    auto transpose = ep::primitive::NewPrimitive<ep::primitive::PermuteFactory>(
        ctx->stream()->device_type(), unpack_from_dim_vec.size());
    CHECK(transpose);
    transpose->Launch(ctx->stream(), in->data_type(), unpack_from_dim_vec.size(),
                      unpack_from_dim_vec.data(), unpack_from_ptr, perm.data(), out->mut_dptr());
  };
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

size_t InferAllGatherNoncontinuousKernelTmpBufferSize(user_op::InferContext* ctx) {
  const user_op::TensorDesc* out_tensor = ctx->OutputTensorDesc("out", 0);
  return GetCudaAlignedSize(out_tensor->shape().elem_cnt()
                            * GetSizeOfDataType(out_tensor->data_type()));
}

template<typename T>
class NcclLogicalS2SKernel final : public user_op::OpKernel {
 public:
  NcclLogicalS2SKernel() = default;
  ~NcclLogicalS2SKernel() override = default;

  std::shared_ptr<user_op::OpKernelState> CreateOpKernelState(
      user_op::KernelInitContext* ctx) const override {
    return std::make_shared<NcclLogicalKernelCommState>(ctx);
  }

 private:
  void Compute(user_op::KernelComputeContext* ctx, user_op::OpKernelState* state) const override {
    auto* nccl_comm = dynamic_cast<NcclLogicalKernelCommState*>(state);
    CHECK(nccl_comm != nullptr);
    const user_op::Tensor* in = ctx->Tensor4ArgNameAndIndex("in", 0);
    user_op::Tensor* out = ctx->Tensor4ArgNameAndIndex("out", 0);
    user_op::Tensor* tmp_buffer = ctx->Tensor4ArgNameAndIndex("tmp_buffer", 0);
    int64_t tmp_size = 0;
    const int64_t dtype_size = GetSizeOfDataType(in->data_type());
    int64_t data_size = GetCudaAlignedSize(in->shape().elem_cnt() * dtype_size);
    // NOTE(chengcheng): in (transpose)-> pack_to_ptr (all2all)-> unpack_from_ptr (transpose)-> out
    const char* pack_to_ptr = in->dptr<char>();
    char* unpack_from_ptr = out->mut_dptr<char>();
    if (tmp_buffer) { tmp_size = tmp_buffer->shape().elem_cnt(); }
    CHECK(tmp_size == 0 || tmp_size == data_size || tmp_size == data_size * 2);

    CHECK_EQ(in->data_type(), out->data_type());
    const int64_t num_ranks = ctx->parallel_ctx().parallel_num();
    CHECK_EQ(in->shape().elem_cnt(), out->shape().elem_cnt());
    const int64_t elem_cnt = in->shape().elem_cnt();
    const int64_t in_split_axis = ctx->Attr<int64_t>("in_split_axis");
    const int64_t out_split_axis = ctx->Attr<int64_t>("out_split_axis");

    DimVector logical_shape_dim_vec;
    in->shape().ToDimVector(&logical_shape_dim_vec);
    logical_shape_dim_vec[in_split_axis] = logical_shape_dim_vec.at(in_split_axis) * num_ranks;

    if (out_split_axis != 0) {
      // NOTE(chengcheng): Do pack. Need transpose in -> pack_to
      // pack use temp buffer offset: [0, data_size]
      pack_to_ptr = tmp_buffer->dptr<char>();
      DimVector transpose_in_dim_vec = logical_shape_dim_vec;
      CHECK_EQ(transpose_in_dim_vec.at(in_split_axis) % num_ranks, 0);
      transpose_in_dim_vec[in_split_axis] = transpose_in_dim_vec.at(in_split_axis) / num_ranks;
      CHECK_EQ(transpose_in_dim_vec.at(out_split_axis) % num_ranks, 0);
      transpose_in_dim_vec[out_split_axis] = transpose_in_dim_vec.at(out_split_axis) / num_ranks;
      transpose_in_dim_vec.insert(transpose_in_dim_vec.begin() + out_split_axis, num_ranks);
      std::vector<int32_t> perm;
      perm.push_back(out_split_axis);
      FOR_RANGE(int64_t, i, 0, transpose_in_dim_vec.size()) {
        if (i != out_split_axis) { perm.push_back(i); }
      }
      auto transpose = ep::primitive::NewPrimitive<ep::primitive::PermuteFactory>(
          ctx->stream()->device_type(), transpose_in_dim_vec.size());
      CHECK(transpose);
      transpose->Launch(ctx->stream(), in->data_type(), transpose_in_dim_vec.size(),
                        transpose_in_dim_vec.data(), in->dptr(), perm.data(),
                        tmp_buffer->mut_dptr());
    }

    if (in_split_axis != 0) {
      // NOTE(chengcheng): Do unpack. Need transpose unpack_from -> out
      // unpack use temp buffer offset: [tmp_size - data_size, tmp_size]
      unpack_from_ptr = tmp_buffer->mut_dptr<char>() + (tmp_size - data_size);
    }

    {
      // NOTE(chengcheng): init nccl comm need before ncclGroupStart.
      ncclComm_t comm = nccl_comm->comm();
      // NOTE(chengcheng): Do S2S
      OF_NCCL_CHECK(ncclGroupStart());
      const int64_t elem_per_chunk = elem_cnt / num_ranks;
      const int64_t chunk_size = elem_per_chunk * dtype_size;
      for (int64_t j = 0; j < num_ranks; ++j) {
        OF_NCCL_CHECK(ncclSend(reinterpret_cast<const void*>(
                                   reinterpret_cast<const char*>(pack_to_ptr) + j * chunk_size),
                               elem_per_chunk, GetNcclDataType(in->data_type()), j, comm,
                               ctx->device_ctx()->cuda_stream()));
        OF_NCCL_CHECK(ncclRecv(
            reinterpret_cast<void*>(reinterpret_cast<char*>(unpack_from_ptr) + j * chunk_size),
            elem_per_chunk, GetNcclDataType(in->data_type()), j, nccl_comm->comm(),
            ctx->device_ctx()->cuda_stream()));
      }
      OF_NCCL_CHECK(ncclGroupEnd());
    }

    if (in_split_axis != 0) {
      // Do unpack.
      CHECK(unpack_from_ptr != out->mut_dptr<char>());
      DimVector unpack_from_dim_vec = logical_shape_dim_vec;
      CHECK_EQ(unpack_from_dim_vec.at(in_split_axis) % num_ranks, 0);
      unpack_from_dim_vec[in_split_axis] = unpack_from_dim_vec.at(in_split_axis) / num_ranks;
      CHECK_EQ(unpack_from_dim_vec.at(out_split_axis) % num_ranks, 0);
      unpack_from_dim_vec[out_split_axis] = unpack_from_dim_vec.at(out_split_axis) / num_ranks;
      unpack_from_dim_vec.insert(unpack_from_dim_vec.begin(), num_ranks);
      std::vector<int32_t> perm;
      FOR_RANGE(int64_t, i, 1, unpack_from_dim_vec.size()) { perm.push_back(i); }
      perm.insert(perm.begin() + in_split_axis, 0);
      auto transpose = ep::primitive::NewPrimitive<ep::primitive::PermuteFactory>(
          ctx->stream()->device_type(), unpack_from_dim_vec.size());
      CHECK(transpose);
      transpose->Launch(ctx->stream(), in->data_type(), unpack_from_dim_vec.size(),
                        unpack_from_dim_vec.data(), unpack_from_ptr, perm.data(), out->mut_dptr());
    }
  };
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

size_t InferS2SKernelTmpBufferSize(user_op::InferContext* ctx) {
  size_t ret = 0;
  const user_op::TensorDesc& in_tensor = ctx->InputTensorDesc("in", 0);
  size_t tensor_byte_size =
      GetCudaAlignedSize(in_tensor.shape().elem_cnt() * GetSizeOfDataType(in_tensor.data_type()));
  const cfg::SbpParallel& in_sbp = ctx->SbpParallel4ArgNameAndIndex("in", 0);
  const cfg::SbpParallel& out_sbp = ctx->SbpParallel4ArgNameAndIndex("out", 0);
  CHECK(in_sbp.has_split_parallel() && out_sbp.has_split_parallel());
  if (in_sbp.split_parallel().axis() != 0) { ret += tensor_byte_size; }
  if (out_sbp.split_parallel().axis() != 0) { ret += tensor_byte_size; }
  return ret;
}

}  // namespace

REGISTER_USER_KERNEL("_nccl_logical_all_reduce")
    .SetCreateFn<NcclLogicalAllReduceKernel>()
    .SetIsMatchedHob(user_op::HobDeviceType() == DeviceType::kGPU);

REGISTER_USER_KERNEL("_nccl_logical_reduce_scatter")
    .SetCreateFn<NcclLogicalReduceScatterKernel>()
    .SetIsMatchedHob(user_op::HobDeviceType() == DeviceType::kGPU);

REGISTER_USER_KERNEL("_nccl_logical_all_gather")
    .SetCreateFn<NcclLogicalAllGatherKernel>()
    .SetIsMatchedHob(user_op::HobDeviceType() == DeviceType::kGPU);

#define REGISTER_ALLGATHER_NONCONTINUOUS_KERNEL(dtype)                                   \
  REGISTER_USER_KERNEL("_nccl_logical_all_gather_noncontinuous")                         \
      .SetCreateFn<NcclLogicalAllGatherNoncontinuous<dtype>>()                           \
      .SetIsMatchedHob((user_op::HobDeviceType() == DeviceType::kGPU)                    \
                       && (user_op::HobDataType("in", 0) == GetDataType<dtype>::value)   \
                       && (user_op::HobDataType("out", 0) == GetDataType<dtype>::value)) \
      .SetInferTmpSizeFn(InferAllGatherNoncontinuousKernelTmpBufferSize);

REGISTER_ALLGATHER_NONCONTINUOUS_KERNEL(int8_t)
REGISTER_ALLGATHER_NONCONTINUOUS_KERNEL(int32_t)
REGISTER_ALLGATHER_NONCONTINUOUS_KERNEL(int64_t)
REGISTER_ALLGATHER_NONCONTINUOUS_KERNEL(float)
REGISTER_ALLGATHER_NONCONTINUOUS_KERNEL(double)
REGISTER_ALLGATHER_NONCONTINUOUS_KERNEL(float16)

#define REGISTER_S2S_KERNEL(dtype)                                                       \
  REGISTER_USER_KERNEL("_nccl_logical_s2s")                                              \
      .SetCreateFn<NcclLogicalS2SKernel<dtype>>()                                        \
      .SetIsMatchedHob((user_op::HobDeviceType() == DeviceType::kGPU)                    \
                       && (user_op::HobDataType("in", 0) == GetDataType<dtype>::value)   \
                       && (user_op::HobDataType("out", 0) == GetDataType<dtype>::value)) \
      .SetInferTmpSizeFn(InferS2SKernelTmpBufferSize);

REGISTER_S2S_KERNEL(int8_t)
REGISTER_S2S_KERNEL(int32_t)
REGISTER_S2S_KERNEL(int64_t)
REGISTER_S2S_KERNEL(float)
REGISTER_S2S_KERNEL(double)
REGISTER_S2S_KERNEL(float16)

}  // namespace oneflow

#endif  // WITH_CUDA && NCCL_VERSION_CODE > 2700
