/*
  Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.
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

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "paddle/framework/net.h"
#include "paddle/framework/op_registry.h"
#include "paddle/framework/operator.h"
#include "paddle/framework/tensor.h"
#include "paddle/operators/recurrent_network_op.h"

namespace paddle {
namespace operators {

class RecurrentOpTest : public ::testing::Test {
protected:
  virtual void SetUp() override {
    CreateGlobalVariables();
    CreateStepNet();
    CreateRNNOp();
  }

  virtual void TearDown() override {}

  void CreateGlobalVariables() {
    scope_ = std::make_shared<Scope>();
    // create input, and init content
    LOG(INFO) << "create global variable x";
    for (auto inlink : std::vector<std::string>{"x", "x0", "x1", "h"}) {
      Variable* x = scope_->CreateVariable(inlink);
      DDim dims = make_ddim(std::vector<int>{
          10 /*sent size*/, 20 /*batch size*/, 30 /*input dim*/});
      x->GetMutable<Tensor>()->mutable_data<float>(dims, platform::CPUPlace());
    }
    // create output alias just for test
    for (auto inlink : std::vector<std::string>{"h@alias"}) {
      Variable* x = scope_->CreateVariable(inlink);
      DDim dims =
          make_ddim(std::vector<int>{20 /*batch size*/, 30 /*input dim*/});
      x->GetMutable<Tensor>()->mutable_data<float>(dims, platform::CPUPlace());
    }

    LOG(INFO) << "create global variable w";
    Variable* w = scope_->CreateVariable("rnn/w");
    w->GetMutable<Tensor>()->mutable_data<float>(
        make_ddim(std::vector<int>{30, 30}), platform::CPUPlace());

    for (auto boot : std::vector<std::string>{"x_boot", "h_boot"}) {
      LOG(INFO) << "create global variable " << boot;
      Variable* h_boot = scope_->CreateVariable(boot);
      h_boot->GetMutable<Tensor>()->mutable_data<float>(
          make_ddim(std::vector<int>{20 /*batch size*/, 30 /*input dim*/}),
          platform::CPUPlace());
    }

    LOG(INFO) << "create variable step_scopes";
    scope_->CreateVariable("step_scopes");

    LOG(INFO) << "create variable h";
    scope_->CreateVariable("h");
  }

  void CreateRNNOp() {
    OpDesc op_desc;

    op_desc.set_type("recurrent_op");
    // inlinks 0
    op_desc.add_inputs("x");
    op_desc.add_inputs("x0");
    op_desc.add_inputs("x1");
    // boot_memories 3
    op_desc.add_inputs("x_boot");
    op_desc.add_inputs("h_boot");
    // step net 5
    op_desc.add_inputs("step_net");
    // outlinks 6
    op_desc.add_outputs("h");
    // step scopes 7
    op_desc.add_outputs("step_scopes");

    auto _input_format = std::vector<int>{
        0,  // in_link
        3,  // memories
        5   // step_net
    };
    auto input_format = op_desc.add_attrs();
    input_format->set_name("input_format");
    input_format->set_type(paddle::framework::AttrType::INTS);
    for (auto i : _input_format) {
      input_format->add_ints(i);
    }

    auto output_format = op_desc.add_attrs();
    output_format->set_name("output_format");
    output_format->set_type(paddle::framework::AttrType::INTS);
    for (auto i : std::vector<int>{0, 1, 2}) {
      output_format->add_ints(i);
    }

    auto inlink_alias = op_desc.add_attrs();
    inlink_alias->set_name("inlink_alias");
    inlink_alias->set_type(paddle::framework::AttrType::STRINGS);

    auto outlink_alias = op_desc.add_attrs();
    outlink_alias->set_name("outlink_alias");
    outlink_alias->set_type(paddle::framework::AttrType::STRINGS);

    auto pre_memories = op_desc.add_attrs();
    pre_memories->set_name("pre_memories");
    pre_memories->set_type(paddle::framework::AttrType::STRINGS);

    auto memories = op_desc.add_attrs();
    memories->set_name("memories");
    memories->set_type(paddle::framework::AttrType::STRINGS);

    // create inlink_alias
    for (const auto& item :
         std::vector<std::string>{"x@alias", "x0@alias", "x1@alias"}) {
      inlink_alias->add_strings(item);
    }
    // pre memories
    for (const auto& item :
         std::vector<std::string>{"rnn/x@pre", "rnn/h@pre"}) {
      pre_memories->add_strings(item);
    }
    // memories
    for (const auto& item : std::vector<std::string>{"rnn/x", "rnn/h"}) {
      memories->add_strings(item);
    }
    // output alias
    for (const auto& item : std::vector<std::string>{"h@alias"}) {
      outlink_alias->add_strings(item);
    }

    rnn_op_ = OpRegistry::CreateOp(op_desc);

    LOG(INFO) << "rnn_op finish init";
  }

  void CreateStepNet() {
    LOG(INFO) << "create variable step_net";
    Variable* var = scope_->CreateVariable("step_net");
    auto net = var->GetMutable<NetOp>();
    // rnn/s is net's input or output?
    net->inputs_ = {"rnn/h@pre", "rnn/w", "rnn/x"};
    net->inputs_ = {"rnn/s", "rnn/h"};
    net->AddOp(
        OpRegistry::CreateOp("mul", {"rnn/h@pre", "rnn/w"}, {"rnn/s"}, {}));

    net->AddOp(
        OpRegistry::CreateOp("add_two", {"rnn/x", "rnn/s"}, {"rnn/h"}, {}));
    net->CompleteAddOp();
  }

  // father scope
  std::shared_ptr<Scope> scope_;
  std::shared_ptr<OperatorBase> rnn_op_;
};

