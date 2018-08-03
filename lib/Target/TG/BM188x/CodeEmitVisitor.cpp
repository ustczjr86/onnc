//===- CodeEmitVisitor.cpp ------------------------------------------------===//
//
//                             The ONNC Project
//
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "CodeEmitVisitor.h"
#include "Compute/AveragePool.h"
#include "Compute/Concat.h"
#include "Compute/Gemm.h"
#include "Compute/GlobalAveragePool.h"
#include "Compute/LRN.h"
#include "Compute/LeakyRelu.h"
#include "Compute/Load.h"
#include "Compute/MaxPool.h"
#include "Compute/Pool.h"
#include "Compute/PRelu.h"
#include "Compute/Relu.h"
#include "Compute/Scale.h"
#include "Compute/Store.h"
#include "Compute/Sum.h"
#include "Compute/Transpose.h"
#include "Compute/Upsample.h"
#include "TGBackend.h"
#include <onnc/Support/Debug.h>
#include <onnc/Target/TG/BM188x/bmkernel_api.h>

#define DEBUG_TYPE "bm188x_codeemit"

using namespace onnc;
using namespace onnc::BM188X;

#define USE_NEW_CE 0

//===----------------------------------------------------------------------===//
// CodeEmitVisitor
//===----------------------------------------------------------------------===//
void BM188X::CodeEmitVisitor::visit(const BM188X::AveragePool& pOperator)
{
  auto *ifmap = m_TGBackend->getMemOpndByValue(pOperator.getInput(0));
  auto *ofmap = m_TGBackend->getMemOpndByValue(pOperator.getOutput(0));

  const onnc::Tensor* inTensor = pOperator.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = inTensor->dimension(3);

  int kh = pOperator.getKernelShape().vector()[0],
      kw = pOperator.getKernelShape().vector()[1];

  int padt = pOperator.getPads().vector()[0],
      padl = pOperator.getPads().vector()[1],
      padb = pOperator.getPads().vector()[2],
      padr = pOperator.getPads().vector()[3];

  int strh = pOperator.getStrides().vector()[0],
      strw = pOperator.getStrides().vector()[1];

  int enRelu = pOperator.getEnableRelu(),
      rsWidth = pOperator.getRShiftWidth(),
      xq = pOperator.getThresholdXQuantized();

  DEBUG(dbgs()
    << "BM188X::AveragePool" << "\n"
    << "  " << ifmap->start() << " " << ofmap->start()
    << " " << n << " " << c << " " << h << " " << w << " "
    << kh << " " << kw << " "
    << padt << " " << padb << " " << padl << " " << padr << " "
    << strh << " " << strw << " "
    << enRelu << " " << rsWidth << " " << xq << " "
    << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_pooling_fixed_forward_bmkernel(
      m_MemOperands[0]->m_Addr,        // ifmap_gaddr
      m_MemOperands[1]->m_Addr,        // ofmap_gaddr
      bmnet::bmnet_asm::GADDR_INVALID, // index_gaddr
      bmnet::bmnet_asm::GADDR_INVALID, // o_findex_gaddr
      n, c, h, w, kh, kw, padt, padb, padl, padr, strh, strw,
      1,                               // is_avg_pooling
      0.0f,                            // avg_const
      enRelu,                          // do_relu
      rsWidth,                         // right_shift_width
      &xq,                             // threshold_x_quantized
      0                                // ceil_mode
  );
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Concat& pOp)
{
  std::vector<uint64_t> inAddrs(pOp.getNumOfInputs());
  for (unsigned i = 0; i < inAddrs.size(); ++i)
    inAddrs[i] = m_TGBackend->getMemOpndByValue(pOp.getInput(i))->start();

  uint64_t oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();

  const auto& inDims = pOp.getInputDims();
  const auto& oDims = pOp.getOutputDims();

  // int axis = pOp.getAxis();
  int needQuanNum = pOp.getNeedQuantizeNum();
  auto& rsWidth = pOp.getRShiftWidth();
  auto& xq = pOp.getThresholdXQuantized();

  DEBUG(dbgs() << "BM188X::Concat\n";
    dbgs() << "  inputs = ";
    for (auto i : inAddrs) dbgs() << i << " ";
    dbgs() << "\n";
    dbgs() << "  " << oAddr << "\n  ";
    for (auto i : inDims) dbgs() << i << " ";
    dbgs() << "\n  ";
    for (auto i : oDims) dbgs() << i << " ";
    dbgs() << "\n  "
           << needQuanNum << "\n";
    dbgs() << "  rswidth = ";
    for (auto i : rsWidth) dbgs() << i << " ";
    dbgs() << "\n  xq = ";
    for (auto i : xq) dbgs() << i << " ";
    dbgs() << "\n";
  );

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_concat_fixed_forward_bmkernel(
      inAddrs.data(), oAddr,
      const_cast<int *>(inDims.data()), inDims.size(),
      axis,
      oDims.size(), const_cast<int *>(oDims.data()),
      needQuanNum,
      rsWidth,      // right_shift_width
      xq            // threshold_x_quantized
  );
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Conv& pOperator)
{
  /**
  float activation_arg[1] = { 0.0f };

  uint64_t biasAddr = m_DoBias ? m_MemOperands[m_BiasIdx]->m_Addr
                               : bmnet::bmnet_asm::GADDR_INVALID;
  uint64_t scale_addr = m_DoScale ? m_MemOperands[m_ScaleIdx]->m_Addr
                                  : bmnet::bmnet_asm::GADDR_INVALID;
  uint64_t scale_bias_addr = m_DoScaleBias
                                 ? m_MemOperands[m_ScaleBiasIdx]->m_Addr
                                 : bmnet::bmnet_asm::GADDR_INVALID;
  bmnet::bmnet_asm::bmnet_conv_parallel_fixed_forward_bmkernel(
      m_MemOperands[0]->m_Addr,        // ifmap
      m_MemOperands[2]->m_Addr,        // ofmap
      m_MemOperands[1]->m_Addr,        // weight
      biasAddr,                        // bias
      bmnet::bmnet_asm::GADDR_INVALID, // ga_bn_mean,
      bmnet::bmnet_asm::GADDR_INVALID, // ga_bn_variance,
      scale_addr,                      // ga_scale,
      scale_bias_addr,                 // ga_scale_bias,
      m_InN, m_InC, m_InH, m_InW, m_Groups, m_OutC, m_KH, m_KW, m_DilationH,
      m_DilationW, m_PadH, m_PadW, m_StrideH, m_StrideW, false, // result_add
      m_DoBias,                                                 // do_bias,
      0,                                                        // do_bn,
      m_DoScale, m_DoScaleBias, pm::match(m_pNode, pm::mTrueAttr("do_relu")),
      0,                  // bn_scale,
      0,                  // bn_eps,
      0,                  // activation_method,
      activation_arg,     // activation_arg[],
      0,                  // activation_ga_slope,
      0,                  // activation_channel_shared
      0,                  // activation_gt_rshift
      0,                  // activation_gt_rshift
      0,                  // activation_le_scale
      0,                  // activation_le_rshift
      m_RShiftWidth,      // right_shift_width
      0,                  // bn_right_shift_width
      m_ScaleRShiftWidth, // scale_right_shift_width
      0                   // use_winograd
  );
  **/
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Gemm& pOp)
{
  uint64_t inAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(0))->start(),
           wAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(1))->start(),
           bAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(2))->start(),
           oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();
  int inRowN = pOp.getInRowNum(),
      inColN = pOp.getInColNum(),
      outColN = pOp.getOutColNum();
  bool haveBias = pOp.haveBias();
  bool do_activation = pOp.isEnableRelu();
  int activation_method = BM188X::Gemm::RELU;
  bool isWeightTp = pOp.isWeightTp();
  int rsWidth = pOp.getRShiftWidth();

  DEBUG(dbgs()
    << "BM188X::Gemm\n" << "  "
    << inAddr << " " << wAddr << " " << bAddr << " " << oAddr << " "
    << inRowN << " " << inColN << " " << outColN << " "
    << haveBias << " " << do_activation << " " << activation_method
    << isWeightTp << " " << rsWidth << " " << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_fc_fixed_forward_bmkernel(
      inAddr,         // input_data_gaddr
      wAddr           // weight_data_gaddr
      bAddr,          // bias_data_gaddr
      oAddr,          // output_data_gaddr
      inRowN,                          // input_row_num
      inColN,                          // input_col_num
      outColN,                         // weight_col_num
      haveBias,                        // have_bias
      do_activation,                   // do_activation
      activation_method,               // activation_method
      bmnet::bmnet_asm::GADDR_INVALID, // activation_ga_slope
      0,                               // activation_channel_shared
      0,                               // activation_gt_scale
      0,                               // activation_gt_rshift
      0,                               // activation_le_scale
      0,                               // activation_le_rshift
      isWeightTp,                      // weight_transpose
      0,                               // left_shift_width //TODO
      rsWidth                          // right_shift_width
  );
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::GlobalAveragePool& pOp)
{
  uint64_t inAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(0))->start(),
           oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();

  const onnc::Tensor* inTensor = pOp.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = inTensor->dimension(3);

  int enRelu = pOp.getEnableRelu(),
      rsWidth = pOp.getRShiftWidth(),
      xq = pOp.getThresholdXQuantized();

  DEBUG(dbgs()
    << "BM188X::GlobalAveragePool\n" << "  "
    << inAddr << " " << oAddr << " "
    << n << " " << c << " " << h << " " << w << " "
    << enRelu << " " << rsWidth << " " << xq << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_pooling_fixed_forward_bmkernel(
      inAddr,         // ifmap_gaddr
      oAddr,          // ofmap_gaddr
      bmnet::bmnet_asm::GADDR_INVALID, // index_gaddr
      bmnet::bmnet_asm::GADDR_INVALID, // o_findex_gaddr
      n, c, h, w, h, w,
      0, 0, 0, 0, 1, 1,
      1,                // is_avg_pooling
      0.0f,             // avg_const
      enRelu,           // do_relu
      rsWidth,          // right_shift_width
      &xq,              // threshold_x_quantized
      0                 // ceil_mode
  );
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::LRN& pOp)
{
  uint64_t inAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(0))->start(),
           sLutAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(1))->start(),
           pLutAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(2))->start(),
           oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();

  const onnc::Tensor* inTensor = pOp.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = inTensor->dimension(3);

  int size = pOp.getSize(),
      sumRSWidth = pOp.getSumRightShftWidth(),
      lrnRSwidth = pOp.getLrnRightShiftWidth();

  const int* pxq = pOp.getThresholdXQuantized();

  DEBUG(dbgs()
    << "BM188X::LRN\n" << "  "
    << inAddr << " " << oAddr << " " << sLutAddr << " " << pLutAddr << " "
    << n << " " << c << " " << h << " " << w << " "
    << size << " " << sumRSWidth << " " << lrnRSwidth
    << pxq[0] << " " << pxq[1] << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_lrn_fixed_forward_bmkernel(
      inAddr, // input
      oAddr, // output
      sLutAddr, // sqr_lut
      pLutAddr, // power_lut,
      n, c, h, w,
      size, sumRSWidth, lrnRSwidth, pxq);
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::LeakyRelu& pOp)
{
  uint64_t inAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(0))->start(),
           oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();

  int n = pOp.getDims().vector()[0],
      c = pOp.getDims().vector()[1],
      h = pOp.getDims().vector()[2],
      w = pOp.getDims().vector()[3];

  int gtscale = pOp.getGTScale(),
      gtrswidth = pOp.getGTRShiftWidth(),
      lerswidth = pOp.getLERShiftWidth(),
      lescale = pOp.getLEScale();

  DEBUG(dbgs()
    << "BM188X::PRelu\n" << "  "
    << inAddr << " " << oAddr << " "
    << n << " " << c << " " << h << " " << w << " "
    << gtrswidth << " " << lerswidth << " "
    << gtscale << " " << lescale << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_leakyrelu_fixed_forward_bmkernel(
      inAddr,         // input_gaddr
      oAddr,          // output_gaddr
      n, c, h, w,
      gtrswidth,      // GT_right_shift_width
      lerswidth,      // LE_right_shift_width
      gtscale,        // GT_scale
      lescale);       // LE_scale
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Load& pOperator)
{
  int ln = pOperator.getLocalDim().vector()[0],
      lc = pOperator.getLocalDim().vector()[1],
      lh = pOperator.getLocalDim().vector()[2],
      lw = pOperator.getLocalDim().vector()[3];

  int gc = pOperator.getGlobalDim().vector()[0],
      gh = pOperator.getGlobalDim().vector()[1],
      gw = pOperator.getGlobalDim().vector()[2];

  // Calculate the address after Global Memory Allocation Pass
  uint64_t gaddr =
    pOperator.getSrcGOffset() +
    m_TGBackend->getMemOpndByValue(pOperator.getInput(0))->start();

  uint64_t dstladdr = pOperator.getDstLAddr();

  bool doTranspose = pOperator.getDoTranspose(),
       isAligned = pOperator.getIsAligned(),
       isNeuron = pOperator.getIsNeuron();

  const std::string splitName = pOperator.getSplitName();

  DEBUG(dbgs()
    << "BM188X::Load" << "\n"
    << "  " << gaddr << " " << dstladdr << " "
    << ln << "  " << lc << "  " << lh << "  " << lw << "  "
    << gc << "  " << gh << "  " << gw << "  "
    << doTranspose << "  " << isAligned << "  " << isNeuron << "\n");
#if USE_NEW_CE
  bmnet::bmnet_asm::asm_context::get_context().name = splitName;
  // TODO(arcbbb): only support 4d tensor for the moment
  bmnet::bmnet_asm::bmnet_tl_load_stride_bmkernel(
      gaddr,          // Src global addr
      dstladdr,       // Dest local addr
      ln, lc, lh, lw, // Local N C H W
      gc, gh, gw,     // Global C H W
      doTranspose,    // Do Transpose
      isAligned,      // Check alignment
      isNeuron        // MemSpace: Neuron or Weight
  );
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::MaxPool& pOperator)
{
  auto *ifmap = m_TGBackend->getMemOpndByValue(pOperator.getInput(0));
  auto *ofmap = m_TGBackend->getMemOpndByValue(pOperator.getOutput(0));

  const onnc::Tensor* inTensor = pOperator.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = inTensor->dimension(3);

  int kh = pOperator.getKernelShape().vector()[0],
      kw = pOperator.getKernelShape().vector()[1];

  int padt = pOperator.getPads().vector()[0],
      padl = pOperator.getPads().vector()[1],
      padb = pOperator.getPads().vector()[2],
      padr = pOperator.getPads().vector()[3];

  int strh = pOperator.getStrides().vector()[0],
      strw = pOperator.getStrides().vector()[1];

  int rsWidth = pOperator.getRShiftWidth(),
      xq = pOperator.getThresholdXQuantized();

  DEBUG(dbgs()
    << "BM188X::MaxPool" << "\n"
    << "  " << ifmap->start() << " " << ofmap->start()
    << " " << n << " " << c << " " << h << " " << w << " "
    << kh << " " << kw << " "
    << padt << " " << padb << " " << padl << " " << padr << " "
    << strh << " " << strw << " "
    << rsWidth << " " << xq << " "
    << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_pooling_fixed_forward_bmkernel(
      ifmap->start(),         // ifmap_gaddr
      ofmap->start(),         // ofmap_gaddr
      bmnet::bmnet_asm::GADDR_INVALID, // index_gaddr
      bmnet::bmnet_asm::GADDR_INVALID, // o_findex_gaddr
      n, c, h, w, kh, kw, padt, padb, padl, padr, strh, strw,
      0,                      // is_avg_pooling
      0.0f,                   // avg_const
      0,                      // do_relu
      rsWidth,                // right_shift_width
      &xq,                    // threshold_x_quantized
      0                       // ceil_mode
  );
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::PRelu& pOp)
{
  uint64_t inAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(0))->start(),
           slopeAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(1))->start(),
           oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();
  bool channelShared = pOp.getChannelShared();
  float slope = pOp.getSlope();

  int n = pOp.getDims().vector()[0],
      c = pOp.getDims().vector()[1],
      h = pOp.getDims().vector()[2],
      w = pOp.getDims().vector()[3];

  int gtscale = pOp.getGTScale(),
      gtrswidth = pOp.getGTRShiftWidth(),
      lerswidth = pOp.getLERShiftWidth();

  DEBUG(dbgs()
    << "BM188X::PRelu\n" << "  "
    << inAddr << " " << slopeAddr << " " << oAddr << " "
    << channelShared << slope
    << n << " " << c << " " << h << " " << w << " "
    << gtscale << " " << gtrswidth << " " << lerswidth << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_prelu_fixed_forward_bmkernel(
      inAddr,         // input_gaddr
      slopeAddr,      // slope_gaddr
      oAddr,          // output_gaddr
      channelShared,  // channel_shared
      slope,          // slope
      n, c, h, w,
      gtscale,        // GT_scale
      gtrswidth,      // GT_right_shift_width
      lerswidth);     // LE_right_shift_width
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Pool& pOp)
{
  const StringAttr& splitName = pOp.getSplitName();
  uint64_t inAddr = pOp.getIFmapAddr(),
           oAddr = pOp.getOFmapAddr();
  int in = pOp.getInDim().vector()[0],
      ic = pOp.getInDim().vector()[1],
      ih = pOp.getInDim().vector()[2],
      iw = pOp.getInDim().vector()[3];

  int on = pOp.getOutDim().vector()[0],
      oc = pOp.getOutDim().vector()[1],
      oh = pOp.getOutDim().vector()[2],
      ow = pOp.getOutDim().vector()[3];

  int kh = pOp.getKernelShape().vector()[0],
      kw = pOp.getKernelShape().vector()[1];

  int padt = pOp.getSlidePads().vector()[0],
      padl = pOp.getSlidePads().vector()[1],
      padb = pOp.getSlidePads().vector()[2],
      padr = pOp.getSlidePads().vector()[3];

  int strh = pOp.getStrides().vector()[0],
      strw = pOp.getStrides().vector()[1];

  bool isavgpooling = pOp.getIsAvgPooling();
  int rswidth = pOp.getRShiftWidth(),
      xq = pOp.getThresholdXQuantized();

  DEBUG(dbgs()
    << "BM188X::Pool\n" << "  " << splitName << " "
    << inAddr << " " << oAddr << " "
    << in << " " << ic << " " << ih << " " << iw << " "
    << on << " " << oc << " " << oh << " " << ow << " "
    << kh << " " << kw << " "
    << strh << " " << strw << " "
    << padt << " " << padb << " " << padl << " " << padr << " "
    << isavgpooling << " " << rswidth << " " << xq << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::asm_context::get_context().name = splitName;
  bmnet::bmnet_asm::bmnet_tl_pooling_forward_bmkernel(
      inAddr, // ifmap
      oAddr, // ofmap
      in, ic, ih, iw, on, oc, oh, ow, kh, kw,
      strh, strw,              // stride
      padt, padb, padl, padr,  // padding
      isavgpooling,          // is_avg_pooling
      rswidth, xq);
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Relu& pOp)
{
  uint64_t inAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(0))->start(),
           oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();
  float nslope = pOp.getNegativeSlope();
  int n = pOp.getDims().vector()[0],
      c = pOp.getDims().vector()[1],
      h = pOp.getDims().vector()[2],
      w = pOp.getDims().vector()[3];

  DEBUG(dbgs()
    << "BM188X::Relu\n" << "  "
    << inAddr << " " << oAddr << " "
    << n << " " << c << " " << h << " " << w << " " << nslope << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_relu_fixed_forward_bmkernel(
      inAddr,     // input_gaddr
      oAddr,      // output_gaddr
      nslope,     // negative_slope
      n, c, h, w);
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Scale& pOp)
{
  const onnc::Tensor* inTensor = pOp.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = inTensor->dimension(3);
  int rswidth = pOp.getRShiftWidth();
  int scaleDim = c;
  int minnerDim = h * w;

  uint64_t inAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(0))->start();
  uint64_t scaleAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(1))->start();
  uint64_t biasAddr = m_TGBackend->getMemOpndByValue(pOp.getInput(2))->start();
  uint64_t oAddr = m_TGBackend->getMemOpndByValue(pOp.getOutput(0))->start();

  DEBUG(dbgs()
    << "BM188X::Scale\n" << "  "
    << inAddr << " " << scaleAddr << " " << biasAddr << " " << oAddr << " "
    << n << " " << c << " " << h << " " << w << " "
    << scaleDim << "  " << minnerDim << "  " << rswidth << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_scale_fixed_forward_bmkernel(
      inAddr,     // input
      scaleAddr,  // scale
      biasAddr,   // bias
      oAddr,      // output
      n, c, h, w, scaleDim, minnerDim, rswidth);
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Store& pOperator)
{
  int ln = pOperator.getLocalDim().vector()[0],
      lc = pOperator.getLocalDim().vector()[1],
      lh = pOperator.getLocalDim().vector()[2],
      lw = pOperator.getLocalDim().vector()[3];

  int gc = pOperator.getGlobalDim().vector()[0],
      gh = pOperator.getGlobalDim().vector()[1],
      gw = pOperator.getGlobalDim().vector()[2];

  // Calculate the address after Global Memory Allocation Pass
  uint64_t gaddr =
    pOperator.getDstGOffset() +
    m_TGBackend->getMemOpndByValue(pOperator.getInput(0))->start();

  uint64_t srcladdr = pOperator.getSrcLAddr();

  bool doTranspose = pOperator.getDoTranspose(),
       isAligned = pOperator.getIsAligned(),
       isNeuron = pOperator.getIsNeuron();

  const std::string splitName = pOperator.getSplitName();

  DEBUG(dbgs()
    << "BM188X::Store" << "\n"
    << "  " << gaddr << " " << srcladdr << " "
    << ln << "  " << lc << "  " << lh << "  " << lw << "  "
    << gc << "  " << gh << "  " << gw << "  "
    << doTranspose << "  " << isAligned << "  " << isNeuron << "\n");
#if USE_NEW_CE
  bmnet::bmnet_asm::asm_context::get_context().name = splitName;
  // TODO(arcbbb): only support 4d tensor for the moment
  bmnet::bmnet_asm::bmnet_tl_load_stride_bmkernel(
      gaddr,          // Src global addr
      srcladdr,       // Dest local addr
      ln, lc, lh, lw, // Local N C H W
      gc, gh, gw,     // Global C H W
      doTranspose,    // Do Transpose
      isAligned,      // Check alignment
      isNeuron        // MemSpace: Neuron or Weight
  );
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Sum& pOperator)
{
  std::vector<uint64_t> inAddrs(pOperator.getNumOfInputs());
  for (unsigned i = 0; i < inAddrs.size(); ++i)
    inAddrs[i] = m_TGBackend->getMemOpndByValue(pOperator.getInput(i))->start();

  auto *ofmap = m_TGBackend->getMemOpndByValue(pOperator.getOutput(0));
  const onnc::Tensor* inTensor = pOperator.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = inTensor->dimension(3);
  bool doRelu = pOperator.getDoRelu();
  int rswidth = pOperator.getRShiftWidth();
  const std::vector<int>& xq = pOperator.getThresholdXQuantized();

  DEBUG(dbgs() << "BM188X::Sum\n";
    dbgs() << "  inputs = ";
    for (auto i : inAddrs) dbgs() << i << " ";
    dbgs() << "\n";
    dbgs() << "  " << ofmap->start()
           << n << " " << c << " " << h << " " << w << " "
           << doRelu << " " << rswidth << "\n";
    dbgs() << "  xq = ";
    for (auto i : xq) dbgs() << i << " ";
    dbgs() << "\n";
  );

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_eltwise_fixed_forward_bmkernel(
      inAddrs.data(), // inputs
      ofmap->start(), // ouput
      inAddrs.size(),
      1,              // op: SUM
      n, c, h, w,
      doRelu,         // do_relu
      0.0,            // relu_slope,
      rswidth,        // right_shift_width
      xq.data());
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Transpose& pOperator)
{
  auto *ifmap = m_TGBackend->getMemOpndByValue(pOperator.getInput(0));
  auto *ofmap = m_TGBackend->getMemOpndByValue(pOperator.getOutput(0));
  const onnc::Tensor* inTensor = pOperator.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = pOperator.getCorrectW();
  const std::vector<int>& order = pOperator.getOrder(),
                          oshape = pOperator.getOutputShape();
  bool needPerm = pOperator.needPermute();
  DEBUG(dbgs()
    << "BM188X::Transpose\n" << "  "
    << ifmap->start() << " " << ofmap->start() << " "
    << n << " " << c << " " << h << " " << w << " "
    << oshape[0] << " " << oshape[1] << " "
    << oshape[2] << " " << oshape[3] << " "
    << order[0] << " " << order[1] << " " << order[2] << " " << order[3] << " "
    << needPerm << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_permute_fixed_forward_bmkernel(
      ifmap->start(), ofmap->start(),
       n, c, h, w,
      oshape[0], oshape[1], oshape[2], oshape[3],
      order[0], order[1], order[2], order[3], needPerm);
#endif
}

void BM188X::CodeEmitVisitor::visit(const BM188X::Upsample& pOperator)
{
  auto *ifmap = m_TGBackend->getMemOpndByValue(pOperator.getInput(0));
  auto *ofmap = m_TGBackend->getMemOpndByValue(pOperator.getOutput(0));

  const onnc::Tensor* inTensor = pOperator.getInput(0);
  int n = inTensor->dimension(0),
      c = inTensor->dimension(1),
      h = inTensor->dimension(2),
      w = inTensor->dimension(3);
  int scale = pOperator.getScale();

  DEBUG(dbgs()
    << "BM188X::Upsample\n" << "  "
    << ifmap->start() << " " << ofmap->start() << " "
    << n << " " << c << " " << h << " " << w << " " << scale << "\n");

#if USE_NEW_CE
  bmnet::bmnet_asm::bmnet_upsample_fixed_bmkernel(
      ifmap->start(), // ifmap_gaddr
      ofmap->start(), // ofmap_gaddr
      n, c, h, w, scale);
#endif
}
