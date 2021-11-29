#pragma once
#include <vector>
#include "../SpSpCommon.hpp"

namespace SpSpIntl {
using namespace SpSpEnum;

template <class T>
using Buffer = std::vector<T>;
using Plain = uint32_t;
using RegIdx = uint32_t;
using IntBool = uint8_t;

using VecPred = Buffer<IntBool>;
using VecRegIdx = Buffer<RegIdx>;

enum class SwInfo {
  Equal = 0,
  WLess = 0,  // Same as Equal, intentional
  WGreater = 1,
  SLess = 2,
  SGreater = 3
};

struct BigCmp {
  int logRun;          // 1bit
  VecPred validAB_2V;  // 2bit
  VecPred validC_2V;   // 2bit

  Buffer<Buffer<SwInfo>> swInfo;  // 14bit
  Buffer<Delta> delta_2V;         // 2bit
  Buffer<OpSrc> opSrc;            // 2bit

  State lastState;  // less than 2bit

  // Added
  VecPred negate_2V;  // 2 bit
  // total 27 bit
  bool operator==(const BigCmp&) const = default;
};

struct MatRes {
  int logRun;      // 0bit ---- (special pos)
  VecRegIdx ann;   // 2 * 6bit
  VecRegIdx gen;   // 2 * 7bit
  VecPred active;  // 0bit ---- (2 * 1bit special pose)

  ABPair<VecPred> setDefMask;  // 2 * 2bit
  VecPred validCMask;          // 2bit
  // total = 2 * (6 + 7 + 1 + 2) = 32 bit
  bool operator==(const MatRes&) const = default;
};

struct ShuffleControl {
  int ann, gen;
};
using BothShuffleControl = ABPair<ShuffleControl>;

};  // namespace SpSpIntl