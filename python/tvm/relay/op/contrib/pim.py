import logging
from functools import reduce

import tvm.ir
from tvm import relay
from tvm.ir import Op
from tvm.relay import expr as _expr
from tvm.relay import transform
from tvm.relay.analysis import analysis as _analysis
from tvm.relay.expr import Call, GlobalVar, TupleGetItem, const
from tvm.relay.expr_functor import ExprMutator, ExprVisitor

from ... import _ffi_api
from ...dataflow_pattern import (
    DFPatternCallback,
    is_constant,
    is_expr,
    is_op,
    rewrite,
    wildcard,
)
from .register import register_pattern_table

def _register_external_op_helper(op_name, supported=True):
    """The helper function to indicate that a given operator can be supported
    by pim.

    Paramters
    ---------
    op_name : Str
        The name of operator that will be registered.

    Returns
    -------
    f : callable
        A function that returns if the operator is supported by pim.
    """

    @tvm.ir.register_op_attr(op_name, "target.pim")
    def _func_wrapper(expr):
        return supported

    return _func_wrapper

_register_external_op_helper("add")
_register_external_op_helper("subtract")
_register_external_op_helper("multiply")