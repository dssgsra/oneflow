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
#include "oneflow/core/kernel/kernel.h"
#include "oneflow/core/common/buffer_manager.h"
#include "oneflow/core/common/multi_client.h"
#include "oneflow/core/job/job_instance.h"
#include "oneflow/core/job/global_for.h"

namespace oneflow {

class ReturnKernel final : public Kernel {
 public:
  OF_DISALLOW_COPY_AND_MOVE(ReturnKernel);
  ReturnKernel() = default;
  ~ReturnKernel() = default;

 private:
  void ForwardDataContent(KernelContext* ctx) const override;
  void ForwardHeader(KernelContext* ctx) const override;
};

void ReturnKernel::ForwardDataContent(KernelContext* ctx) const {
  if (CHECK_JUST(IsMultiClient())) {
    CHECK(this->op_conf().return_conf().has_job_name());
    const auto& job_name = this->op_conf().return_conf().job_name();
    const auto& op_name = this->op_conf().name();
    auto* buffer_mgr = Global<BufferMgr<std::shared_ptr<JobInstance>>>::Get();
    auto* buffer = buffer_mgr->Get(GetOutputBufferName(job_name, op_name));
    std::shared_ptr<JobInstance> job_instance;
    BufferStatus buffer_status = buffer->TryReceive(&job_instance);
    CHECK_NE(buffer_status, kBufferStatusEmpty);
    if (buffer_status == kBufferStatusSuccess) {
      OfBlob ofblob(ctx->device_ctx(), ctx->BnInOp2Blob("in"));
      job_instance->PullBlobByOpName(reinterpret_cast<uint64_t>(&ofblob), op_name);
    }
  } else {
    AutoMemcpy(ctx->stream_ctx(), ctx->BnInOp2Blob("out"), ctx->BnInOp2Blob("in"));
    CHECK_JUST(ctx->stream()->Sync());
  }
}

void ReturnKernel::ForwardHeader(KernelContext* ctx) const {
  if (CHECK_JUST(IsMultiClient())) {
    // Do nothing.
  } else {
    ctx->BnInOp2Blob("out")->CopyHeaderFrom(ctx->BnInOp2Blob("in"));
  }
}

REGISTER_KERNEL(OperatorConf::kReturnConf, ReturnKernel);

}  // namespace oneflow
