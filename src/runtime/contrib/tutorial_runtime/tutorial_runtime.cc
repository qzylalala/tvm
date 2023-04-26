#include <dmlc/logging.h>
#include <tvm/runtime/c_runtime_api.h>
#include <tvm/runtime/memory.h>
#include <tvm/runtime/module.h>
#include <tvm/runtime/ndarray.h>
#include <tvm/runtime/object.h>
#include <tvm/runtime/packed_func.h>
#include <tvm/runtime/registry.h>

#include <fstream>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace tvm {
namespace runtime {
class ExampleJsonModule : public ModuleNode {
 public:
  explicit ExampleJsonModule(std::string graph_json);

  PackedFunc GetFunction(const std::string& name,
                         const ObjectPtr<Object>& sptr_to_self) final;

  const char* type_key() const { return "examplejson"; }

  void SaveToBinary(dmlc::Stream* stream) final;

  static Module LoadFromBinary(void* strm);

  static Module Create(const std::string& path);

  std::string GetSource(const std::string& format = "");

  void Run(int id, const std::vector<int>& inputs, int output);

  void ParseJson(const std::string& json);

 private:
  /* \brief 代表计算图的 json 字符串。 */
  std::string graph_json_;
  /* \brief 正在被处理的子图。 */
  std::string curr_subgraph_;
  /*! \brief 由子图 id 到节点条目的简单图。 */
  std::map<std::string, std::vector<NodeEntry> > graph_;
  /* \brief 包含图中每一个节点的张量的简单池。 */
  std::vector<NDArray> data_entry_;
  /* \brief 从节点 id 到算子名字的映射。 */
  std::vector<std::string> op_id_;
};


explicit ExampleJsonModule::ExampleJsonModule(std::string graph_json) {
  this->graph_json_ = graph_json;
  ParseJson(this->graph_json_);
}

void ExampleJsonModule::ParseJson(const std::string& json) {
  std::string line;
  std::string curr_subgraph;
  std::stringstream ss(json);

  while (std::getline(ss, line, '\n')) {
    std::stringstream ss2(line);
    std::string token;
    int id = 0;

    ss2 >> token;
    if (token.find("subgraph_") != std::string::npos) {
      curr_subgraph = token;
      continue;
    }

    ss2 >> id;
    if (op_id_.size() <= static_cast<size_t>(id)) {
      op_id_.resize(id + 1);
      data_entry_.resize(id + 1);
    }

    int64_t total_elements = 1;
    std::vector<int64_t> shape;
    if (token == "input") {
      int64_t size = 0;
      while (ss2 >> size) {
        total_elements *= size;
        shape.push_back(size);
      }
    } else {
      op_id_[id] = token; // 注 1
      bool shape_data = false;
      NodeEntry entry;
      while (ss2 >> token) {
        if (token == "shape:") {
          shape_data = true;
        } else if (shape_data) {
          total_elements *= std::stoll(token);
          shape.push_back(std::stoll(token));
        } else if (token != "inputs:") {
          entry.inputs.push_back(std::stoi(token));
        }
      }
      entry.id = id;
      entry.output = id;
      graph_[curr_subgraph].push_back(entry); // 注 2
    }
    DLDevice dev;
    dev.device_type = static_cast<DLDeviceType>(1);
    dev.device_id = 0;
    data_entry_[id] = NDArray::Empty(shape, DLDataType{kDLFloat, 32, 1}, dev); // 注 3
  }
}


PackedFunc ExampleJsonModule::GetFunction(const std::string& name,
                       const ObjectPtr<Object>& sptr_to_self) {
  if (this->graph_.find(name) != this->graph_.end()) {
    this->curr_subgraph_ = name;
    return PackedFunc([sptr_to_self, this](TVMArgs args, TVMRetValue* rv) {

      // Copy input tensors to corresponding data entries.
      for (auto i = 0; i < args.size(); ++i) {
        ICHECK(args[i].type_code() == kNDArrayContainer || args[i].type_code() == kArrayHandle)
            << "Expect NDArray or DLTensor as inputs\n";
        if (args[i].type_code() == kArrayHandle) {
          DLTensor* arg = args[i];
          this->data_entry_[i].CopyFrom(arg);
        } else {
          NDArray arg = args[i];
          this->data_entry_[i].CopyFrom(arg);
        }
      }

      // Execute the subgraph.
      for (const auto& it : this->graph_[this->curr_subgraph_]) {
        this->Run(it.id, it.inputs, it.output);
      }
      ICHECK_GT(graph_.count(this->curr_subgraph_), 0U);

      // Copy the output from a data entry back to TVM runtime argument.
      auto out_idx = graph_[this->curr_subgraph_].back().output;
      if (args[args.size() - 1].type_code() == kArrayHandle) {
        DLTensor* arg = args[args.size() - 1];
        this->data_entry_[out_idx].CopyTo(arg);
      } else {
        NDArray arg = args[args.size() - 1];
        this->data_entry_[out_idx].CopyTo(arg);
      }
      *rv = data_entry_.back();
    });
  } else {
    LOG(FATAL) << "Unknown subgraph: " << name << "\n";
    return PackedFunc();
  }
}


void ExampleJsonModule::Run(int id, const std::vector<int>& inputs, int output) {
  // Make a list data entry indexs.
  std::vector<int> args(inputs.begin(), inputs.end());
  args.push_back(output);

  // Initialize data holders.
  std::vector<TVMValue> values(args.size());
  std::vector<int> type_codes(args.size());

  // Initialize a TVM arg setter with TVMValue and its type code.
  TVMArgsSetter setter(values.data(), type_codes.data());

  // Set each argument to its corresponding data entry.
  if (op_id_[id] == "add" || op_id_[id] == "sub" || op_id_[id] == "mul") {
    for (size_t i = 0; i < args.size(); i++) {
      setter(i, data_entry_[args[i]]);
    }
  }

  // Invoke the corresponding operator function.
  if (op_id_[id] == "add") {
    Add(values.data(), type_codes.data(), args.size());
  } else if (op_id_[id] == "sub") {
    Sub(values.data(), type_codes.data(), args.size());
  } else if (op_id_[id] == "mul") {
    Mul(values.data(), type_codes.data(), args.size());
  } else {
    LOG(FATAL) << "Unknown op: " << op_id_[id] << "\n";
  }
}

TVM_REGISTER_GLOBAL("module.examplejson_module_create")
.set_body_typed([](std::string code){
    auto n = make_object<ExampleJsonModule>(code);
    return runtime::Module(n);
});

void ExampleJsonModule::SaveToBinary(dmlc::Stream* stream) final {
    stream->Write(this->graph_json_);
}

static Module LoadFromBinary(void* strm) {
  dmlc::Stream* stream = static_cast<dmlc::Stream*>(strm);
  std::string graph_json;
  stream->Read(&graph_json);
  auto n = tvm::runtime::make_object<ExampleJsonModule>(graph_json);
  return Module(n);
}

TVM_REGISTER_GLOBAL("module.loadbinary_examplejson")
.set_body_typed(ExampleJsonModule::LoadFromBinary);


static Module ExampleJsonModule::Create(const std::string& path) {
    std::ifstream filep;
    filep.open(path, std::ios::in);
    std::string graph_json;
    std::string line;
    while (std::getline(filep, line)) {
        graph_json += line;
        graph_json += "\n";
    }
    filep.close();
    auto n = tvm::runtime::make_object<ExampleJsonModule>(graph_json);
    return Module(n);
}

TVM_REGISTER_GLOBAL("module.loadfile_examplejson")
.set_body([](TVMArgs args, TVMRetValue* rv) {
    *rv = ExampleJsonModule::Create(args[0]);
});

}
}