//===- GatherLower.cpp -------------------------------------------===//
//
//                             The ONNC Project
//
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <onnc/Transforms/TensorSel/Lower.h>
#include <onnc/Transforms/TensorSel/Standards/GatherLower.h>
#include <onnc/IR/Compute/Gather.h>
#include <onnc/IR/IRBuilder.h>

using namespace onnc;

//===----------------------------------------------------------------------===//
// GatherLower
//===----------------------------------------------------------------------===//
GatherLower::GatherLower()
{
}

GatherLower::~GatherLower()
{
}

int GatherLower::isMe(const ::onnx::Node& pNode) const
{
  if (pNode.kind() == ::onnx::Symbol("Gather"))
    return kStdLower;
  return kNotMe;
}

ComputeOperator*
GatherLower::activate(ComputeGraph& pGraph, ::onnx::Node& pNode) const
{
  // check input/output number
  if (pNode.inputs().size() != 2)
    return nullptr;

  if (pNode.outputs().size() != 1)
    return nullptr;

  // check input/output name
  for (::onnx::Value* xv : pNode.inputs()) {
    if (!xv->has_unique_name())
      return nullptr;
  }

  for (::onnx::Value* xv : pNode.outputs()) {
    if (!xv->has_unique_name())
      return nullptr;
  }

  // check default attributes
  

  // create operators
  onnc::Gather* op = pGraph.addOperator<onnc::Gather>();

  // set optional attributes
  if (pNode.hasAttribute(::onnx::Symbol("axis")))
    op->setAxis(pNode.i(::onnx::Symbol("axis")));

  // set input/output
  for (::onnx::Value* xv : pNode.inputs()) {
    onnc::Tensor* tensor = pGraph.getValue<onnc::Tensor>(xv->uniqueName());
    if (nullptr == tensor)
      tensor = IRBuilder::CreateComputeTensor(pGraph, *xv);
    op->addInput(*tensor);
  }

  for (::onnx::Value* xv : pNode.outputs()) {
    onnc::Tensor* tensor = pGraph.getValue<onnc::Tensor>(xv->uniqueName());
    if (nullptr == tensor)
      tensor = IRBuilder::CreateComputeTensor(pGraph, *xv);
    op->addOutput(*tensor);
  }
  return op;
}