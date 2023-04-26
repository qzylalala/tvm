#include <tvm/relay/expr_functor.h>
#include <tvm/relay/transform.h>
#include <tvm/relay/type.h>
#include <tvm/runtime/module.h>
#include <tvm/runtime/object.h>

#include <fstream>
#include <sstream>

#include "codegen_c.h"

namespace tvm {
namespace relay {
namespace contrib {

class CodegenC : public ExprVisitor, public CodegenCBase {
  public:
    explicit CodegenC(const std::string& id) { this->ext_func_id_ = id; }

    void VisitExpr_(const VarNode* node) { ; }
    void VisitExpr_(const CallNode* call) final { ; }
    std::string JIT() { ; }

  private:
    /*! \brief The function id that represents a C source function. */
    std::string ext_func_id_ = "";
    /*! \brief The index of a wrapped C function. */
    int func_idx = 0;
    /*! \brief The index of allocated buffers. */
    int buf_idx_ = 0;
    /*! \brief The arguments of a C compiler compatible function. */
    std::vector<std::string> ext_func_args_;
    /*! \brief The statements of a C compiler compatible function. */
    std::vector<std::string> ext_func_body;
    /*! \brief The declaration statements of a C compiler compatible function. */
    std::vector<std::string> func_decl_;
    /*! \brief The declaration statements of buffers. */
    std::vector<std::string> buf_decl_;
    /*! \brief The name and index pairs for output. */
    std::vector<std::pair<std::string, int>> out_;
};


void CodegenC::VisitExpr_(const CallNode* call) {
    std::ostringstream macro_stream;
    std::ostringstream decl_stream;
    std::ostringstream buf_stream;

    // 1. 生成函数声明

    // Generate a unique function name you like.
    std::string func_name = ext_func_id_ + "_" + std::to_string(func_idx++);

    // Make function declaration string.
    macro_stream << "CSOURCE_BINARY_OP_" << call->args.size() << "D(" << func_name << ", ";

    // Check the operator type.
    if (IsOp(call, "add")) {
    macro_stream << "+";
    } else if (IsOp(call, "subtract")) {
    macro_stream << "-";
    } else if (IsOp(call, "multiply")) {
    macro_stream << "*";
    } else {
    LOG(FATAL) << "Unrecognized op";
    }

    // Extract the input tensor shape.
    auto in_shape = GetShape(call->args[0]->checked_type());
    for (size_t i = 0; i < in_shape.size(); ++i) {
    macro_stream << ", " << in_shape[i];
    }
    macro_stream << ");";
    func_decl_.push_back(macro_stream.str());


    // 2. 生成函数调用
    bool first = true;
    decl_stream << func_name << "(";
    for (size_t i = 0; i < call->args.size(); ++i) {
        VisitExpr(call->args[i]); // 注 1
        for (auto out : out_) {
            if (!first) {
            decl_stream << ", ";
            }
            first = false;
            decl_stream << out.first;
        }
    }
    

    // 3. 生成输出数组（output buffer）
    auto type_node = call->checked_type().as<TensorTypeNode>();
    ICHECK(type_node != nullptr && runtime::TypeMatch(type_node->dtype, kDLFloat, 32))
        << "Only support single output tensor with float type";

    // 生成一个唯一的数组名字。
    std::string out = "buf_" + std::to_string(buf_idx_++);

    // 提取 shape 作为数组大小。
    auto out_shape = GetShape(call->checked_type());
    int out_size = 1;
    for (size_t i = 0; i < out_shape.size(); ++i) {
    out_size *= out_shape[i];
    }

    // 分配数组并推送至数组声明
    buf_stream << "float* " << out << " = (float*)std::malloc(4 * " << out_size << ");";
    buf_decl_.push_back(buf_stream.str());


    decl_stream << ", " << out << ");";
    ext_func_body.push_back(decl_stream.str());


    // 4. 更新输出数组
    out_.clear();
    out_.push_back({out, out_size});
}


void CodegenC::VisitExpr_(const VarNode* node) {
    ext_func_args_.push_back(node->name_hint());
    out_.clear();
    out_.push_back({node->name_hint(), 0});
}

std::string CodegenC::JIT() {
  // Write function macros
  for (auto decl : func_decl_) {
    code_stream_ << decl << "\n";
  }
  return JitImpl(ext_func_id_, ext_func_args_, buf_decl_, ext_func_body, out_);
}


class CSourceCodegen : public CSourceModuleCodegenBase {
 public:
  // 传递一个子图函数, 并生成 C 代码。
  void GenCFunc(const Function& func) { ; }

  // 使用 GenCFunc 来生成 C 代码并将它包装成一个 C 源模块。
  runtime::Module CreateCSourceModule(const NodeRef& ref) override { ; }

