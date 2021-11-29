#pragma once
#include <bit>
#include <cassert>
#include <iostream>
#include "../PolicyFactory.hpp"
#include "../SpSpCommon.hpp"
#include "InternalCommon.hpp"
#include "Size32Cmp.hpp"

namespace SpSpIntl {

using namespace SpSpEnum;
using namespace SpSpPolicyFactory;

constexpr inline BothShuffleControl translate(BothAction bothAct) {
  BothShuffleControl c = {{0, 0}, {0, 0}};
  switch (bothAct.A) {
    case Action::NoPush:
      c.A.ann = 1;
      break;
    case Action::PushDefault:
    case Action::PushLast:
      c.A.ann = 1;
      c.A.gen = 1;
      break;
    case Action::PushThis:
      break;
    default:
      break;
  }
  switch (bothAct.B) {
    case Action::NoPush:
      break;
    case Action::PushDefault:
    case Action::PushLast:
      c.B.gen = 1;
      break;
    case Action::PushThis:
      // Should report error
      break;
    default:
      break;
  }

  return c;
}

constexpr inline BothShuffleControl translate(BothAction bothAct,
                                              OpSrc whoAmI) {
  if (whoAmI == OpSrc::A) {
    return translate(bothAct);
  } else {
    return translate(bothAct.exchangeAB()).exchangeAB();
  }
}

struct SimdCPU {
  uint32_t v, v2;
  uint32_t logV, logV2;

  SimdCPU(uint32_t v) : v(v), v2(2 * v) {
    if (v & (v - 1)) {
      throw("v is not the power of 2.");
    }
    logV = std::bit_width(v) - 1;
    logV2 = logV + 1;
  }

  VecPred makeMask(int start, int end) const {
    VecPred pred;
    pred.assign(v, false);
    for (int i = 0; i < v; ++i) {
      pred[i] = (start + i) < end;
    }
    return pred;
  }

  template <class T>
  Buffer<T> load(VecPred pred, const T *pointer) const {
    static_assert(sizeof(T) == sizeof(Plain));
    Buffer<T> out;
    out.resize(v);
    for (int i = 0; i < v; ++i) {
      if (pred[i]) {
        out[i] = pointer[i];
      }
    }
    return out;
  }

  template <class T>
  void store(VecPred pred, T *pointer, Buffer<T> data) const {
    static_assert(sizeof(T) == sizeof(Plain));
    for (int i = 0; i < v; ++i) {
      if (pred[i]) {
        pointer[i] = data[i];
      }
    }
  }

  template <class T>
  Buffer<T> simd_or(Buffer<T> a, Buffer<T> b) const {
    Buffer<T> c;
    c.resize(v);
    for (int i = 0; i < v; ++i) {
      c[i] = a[i] | b[i];
    }
    return c;
  }
  template <class T>
  Buffer<T> simd_add(Buffer<T> a, Buffer<T> b) const {
    Buffer<T> c;
    c.resize(v);
    for (int i = 0; i < v; ++i) {
      c[i] = a[i] + b[i];
    }
    return c;
  }

  template <class T>
  T lastActiveElem(const VecPred &pred, const Buffer<T> &data,
                   T defaultVal) const {
    T lastActive = defaultVal;
    for (int i = 0; i < v; ++i) {
      if (pred[i]) {
        lastActive = data[i];
      }
    }
    return lastActive;
  }

  BigCmp InitBigCmp(Limit config, VecPred predLeft, VecPred predRight) const {
    BigCmp bigCmp = {};
    bigCmp.logRun = config.logRun;
    bigCmp.validAB_2V = concat(predLeft, predRight);
    bigCmp.swInfo.assign(logV2, Buffer<SwInfo>(v2, SwInfo::Equal));

    bigCmp.opSrc.assign(v2, OpSrc::A);
    for (int i = 0; i < v; ++i) bigCmp.opSrc[i + v] = OpSrc::B;

    bigCmp.delta_2V.assign(v2, Delta::Equal);
    bigCmp.validC_2V.assign(v2, false);

    bigCmp.lastState = config.lastState;
    bigCmp.negate_2V.assign(v2, false);
    return bigCmp;
  }

