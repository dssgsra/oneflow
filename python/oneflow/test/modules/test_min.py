"""
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
"""

import unittest
from collections import OrderedDict

import numpy as np

from oneflow.test_utils.automated_test_util import *
from test_util import GenArgList

import oneflow as flow
import oneflow.unittest


def _test_min(test_case, device, shape, dim, keepdims):
    input_arr = np.random.randn(*shape)
    np_out = np.amin(input_arr, axis=dim, keepdims=keepdims)
    x = flow.tensor(
        input_arr, dtype=flow.float32, device=flow.device(device), requires_grad=True
    )
    of_out = flow.min(x, dim, keepdims)
    if dim != None:
        of_out = of_out[0]

    test_case.assertTrue(np.allclose(of_out.numpy(), np_out, 1e-05, 1e-05))
    of_out = of_out.sum()
    of_out.backward()
    np_out_grad = np.zeros_like(input_arr)
    if dim == None:
        arg_min = np.argmin(input_arr)
        np.put(np_out_grad, arg_min, 1)
    else:
        arg_min = np.expand_dims(np.argmin(input_arr, axis=dim), axis=dim)
        np.put_along_axis(np_out_grad, arg_min, 1, axis=dim)
    test_case.assertTrue(np.allclose(x.grad.numpy(), np_out_grad, 0.0001, 0.0001))


def _test_min_tensor_function(test_case, device, shape, dim, keepdims):
    input_arr = np.random.randn(*shape)
    np_out = np.amin(input_arr, axis=dim, keepdims=keepdims)
    x = flow.tensor(
        input_arr, dtype=flow.float32, device=flow.device(device), requires_grad=True
    )
    of_out = x.min(dim, keepdims)
    if dim != None:
        of_out = of_out[0]

    test_case.assertTrue(np.allclose(of_out.numpy(), np_out, 1e-05, 1e-05))
    of_out = of_out.sum()
    of_out.backward()
    np_out_grad = np.zeros_like(input_arr)
    if dim == None:
        arg_min = np.argmin(input_arr)
        np.put(np_out_grad, arg_min, 1)
    else:
        arg_min = np.expand_dims(np.argmin(input_arr, axis=dim), axis=dim)
        np.put_along_axis(np_out_grad, arg_min, 1, axis=dim)
    test_case.assertTrue(np.allclose(x.grad.numpy(), np_out_grad, 0.0001, 0.0001))


@flow.unittest.skip_unless_1n1d()
class TestMinModule(flow.unittest.TestCase):
    def test_min(test_case):
        arg_dict = OrderedDict()
        arg_dict["test_fun"] = [_test_min, _test_min_tensor_function]
        arg_dict["device"] = ["cpu", "cuda"]
        arg_dict["shape"] = [(2,), (2, 3), (2, 3, 4, 5)]
        arg_dict["dim"] = [None, 0, -1]
        arg_dict["keepdims"] = [False, True]
        for arg in GenArgList(arg_dict):
            arg[0](test_case, *arg[1:])

    def test_min_against_pytorch(test_case):
        arg_dict = OrderedDict()
        arg_dict["test_type"] = [test_flow_against_pytorch, test_tensor_against_pytorch]
        arg_dict["device"] = ["cpu", "cuda"]
        for arg in GenArgList(arg_dict):
            arg[0](test_case, "min", device=arg[1])


if __name__ == "__main__":
    unittest.main()
