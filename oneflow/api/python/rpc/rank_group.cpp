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
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "oneflow/api/python/of_api_registry.h"
#include "oneflow/core/framework/rank_group_rpc_util.h"
#include "oneflow/core/job/rank_group.h"
#include "oneflow/core/job/rank_group_scope.h"
#include "oneflow/core/common/symbol.h"

namespace py = pybind11;

namespace oneflow {

namespace {

Maybe<void> CheckCurrentRankGroupConsistency(int64_t seconds) {
  const auto& rank_group = JUST(RankGroupScope::CurrentRankGroup());
  const auto& ctx = JUST(CheckTransportToken(rank_group));
  JUST(TransportUtil::WaitUntilDoneOrTimeout(*ctx, seconds));
  return Maybe<void>::Ok();
}

}  // namespace

ONEFLOW_API_PYBIND11_MODULE("", m) {
  m.def("check_current_rank_group_consistency",
        [](int64_t seconds) { return CheckCurrentRankGroupConsistency(seconds).GetOrThrow(); });
  m.def("check_current_rank_group_consistency",
        []() { return CheckCurrentRankGroupConsistency(60 * 5).GetOrThrow(); });
}

}  // namespace oneflow