  BigCmp NextBigCmpFromMatRes(Limit config, MatRes oldMatRes) const {
    auto [predLeft, predRight] = split(oldMatRes.validCMask);
    return InitBigCmp(config, predLeft, predRight);
  }
  BigCmp NextBigCmpFromBigCmp(Limit config, BigCmp oldBigCMp) const {
    auto [predLeft, predRight] = split(oldBigCMp.validC_2V);
    return InitBigCmp(config, predLeft, predRight);
  }

  struct CompareItem {
    std::pair<uint8_t, Plain> data;  // (1-valid,idx)
    std::pair<OpSrc, RegIdx> source;
  };

  template <Size32 T>
  BigCmp KeyCombine(BigCmp oldBigCmp, Buffer<T> idxLeft, Buffer<T> idxRight,
                    CPMethod method) const {
    Buffer<Plain> idxLeftCVT, idxRightCVT;
    idxLeftCVT.resize(v);
    idxRightCVT.resize(v);
    for (int i = 0; i < v; ++i) {
      idxLeftCVT[i] = reinterpret_cast<Plain &>(idxLeft[i]);
      idxRightCVT[i] = reinterpret_cast<Plain &>(idxRight[i]);
    }
    return KeyCombineBase(oldBigCmp, idxLeftCVT, idxRightCVT, method);
  }

  BigCmp KeyCombineBase(BigCmp oldBigCmp, Buffer<Plain> idxLeft,
                        Buffer<Plain> idxRight, CPMethod method) const {
    auto logRun = oldBigCmp.logRun;
    assert(0 <= logRun && logRun <= logV);

    auto newBigCmp = oldBigCmp;

    Buffer<CompareItem> row;
    row.resize(v2);
    for (int i = 0; i < v; ++i) {
      row[i].data.second = idxLeft[i];
      row[i + v].data.second = idxRight[i];
    }
    for (int i = 0; i < v2; ++i) {
      row[i].data.first = 1 - oldBigCmp.validAB_2V[i];
      row[i].data.second = MultiTypeCompare::converte(
          newBigCmp.negate_2V[i], row[i].data.second, method);
      row[i].source.first =
          selectInRun(i, oldBigCmp.logRun, OpSrc::A, OpSrc::B);
      row[i].source.second = i;
    }

    PreSwap(row, logRun);

    int startStage = logV2 - oldBigCmp.logRun - 1;
    for (int st = startStage; st < logV2; ++st) {
      butterflyApply(
          st, row.data(), newBigCmp.swInfo[st].data(),
          [](SwInfo &swinfo, CompareItem &left, CompareItem &right) {
            // Compare
            // Skip comapre when swinfo is already in strong state
            if (swinfo == SwInfo::WLess || swinfo == SwInfo::WGreater) {
              if (left.data < right.data) {
                swinfo = SwInfo::SLess;
              } else if (left.data > right.data) {
                swinfo = SwInfo::SGreater;
              } else if (left.source < right.source) {
                swinfo = SwInfo::WLess;
              } else if (left.source > right.source) {
                swinfo = SwInfo::WGreater;
              }
            }
            // swap
            if (swinfo == SwInfo::WGreater || swinfo == SwInfo::SGreater) {
              std::swap(left, right);
            }
          });
    }

    for (int i = 0; i < v2; ++i) {
      if (i != 0) {
        if (row[i].data != row[i - 1].data) {
          newBigCmp.delta_2V[i] = Delta::NotEqual;
        }
      }
      newBigCmp.opSrc[i] = row[i].source.first;
      newBigCmp.validC_2V[i] = 1 - row[i].data.first;
    }

    return newBigCmp;
  }

  template <class T>
  Buffer<T> butterflyPermute(BigCmp bigCmp, const Buffer<T> &left,
                             const Buffer<T> &right, LRPart part) const {
    auto row = concat(left, right);

    PreSwap(row, bigCmp.logRun);

    int startStage = logV2 - bigCmp.logRun - 1;
    for (int st = startStage; st < logV2; ++st) {
      bufferflySwap(row, st, bigCmp.swInfo[st]);
    }
    auto [outLeft, outRight] = split(row);
    if (part == LRPart::Left) {
      return outLeft;
    } else {
      return outRight;
    }
  }

