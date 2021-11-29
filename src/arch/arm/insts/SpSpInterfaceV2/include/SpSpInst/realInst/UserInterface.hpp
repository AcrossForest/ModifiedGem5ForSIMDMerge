#pragma once
#include <arm_sve.h>
#include <bit>
#include "../EncodeDecode.hpp"
#include "../SpSpCommon.hpp"

namespace SpSpInst {
using namespace SpSpEnum;
using SpSpEncodeDecode::pack;
using SpSpEncodeDecode::unpack;
using SpSpPolicyFactory::PolicyFactory;

using VecIdx = svuint32_t;
using VecInt = svint32_t;
using VecFlt = svfloat32_t;
using VecBool = svbool_t;
using VecPred = svbool_t;

using ScaIdx = uint32_t;
using ScaInt = int32_t;
using ScaFlt = float32_t;

namespace HideDetail {
template <class T>
struct RegVecTypeStruct {
  using VecType = void;
};
template <>
struct RegVecTypeStruct<ScaIdx> {
  using VecType = VecIdx;
};
template <>
struct RegVecTypeStruct<ScaInt> {
  using VecType = VecInt;
};
template <>
struct RegVecTypeStruct<float> {
  using VecType = VecFlt;
};
template <>
struct RegVecTypeStruct<bool> {
  using VecType = VecBool;
};

template <class VecType>
struct ScaTypeStruct {
  using ScaType = void;
};
template <>
struct ScaTypeStruct<VecIdx> {
  using ScaType = ScaIdx;
};
template <>
struct ScaTypeStruct<VecFlt> {
  using ScaType = ScaFlt;
};
template <>
struct ScaTypeStruct<VecInt> {
  using ScaType = ScaInt;
};
};  // namespace HideDetail

template <class T>
using VReg = HideDetail::RegVecTypeStruct<T>::VecType;

template <class T>
using GetScaType = HideDetail::ScaTypeStruct<T>::ScaType;

using VBigCmp = VReg<uint32_t>;
using VMatRes = VReg<uint32_t>;

inline VecBool whilelt(ScaIdx start, ScaIdx end) {
  return svwhilelt_b32(start, end);
}

inline VecBool ptrue(){
  return svptrue_b32();
}

template <class VecType>
inline VecType simd_or(VecBool pred, VecType op1, VecType op2) {
  return svorr_x(pred, op1, op2);
}

template <class VecType>
inline VecType simd_add(VecBool pred, VecType op1, VecType op2) {
  return svadd_x(pred, op1, op2);
}

template <class VecType>
inline VecType simd_mul(VecBool pred, VecType op1, VecType op2) {
  return svmul_x(pred, op1, op2);
}

inline const CPUInfo cpu = CPUInfo(svcntw());

template <class ScaType>
inline VReg<ScaType> load(VecBool pred, const ScaType* base) {
  return svld1(pred, base);
}
template <class ScaType>
inline void store(VecBool pred, ScaType* base, VReg<ScaType> data) {
  svst1(pred, base, data);
}

inline int vectorLen(){
  return svcntw();
}

template <class ScaType>
inline ScaType lastActiveElem(const VecBool& pred, const VReg<ScaType>& data,
                              ScaType defaultVal) {
  return svclastb(pred, defaultVal, data);
}

inline VBigCmp InitBigCmp(uint64_t limit, const VecPred& predLeft,
                          const VecPred& predRight) {
  return svSpSpInitBigCmp(limit, predLeft, predRight);
}

inline VBigCmp NextBigCmpFromMatRes(uint64_t limit, const VMatRes& oldMatRes) {
  return svSpSpInitBigCmpFromMatRes(limit, oldMatRes);
}

// inline VBigCmp NextBigCmpFromBigCmp(uint64_t limit, const VBigCmp&
// oldBigCmp){
// }

template <CPMethod method = DefMethod<ScaIdx>>
inline VBigCmp KeyCombine(VBigCmp oldBigCmp, VReg<ScaIdx> idxLeft,
                          VReg<ScaIdx> idxRight) {
  constexpr auto xx = uint64_t(pack(method));
  return svSpSpKeyCombine_u32(oldBigCmp, idxLeft, idxRight, xx);
}

template <CPMethod method = DefMethod<ScaInt>>
inline VBigCmp KeyCombine(VBigCmp oldBigCmp, VReg<ScaInt> idxLeft,
                          VReg<ScaInt> idxRight) {
  constexpr auto xx = uint64_t(pack(method));
  return svSpSpKeyCombine_s32(oldBigCmp, idxLeft, idxRight, xx);
}

template <CPMethod method = DefMethod<ScaFlt>>
inline VBigCmp KeyCombine(VBigCmp oldBigCmp, VReg<ScaFlt> idxLeft,
                          VReg<ScaFlt> idxRight) {
  constexpr auto xx = uint64_t(pack(method));
  return svSpSpKeyCombine_f32(oldBigCmp, idxLeft, idxRight, xx);
}

namespace HideDetail {
template <LRPart lr>
inline VecInt BFPermuteImpl(VBigCmp oldBigCmp, VecInt srcLeft,
                            VecInt srcRight) {
  constexpr auto xx = uint64_t(pack(lr));
  return svSpSpBFPermute_s32(oldBigCmp, srcLeft, srcRight, xx);
}
template <LRPart lr>
inline VecIdx BFPermuteImpl(VBigCmp oldBigCmp, VecIdx srcLeft,
                            VecIdx srcRight) {
  constexpr auto xx = uint64_t(pack(lr));
  return svSpSpBFPermute_u32(oldBigCmp, srcLeft, srcRight, xx);
}
template <LRPart lr>
inline VecFlt BFPermuteImpl(VBigCmp oldBigCmp, VecFlt srcLeft,
                            VecFlt srcRight) {
  constexpr auto xx = uint64_t(pack(lr));
  return svSpSpBFPermute_f32(oldBigCmp, srcLeft, srcRight, xx);
}
};  // namespace HideDetail

template <LRPart lr, class VecType>
inline VecType BFPermute(VBigCmp oldBigCmp, VecType srcLeft, VecType srcRight) {
  return HideDetail::BFPermuteImpl<lr>(oldBigCmp, srcLeft, srcRight);
}

inline VMatRes Match(VBigCmp bigCmp, uint64_t policyMask1,
                     uint64_t policyMask2) {
  return svSpSpMatch(bigCmp, policyMask1, policyMask2);
}

namespace HideDetail {
template <SEPart part>
inline VecInt SEPermuteImpl(VMatRes matRes, VecInt source,
                            uint64_t defaultAndLastVal) {
  constexpr auto xx = pack(part);
  return svSpSpSEPermute_s32(matRes, source, defaultAndLastVal, xx);
}
template <SEPart part>
inline VecIdx SEPermuteImpl(VMatRes matRes, VecIdx source,
                            uint64_t defaultAndLastVal) {
  constexpr auto xx = pack(part);
  return svSpSpSEPermute_u32(matRes, source, defaultAndLastVal, xx);
}
template <SEPart part>
inline VecFlt SEPermuteImpl(VMatRes matRes, VecFlt source,
                            uint64_t defaultAndLastVal) {
  constexpr auto xx = pack(part);
  return svSpSpSEPermute_f32(matRes, source, defaultAndLastVal, xx);
}
};  // namespace HideDetail

template <SEPart part, class VecType>
inline VecType SEPermute(VMatRes matRes, VecType source,
                         uint64_t defaultAndLastVal) {
  return HideDetail::SEPermuteImpl<part>(matRes, source, defaultAndLastVal);
}

inline uint64_t GetLimit(VBigCmp bigCmp, uint64_t simPolicyMask, uint64_t op2) {
  return svSpSpGetLimit(bigCmp, simPolicyMask, op2);
}

};  // namespace SpSpInst