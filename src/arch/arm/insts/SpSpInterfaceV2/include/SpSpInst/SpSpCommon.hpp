#pragma once
#include <stdint.h>
#include <bit>
// Following enum and structs should appears here:
// 1. The enum/struct used to be create the control bitmap for instructions
// 2. The structure used to define policies

// InitBigCmp, NextBigCmpFrom xxx
// To create "limit"

namespace SpSpEnum {

template <typename T>
concept Size32 = sizeof(T) == 4;

enum class Delta : uint8_t { Equal = 0, NotEqual = 1 };

enum class OpSrc : uint8_t { A = 0, B = 1 };

template <class T>
struct ABPair {
  T A, B;

  ABPair<T> exchangeAB() const { return {B, A}; }

  T& get(OpSrc part) {
    if (part == OpSrc::A) {
      return A;
    } else {
      return B;
    }
  }

  const T& get(OpSrc part) const {
    if (part == OpSrc::A) {
      return A;
    } else {
      return B;
    }
  }

  friend bool operator==(ABPair<T>, ABPair<T>) = default;

  template <class Callable>
  constexpr auto map(Callable fun) {
    using G = decltype(fun(A));
    return ABPair<G>{fun(A), fun(B)};
  }
};

struct State {
  OpSrc lastSrc;
  Delta lastDelta;

  constexpr bool operator==(const State&) const = default;
};

enum class CPMethod : uint8_t { IsUInt, IsInt, IsFloat, IsFollower };

namespace SpSpCommonHideDetail {
template <Size32 T>
struct DefaultCPMethodStr {};

template <>
struct DefaultCPMethodStr<int32_t> {
  constexpr static CPMethod defaultMethod = CPMethod::IsInt;
};
template <>
struct DefaultCPMethodStr<uint32_t> {
  constexpr static CPMethod defaultMethod = CPMethod::IsUInt;
};
template <>
struct DefaultCPMethodStr<float> {
  constexpr static CPMethod defaultMethod = CPMethod::IsFloat;
};

};  // namespace SpSpCommonHideDetail

template <Size32 T>
inline constexpr CPMethod DefMethod =
    SpSpCommonHideDetail::DefaultCPMethodStr<T>::defaultMethod;

struct Limit {
  uint8_t logRun;
  State lastState;
  ABPair<uint8_t> consume;
  ABPair<uint8_t> generate;

  constexpr bool operator==(const Limit&) const = default;
};

// Match Instruction
using PolicyPack = ABPair<uint64_t>;

// GetLimit, Op2
using SimPolicyPack = uint64_t;
using GetLimitOp2Pack = uint64_t;

using EagerPolicyMask = uint8_t;
enum class LRPart { Left = 0, Right = 1 };

struct SEPart {
  OpSrc operand;
  LRPart lrPart;
};

enum class Next {
  Same = 0,
  Epsilon = 1,
  Inf = 3  // Deliberate use 3 (0b11) instead of 2
};

struct SingleEnd {
  Next full, partial;

  constexpr bool operator==(const SingleEnd& that) const = default;
};

using BothEnd = ABPair<SingleEnd>;

enum class ForceEq { Yes, No };

struct GetLimitOp2 {
  ForceEq forceEqual;
  EagerPolicyMask eagerMask;
  BothEnd endCondition;

  constexpr bool operator==(const GetLimitOp2& that) const = default;
};

// Policy creation

struct Match5Bit {
  OpSrc lastSrc, thisSrc, nextSrc;
  Delta lastDelta, nextDelta;
};

struct Match3Bit {
  OpSrc leftSrc, rightSrc;
  Delta delta;
};

enum class Action { NoPush, PushThis, PushLast, PushDefault };
using BothAction = ABPair<Action>;

enum class SimAction { NoPush, Push };
using BothSimAction = ABPair<SimAction>;

struct CPUInfo {
  uint8_t v, v2, logV, logV2;
  constexpr CPUInfo(uint8_t v) : v(v), v2(2 * v), logV(0), logV2(0) {
    if (v & (v - 1)) {
      throw("v is not the power of 2.");
    }
    logV = std::bit_width(v) - 1;
    logV2 = logV + 1;
  }
};

template <Size32 T>
struct SEPair {
  T def, lastVal;

  constexpr SEPair(T def, T lastVal) : def(def), lastVal(lastVal) {}

  SEPair(uint64_t val) {
    uint32_t defPart = val >> 32;
    uint32_t lastPart = (val << 32) >> 32;
    def = reinterpret_cast<const T&>(defPart);
    lastVal = reinterpret_cast<const T&>(lastPart);
  }

  operator uint64_t() const {
    uint64_t pack = 0;
    pack |= uint64_t(reinterpret_cast<const uint32_t&>(def)) << 32;
    pack |= uint64_t(reinterpret_cast<const uint32_t&>(lastVal)) << 0;
    return pack;
  }
};

struct PolicyStruct {
  ABPair<uint64_t> policyMask;
  uint64_t simPolicyMask;
  uint8_t eagerMask;

  // inline constexpr uint64_t makeGetLimitOp2(ForceEqualGenC forceEq, BothEnd
  // end) const {
  //     return pack(GetLimitOp2(forceEq,eagerMask,end));
  // }
};

}  // namespace SpSpEnum