  MatRes Match(BigCmp bigCmp, uint64_t policyMask1,
               uint64_t policyMask2) const {
    int logRun = bigCmp.logRun;
    int logRun2 = logRun + 1;
    auto isRun2Begin = [=](int i) { return i == ((i >> logRun2) << logRun2); };
    auto isRun2End = [=](int i) {
      return (i + 1) == (((i + 1) >> logRun2) << logRun2);
    };
    ABPair<uint64_t> table = {policyMask1, policyMask2};
    auto policy = [=](Match5Bit ctx) { return lookupPolicy(table, ctx); };

    Buffer<Match5Bit> matchInfo;
    matchInfo.resize(v2);
    for (int i = 0; i < v2; ++i) {
      OpSrc lastSrc =
          isRun2Begin(i) ? bigCmp.lastState.lastSrc : bigCmp.opSrc[i - 1];
      OpSrc thisSrc = bigCmp.opSrc[i];
      OpSrc nextSrc = isRun2End(i) ? OpSrc::A : bigCmp.opSrc[i + 1];
      Delta lastDelta =
          isRun2Begin(i) ? bigCmp.lastState.lastDelta : bigCmp.delta_2V[i];
      Delta nextDelta = isRun2End(i) ? Delta::NotEqual : bigCmp.delta_2V[i + 1];
      matchInfo[i] = {lastSrc, thisSrc, nextSrc, lastDelta, nextDelta};
    }

    Buffer<BothAction> bothAction;
    bothAction.resize(v2);
    for (int i = 0; i < v2; ++i) {
      bothAction[i] = policy(matchInfo[i]);
    }

    Buffer<BothShuffleControl> bothShuffle;
    bothShuffle.resize(v2);
    for (int i = 0; i < v2; ++i) {
      bothShuffle[i] = translate(bothAction[i], matchInfo[i].thisSrc);
      // If this element is invalid, set it to be ann=1
      bothShuffle[i].get(matchInfo[i].thisSrc).ann |= !bigCmp.validC_2V[i];
      // if(!bigCmp.validC_2V[i]){
      //     printf("Stop here!");
      // }
    }

    // Compute the segmented exclusive prefix sum
    // Although we wrote this as "Sequential code", but it can be implemented in
    // hardware in parallel
    Buffer<BothShuffleControl> prefix = bothShuffle;
    BothShuffleControl accum = {{0, 0}, {0, 0}};
    for (int i = 0; i < v2; ++i) {
      if (i == ((i >> logRun2) << logRun2)) {
        // Is the start of a new run2
        accum = {{0, 0}, {0, 0}};
      }
      prefix[i].A.ann = accum.A.ann;
      prefix[i].A.gen = accum.A.gen;
      prefix[i].B.ann = accum.B.ann;
      prefix[i].B.gen = accum.B.gen;
      accum.A.ann += bothShuffle[i].A.ann;
      accum.A.gen += bothShuffle[i].A.gen;
      accum.B.ann += bothShuffle[i].B.ann;
      accum.B.gen += bothShuffle[i].B.gen;
    }
    // Combine and shuffle back
    Buffer<ShuffleControl> combine;
    Buffer<RegIdx> ann, gen;
    VecPred activeAB;
    combine.assign(v2, {0, 0});
    ann.resize(v2);
    gen.resize(v2);
    activeAB.resize(v2);
    for (int i = 0; i < v2; ++i) {
      combine[i] = prefix[i].get(matchInfo[i].thisSrc);
      ann[i] = combine[i].ann;
      gen[i] = combine[i].gen;
      activeAB[i] = bigCmp.validC_2V[i] &&
                    bothAction[i].get(matchInfo[i].thisSrc) == Action::PushThis;
    }

    butterflyBack(logRun, bigCmp.swInfo, ann);
    butterflyBack(logRun, bigCmp.swInfo, gen);
    butterflyBack(logRun, bigCmp.swInfo, activeAB);

    // if(logRun == 0){
    //     for(int i=0; i<v2; ++i){
    //         if(ann[i]){
    //             throw("What the fuck!");
    //         }
    //     }
    // }

    // Make the active mask
    VecPred validCMask;
    validCMask.assign(v2, false);
    for (int run2Id = 0; run2Id < (v2 >> logRun2); ++run2Id) {
      int run2Base = run2Id << logRun2;
      ABPair<uint8_t> pushCount = {0, 0};
      // local reduction ... although in sequential code, it can be parallel
      for (int offset = 0; offset < (1 << logRun2); ++offset) {
        int i = run2Base + offset;
        pushCount.A += isPush(bothAction[i].A) && bigCmp.validC_2V[i];
        pushCount.B += isPush(bothAction[i].B) && bigCmp.validC_2V[i];
      }

      // make mask, broadcast and compare in parallel
      for (int offset = 0; offset < (1 << logRun2); ++offset) {
        int i = run2Base + offset;
        validCMask[i] = offset < pushCount.A && offset < pushCount.B;
      }
    }

    // Make the default mask
    auto makeDefaultMask = [&](OpSrc X) -> VecPred {
      VecPred isDefaultRaw;
      isDefaultRaw.assign(v2, false);
      VecRegIdx shuffleLeftAmount;
      shuffleLeftAmount.assign(v2, 0);
      bool currentIsDefault = false;
      int currentShuffleLeft = 0;
      // segemented scan
      for (int i = 0; i < v2; ++i) {
        if (isRun2Begin(i)) {
          currentIsDefault = false;
          currentShuffleLeft = 0;
        }
        auto act = bothAction[i].get(X);
        shuffleLeftAmount[i] = currentShuffleLeft;
        if (isPush(act) && bigCmp.validC_2V[i]) {
          if (act == Action::PushDefault) {
            currentIsDefault = true;
          } else if (act == Action::PushThis) {
            currentIsDefault = false;
          } else if (act == Action::PushLast) {
            // Do nothing, pass
            // currentIsDefault = currentIsDefault;
          }
          isDefaultRaw[i] = currentIsDefault;
        } else {
          currentShuffleLeft += 1;
        }
      }
      // squeeze negwork, the left shift is (ann-gen) we have hardware to do
      // this in parallel
      VecPred isDefault;
      isDefault.assign(v2, false);
      for (int i = 0; i < v2; ++i) {
        auto act = bothAction[i].get(X);
        if (isPush(act)) {
          isDefault[i - shuffleLeftAmount[i]] = isDefaultRaw[i];
        }
      }
      return isDefault;
    };
    ABPair<VecPred> setDefMask = {makeDefaultMask(OpSrc::A),
                                  makeDefaultMask(OpSrc::B)};

    // // Ajudst for SEPermute
    // if (logRun < logV)
    // {
    //     for (int i = 0; i < v2; ++i)
    //     {
    //         // if(i >= ann.size()) printf("I am wrong!");
    //         ann[i] += selectInRun(i, logRun, 0, 1 << logRun);
    //     }
    //     for (int i = 0; i < v; ++i)
    //     {
    //         gen[i + v] += v;
    //     }
    // }

    return MatRes(logRun, ann, gen, activeAB, setDefMask, validCMask);
  }

