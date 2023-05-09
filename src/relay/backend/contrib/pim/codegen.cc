#include <tvm/relay/expr_functor.h>
#include <tvm/relay/transform.h>
#include <tvm/relay/type.h>
#include <tvm/runtime/module.h>
#include <tvm/runtime/object.h>

#include <fstream>
#include <sstream>

#include "codegen_c.h"
#include "../../utils.h"
#include "../../../transforms/compiler_function_utils.h"

namespace tvm {
namespace relay {
namespace contrib {

class CodegenC : public backend::MemoizedExprTranslator<std::vector<Output>>, public CodegenCBase {
  public:
    CodegenC(std::unordered_map<std::string, runtime::NDArray>* const_name_to_constant,
           Array<String>* const_names, bool* needs_extra_headers, std::string ext_func_id)
      : const_name_to_constant_(const_name_to_constant),
        const_names_(const_names),
        needs_extra_headers_(needs_extra_headers),
        ext_func_id_(std::move(ext_func_id)) {}

    explicit CodegenC(const std::string& id) { this->ext_func_id_ = id; }

    std::vector<Output> VisitExpr_(const VarNode* node) override;

    std::vector<Output> VisitExpr_(const CallNode* call) override;

    std::string JIT(const std::vector<Output>& out) override;

  private:
    /*!
    * \brief The accumulated constant name to constant mapping. Shared between all generated
    * functions.
    */
    std::unordered_map<std::string, runtime::NDArray>* const_name_to_constant_;
    /*! \brief The accumulated constant names, in the order they were generated. */
    Array<String>* const_names_;
    /*!
    * \brief Set to true if the ndarray and packed function headers are required to declare and
    * manage the constants array.
    */
    bool* needs_extra_headers_;
    /*! \brief The function id that represents a C source function. */
    std::string ext_func_id_ = "";
    /*! \brief The index of a wrapped C function. */
    int func_idx = 0;
    /*! \brief The index of allocated buffers. */
    int buf_idx_ = 0;
    /*! \brief The arguments of a C compiler compatible function. */
    Array<Var> ext_func_args_;
    /*! \brief The statements of a C compiler compatible function. */
    std::vector<std::string> ext_func_body;
    /*! \brief The array declared to store the constant values. */
    std::string const_array_name_;
    /*! \brief The declaration statements of a C compiler compatible function. */
    std::vector<std::string> func_decl_;
    /*! \brief The declaration statements of buffers. */
    std::vector<std::string> buf_decl_;
    /*! \brief The name and index pairs for output. */
    std::vector<Output> out_;
};


std::vector<Output> CodegenC::VisitExpr_(const VarNode* node) {
  ext_func_args_.push_back(GetRef<Var>(node));
  Output output;
  output.name = node->name_hint();
  return {output};
}


std::vector<Output> CodegenC::VisitExpr_(const CallNode* call) {
  std::ostringstream macro_stream;
  std::ostringstream decl_stream;
  std::ostringstream buf_stream;

  // Generate a unique function name you like.
  std::string func_name = ext_func_id_ + "_" + std::to_string(func_idx++);

  // Make function declaration string.
  macro_stream << "CSOURCE_BINARY_OP_" << call->args.size() << "D(" << func_name << ", ";

  // Check the operator type.
  if (backend::IsOp(call, "add")) {
    macro_stream << "+";
  } else if (backend::IsOp(call, "subtract")) {
    macro_stream << "-";
  } else if (backend::IsOp(call, "multiply")) {
    macro_stream << "*";
  } else {
    LOG(FATAL) << "Unrecognized op";
  }

  // Extract the input tensor shape.
  auto in_shape = backend::GetShape(call->args[0]->checked_type());
  for (size_t i = 0; i < in_shape.size(); ++i) {
    macro_stream << ", " << in_shape[i];
  }

  macro_stream << ");";
  func_decl_.push_back(macro_stream.str());

  // Make function call when visiting arguments
  bool first = true;
  decl_stream << func_name << "(";
  for (size_t i = 0; i < call->args.size(); ++i) {
    auto res = VisitExpr(call->args[i]); // 注 1
    for (auto out : res) {
      if (!first) {
        decl_stream << ", ";
      }
      first = false;
      decl_stream << out.name;
    }
  }
  // 注 2


  // 这个例子仅支持单个输出。
  auto type_node = call->checked_type().as<TensorTypeNode>();
  ICHECK(type_node != nullptr && runtime::TypeMatch(type_node->dtype, kDLFloat, 32))
        << "Only support single output tensor with float type";

  const auto& dtype = GetDtypeString(type_node);
  macro_stream << ", " << dtype;

  // 生成一个唯一的数组名字。
  std::string out = "buf_" + std::to_string(buf_idx_++);

  // 提取 shape 作为数组大小。
  auto out_shape = backend::GetShape(call->checked_type());
  int out_size = 1;
  for (size_t i = 0; i < out_shape.size(); ++i) {
    out_size *= out_shape[i];
  }

  // 分配数组并推送至数组声明
  buf_stream << "float* " << out << " = (float*)std::malloc(4 * " << out_size << ");";
  buf_decl_.push_back(buf_stream.str());


  decl_stream << ", " << out << ");";
  ext_func_body.push_back(decl_stream.str());

  // Update output buffer
  // Note C codegen only handles TensorType. Therefore, we don't flatten
  // tuples and only return a single vaule.
  Output output;
  output.name = out;
  output.dtype = dtype;
  output.need_copy = true;
  output.size = out_size;
  return {output};
}


std::string CodegenC::JIT(const std::vector<Output>& out) {
  // Write function macros
  for (auto decl : func_decl_) {
    code_stream_ << decl << "\n";
  }
  return JitImpl(ext_func_id_, ext_func_args_, buf_decl_, ext_func_body, CreateNDArrayPool(ext_func_id_), out);
}


class CSourceCodegen : public CSourceModuleCodegenBase {
 public:
  // 传递一个子图函数, 并生成 C 代码。
  void GenCFunc(const Function& func);

