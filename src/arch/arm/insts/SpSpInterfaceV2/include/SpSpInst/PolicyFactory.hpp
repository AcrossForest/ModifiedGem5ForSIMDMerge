#pragma once
#include <initializer_list>
#include "EncodeDecode.hpp"
#include "SpSpCommon.hpp"

namespace SpSpPolicyFactory {
using namespace SpSpEnum;
using namespace SpSpEncodeDecode;

constexpr inline bool isPush(Action act) {
  return act == Action::NoPush ? false : true;
}
constexpr inline uint8_t encodeBothSimAction(BothSimAction bothSimAct) {
  uint8_t pack = 0;
  pack |= uint8_t(bothSimAct.A) << 1;
  pack |= uint8_t(bothSimAct.B) << 0;
  return pack;
}

using MatchPolicyFun = BothAction(Match5Bit);
// template<class PolicyFunType = MatchPolicyFun*>
struct PolicyFactory {
  const MatchPolicyFun* policy;
  constexpr PolicyFactory(MatchPolicyFun policy) : policy(policy){};

  constexpr ABPair<uint64_t> policyMask() const {
    ABPair<uint64_t> pack = {0, 0};
    for (int i = 0; i < 32; ++i) {
      BothAction bothAct = policy(Pack<Match5Bit>::decode(i));
      ABPair<uint64_t> shifft = {0, 0};
      shifft.A = uint8_t(bothAct.A) << (2 * (i % 4));
      shifft.B = uint8_t(bothAct.B) << (2 * (i % 4));
      pack.A |= shifft.A << (8 * (i / 4));
      pack.B |= shifft.B << (8 * (i / 4));
    }
    return pack;
  }
  // constexpr static ConstexprMatchPolicy* fullPolicy = policy;

  constexpr BothSimAction simPolicy(Match5Bit ctx) const {
    auto fullPolicy = policy(ctx);
    return {isPush(fullPolicy.A) ? SimAction::Push : SimAction::NoPush,
            isPush(fullPolicy.B) ? SimAction::Push : SimAction::NoPush};
  }

  constexpr uint64_t simPolicyMask() const {
    uint64_t pack = 0;
    for (int i = 0; i < 32; ++i) {
      BothSimAction bothSim = simPolicy(Pack<Match5Bit>::decode(i));
      uint64_t shifft1 = encodeBothSimAction(bothSim) << (2 * (i % 4));
      pack |= shifft1 << (8 * (i / 4));
    }
    return pack;
  }

  constexpr bool eager(Match3Bit ctx) const {
    return eager(ctx.leftSrc, ctx.rightSrc, ctx.delta);
  }
  constexpr bool eager(OpSrc lastSrc, OpSrc thisSrc, Delta lastDelta) const {
    return TestCondition1(lastSrc, thisSrc, lastDelta) &&
           TestCondition2(thisSrc);
  }

  constexpr uint8_t eagerMask() const {
    uint8_t pack = 0;
    for (int i = 0; i < 8; ++i) {
      pack |= uint8_t(eager(Pack<Match3Bit>::decode(i))) << i;
    }
    return pack;
  }

  constexpr bool TestCondition1(Match3Bit ctx) const {
    return TestCondition1(ctx.leftSrc, ctx.rightSrc, ctx.delta);
  }
  constexpr bool TestCondition1(OpSrc lastSrc, OpSrc thisSrc,
                                Delta lastDelta) const {
    bool pass = true;
    auto case1 =
        policy(Match5Bit(lastSrc, thisSrc, OpSrc::A, lastDelta, Delta::Equal));
    for (auto nextSrc : {OpSrc::A, OpSrc::B}) {
      for (auto nextDelta : {Delta::Equal, Delta::NotEqual}) {
        pass &= case1 == policy(Match5Bit(lastSrc, thisSrc, nextSrc, lastDelta,
                                          nextDelta));
      }
    }
    return pass;
  }

  constexpr bool TestCondition2(OpSrc lastSrc) const {
    bool pass = true;
    for (auto thisSrc : {OpSrc::A, OpSrc::B}) {
      for (auto nextSrc : {OpSrc::A, OpSrc::B}) {
        for (auto nextDelta : {Delta::Equal, Delta::NotEqual}) {
          pass &= (policy(Match5Bit(lastSrc, thisSrc, nextSrc, Delta::Equal,
                                    nextDelta)) ==
                   policy(Match5Bit(lastSrc, thisSrc, nextSrc, Delta::NotEqual,
                                    nextDelta)));
        }
      }
    }
    return pass;
  }

  constexpr PolicyStruct dump() const {
    return {policyMask(), simPolicyMask(), eagerMask()};
  }
};

constexpr BothAction lookupPolicy(ABPair<uint64_t> table, Match5Bit ctx) {
  int policyId = pack(ctx);  // ctx.encode();
  return {Action(getBits(table.A, 2 * policyId, 2)),
          Action(getBits(table.B, 2 * policyId, 2))};
}

constexpr BothSimAction lookupSimPolicy(uint64_t table, Match5Bit ctx) {
  auto bits = getBits(table, 2 * pack(ctx), 2);
  return {SimAction(getBits(bits, 1, 1)), SimAction(getBits(bits, 0, 1))};
}

constexpr bool lookupEager(uint8_t table, Match3Bit ctx) {
  return bool(getBits(table, pack(ctx), 1));
}

}  // namespace SpSpPolicyFactory