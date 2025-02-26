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
#ifndef ONEFLOW_CORE_KERNEL_NEW_KERNEL_UTIL_H_
#define ONEFLOW_CORE_KERNEL_NEW_KERNEL_UTIL_H_

#include "oneflow/core/kernel/util/interface_bridge.h"

namespace oneflow {

namespace ep {

class Stream;

}

template<DeviceType deivce_type>
struct NewKernelUtil : public DnnIf<deivce_type>,
                       public BlasIf<deivce_type>,
                       public ArithemeticIf<deivce_type> {};

template<DeviceType device_type>
void Memcpy(DeviceCtx*, void* dst, const void* src, size_t sz);

template<DeviceType device_type>
void Memset(DeviceCtx*, void* dst, const char value, size_t sz);

template<DeviceType device_type>
void Memcpy(ep::Stream* stream, void* dst, const void* src, size_t sz);

template<DeviceType device_type>
void Memset(ep::Stream* stream, void* dst, const char value, size_t sz);

void WithHostBlobAndStreamSynchronizeEnv(DeviceCtx* ctx, Blob* blob,
                                         std::function<void(Blob*)> Callback);

}  // namespace oneflow

#endif  // ONEFLOW_CORE_KERNEL_NEW_KERNEL_UTIL_H_