  // 使用 GenCFunc 来生成 C 代码并将它包装成一个 C 源模块。
  runtime::Module CreateCSourceModule(const ObjectRef& ref) override;

 private:
  std::ostringstream code_stream_;
};


void CSourceCodegen::GenCFunc(const Function& func) {
  ICHECK(func.defined()) << "Input error: expect a Relay function.";

  // 记录运行查找的外部符号。
  auto sid = backend::GetExtSymbol(func);

  CodegenC builder(sid);
  auto out = builder.VisitExpr(func->body);
  code_stream_ << builder.JIT(out);
}


runtime::Module CSourceCodegen::CreateCSourceModule(const ObjectRef& ref) {
  // 创建头文件
  code_stream_ << "#include <cstdint>\n";
  code_stream_ << "#include <iostream>\n";
  code_stream_ << "#include <cstdlib>\n";
  code_stream_ << "#include <stdio.h>\n";
  code_stream_ << "#include <cstring>\n";
  code_stream_ << "#include <tvm/runtime/c_runtime_api.h>\n";
  code_stream_ << "#include <tvm/runtime/packed_func.h>\n";
  code_stream_ << "#include <dlpack/dlpack.h>\n";

  code_stream_ << "#include <dnnl/dnnl_kernel.h>\n";
  code_stream_ << "using namespace tvm::runtime;\n";
  code_stream_ << "using namespace tvm::runtime::contrib;\n";
  code_stream_ << "\n";

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
  } else {
    LOG(FATAL) << "The input ref is expected to be a Relay function or module"
               << "\n";
  }

  // 创建一个 CSourceModule
  const auto* pf = runtime::Registry::Get("module.CSourceModuleCreate");
  ICHECK(pf != nullptr) << "Cannot find csource module to create the external runtime module";
  return (*pf)(code_stream_.str(), "cc");
}

runtime::Module PIMCompiler(const ObjectRef& ref) {
  CSourceCodegen csource;
  return csource.CreateCSourceModule(ref);
}

TVM_REGISTER_GLOBAL("relay.ext.PIMCompiler").set_body_typed(PIMCompiler);

}
}
}