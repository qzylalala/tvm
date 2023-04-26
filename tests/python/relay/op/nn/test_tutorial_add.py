import pytest
import tvm
from tvm import relay
from tvm.relay import testing
from tvm.relay.backend.interpreter import ConstructorValue
from tvm.relay import create_executor
from tvm.relay.prelude import Prelude, StaticTensorArrayOps
from tvm.relay.testing import count as count_, make_nat_value, make_nat_expr

import numpy as np

prelude = p = Prelude(tvm.IRModule({}))


def test_tutorial_add():
    x = relay.var("x")
    call = relay.op.nn.tutorial_add(x, 1.0)
    assert isinstance(call, relay.Call)


if __name__ == "__main__":
    tvm.testing.main()