  template <class T>
  Buffer<T> SEPermute(MatRes matRes, Buffer<T> source, SEPart part,
                      T defaultVal, T lastVal) const {
    int logRun = matRes.logRun;
    int logRun2 = matRes.logRun + 1;
    bool isSubV = logRun != logV;

    OpSrc who = part.operand;

    auto setDefMaskX = matRes.setDefMask.get(who);
    VecPred extraActiveMask;
    extraActiveMask.resize(v2);
    for (int i = 0; i < v2; ++i) {
      extraActiveMask[i] =
          selectInRun(i, logRun, who == OpSrc::A, who == OpSrc::B);
    }

    LRPart useControlPart;
    if (isSubV) {
      useControlPart =
          (part.lrPart == LRPart::Left) ? LRPart::Left : LRPart::Right;
    } else {
      useControlPart =
          (part.operand == OpSrc::A) ? LRPart::Left : LRPart::Right;
    }

    auto ann = getLRPart(matRes.ann, useControlPart);
    auto gen = getLRPart(matRes.gen, useControlPart);
    auto active = getLRPart(matRes.active, useControlPart);
    auto extraActive = getLRPart(extraActiveMask, useControlPart);

    // Ajudst for SEPermute
    if (logRun < logV) {
      for (int i = 0; i < v; ++i) {
        // if(i >= ann.size()) printf("I am wrong!");
        ann[i] += selectInRun(i, logRun, 0, 1 << logRun);
      }
      if (useControlPart == LRPart::Right) {
        for (int i = 0; i < v; ++i) {
          gen[i] += v;
        }
      }
    }

    Buffer<T> dest;
    dest.assign(v2, lastVal);

    // printf("======================\n");
    // printVector(source);

    // printVector(dest);

    for (int i = 0; i < v; ++i) {
      // See my paper to how this may be implemented in parallel
      if (active[i] && extraActive[i]) {
        int destPos = i - ann[i] + gen[i];
        for (int bd = 0; bd < (1 << logRun); ++bd) {
          int destBDPos = destPos + bd;
          if ((destBDPos >> logRun2) == (destPos >> logRun2)) {
            dest[destBDPos] = source[i];
          }
        }
      }
    }
    // printVector(dest);

    for (int i = 0; i < v2; ++i) {
      if (setDefMaskX[i]) {
        dest[i] = defaultVal;
      }
      if (!matRes.validCMask[i]) {
        dest[i] = 0;
      }
    }
    // printVector(dest);
    // printf("======================\n");

    return getLRPart(dest, part.lrPart);
  }

