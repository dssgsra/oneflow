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
#include "oneflow/core/ep/cuda/cuda_stream.h"
#include "oneflow/core/job/global_for.h"
#include "oneflow/core/job/resource_desc.h"
#include "oneflow/core/device/node_device_descriptor_manager.h"
#include "oneflow/core/device/cuda_device_descriptor.h"

#ifdef WITH_CUDA

namespace oneflow {

namespace ep {

namespace {

constexpr size_t kDefaultWorkspaceSize = 4 * 1024 * 1024;  // 4M

void SetAffinityByDevice(int64_t dev_id) {
  auto node_device_desc =
      Global<device::NodeDeviceDescriptorManager>::Get()->GetLocalNodeDeviceDescriptor();
  auto cuda_device = std::dynamic_pointer_cast<const device::CudaDeviceDescriptor>(
      node_device_desc->GetDevice(device::kCudaDeviceDescriptorClassName, dev_id));
  if (!cuda_device) { return; }
  node_device_desc->Topology()->SetCPUAffinityByPCIBusID(cuda_device->PCIBusID());
  node_device_desc->Topology()->SetMemoryAffinityByPCIBusID(cuda_device->PCIBusID());
}

}  // namespace

#ifdef WITH_CUDA_GRAPHS

CudaGraphExecutable::CudaGraphExecutable() : graph_exec_(nullptr), dev_(-1) {}

CudaGraphExecutable::~CudaGraphExecutable() { Reset(); }

void CudaGraphExecutable::Update(cudaGraph_t graph) {
  int dev = -1;
  OF_CUDA_CHECK(cudaGetDevice(&dev));
  if (dev != dev_) { Reset(); }
  dev_ = dev;
  if (graph_exec_ != nullptr) {
    cudaGraphExecUpdateResult update_result{};
    cudaGraphNode_t error_node = nullptr;
    OF_CUDA_CHECK(cudaGraphExecUpdate(graph_exec_, graph, &error_node, &update_result));
    if (update_result == cudaGraphExecUpdateSuccess) { return; }
  }
  Reset();
  OF_CUDA_CHECK(cudaGraphInstantiate(&graph_exec_, graph, NULL, NULL, 0));
}

void CudaGraphExecutable::Launch(cudaStream_t stream) const {
  OF_CUDA_CHECK(cudaGraphLaunch(graph_exec_, stream));
}

bool CudaGraphExecutable::IsInstantiated() const { return graph_exec_ != nullptr; }

void CudaGraphExecutable::Reset() {
  if (graph_exec_ != nullptr) {
    CudaCurrentDeviceGuard guard(dev_);
    OF_CUDA_CHECK(cudaGraphExecDestroy(graph_exec_));
  }
}

#endif  // WITH_CUDA_GRAPHS

CudaStream::CudaStream(int device_ordinal) : device_ordinal_(device_ordinal) {
  CudaCurrentDeviceGuard guard(device_ordinal_);
  // cuda_stream
  OF_CUDA_CHECK(cudaStreamCreate(&cuda_stream_));
  // cublas_handle
  OF_CUBLAS_CHECK(cublasCreate(&cublas_handle_));
  OF_CUBLAS_CHECK(cublasSetStream(cublas_handle_, cuda_stream_));
#if CUBLAS_VERSION >= 11000
  if (Global<ResourceDesc, ForSession>::Get()->enable_tensor_float_32_compute()) {
    OF_CUBLAS_CHECK(cublasSetMathMode(cublas_handle_, CUBLAS_TF32_TENSOR_OP_MATH));
  }
#endif  // CUBLAS_VERSION >= 11000
#if CUBLAS_VERSION >= 11200
  workspace_size_ = kDefaultWorkspaceSize;
  OF_CUDA_CHECK(cudaMalloc(&workspace_, workspace_size_));
  OF_CUBLAS_CHECK(cublasSetWorkspace(cublas_handle_, workspace_, workspace_size_));
#endif  // CUBLAS_VERSION >= 11200
  // cudnn_handle
  if (IsCuda9OnTuringDevice()) {
    OF_CUDA_CHECK(cudaDeviceSynchronize());
    OF_CUDA_CHECK(cudaGetLastError());
  }
  OF_CUDNN_CHECK(cudnnCreate(&cudnn_handle_));
  if (IsCuda9OnTuringDevice()) {
    OF_CUDA_CHECK(cudaDeviceSynchronize());
    cudaGetLastError();
  }
  OF_CUDNN_CHECK(cudnnSetStream(cudnn_handle_, cuda_stream_));
}

CudaStream::~CudaStream() {
  CudaCurrentDeviceGuard guard(device_ordinal_);
  OF_CUDA_CHECK(cudaStreamSynchronize(cuda_stream_));
  OF_CUDNN_CHECK(cudnnDestroy(cudnn_handle_));
  OF_CUBLAS_CHECK(cublasDestroy(cublas_handle_));
  OF_CUDA_CHECK(cudaStreamDestroy(cuda_stream_));
#if CUBLAS_VERSION >= 11200
  OF_CUDA_CHECK(cudaFree(workspace_));
#endif  // CUBLAS_VERSION >= 11200
}

Maybe<void> CudaStream::OnExecutionContextSetup() {
  SetAffinityByDevice(device_ordinal_);
  OF_CUDA_CHECK(cudaSetDevice(device_ordinal_));
  return Maybe<void>::Ok();
}

Maybe<void> CudaStream::OnExecutionContextTeardown() { return Maybe<void>::Ok(); }

DeviceType CudaStream::device_type() const { return DeviceType::kGPU; }

Maybe<void> CudaStream::Sync() {
  cudaError_t err = cudaStreamSynchronize(cuda_stream_);
  if (err == cudaSuccess) {
    return Maybe<void>::Ok();
  } else {
    return Error::RuntimeError() << cudaGetErrorString(err) << " (" << err << ") ";
  }
}

cudaStream_t CudaStream::cuda_stream() const { return cuda_stream_; }

cublasHandle_t CudaStream::cublas_handle() const { return cublas_handle_; }

cudnnHandle_t CudaStream::cudnn_handle() const { return cudnn_handle_; }

#ifdef WITH_CUDA_GRAPHS

void CudaStream::BeginGraphCapture() {
  CHECK(!is_graph_capturing_);
  is_graph_capturing_ = true;
  OF_CUDA_CHECK(cudaStreamBeginCapture(cuda_stream_, cudaStreamCaptureModeThreadLocal));
}

void CudaStream::EndGraphCapture(CudaGraphExecutable* executable) {
  cudaGraph_t graph = nullptr;
  OF_CUDA_CHECK(cudaStreamEndCapture(cuda_stream_, &graph));
  executable->Update(graph);
  OF_CUDA_CHECK(cudaGraphDestroy(graph));
  is_graph_capturing_ = false;
}

bool CudaStream::IsGraphCapturing() const { return is_graph_capturing_; }

void CudaStream::LaunchGraph(const CudaGraphExecutable* executable) {
  executable->Launch(cuda_stream_);
}

#endif  // WITH_CUDA_GRAPHS

}  // namespace ep

}  // namespace oneflow

#endif  // WITH_CUDA
