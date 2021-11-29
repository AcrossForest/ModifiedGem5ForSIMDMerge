#pragma once
#include "../EncodeDecode.hpp"
#include "InternalCommon.hpp"
#include "UserCommon.hpp"

namespace SpSpEncodeDecode {
using SpSpIntl::BigCmp;
using SpSpIntl::Buffer;
using SpSpIntl::MatRes;
using SpSpIntl::SwInfo;
using SpSpUserCommon::VBigCmp;
using SpSpUserCommon::VMatRes;

template <>
struct Pack<SwInfo> : PackEnum<SwInfo, 2> {};

template <>
struct Pack<BigCmp> {
  using ClsType = BigCmp;
  using IntType = VBigCmp;
  static constexpr int BitWidth = 32;

  static inline IntType encode(const ClsType& bigCmp) {
    CPUInfo cpu = CPUInfo(bigCmp.validAB_2V.size() >> 1);
    IntType vecReg;
    vecReg.assign(cpu.v, 0);

    for (int i = 0; i < cpu.v; ++i) {
      uint32_t scalar = 0;
      scalar |= (i < bigCmp.logRun) << 0;
      scalar |= (bigCmp.validAB_2V[i]) << 1;
      scalar |= (bigCmp.validAB_2V[i + cpu.v]) << 2;
      scalar |= (bigCmp.validC_2V[i]) << 3;
      scalar |= (bigCmp.validC_2V[i + cpu.v]) << 4;
      // SwInfo: <5:5+ 7*2 - 1>, but may be terminating ealier
      // SwInfo: <5:18>
      for (int st = 0; st < cpu.logV2; ++st) {
        int bitPos = 5 + st * 2;
        scalar |= pack(bigCmp.swInfo[st][i]) << bitPos;
      }
      scalar |= pack(bigCmp.delta_2V[i]) << 19;
      scalar |= pack(bigCmp.delta_2V[i + cpu.v]) << 20;
      scalar |= pack(bigCmp.opSrc[i]) << 21;
      scalar |= pack(bigCmp.opSrc[i + cpu.v]) << 22;
      if (i == 0) {
        scalar |= pack(bigCmp.lastState.lastSrc) << 23;
        scalar |= pack(bigCmp.lastState.lastDelta) << 24;
      }
      scalar |= (bigCmp.negate_2V[i]) << 25;
      scalar |= (bigCmp.negate_2V[i + cpu.v]) << 26;
      vecReg[i] = scalar;
    }
    return vecReg;
  }

  static inline ClsType decode(const IntType& vecReg) {
    CPUInfo cpu = CPUInfo(vecReg.size());
    ClsType bigCmp;
    bigCmp.logRun = 0;
    bigCmp.validAB_2V.resize(cpu.v2);
    bigCmp.validC_2V.resize(cpu.v2);
    bigCmp.swInfo.resize(cpu.logV2);
    for (auto& s : bigCmp.swInfo) s.resize(cpu.v2);
    bigCmp.delta_2V.resize(cpu.v2);
    bigCmp.opSrc.resize(cpu.v2);
    bigCmp.negate_2V.resize(cpu.v2);
    for (int i = 0; i < cpu.v; ++i) {
      uint32_t scalar = vecReg[i];
      bigCmp.logRun += getBits(scalar, 0, 1);
      bigCmp.validAB_2V[i] = getBits(scalar, 1, 1);
      bigCmp.validAB_2V[i + cpu.v] = getBits(scalar, 2, 1);
      bigCmp.validC_2V[i] = getBits(scalar, 3, 1);
      bigCmp.validC_2V[i + cpu.v] = getBits(scalar, 4, 1);
      for (int st = 0; st < cpu.logV2; ++st) {
        int bitPos = 5 + st * 2;
        bigCmp.swInfo[st][i] = unpack<SwInfo>(getBits(scalar, bitPos, 2));
      }
      bigCmp.delta_2V[i] = unpack<Delta>(getBits(scalar, 19, 1));
      bigCmp.delta_2V[i + cpu.v] = unpack<Delta>(getBits(scalar, 20, 1));
      bigCmp.opSrc[i] = unpack<OpSrc>(getBits(scalar, 21, 1));
      bigCmp.opSrc[i + cpu.v] = unpack<OpSrc>(getBits(scalar, 22, 1));

      bigCmp.negate_2V[i] = getBits(scalar, 25, 1);
      bigCmp.negate_2V[i + cpu.v] = getBits(scalar, 26, 1);
    }

    bigCmp.lastState.lastSrc = unpack<OpSrc>(getBits(vecReg[0], 23, 1));
    bigCmp.lastState.lastDelta = unpack<Delta>(getBits(vecReg[0], 24, 1));

    return bigCmp;
  }
};

template <>
struct Pack<MatRes> {
  using ClsType = MatRes;
  using IntType = VMatRes;
  static constexpr int BitWidth = 32;