  Limit GetLimit(BigCmp bigCmp, SimPolicyPack simPolicyMask,
                 GetLimitOp2 op2) const {
    auto forceEqual = op2.forceEqual;
    auto eagerMask = op2.eagerMask;
    auto endCond = op2.endCondition;

    auto simPolicy = [=](Match5Bit ctx) -> BothSimAction {
      return lookupSimPolicy(simPolicyMask, ctx);
    };
    auto eagerPolicy = [=](Match3Bit ctx) -> bool {
      return lookupEager(eagerMask, ctx);
    };

    Buffer<Match5Bit> matchInfo;
    matchInfo.resize(v2);
    for (int i = 0; i < v2; ++i) {
      OpSrc lastSrc =
          (i - 1 < 0) ? bigCmp.lastState.lastSrc : bigCmp.opSrc[i - 1];
      OpSrc thisSrc = bigCmp.opSrc[i];
      OpSrc nextSrc = (i + 1 >= v2) ? OpSrc::A : bigCmp.opSrc[i + 1];
      Delta lastDelta =
          (i - 1 < 0) ? bigCmp.lastState.lastDelta : bigCmp.delta_2V[i];
      Delta nextDelta =
          (i + 1 >= v2) ? Delta::NotEqual : bigCmp.delta_2V[i + 1];
      matchInfo[i] = {lastSrc, thisSrc, nextSrc, lastDelta, nextDelta};
    }

    Buffer<BothSimAction> bothSimAction;
    bothSimAction.resize(v2);
    Buffer<ABPair<uint8_t>> push1;
    Buffer<ABPair<uint8_t>> consumed;
    push1.assign(v2, {0, 0});
    consumed.assign(v2, {0, 0});
    for (int i = 0; i < v2; ++i) {
      bothSimAction[i] = simPolicy(matchInfo[i]);
      push1[i].A = bothSimAction[i].A == SimAction::Push;
      push1[i].B = bothSimAction[i].B == SimAction::Push;
      consumed[i].A = matchInfo[i].thisSrc == OpSrc::A;
      consumed[i].B = matchInfo[i].thisSrc == OpSrc::B;
    }

    // inclusive prefix sum
    Buffer<ABPair<uint8_t>> pushPrefix = push1;
    Buffer<ABPair<uint8_t>> consumedPrefix = consumed;
    VecPred equalLenMask;
    equalLenMask.assign(v2, false);
    for (int i = 1; i < v2; ++i) {
      pushPrefix[i].A += pushPrefix[i - 1].A;
      pushPrefix[i].B += pushPrefix[i - 1].B;
      consumedPrefix[i].A += consumedPrefix[i - 1].A;
      consumedPrefix[i].B += consumedPrefix[i - 1].B;
    }
    for (int i = 0; i < v2; ++i) {
      equalLenMask[i] = pushPrefix[i].A == pushPrefix[i].B;
    }

    auto makeAccepted = [&](OpSrc X) {
      VecPred conservative;
      conservative.assign(v2, false);
      bool active = false;
      // backward prefix sum
      for (int i = v2; i > 0;) {
        --i;
        if (matchInfo[i].thisSrc == X) {
          active = true;
        }
        conservative[i] = active;
      }

      VecPred extended;
      extended.assign(v2, false);
      // forward prefix sum
      for (int i = 0; i < v2; ++i) {
        extended[i] = matchInfo[i].thisSrc == X;
        if (i > 0 && matchInfo[i].lastDelta == Delta::Equal) {
          extended[i] |= extended[i - 1];
        }
      }

      // two above prefix sum for conservative and extended can be done in
      // parallel now, merge their result
      for (int i = 0; i < v2; ++i) {
        extended[i] |= conservative[i];
      }

      VecPred allOK;
      allOK.assign(v2, true);

      bool IAmFull = (X == OpSrc::A) ? bigCmp.validAB_2V[v - 1]
                                     : bigCmp.validAB_2V[v2 - 1];

      auto nextAssum = IAmFull ? endCond.get(X).full : endCond.get(X).partial;

      VecPred accepted;

      if (nextAssum == Next::Same) {
        accepted = conservative;
      } else if (nextAssum == Next::Epsilon) {
        accepted = extended;
      } else {
        // nextAssum == Next::Inf
        accepted = allOK;
      }

      return accepted;
    };

    auto acceptedA = makeAccepted(OpSrc::A);
    auto acceptedB = makeAccepted(OpSrc::B);

    VecPred bothAccepted;  // find the shorter one of A and B
    bothAccepted.resize(v2);
    for (int i = 0; i < v2; ++i) {
      bothAccepted[i] = acceptedA[i] && acceptedB[i];
    }

    // Now, make eager
    VecPred eager;
    eager.assign(v2, false);
    for (int i = 0; i < v2; ++i) {
      eager[i] = eagerPolicy(Match3Bit(
          matchInfo[i].lastSrc, matchInfo[i].thisSrc, matchInfo[i].lastDelta));
    }

    bool AIsFull = bigCmp.validAB_2V[v - 1];
    bool ANextIsInf =
        (AIsFull ? endCond.A.full : endCond.A.partial) == Next::Inf;
    bool BIsFull = bigCmp.validAB_2V[v2 - 1];
    bool BNextIsInf =
        (BIsFull ? endCond.B.full : endCond.B.partial) == Next::Inf;
    bool specialReady = ANextIsInf && BNextIsInf;

    VecPred executable;
    executable.assign(v2, false);
    for (int i = 0; i < v2; ++i) {
      bool nextAccepted = (i + 1 < v2) ? bothAccepted[i + 1] : specialReady;
      executable[i] =
          bothAccepted[i] && (nextAccepted || eager[i]) && bigCmp.validC_2V[i];
    }

    VecPred finalMask;
    finalMask.assign(v2, false);
    if (forceEqual == ForceEq::Yes) {
      for (int i = 0; i < v2; ++i) {
        finalMask[i] = executable[i] && equalLenMask[i];
      }
    } else {
      finalMask = executable;
    }

    // take the last active element, which is a special reduction opeartion
    Limit finalLimit;
    finalLimit.logRun = bigCmp.logRun;
    finalLimit.lastState = bigCmp.lastState;
    finalLimit.generate = {0, 0};
    finalLimit.consume = {0, 0};

    for (int i = 0; i < v2; ++i) {
      if (finalMask[i]) {
        // Don't worry about the nextDelta in eager mode. If in eager mode,
        // nextDelta = Equal or NotEqual must has identical results.
        finalLimit.lastState = {matchInfo[i].thisSrc, matchInfo[i].nextDelta};
        finalLimit.generate = pushPrefix[i];
        finalLimit.consume = consumedPrefix[i];
      }
    }
    // if(finalLimit.consume.A + finalLimit.consume.B == 0)
    //     throw("Wtf?");

    return finalLimit;
  }