TEST_F(RecurrentOpTest, Run) {
  platform::CPUDeviceContext ctx;
  rnn_op_->InferShape(scope_);
  rnn_op_->Run(scope_, ctx);
}

class RecurrentGradientAlgorithmTest : public ::testing::Test {
protected:
  virtual void SetUp() override {
    CreateGlobalVariables();
    CreateStepScopes();
    CreateStepNet();
    CreateRNNGradientAlgorithm();

    // segment inputs
    SegmentInputs();
    // link forward memories
    LinkeMemories();
  }

  virtual void TearDown() override {}

  void CreateGlobalVariables() {
    scope_ = std::make_shared<Scope>();
    // inputs: x
    LOG(INFO) << "create global variable x";
    Variable* x = scope_->CreateVariable("x");
    DDim dims =
        make_ddim({10 /*sent size*/, 20 /*batch size*/, 30 /*input dim*/});
    x->GetMutable<Tensor>()->mutable_data<float>(dims, platform::CPUPlace());
    // inputs: h_boot
    LOG(INFO) << "create global variable h_boot";
    Variable* h_boot = scope_->CreateVariable("h_boot");
    h_boot->GetMutable<Tensor>()->mutable_data<float>(
        make_ddim({20 /*batch size*/, 30 /*input dim*/}), platform::CPUPlace());
    // inputs: w
    LOG(INFO) << "create global variable w";
    Variable* w = scope_->CreateVariable("rnn/w");
    w->GetMutable<Tensor>()->mutable_data<float>(make_ddim({30, 30}),
                                                 platform::CPUPlace());
    // inputs: h_grad
    LOG(INFO) << "create variable h_grad";
    Variable* dh = scope_->CreateVariable("h_grad");
    dh->GetMutable<Tensor>()->mutable_data<float>(make_ddim({10, 20, 30}),
                                                  platform::CPUPlace());
    // inputs: step_scopes
    LOG(INFO) << "create variable step_scopes";
    scope_->CreateVariable("step_scopes");
    // inputs: step_net
    LOG(INFO) << "create variable step_net";
    scope_->CreateVariable("step_net");
    // outputs: w_grad
    LOG(INFO) << "create global variable w_grad";
    scope_->CreateVariable("rnn/w_grad");
    // outputs: x_grad
    LOG(INFO) << "create global variable x_grad";
    scope_->CreateVariable("x_grad");
    // outputs: h_boot_grad
    LOG(INFO) << "create global variable h_boot_grad";
    scope_->CreateVariable("h_boot_grad");
  }

  void CreateStepScopes() {
    std::vector<std::shared_ptr<Scope>>* step_scopes =
        scope_->GetVariable("step_scopes")
            ->GetMutable<std::vector<std::shared_ptr<Scope>>>();
    for (int i = 0; i < 10; ++i) {
      auto scope = std::make_shared<Scope>(scope_);
      auto pre_t = scope->CreateVariable("rnn/pre_h")->GetMutable<Tensor>();
      pre_t->mutable_data<float>(make_ddim({20, 30}), platform::CPUPlace());
      auto tensor = scope->CreateVariable("rnn/h")->GetMutable<Tensor>();
      tensor->mutable_data<float>(make_ddim({20, 30}), platform::CPUPlace());

      // for unit test of ConcatOutputs
      auto xg = scope->CreateVariable("rnn/x_grad")->GetMutable<Tensor>();
      xg->mutable_data<float>(make_ddim({20, 30}), platform::CPUPlace());

      step_scopes->push_back(scope);
    }

    // last time step
    auto g = (*step_scopes)[9]
                 ->CreateVariable("rnn/h_pre_grad")
                 ->GetMutable<Tensor>();
    g->mutable_data<float>(make_ddim({20, 30}), platform::CPUPlace());
  }

  void CreateRNNGradientAlgorithm() {
    std::unique_ptr<rnn::Argument> arg(new rnn::Argument());
    arg->step_net = "step_net";
    arg->step_scopes = "step_scopes";
    rnn::Link inlink;
    inlink.external = "h_grad";
    inlink.internal = "rnn/h_grad";
    arg->inlinks = std::vector<rnn::Link>{inlink};

    rnn::Link outlink;
    outlink.external = "x_grad";
    outlink.internal = "rnn/x_grad";
    arg->outlinks = std::vector<rnn::Link>{outlink};

    rnn::MemoryAttr mem_attr;
    mem_attr.pre_var = "rnn/h_pre_grad";
    mem_attr.var = "rnn/h_grad";
    mem_attr.boot_var = "h_boot_grad";
    arg->memories = std::vector<rnn::MemoryAttr>{mem_attr};

    rnn_grad_algo_.Init(std::move(arg));
  }

