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
#include "oneflow/user/ops/loss_op_util.h"

namespace oneflow {

namespace {

Maybe<void> InferTensorDescFn(user_op::InferContext* ctx) {
  const auto& input_desc = ctx->InputTensorDesc("input", 0);
  const auto& target_desc = ctx->InputTensorDesc("target", 0);
  CHECK_EQ_OR_RETURN(input_desc.is_dynamic(), target_desc.is_dynamic());
  CHECK_GE_OR_RETURN(input_desc.shape().NumAxes(), 2);
  CHECK_EQ_OR_RETURN(target_desc.shape().NumAxes(), 1);
  CHECK_EQ_OR_RETURN(input_desc.shape().At(0), target_desc.shape().At(0));
  if (ctx->has_input("weight", 0)) {
    const auto& weight_desc = ctx->InputTensorDesc("weight", 0);
    CHECK_EQ_OR_RETURN(weight_desc.is_dynamic(), input_desc.is_dynamic());
    CHECK_EQ_OR_RETURN(weight_desc.shape(), Shape({input_desc.shape().At(1)}));
  }

  JUST(CheckLossReductionAndInferOutputTenserDesc(ctx, "out", input_desc.is_dynamic(),
                                                  target_desc.shape()));

  user_op::TensorDesc* total_weight_desc = ctx->OutputTensorDesc("total_weight", 0);
  *total_weight_desc->mut_is_dynamic() = input_desc.is_dynamic();
  *total_weight_desc->mut_shape() = Shape({1});

  return Maybe<void>::Ok();
}

Maybe<void> InferDataType(user_op::InferContext* ctx) {
  const user_op::TensorDesc& target_desc = ctx->InputTensorDesc("target", 0);
  CHECK_OR_RETURN(IsIndexDataType(target_desc.data_type()));

  *ctx->OutputDType("out", 0) = ctx->InputDType("input", 0);
  *ctx->OutputDType("total_weight", 0) = ctx->InputDType("input", 0);

  return Maybe<void>::Ok();
}

Maybe<void> InferGradTensorDescFn(user_op::InferContext* ctx) {
  const auto& input_desc = ctx->InputTensorDesc("input", 0);
  const auto& target_desc = ctx->InputTensorDesc("target", 0);
  const auto& total_weight_desc = ctx->InputTensorDesc("total_weight", 0);
  CHECK_EQ_OR_RETURN(input_desc.is_dynamic(), target_desc.is_dynamic());
  CHECK_GE_OR_RETURN(input_desc.shape().NumAxes(), 2);
  CHECK_EQ_OR_RETURN(target_desc.shape().NumAxes(), 1);
  CHECK_EQ_OR_RETURN(input_desc.shape().At(0), target_desc.shape().At(0));
  CHECK_EQ_OR_RETURN(total_weight_desc.shape(), Shape({1}));
  if (ctx->has_input("weight", 0)) {
    const auto& weight_desc = ctx->InputTensorDesc("weight", 0);
    CHECK_EQ_OR_RETURN(weight_desc.is_dynamic(), input_desc.is_dynamic());
    CHECK_EQ_OR_RETURN(weight_desc.shape(), Shape({input_desc.shape().At(1)}));
  }
  JUST(CheckLossReductionAndCheckInputTenserDesc(ctx, "dy", target_desc.shape()));

  user_op::TensorDesc* dx_desc = ctx->OutputTensorDesc("dx", 0);
  *dx_desc->mut_is_dynamic() = input_desc.is_dynamic();
  *dx_desc->mut_shape() = input_desc.shape();

  return Maybe<void>::Ok();
}

Maybe<void> InferGradDataType(user_op::InferContext* ctx) {
  const user_op::TensorDesc& target_desc = ctx->InputTensorDesc("target", 0);
  CHECK_OR_RETURN(IsIndexDataType(target_desc.data_type()));

  *ctx->OutputDType("dx", 0) = ctx->InputDType("dy", 0);

  return Maybe<void>::Ok();
}
}  // namespace

REGISTER_USER_OP("nll")
    .Input("input")
    .Input("target")
    .OptionalInput("weight")
    .Output("out")
    .Output("total_weight")
    .Attr<int64_t>("ignore_index")
    .Attr<std::string>("reduction")
    .SetTensorDescInferFn(InferTensorDescFn)
    .SetInputArgModifyFn([](const user_op::GetInputArgModifier& GetInputArgModifierFn,
                            const user_op::UserOpConfWrapper&) -> Maybe<void> {
      user_op::InputArgModifier* target_modifier = GetInputArgModifierFn("target", 0);
      CHECK_OR_RETURN(target_modifier != nullptr);
      target_modifier->set_requires_grad(false);
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn(InferDataType)
    .SetGetSbpFn(GenLossForwardDefaultGetSbpFn([](user_op::UserOpSbpSignatureBuilder& builder) {
      builder.Broadcast(user_op::OpArg("total_weight", 0));
    }));

REGISTER_USER_OP("nll_grad")
    .Input("input")
    .Input("target")
    .Input("total_weight")
    .OptionalInput("weight")
    .Input("dy")
    .Output("dx")
    .Attr<int64_t>("ignore_index")
    .Attr<std::string>("reduction")
    .SetTensorDescInferFn(InferGradTensorDescFn)
    .SetDataTypeInferFn(InferGradDataType)
    .SetGetSbpFn(GenLossBackwardDefaultGetSbpFn([](user_op::UserOpSbpSignatureBuilder& builder) {
      builder.Broadcast(user_op::OpArg("total_weight", 0));
    }));

REGISTER_USER_OP_GRAD("nll").SetGenBackwardOpConfFn(
    [](const user_op::UserOpWrapper& op, const user_op::AddOpFn& AddOp) -> Maybe<void> {
      if (op.NeedGenGradTensor4OpInput("input", 0)) {
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        builder.Op("nll_grad")
            .Input("input", op.input("input", 0))
            .Input("target", op.input("target", 0))
            .Input("total_weight", op.output("total_weight", 0))
            .Input("dy", op.GetGradTensorWithOpOutput("out", 0))
            .Output("dx")
            .Attr("ignore_index", op.attr<int64_t>("ignore_index"))
            .Attr("reduction", op.attr<std::string>("reduction"));
        if (op.user_op_conf().has_input("weight", 0)) {
          builder.Input("weight", op.input("weight", 0));
        }
        user_op::UserOpConfWrapper grad_op = builder.Build();
        op.BindGradTensorWithOpInput(grad_op.output("dx", 0), "input", 0);
        AddOp(grad_op);
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
