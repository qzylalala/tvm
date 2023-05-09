import sys
from collections import OrderedDict
import numpy as np
import pytest

import tvm
import tvm.testing
from tvm import relay, runtime
from tvm.relay.build_module import bind_params_by_name
from tvm.relay.op.annotation import compiler_begin, compiler_end
from utils.external_codegen import (
    update_lib,
    set_external_func_attr,
    parametrize_external_codegen_checks,
    parametrize_external_json_codegen_checks,
    check_graph_executor_result,
    check_vm_result,
)

import numpy as np


x = relay.var("x", shape=(2, 2))
y = relay.var("y", shape=(2, 2))

# subgraph for mul
x0 = relay.var("x0", shape=(2, 2))
y0 = relay.var("y0", shape=(2, 2))
mul = x0 * y0
mul = relay.Function([x0, y0], mul)
mul = set_external_func_attr(mul, "dnnl", "PIMCompiler_2")
call_mul = relay.Call(mul, [y, y])

# subgraph for add
x1 = relay.var("x1", shape=(2, 2))
y1 = relay.var("y1", shape=(2, 2))
add = x1 + y1
add = relay.Function([x1, y1], add)
add = set_external_func_attr(add, "dnnl", "PIMCompiler_1")
call_add = relay.Call(add, [x, x])

# subgraph for sub
x2 = relay.var("x2", shape=(2, 2))
y2 = relay.var("y2", shape=(2, 2))
sub = x2 - y2
sub = relay.Function([x2, y2], sub)
sub = set_external_func_attr(sub, "dnnl", "PIMCompiler_0")
call_sub = relay.Call(sub, [call_mul, call_add])
mod = tvm.IRModule.from_expr(call_sub)

print(mod)

lib = relay.build_module.build(mod, "llvm")
module = lib["default"]

print(module)