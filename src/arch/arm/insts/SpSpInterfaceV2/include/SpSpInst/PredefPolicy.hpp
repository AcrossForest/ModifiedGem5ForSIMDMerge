#pragma once
#include "PolicyFactory.hpp"
#include "SpSpCommon.hpp"

namespace SpSPPredefPolicy {
using namespace SpSpEnum;

constexpr inline BothAction UniqueOR(Match5Bit ctx) {
  BothAction act = {Action::NoPush, Action::NoPush};
  if (ctx.thisSrc == OpSrc::A) {
    bool match = ctx.nextSrc == OpSrc::B && ctx.nextDelta == Delta::Equal;
    if (match) {
      act = {Action::PushThis, Action::NoPush};
    } else {
      act = {Action::PushThis, Action::PushDefault};
    }
  } else {  // ctx.thisSrc == OpSrc::B
    bool match = ctx.lastSrc == OpSrc::A && ctx.lastDelta == Delta::Equal;
    if (match) {
      act = {Action::NoPush, Action::PushThis};
    } else {
      act = {Action::PushDefault, Action::PushThis};
    }
  }
  return act;
}

constexpr inline BothAction UniqueAND(Match5Bit ctx) {
  BothAction act = {Action::NoPush, Action::NoPush};
  if (ctx.thisSrc == OpSrc::A) {
    bool match = ctx.nextSrc == OpSrc::B && ctx.nextDelta == Delta::Equal;
    if (match) {
      act = {Action::PushThis, Action::NoPush};
    } else {
      act = {Action::NoPush, Action::NoPush};
    }
  } else {  // ctx.thisSrc == OpSrc::B
    bool match = ctx.lastSrc == OpSrc::A && ctx.lastDelta == Delta::Equal;
    if (match) {
      act = {Action::NoPush, Action::PushThis};
    } else {
      act = {Action::NoPush, Action::NoPush};
    }
  }
  return act;
}

constexpr inline BothAction UniqueXOR(Match5Bit ctx) {
  BothAction act = {Action::NoPush, Action::NoPush};
  if (ctx.thisSrc == OpSrc::A) {
    bool match = ctx.nextSrc == OpSrc::B && ctx.nextDelta == Delta::Equal;
    if (match) {
      act = {Action::NoPush, Action::NoPush};
    } else {
      act = {Action::PushThis, Action::PushDefault};
    }
  } else {  // ctx.thisSrc == OpSrc::B
    bool match = ctx.lastSrc == OpSrc::A && ctx.lastDelta == Delta::Equal;
    if (match) {
      act = {Action::NoPush, Action::NoPush};
    } else {
      act = {Action::PushDefault, Action::PushThis};
    }
  }
  return act;
}

constexpr inline BothAction SORT(Match5Bit ctx) {
  BothAction act = {Action::NoPush, Action::NoPush};
  if (ctx.thisSrc == OpSrc::A) {
    bool match = ctx.nextSrc == OpSrc::B && ctx.nextDelta == Delta::Equal;
    if (match) {
      act = {Action::PushThis, Action::PushDefault};
    } else {
      act = {Action::PushThis, Action::PushDefault};
    }
  } else {  // ctx.thisSrc == OpSrc::B
    bool match = ctx.lastSrc == OpSrc::A && ctx.lastDelta == Delta::Equal;
    if (match) {
      act = {Action::PushDefault, Action::PushThis};
    } else {
      act = {Action::PushDefault, Action::PushThis};
    }
  }
  return act;
}

constexpr inline BothAction UniqueDiff(Match5Bit ctx) {
  BothAction act = {Action::NoPush, Action::NoPush};
  if (ctx.thisSrc == OpSrc::A) {
    bool match = ctx.nextSrc == OpSrc::B && ctx.nextDelta == Delta::Equal;
    if (match) {
      act = {Action::NoPush, Action::NoPush};
    } else {
      act = {Action::PushThis, Action::PushDefault};
    }
  } else {  // ctx.thisSrc == OpSrc::B
    bool match = ctx.lastSrc == OpSrc::A && ctx.lastDelta == Delta::Equal;
    if (match) {
      act = {Action::NoPush, Action::NoPush};
    } else {
      act = {Action::NoPush, Action::NoPush};
    }
  }
  return act;
}

constexpr inline BothAction UniqueInterpolate(Match5Bit ctx) {
  BothAction act = {Action::NoPush, Action::NoPush};
  if (ctx.thisSrc == OpSrc::A) {
    bool match = ctx.nextSrc == OpSrc::B;
    if (match) {
      act = {Action::PushThis, Action::NoPush};
    } else {
      act = {Action::NoPush, Action::NoPush};
    }
  } else {  // ctx.thisSrc == OpSrc::B
    bool match = ctx.lastSrc == OpSrc::A;
    if (match) {
      act = {Action::NoPush, Action::PushThis};
    } else {
      act = {Action::PushLast, Action::PushThis};
    }
  }
  return act;
}

}  // namespace SpSPPredefPolicy

namespace SpSpInst {
constexpr auto PolicyOR =
    SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::UniqueOR).dump();
constexpr auto PolicyAND =
    SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::UniqueAND).dump();
constexpr auto PolicyDiff =
    SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::UniqueDiff).dump();
constexpr auto PolicyXOR =
    SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::UniqueXOR).dump();
constexpr auto PolicyITPL =
    SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::UniqueInterpolate)
        .dump();
constexpr auto PolicySORT =
    SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::SORT).dump();

};  // namespace SpSpInst