  void CreateStepNet() {
    LOG(INFO) << "create variable step_net";
    Variable* var = scope_->CreateVariable("step_net");
    auto net = var->GetMutable<NetOp>();
    net->AddOp(OpRegistry::CreateOp("mul",
                                    {"rnn/h_pre", "rnn/w", "rnn/s_grad"},
                                    {"rnn/h_pre_grad", "rnn/w_grad"},
                                    {}));

    net->AddOp(OpRegistry::CreateOp(
        "add_two", {"rnn/h_grad"}, {"rnn/x_grad", "rnn/s_grad"}, {}));
    net->CompleteAddOp();
  }

  void SegmentInputs() {
    LOG(INFO) << "segment inputs";
    std::vector<std::string> inlinks = {"x"};
    std::vector<std::string> inlinks_alias = {"rnn/x"};

    rnn::Link inlink;
    inlink.external = "x";
    inlink.internal = "rnn/x";
    std::vector<std::shared_ptr<Scope>>* step_scopes =
        scope_->GetVariable("step_scopes")
            ->GetMutable<std::vector<std::shared_ptr<Scope>>>();
    rnn::SegmentInputs(*step_scopes, std::vector<rnn::Link>{inlink}, 10);
  }

  void LinkeMemories() {
    LOG(INFO) << "link memories";
    rnn::MemoryAttr mem_attr;
    mem_attr.pre_var = "rnn/h_pre";
    mem_attr.var = "rnn/h";
    mem_attr.boot_var = "boot_h";
    std::vector<rnn::MemoryAttr> memories;
    memories.push_back(mem_attr);
    std::vector<std::shared_ptr<Scope>>* step_scopes =
        scope_->GetVariable("step_scopes")
            ->GetMutable<std::vector<std::shared_ptr<Scope>>>();
    for (int i = 1; i < 10; ++i) {
      rnn::LinkMemories(*step_scopes, memories, i, -1);
    }
  }

  std::shared_ptr<Scope> scope_;
  RecurrentGradientAlgorithm rnn_grad_algo_;
};

// TEST_F(RecurrentGradientAlgorithmTest, Run) {
//   platform::CPUDeviceContext ctx;
//   rnn_grad_algo_.Run(scope_, ctx);
// }

}  // namespace operators
}  // namespace paddle

TEST(RecurrentOp, LinkMemories) {
  using namespace paddle::framework;
  using namespace paddle::platform;
  using namespace paddle::operators;

  // create and init step scopes
  int len = 10;
  std::vector<std::shared_ptr<Scope>> step_scopes;
  for (int i = 0; i < len; ++i) {
    auto scope = std::make_shared<Scope>();
    scope->CreateVariable("pre_h");
    auto tensor = scope->CreateVariable("h")->GetMutable<Tensor>();
    float* data = tensor->mutable_data<float>(make_ddim({15, 20}), CPUPlace());
    for (int i = 0; i < 15 * 20; ++i) {
      data[i] = rand() * (1. / (double)RAND_MAX);
    }
    step_scopes.push_back(scope);
  }

  // create MemoryAttr
  rnn::MemoryAttr mem_attr;
  mem_attr.pre_var = "pre_h";
  mem_attr.var = "h";
  mem_attr.boot_var = "boot_h";
  std::vector<rnn::MemoryAttr> memories;
  memories.push_back(mem_attr);

  for (int i = 1; i < len; ++i) {
    rnn::LinkMemories(step_scopes, memories, i, -1);
  }
  // check
  for (int i = 0; i < len - 1; ++i) {
    const float* a =
        step_scopes[i]->GetVariable("h")->GetMutable<Tensor>()->data<float>();
    const float* b = step_scopes[i + 1]
                         ->GetVariable("pre_h")
                         ->GetMutable<Tensor>()
                         ->data<float>();
    for (size_t i = 0; i < 15 * 20; ++i) {
      ASSERT_FLOAT_EQ(a[i], b[i]);
    }
  }

  for (int i = len - 2; i >= 0; --i) {
    rnn::LinkMemories(step_scopes, memories, i, 1);
  }
  // check
  for (int i = len - 2; i >= 0; --i) {
    const float* a = step_scopes[i]
                         ->GetVariable("pre_h")
                         ->GetMutable<Tensor>()
                         ->data<float>();
    const float* b = step_scopes[i + 1]
                         ->GetVariable("h")
                         ->GetMutable<Tensor>()
                         ->data<float>();
    for (size_t i = 0; i < 15 * 20; ++i) {
      ASSERT_FLOAT_EQ(a[i], b[i]);
    }
  }
}

USE_OP(add_two);
USE_OP(mul);