 public:
  template <class T>
  Buffer<T> concat(const Buffer<T> &left, const Buffer<T> &right) const {
    Buffer<T> out;
    out.assign(v2, T{});
    for (int i = 0; i < v; ++i) {
      out[i] = left[i];
      out[i + v] = right[i];
    }
    return out;
  }

  template <class T>
  std::pair<Buffer<T>, Buffer<T>> split(const Buffer<T> &both) const {
    Buffer<T> left, right;
    left.resize(v);
    right.resize(v);
    for (int i = 0; i < v; ++i) {
      left[i] = both[i];
      right[i] = both[i + v];
    }
    return {std::move(left), std::move(right)};
  }

  template <class T>
  Buffer<T> getLRPart(const Buffer<T> &both, LRPart part) const {
    Buffer<T> out;
    out.resize(v);
    int offset = part == LRPart::Left ? 0 : v;
    for (int i = 0; i < v; ++i) {
      out[i] = both[i + offset];
    }
    return out;
  }

 private:
  template <class T>
  void PreSwap(Buffer<T> &row, int st) const {
    /*
    The l = logSortedRun-th LSB is the position to decide
    [..1...] = 1 << l
    Given [aabccc], we need transform as follow
    [aa0ccc] -> [aa0ccc]                (e.g. 10[0]101 -> 10[0]101)
    [aa1ccc] -> [aa1ccc] ^ [000111]     (e.g. 10[1]101 -> 10[1]010)
 */

    assert(0 <= st && st <= logV);
    int swapMask = (1 << st) - 1;

    for (int i = 0; i < v2; ++i) {
      int mainBit = (i >> st) & 1;
      int toPos = mainBit ? i : (i ^ swapMask);
      if (i < toPos)  // The first time
      {
        std::swap(row[i], row[toPos]);
      }
    }
  }

