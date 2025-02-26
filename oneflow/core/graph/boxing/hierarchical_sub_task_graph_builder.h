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
#ifndef ONEFLOW_CORE_GRAPH_BOXING_HIERARCHICAL_SUB_TASK_GRAPH_BUILDER_H_
#define ONEFLOW_CORE_GRAPH_BOXING_HIERARCHICAL_SUB_TASK_GRAPH_BUILDER_H_

#include "oneflow/core/common/util.h"
#include "oneflow/core/graph/boxing/sub_task_graph_builder_context.h"
#include "oneflow/core/graph/boxing/sub_task_graph_builder_status_util.h"

namespace oneflow {

class HierarchicalSubTskGphBuilder {
 public:
  OF_DISALLOW_COPY_AND_MOVE(HierarchicalSubTskGphBuilder);
  HierarchicalSubTskGphBuilder() = default;
  virtual ~HierarchicalSubTskGphBuilder() = default;

  virtual Maybe<SubTskGphBuilderStatus> Build(
      SubTskGphBuilderCtx* ctx, const std::vector<TaskNode*>& sorted_in_tasks,
      std::vector<TaskNode*>* sorted_out_tasks,
      std::vector<std::vector<TaskNode*>>* sorted_ctrl_tasks, const ParallelDesc& in_parallel_desc,
      const ParallelDesc& out_parallel_desc, const LogicalBlobId& lbi,
      const BlobDesc& logical_blob_desc, const cfg::NdSbp& in_nd_sbp, const cfg::NdSbp& out_nd_sbp,
      const Shape& time_shape) const = 0;
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_GRAPH_BOXING_HIERARCHICAL_SUB_TASK_GRAPH_BUILDER_H_
