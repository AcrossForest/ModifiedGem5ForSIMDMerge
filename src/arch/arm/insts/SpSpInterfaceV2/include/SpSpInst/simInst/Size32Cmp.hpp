#pragma once
#include "../SpSpCommon.hpp"
#include "InternalCommon.hpp"

namespace MultiTypeCompare {
using SpSpEnum::CPMethod;
using SpSpIntl::Plain;

inline Plain converte_As_unsigned(uint8_t &negate, Plain val) {
  negate = 0;
  return val;
}

inline Plain converte_As_2comp(uint8_t &negate, Plain val) {
  negate = 0;
  return val + (1 << 31);
}

inline Plain converte_As_signedMag(uint8_t &negate, Plain val) {
  negate = val >> 31;
  return (val >> 31) ? (~val) : (val | (1 << 31));
}

inline Plain converte_As_follower(uint8_t &negate, Plain val) {
  negate = negate;
  return negate ? (~val) : val;
}

inline Plain converte(uint8_t &negate, Plain val, CPMethod method) {
  switch (method) {
    case CPMethod::IsUInt:
      return converte_As_unsigned(negate, val);
      break;

    case CPMethod::IsInt:
      return converte_As_2comp(negate, val);
      break;
    case CPMethod::IsFloat:
      return converte_As_signedMag(negate, val);
      break;
    case CPMethod::IsFollower:
      return converte_As_follower(negate, val);
      break;
    default:
      return 0;
      break;
  }
}
}  // namespace MultiTypeCompare