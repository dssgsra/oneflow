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

namespace oneflow {
REGISTER_NO_GRAD_USER_OP("arange")
    .Output("out")
    .Attr<int64_t>("integer_start")
    .Attr<int64_t>("integer_delta")
    .Attr<int64_t>("integer_limit")
    .Attr<double>("float_start")
    .Attr<double>("float_delta")
    .Attr<double>("float_limit")
    .Attr<DataType>("dtype")
    .Attr<std::vector<std::string>>("nd_sbp")
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      Shape* out_shape = ctx->OutputShape("out", 0);
      DataType dtype = ctx->Attr<DataType>("dtype");
      int64_t range_elem_cnt = 0;
      if (IsIntegralDataType(dtype)) {
        int64_t integer_delta = ctx->Attr<int64_t>("integer_delta");
        CHECK_NE_OR_RETURN(integer_delta, static_cast<int64_t>(0))
            << "RuntimeError: step must be nonzero. ";
        int64_t integer_start = ctx->Attr<int64_t>("integer_start");
        int64_t integer_limit = ctx->Attr<int64_t>("integer_limit");
        // CHECK when limit > start, delta > 0; limit < start, delta < 0;
        CHECK_GT_OR_RETURN((integer_limit - integer_start) / integer_delta, static_cast<int64_t>(0))
            << "RuntimeError: upper bound and larger bound inconsistent with step sign";
        range_elem_cnt =
            std::ceil(static_cast<double>(integer_limit - integer_start) / integer_delta);
      } else {
        double float_delta = ctx->Attr<double>("float_delta");
        CHECK_NE_OR_RETURN(float_delta, static_cast<double>(0.0))
            << "RuntimeError: step must be nonzero. ";
        double float_start = ctx->Attr<double>("float_start");
        double float_limit = ctx->Attr<double>("float_limit");
        // CHECK when limit > start, delta > 0; limit < start, delta < 0;
        CHECK_GT_OR_RETURN((float_limit - float_start) / float_delta, static_cast<double>(0.0))
            << "RuntimeError: upper bound and larger bound inconsistent with step sign";
        range_elem_cnt = std::ceil(static_cast<double>(float_limit - float_start) / float_delta);
      }
      *out_shape = Shape({range_elem_cnt});
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder().Broadcast(ctx->inputs()).Broadcast(ctx->outputs()).Build();
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      *ctx->OutputDType("out", 0) = ctx->Attr<DataType>("dtype");
      return Maybe<void>::Ok();
    })
    .SetNdSbpInferFn([](user_op::InferNdSbpFnContext* ctx) -> Maybe<void> {
      cfg::SbpParallel default_sbp;
      default_sbp.mutable_broadcast_parallel();
      return user_op::InferNdSbp4SrcOp(ctx, default_sbp);
    });

}  // namespace oneflow
