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
#include "oneflow/core/graph/boxing/boxing_logger.h"
#include "oneflow/core/job/sbp_parallel.h"

namespace oneflow {

namespace {

#define OF_BOXING_LOGGER_CSV_COLNUM_NAME_FIELD                   \
  "src_op_name,dst_op_name,src_parallel_desc,dst_parallel_desc," \
  "src_nd_sbp,"                                                  \
  "dst_nd_sbp,lbi,dtype,shape,builder,comment\n"

std::string ShapeToString(const Shape& shape) {
  std::stringstream shape_ss;
  auto dim_vec = shape.dim_vec();
  shape_ss << "(";
  for (int32_t i = 0; i < dim_vec.size(); ++i) {
    shape_ss << dim_vec.at(i);
    if (i != dim_vec.size() - 1) { shape_ss << " "; }
  }
  shape_ss << ")";
  return shape_ss.str();
}

std::string ParallelDescToString(const ParallelDesc& parallel_desc) {
  std::string serialized_parallel_desc;
  std::string device_type;
  device_type = *CHECK_JUST(DeviceTag4DeviceType(parallel_desc.device_type()));
  auto sorted_machine_ids = parallel_desc.sorted_machine_ids();
  serialized_parallel_desc += "{";
  for (int64_t i = 0; i < sorted_machine_ids.size(); ++i) {
    const int64_t machine_id = sorted_machine_ids.at(i);
    serialized_parallel_desc += std::to_string(machine_id) + ":" + device_type + ":";
    int64_t min_id = parallel_desc.sorted_dev_phy_ids(machine_id).front();
    int64_t max_id = parallel_desc.sorted_dev_phy_ids(machine_id).back();
    serialized_parallel_desc += std::to_string(min_id) + "-" + std::to_string(max_id);
    serialized_parallel_desc += " ";
  }
  serialized_parallel_desc += ShapeToString(*parallel_desc.hierarchy());
  serialized_parallel_desc += "}";
  return serialized_parallel_desc;
}

std::string NdSbpToString(const cfg::NdSbp& nd_sbp) {
  std::string serialized_nd_sbp;
  const int64_t num_axes = nd_sbp.sbp_parallel_size();
  serialized_nd_sbp += "[";
  for (int64_t i = 0; i < num_axes - 1; ++i) {
    serialized_nd_sbp += SbpParallelToString(nd_sbp.sbp_parallel(i)) + " ";
  }
  serialized_nd_sbp += SbpParallelToString(nd_sbp.sbp_parallel(num_axes - 1)) + "]";
  return serialized_nd_sbp;
}

std::string MakeBoxingLoggerCsvRow(const SubTskGphBuilderStatus& status,
                                   const std::string& src_op_name, const std::string& dst_op_name,
                                   const ParallelDesc& src_parallel_desc,
                                   const ParallelDesc& dst_parallel_desc,
                                   const cfg::NdSbp& src_nd_sbp, const cfg::NdSbp& dst_nd_sbp,
                                   const LogicalBlobId& lbi, const BlobDesc& logical_blob_desc) {
  std::string serialized_status;
  serialized_status += src_op_name + ",";
  serialized_status += dst_op_name + ",";
  serialized_status += ParallelDescToString(src_parallel_desc) + ",";
  serialized_status += ParallelDescToString(dst_parallel_desc) + ",";
  serialized_status += NdSbpToString(src_nd_sbp) + ",";
  serialized_status += NdSbpToString(dst_nd_sbp) + ",";
  serialized_status += GenLogicalBlobName(lbi) + ",";
  serialized_status += DataType_Name(logical_blob_desc.data_type()) + ",";
  serialized_status += ShapeToString(logical_blob_desc.shape()) + ",";
  serialized_status += status.builder_name() + ",";
  if (status.comment().empty()) {
    serialized_status += "-";
  } else {
    serialized_status += status.comment();
  }
  serialized_status += "\n";
  return serialized_status;
}

}  // namespace

CsvBoxingLogger::CsvBoxingLogger(std::string path) {
  log_stream_ = TeePersistentLogStream::Create(path);
  log_stream_ << OF_BOXING_LOGGER_CSV_COLNUM_NAME_FIELD;
}

CsvBoxingLogger::~CsvBoxingLogger() { log_stream_->Flush(); }

void CsvBoxingLogger::Log(const SubTskGphBuilderStatus& status, const std::string& src_op_name,
                          const std::string& dst_op_name, const ParallelDesc& src_parallel_desc,
                          const ParallelDesc& dst_parallel_desc, const cfg::NdSbp& src_nd_sbp,
                          const cfg::NdSbp& dst_nd_sbp, const LogicalBlobId& lbi,
                          const BlobDesc& logical_blob_desc) {
  log_stream_ << MakeBoxingLoggerCsvRow(status, src_op_name, dst_op_name, src_parallel_desc,
                                        dst_parallel_desc, src_nd_sbp, dst_nd_sbp, lbi,
                                        logical_blob_desc);
}

}  // namespace oneflow