 private:
  std::ostringstream code_stream_;
};

void CSourceCodegen::GenCFunc(const Function& func) {
  ICHECK(func.defined()) << "Input error: expect a Relay function.";

  // 记录运行查找的外部符号。
  auto sid = GetExtSymbol(func);

  CodeGenC builder(sid);
  builder.VisitExpr(func->body);
  code_stream_ << builder.JIT();
}


runtime::Module CSourceCodegen::CreateCSourceModule(const NodeRef& ref) override {
  // 创建头文件
  code_stream_ << "#include <cstdint>\n";
  code_stream_ << "#include <iostream>\n";
  code_stream_ << "#include <cstdlib>\n";
  code_stream_ << "#include <stdio.h>\n";
  code_stream_ << "#include <cstring>\n";
  code_stream_ << "#include <tvm/runtime/c_runtime_api.h>\n";
  code_stream_ << "#include <dlpack/dlpack.h>\n";

  // 为算子定义添加一些公共宏。
  const char* operator_macro = R"op_macro(
  #define CSOURCE_BINARY_OP_1D(p_ID_, p_OP_, p_DIM1_)       \
    extern "C" void p_ID_(float* a, float* b, float* out) { \
      for (int64_t i = 0; i < p_DIM1_; ++i) {               \
        out[i] = a[i] p_OP_ b[i];                           \
      }                                                     \
    }

  #define CSOURCE_BINARY_OP_2D(p_ID_, p_OP_, p_DIM1_, p_DIM2_)  \
    extern "C" void p_ID_(float* a, float* b, float* out) {     \
      for (int64_t i = 0; i < p_DIM1_; ++i) {                   \
        for (int64_t j = 0; j < p_DIM2_; ++j) {                 \
          int64_t k = i * p_DIM2_ + j;                          \
          out[k] = a[k] p_OP_ b[k];                             \
        }                                                       \
      }                                                         \
    }
  )op_macro";

  code_stream_ << operator_macro << "\n\n";

  // 为子图生成 C 代码。
  if (ref->IsInstance<FunctionNode>()) {
    GenCFunc(Downcast<Function>(ref));
  } else if (ref->IsInstance<relay::ModuleNode>()) {
    relay::Module mod = Downcast<relay::Module>(ref);
    for (const auto& it : mod->functions) {
      GenCFunc(Downcast<Function>(it.second));
    }
  } else {
    LOG(FATAL) << "The input ref is expected to be a Relay function or module"
               << "\n";
  }

  // 创建一个 CSourceModule
  const auto* pf = runtime::Registry::Get("module.csource_module_create");
  ICHECK(pf != nullptr) << "Cannot find csource module to create the external runtime module";
  return (*pf)(code_stream_.str(), "cc");
}



runtime::Module CCompiler(const NodeRef& ref) {
  CSourceCodegen csource;
  return csource.CreateCSourceModule(ref);
}
TVM_REGISTER_GLOBAL("relay.ext.tutorial_compiler").set_body_typed(CCompiler);

}
}
}



/**
 *  generated code like following
 * 
#define GCC_BINARY_OP_1D(p_ID_, p_OP_, p_DIM1_)           \
  extern "C" void p_ID_(float* a, float* b, float* out) { \
    for (int64_t i = 0; i < p_DIM1_; ++i) {               \
      out[i] = a[i] p_OP_ b[i];                           \
    }                                                     \
  }

#define GCC_BINARY_OP_2D(p_ID_, p_OP_, p_DIM1_, p_DIM2_)  \
  extern "C" void p_ID_(float* a, float* b, float* out) { \
    for (int64_t i = 0; i < p_DIM1_; ++i) {               \
      for (int64_t j = 0; j < p_DIM2_; ++j) {             \
        int64_t k = i * p_DIM2_ + j;                      \
        out[k] = a[k] p_OP_ b[k];                         \
      }                                                   \
    }                                                     \
  }

// 注 1
GCC_BINARY_OP_2D(gcc_0_0, *, 10, 10);
GCC_BINARY_OP_2D(gcc_0_1, -, 10, 10);
GCC_BINARY_OP_2D(gcc_0_2, +, 10, 10);

// 注 2
extern "C" void gcc_0_(float* gcc_input0, float* gcc_input1,
                       float* gcc_input2, float* gcc_input3, float* out) {
  float* buf_0 = (float*)malloc(4 * 100);
  float* buf_1 = (float*)malloc(4 * 100);
  gcc_0_2(gcc_input0, gcc_input1, buf_0);
  gcc_0_1(buf_0, gcc_input2, buf_1);
  gcc_0_0(buf_1, gcc_input3, out);
  free(buf_0);
  free(buf_1);
}

// 注 3
extern "C" int gcc_0_wrapper(DLTensor* arg0, DLTensor* arg1, DLTensor* arg2,
                             DLTensor* arg3, DLTensor* out) {
  gcc_0_(static_cast<float*>(arg0->data), static_cast<float*>(arg1->data),
         static_cast<float*>(arg2->data), static_cast<float*>(arg3->data),
         static_cast<float*>(out->data));
  return 0;
}
TVM_DLL_EXPORT_TYPED_FUNC(gcc_0, gcc_0_wrapper);
*/