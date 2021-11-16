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

from random import shuffle
import numpy as np
from test_util import GenArgList
from oneflow.test_utils.automated_test_util import *

import oneflow as flow
import oneflow.unittest


def _test_permute_impl(test_case, device):
    input = flow.tensor(
        np.random.randn(2, 6, 5, 3),
        dtype=flow.float32,
        device=flow.device(device),
        requires_grad=True,
    )
    of_out1 = flow.permute(input, (1, 0, 2, 3))
    np_out = input.numpy().transpose((1, 0, 2, 3))
    test_case.assertTrue(np.array_equal(of_out1.numpy().flatten(), np_out.flatten()))
    of_out = of_out1.sum()
    of_out.backward()
    np_grad = np.ones((2, 6, 5, 3))
    test_case.assertTrue(np.allclose(input.grad.numpy(), np_grad, 0.0001, 0.0001))


def _test_tensor_permute_impl(test_case, device):
    input = flow.tensor(
        np.random.randn(2, 6, 5, 3),
        dtype=flow.float32,
        device=flow.device(device),
        requires_grad=True,
    )
    of_out1 = input.permute(1, 0, 2, 3)
    of_out2 = input.permute(*(1, 0, 2, 3))
    of_out3 = input.permute((1, 0, 2, 3))
    of_out4 = input.permute([1, 0, 2, 3])
    np_out = input.numpy().transpose((1, 0, 2, 3))
    test_case.assertTrue(np.array_equal(of_out1.numpy().flatten(), np_out.flatten()))
    test_case.assertTrue(np.array_equal(of_out2.numpy().flatten(), np_out.flatten()))
    test_case.assertTrue(np.array_equal(of_out3.numpy().flatten(), np_out.flatten()))
    test_case.assertTrue(np.array_equal(of_out4.numpy().flatten(), np_out.flatten()))
    of_out = of_out1.sum()
    of_out.backward()
    np_grad = np.ones((2, 6, 5, 3))
    test_case.assertTrue(np.allclose(input.grad.numpy(), np_grad, 0.0001, 0.0001))


@flow.unittest.skip_unless_1n1d()
class TestPermute(flow.unittest.TestCase):
    def test_permute(test_case):
        arg_dict = OrderedDict()
        arg_dict["device"] = ["cpu", "cuda"]
        for arg in GenArgList(arg_dict):
            _test_permute_impl(test_case, *arg)
            _test_tensor_permute_impl(test_case, *arg)

    @unittest.skip("pytorch 1.9.0 exist not torch.permute api")
    @autotest()
    def test_permute_with_random_data(test_case):
        device = random_device()
        ndim = 4
        permute_list = [0, 1, 2, 3]
        shuffle(permute_list)
        x = random_pytorch_tensor(ndim=ndim).to(device)
        y = torch.permute(x, dims=permute_list)
        return y

    @autotest(check_graph=False)
    def _test_permute_tensor_with_random_data(test_case):
        device = random_device()
        ndim = 4
        permute_list = [0, 1, 2, 3]
        shuffle(permute_list)
        x = random_pytorch_tensor(ndim=ndim).to(device)
        y = x.permute(permute_list)
        return y


if __name__ == "__main__":
    unittest.main()
