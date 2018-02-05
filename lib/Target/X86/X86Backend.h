//===- X86Backend.h -------------------------------------------------------===//
//
//                             The ONNC Project
//
// See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef TARGET_X86_X86_BACKEND_H
#define TARGET_X86_X86_BACKEND_H
#include <string>
#include <onnc/Target/NPUTargetBackend.h>

namespace onnc {

class X86Backend : public NPUTargetBackend
{
public:
  X86Backend(const CompilerConfig& pConfig);

  virtual ~X86Backend();
};

class X86_32Backend : public X86Backend
{
public:
  X86_32Backend(const CompilerConfig& pConfig);

  virtual ~X86_32Backend();
};

class X86_64Backend : public X86Backend
{
public:
  X86_64Backend(const CompilerConfig& pConfig);

  virtual ~X86_64Backend();
};

}  // namespace onnc

#endif  // TARGET_SOPHON_SOPHON_BACKEND_H
