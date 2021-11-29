#pragma once
#include <bit>
#include "EncodeDecode.hpp"
#include "SpSpCommon.hpp"
#include "simInst/CPUImpl.hpp"
#include "simInst/SimEncode.hpp"
#include "simInst/UserCommon.hpp"

namespace SpSpGem5 {
using namespace SpSpEnum;
using namespace SpSpUserCommon;
using SpSpEncodeDecode::checkPackIdentical;
using SpSpEncodeDecode::pack;
using SpSpEncodeDecode::unpack;
using SpSpIntl::SimdCPU;
using SpSpPolicyFactory::PolicyFactory;

struct WrapperCPU {
  const SimdCPU cpuImpl;
  WrapperCPU(int VecLen) : cpuImpl(VecLen) {}

  inline VecBool whilelt(const ScaIdx start, const ScaIdx end) {
    return cpuImpl.makeMask(start, end);
  }

  template <class T>
  inline VReg<T> load(const VecBool& pred, const T* base) {
    return cpuImpl.load(pred, base);
  }

  template <class T>
  inline void store(const VecBool& pred, T* base, const VReg<T>& data) {
    return cpuImpl.store(pred, base, data);
  }

  template <class T>
  inline T lastActiveElem(const VecBool& pred, const VReg<T>& data,
                          T defaultVal) {
    return cpuImpl.lastActiveElem(pred, data, defaultVal);
  }

  template <class T>
  inline VReg<T> simd_or(const VecBool& pred, const VReg<T>& op1,
                           const VReg<T>& op2) {
    VReg<T> out(cpuImpl.v, 0);
    for (int i = 0; i < cpuImpl.v; ++i) {
      if (pred[i]) {
        out[i] = op1[i] | op2[i];
      }
    }
    return out;
  }
  template <class T>
  inline VReg<T> simd_add(const VecBool& pred, const VReg<T>& op1,
                            const VReg<T>& op2) {
    VReg<T> out(cpuImpl.v, 0);
    for (int i = 0; i < cpuImpl.v; ++i) {
      if (pred[i]) {
        out[i] = op1[i] + op2[i];
      }
    }
    return out;
  }
  template <class T>
  inline VReg<T> simd_mul(const VecBool& pred, const VReg<T>& op1,
                            const VReg<T>& op2) {
    VReg<T> out(cpuImpl.v, 0);
    for (int i = 0; i < cpuImpl.v; ++i) {
      if (pred[i]) {
        out[i] = op1[i] * op2[i];
      }
    }
    return out;
  }

  inline VBigCmp InitBigCmp(uint64_t limit, const VecPred& predLeft,
                            const VecPred& predRight) {
    Limit limitStr = unpack<Limit>(limit);
    SpSpIntl::BigCmp bigCmp = cpuImpl.InitBigCmp(limitStr, predLeft, predRight);
    SpSpEncodeDecode::checkPackIdentical(bigCmp);
    return pack(bigCmp);
    // return cpuImpl.InitBigCmp()
  }

  inline VBigCmp NextBigCmpFromMatRes(uint64_t limit,
                                      const VMatRes& oldMatRes) {
    SpSpIntl::BigCmp outBigCmp = cpuImpl.NextBigCmpFromMatRes(
        unpack<Limit>(limit), unpack<SpSpIntl::MatRes>(oldMatRes));
    SpSpEncodeDecode::checkPackIdentical(outBigCmp);
    return pack(outBigCmp);
    // return pack(cpuImpl.NextBigCmpFromMatRes(
    //     unpack<Limit>(limit),
    //     unpack<SpSpIntl::MatRes>(oldMatRes)
    // ));
  }

  inline VBigCmp NextBigCmpFromBigCmp(uint64_t limit,
                                      const VBigCmp& oldBigCmp) {
    SpSpIntl::BigCmp outBigCmp = cpuImpl.NextBigCmpFromBigCmp(
        unpack<Limit>(limit), unpack<SpSpIntl::BigCmp>(oldBigCmp));
    SpSpEncodeDecode::checkPackIdentical(outBigCmp);
    return pack(outBigCmp);
    // return pack(cpuImpl.NextBigCmpFromBigCmp(
    //     unpack<Limit>(limit),
    //     unpack<SpSpIntl::BigCmp>(oldBigCmp)
    // ));
  }


  template <class T>
  inline VBigCmp KeyCombine(const VBigCmp& oldBigCmp,
                                const VReg<T>& idxLeft,
                                const VReg<T>& idxRight, uint8_t imm) {
    SpSpIntl::BigCmp outBigCmp = cpuImpl.KeyCombine(
        unpack<SpSpIntl::BigCmp>(oldBigCmp), idxLeft, idxRight, unpack<CPMethod>(imm));
    SpSpEncodeDecode::checkPackIdentical(outBigCmp);
    return pack(outBigCmp);
  }


  template <class T>
  inline VReg<T> BFPermute(const VBigCmp& oldBigCmp, const VReg<T>& srcLeft,
                             const VReg<T>& srcRight,uint8_t imm) {
    return cpuImpl.butterflyPermute(unpack<SpSpIntl::BigCmp>(oldBigCmp),
                                    srcLeft, srcRight, unpack<LRPart>(imm));
  }

  inline VMatRes Match(VBigCmp bigCmp, uint64_t policyMask1,
                       uint64_t policyMask2) {
    SpSpIntl::MatRes outMatRes = cpuImpl.Match(unpack<SpSpIntl::BigCmp>(bigCmp),
                                               policyMask1, policyMask2);
    SpSpEncodeDecode::checkPackIdentical(outMatRes);
    return pack(outMatRes);

    // return pack(cpuImpl.Match(
    //     unpack<SpSpIntl::BigCmp>(bigCmp),
    //     policyMask1,policyMask2
    // ));
  }

  template <class T>
  inline VReg<T> SEPermute(VMatRes matRes, VReg<T> source,
                             uint64_t defaultAndLastVal,uint8_t imm) {
    static_assert(sizeof(T) == 4);

    SEPair<T> pair(defaultAndLastVal);

    T defaultVal = pair.def;
    T lastVal = pair.lastVal;

    return cpuImpl.SEPermute(unpack<SpSpIntl::MatRes>(matRes), source, unpack<SEPart>(imm),
                             defaultVal, lastVal);
  }

  inline uint64_t GetLimit(VBigCmp bigCmp, uint64_t simPolicyMask,
                           uint64_t op2) {
    Limit outLimit = cpuImpl.GetLimit(unpack<SpSpIntl::BigCmp>(bigCmp),
                                      simPolicyMask, unpack<GetLimitOp2>(op2));
    return pack(outLimit);

    // return pack(cpuImpl.getLimit(
    //     unpack<SpSpIntl::BigCmp>(bigCmp),
    //     simPolicyMask,
    //     unpack<GetLimitOp2>(op2)
    // ));
  }
};

};  // namespace SpSpGem5