  template <class IT_Data, class It_Sw, class Callable>
  void butterflyApply(int st, IT_Data iter_data, It_Sw iter_Sw,
                      Callable fun) const {
    assert(0 <= st && st < logV2);
    int block = v2 >> st;
    int blockNum = 1 << st;
    int distance = block >> 1;
    for (int blockId = 0; blockId < blockNum; ++blockId) {
      int base = block * blockId;
      int swNumBase = (block >> 1) * blockId;
      for (int offset = 0; offset < distance; ++offset) {
        int left = base + offset;
        int right = base + offset + distance;
        int swNum = swNumBase + offset;
        fun(iter_Sw[swNum], iter_data[left], iter_data[right]);
      }
    }
  }
  template <class T>
  void bufferflySwap(Buffer<T> &row, int st,
                     const Buffer<SwInfo> &swinfoRow) const {
    butterflyApply(
        st, row.data(), swinfoRow.data(),
        [](const SwInfo &swinfo, T &left, T &right) {
          if (swinfo == SwInfo::SGreater || swinfo == SwInfo::WGreater) {
            std::swap(left, right);
          }
        });
  }

  template <class T>
  T selectInRun(int i, int logRun, T a, T b) const {
    return ((i >> logRun) & 1) ? b : a;
  }

  template <class T>
  void butterflyBack(int logRun, const Buffer<Buffer<SwInfo>> &swInfo,
                     Buffer<T> &row) const {
    int startStage = logV2 - logRun - 1;
    for (int st = logV2; st > startStage;) {
      --st;
      bufferflySwap(row, st, swInfo[st]);
    }
    PreSwap(row, logRun);
  }

 public:
  template <class T>
  void printVector(const Buffer<T> &vec) const {
    std::cout << "[";
    for (auto x : vec) {
      std::cout << x << ',';
    }
    std::cout << "]\n";
  }
};

};  // namespace SpSpIntl