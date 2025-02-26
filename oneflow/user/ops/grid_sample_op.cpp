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

namespace {

Maybe<void> CheckAttr(const user_op::UserOpDefWrapper& def,
                      const user_op::UserOpConfWrapper& conf) {
  bool pass_checked = true;
  std::stringstream err;
  err << "Illegal value for " << conf.op_type_name() << " op " << conf.op_name() << ": ";

  const auto& interpolation_mode = conf.attr<std::string>("interpolation_mode");
  if (!(interpolation_mode == "bilinear" || interpolation_mode == "nearest"
        || interpolation_mode == "bicubic")) {
    err << " interpolation_mode:" << interpolation_mode;
    pass_checked = false;
  }

  const auto& padding_mode = conf.attr<std::string>("padding_mode");
  if (!(padding_mode == "zeros" || padding_mode == "border" || padding_mode == "reflection")) {
    err << " padding_mode:" << padding_mode;
    pass_checked = false;
  }

  if (pass_checked) {
    return Maybe<void>::Ok();
  } else {
    return oneflow::Error::CheckFailedError() << err.str();
  }
}

}  // namespace

REGISTER_USER_OP("grid_sample")
    .Input("input")
    .Input("grid")
    .Output("output")
    .Attr<std::string>("interpolation_mode")
    .Attr<std::string>("padding_mode")
    .Attr<bool>("align_corners")
    .SetCheckAttrFn(CheckAttr)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const user_op::TensorDesc& input = ctx->InputTensorDesc("input", 0);
      const user_op::TensorDesc& grid = ctx->InputTensorDesc("grid", 0);
      user_op::TensorDesc& output = *(ctx->OutputTensorDesc("output", 0));
      // Only support 4D or 5D input with NCHW layout
      // For 4D grid: input  = { N, C, H_in, W_in },
      //              grid   = { N, H_out, W_out, 2 }
      //              output = { N, C, H_out, W_out }
      // For 5D grid: input  = { N, C, D_in, H_in, W_in },
      //              grid   = { N, D_out, H_out, W_out, 3 }
      //              output = { N, C, D_out, H_out, W_out }
      const Shape& input_shape = input.shape();
      const Shape& grid_shape = grid.shape();

      bool is_4d_input = true;
      if (input_shape.NumAxes() == 4) {
        CHECK_EQ_OR_RETURN(grid_shape.NumAxes(), 4) << "Grid and input MUST have same dimention";
        CHECK_EQ_OR_RETURN(grid_shape.At(3), 2) << "Grid shape MUST (N, H_out, W_out, 2)";
        is_4d_input = true;
      } else if (input_shape.NumAxes() == 5) {
        CHECK_EQ_OR_RETURN(grid_shape.NumAxes(), 5) << "Grid and input MUST have same dimention";
        CHECK_EQ_OR_RETURN(grid_shape.At(4), 3) << "Grid shape MUST (N, H_out, W_out, 3)";
        if (ctx->Attr<std::string>("interpolation_mode") == "bicubic") {
          oneflow::Error::CheckFailedError() << "Mode='bicubic' supports only 4-D input";
        }
        is_4d_input = false;
      } else {
        CHECK_OR_RETURN(false) << "MUST be 4D or 5D input";
      }
      *output.mut_is_dynamic() = grid.is_dynamic();
      if (is_4d_input) {
        *(output.mut_shape()) = {input_shape.At(0), input_shape.At(1), grid_shape.At(1),
                                 grid_shape.At(2)};
      } else {
        *(output.mut_shape()) = {input_shape.At(0), input_shape.At(1), grid_shape.At(1),
                                 grid_shape.At(2), grid_shape.At(3)};
      }
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder()
          .Split(user_op::OpArg("input", 0), 0)
          .Split(user_op::OpArg("grid", 0), 0)
          .Split(user_op::OpArg("output", 0), 0)
          .Build();
      ctx->NewBuilder()
          .Split(user_op::OpArg("input", 0), 1)
          .Broadcast(user_op::OpArg("grid", 0))
          .Split(user_op::OpArg("output", 0), 1)
          .Build();
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      *ctx->OutputDType("output", 0) = ctx->InputDType("input", 0);
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("grid_sample_grad")
    .Input("doutput")
    .Input("input")
    .Input("grid")
    .Output("dinput")
    .Output("dgrid")
    .Attr<std::string>("interpolation_mode")
    .Attr<std::string>("padding_mode")
    .Attr<bool>("align_corners")
    .SetCheckAttrFn(CheckAttr)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      *(ctx->OutputTensorDesc("dinput", 0)->mut_shape()) = ctx->InputTensorDesc("input", 0).shape();
      *(ctx->OutputTensorDesc("dgrid", 0)->mut_shape()) = ctx->InputTensorDesc("grid", 0).shape();
      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder()
          .Split(user_op::OpArg("doutput", 0), 0)
          .Split(user_op::OpArg("input", 0), 0)
          .Split(user_op::OpArg("grid", 0), 0)
          .Split(user_op::OpArg("dinput", 0), 0)
          .Split(user_op::OpArg("dgrid", 0), 0)
          .Build();
      ctx->NewBuilder()
          .Split(user_op::OpArg("doutput", 0), 1)
          .Split(user_op::OpArg("input", 0), 1)
          .Broadcast(user_op::OpArg("grid", 0))
          .Split(user_op::OpArg("dinput", 0), 1)
          .Broadcast(user_op::OpArg("dgrid", 0))
          .Build();
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      *ctx->OutputDType("dinput", 0) = ctx->InputDType("input", 0);
      *ctx->OutputDType("dgrid", 0) = ctx->InputDType("grid", 0);
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP_GRAD("grid_sample")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                               const user_op::AddOpFn& AddOp) -> Maybe<void> {
      if (op.NeedGenGradTensor4OpInput("input", 0) || op.NeedGenGradTensor4OpInput("grid", 0)) {
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper grad_op =
            builder.Op("grid_sample_grad")
                .Input("doutput", op.GetGradTensorWithOpOutput("output", 0))
                .Input("input", op.input("input", 0))
                .Input("grid", op.input("grid", 0))
                .Output("dinput")
                .Output("dgrid")
                .Attr("interpolation_mode", op.attr<std::string>("interpolation_mode"))
                .Attr("padding_mode", op.attr<std::string>("padding_mode"))
                .Attr("align_corners", op.attr<bool>("align_corners"))
                .Build();

        if (op.NeedGenGradTensor4OpInput("input", 0)) {
          op.BindGradTensorWithOpInput(grad_op.output("dinput", 0), "input", 0);
        }
        if (op.NeedGenGradTensor4OpInput("grid", 0)) {
          op.BindGradTensorWithOpInput(grad_op.output("dgrid", 0), "grid", 0);
        }
        AddOp(grad_op);
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