  /*
      Because V * 32bit is not enough to encode the MatRes using normal encoding
     method, we need to identify some other encoding strategy for this purpose.
      because ann[0] = 0 is true anyway, so we can use this 6 bit to encode
     logRun.

      As for active[i], if have
       active[i] == ture <=> ann[i+1] = ann[i] for any sub-run2,
      In this way, we can infer active except those i at the end of each run2.
      We only need to finad place to hold the "active" at the end of each
     sub-run2

      But avoid jump of different length, we use simpler approach: we store
     "rotated ann" Rann[i] = ann[i+1] for all i except last i

      Then, we can do packing more simpler
  */

  static inline IntType encode(const ClsType& matRes) {
    CPUInfo cpu = CPUInfo(matRes.ann.size() >> 1);
    IntType vecReg;
    vecReg.assign(cpu.v, 0);

    int logRun = matRes.logRun;
    auto isEndOfRun = [=](int pos) {
      return (pos + 1) == ((pos + 1) >> logRun) << logRun;
    };

    Buffer<uint8_t> Rann;
    Rann.resize(cpu.v2);
    for (int i = 0; i < cpu.v2; ++i) {
      if (isEndOfRun(i)) {
        // Rann[i] = matRes.ann[i] ^ (!matRes.active[i]);
        Rann[i] = (matRes.ann[i] & 1) ^ (!matRes.active[i]);
      } else {
        Rann[i] = matRes.ann[i + 1];
      }
    }
    Rann[cpu.v2 - 1] |= matRes.logRun << 1;

    for (int i = 0; i < cpu.v; ++i) {
      uint32_t scalar = 0;
      scalar |= (Rann[i]) << 0;                        // <0:5>
      scalar |= (Rann[i + cpu.v]) << 6;                // <6:11>
      scalar |= (matRes.gen[i]) << 12;                 // <12:18>
      scalar |= (matRes.gen[i + cpu.v]) << 19;         // <19:25>
      scalar |= (matRes.validCMask[i]) << 26;          // <26>
      scalar |= (matRes.validCMask[i + cpu.v]) << 27;  // <27>
      scalar |= (matRes.setDefMask.A[i]) << 28;
      scalar |= (matRes.setDefMask.A[i + cpu.v]) << 29;
      scalar |= (matRes.setDefMask.B[i]) << 30;
      scalar |= (matRes.setDefMask.B[i + cpu.v]) << 31;
      vecReg[i] = scalar;
    }
    // auto s = decode(vecReg);
    // if(matRes != s){
    //     throw("What the fuck!");
    // }
    return vecReg;
  }

  static inline ClsType decode(const IntType& vecReg) {
    CPUInfo cpu = CPUInfo(vecReg.size());
    ClsType matRes;
    matRes.logRun = 0;
    matRes.ann.assign(cpu.v2, 0);
    matRes.gen.assign(cpu.v2, 0);
    matRes.active.assign(cpu.v2, false);
    matRes.setDefMask.A.assign(cpu.v2, false);
    matRes.setDefMask.B.assign(cpu.v2, false);
    matRes.validCMask.assign(cpu.v2, false);

    Buffer<uint8_t> Rann;
    Rann.assign(cpu.v2, 0);

    for (int i = 0; i < cpu.v; ++i) {
      uint32_t scalar = vecReg[i];
      Rann[i] = getBits(scalar, 0, 6);
      Rann[i + cpu.v] = getBits(scalar, 6, 6);
      matRes.gen[i] = getBits(scalar, 12, 7);
      matRes.gen[i + cpu.v] = getBits(scalar, 19, 7);
      matRes.validCMask[i] = getBits(scalar, 26, 1);
      matRes.validCMask[i + cpu.v] = getBits(scalar, 27, 1);
      matRes.setDefMask.A[i] = getBits(scalar, 28, 1);
      matRes.setDefMask.A[i + cpu.v] = getBits(scalar, 29, 1);
      matRes.setDefMask.B[i] = getBits(scalar, 30, 1);
      matRes.setDefMask.B[i + cpu.v] = getBits(scalar, 31, 1);
    }

    int logRun = Rann[cpu.v2 - 1] >> 1;
    matRes.logRun = logRun;
    // auto isEndOfRun = [=](int pos) {
    //   return (pos + 1) == ((pos + 1) >> logRun) << logRun;
    // };
    auto isBeginOfRun = [=](int i) { return i == ((i >> logRun) << logRun); };
    for (int i = 0; i < cpu.v2; ++i) {
      if (isBeginOfRun(i)) {
        uint8_t implicitAnn = 0;
        // uint8_t implicitAnn = ((i >> cpu.logV) & 1) << cpu.logV;
        matRes.ann[i] = implicitAnn;
        matRes.active[i] = !((Rann[i] & 1) ^ (implicitAnn));
      } else {
        matRes.ann[i] = Rann[i - 1];
        matRes.active[i] = !((Rann[i] & 1) ^ (Rann[i - 1] & 1));
      }
    }
    return matRes;
  }
};

}  // namespace SpSpEncodeDecode