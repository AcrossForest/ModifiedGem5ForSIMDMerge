#pragma once
#include <tuple>
#include <type_traits>
#include <utility>
#include "SpSpCommon.hpp"
namespace SpSpEncodeDecode {

using namespace SpSpEnum;

template <class T>
concept EnumType = std::is_enum_v<T>;

template <class T>
struct Pack {};

template <class T>
concept FullPack = requires {
  typename Pack<T>::ClsType;
  typename Pack<T>::IntType;
  {Pack<T>::BitWidth + 0};
  {Pack<T>::decode({})};
  {Pack<T>::encode(T{})};
};

template <FullPack T>
inline constexpr int packW = Pack<T>::BitWidth;

template <FullPack T>
inline constexpr Pack<T>::IntType pack(T cls) {
  return Pack<T>::encode(cls);
}

template <FullPack T>
inline constexpr Pack<T>::ClsType unpack(typename Pack<T>::IntType val) {
  return Pack<T>::decode(val);
}

template <EnumType E, int N>
struct PackEnum {
  using ClsType = E;
  using IntType = std::underlying_type_t<E>;
  static constexpr int BitWidth = N;

  constexpr static IntType encode(ClsType cls) { return IntType(cls); }
  constexpr static ClsType decode(IntType val) { return ClsType(val); }
};

// template<> struct Pack<SwInfo>:PackEnum<SwInfo,2>{};
template <>
struct Pack<Delta> : PackEnum<Delta, 1> {};
template <>
struct Pack<OpSrc> : PackEnum<OpSrc, 1> {};
template <>
struct Pack<CPMethod> : PackEnum<CPMethod, 2> {};
template <>
struct Pack<LRPart> : PackEnum<LRPart, 1> {};
template <>
struct Pack<Next> : PackEnum<Next, 2> {};
template <>
struct Pack<Action> : PackEnum<Action, 2> {};
template <>
struct Pack<SimAction> : PackEnum<SimAction, 1> {};
template <>
struct Pack<ForceEq> : PackEnum<ForceEq, 1> {};

template <class T>
constexpr inline T getBits(T data, int low, int width) {
  return (data >> low) & ((1ULL << width) - 1);
}

template <>
struct Pack<Match5Bit> {
  using ClsType = Match5Bit;
  using IntType = uint8_t;
  static constexpr int BitWidth = 5;

  constexpr static IntType encode(ClsType cls) {
    uint8_t val = 0;
    val |= uint8_t(cls.lastSrc) << 4;
    val |= uint8_t(cls.thisSrc) << 3;
    val |= uint8_t(cls.nextSrc) << 2;
    val |= uint8_t(cls.lastDelta) << 1;
    val |= uint8_t(cls.nextDelta) << 0;
    return val;
  }

  constexpr static ClsType decode(IntType val) {
    return {
        OpSrc(1 & (val >> 4)), OpSrc(1 & (val >> 3)), OpSrc(1 & (val >> 2)),
        Delta(1 & (val >> 1)), Delta(1 & (val >> 0)),
    };
  }
};

template <>
struct Pack<Match3Bit> {
  using ClsType = Match3Bit;
  using IntType = uint8_t;
  static constexpr int BitWidth = 3;

  constexpr static IntType encode(ClsType cls) {
    uint8_t val = 0;
    val |= uint8_t(cls.leftSrc) << 2;
    val |= uint8_t(cls.rightSrc) << 1;
    val |= uint8_t(cls.delta) << 0;
    return val;
  }
  constexpr static ClsType decode(IntType val) {
    return {
        OpSrc(1 & (val >> 2)),
        OpSrc(1 & (val >> 1)),
        Delta(1 & (val >> 0)),
    };
  }
};

template <>
struct Pack<GetLimitOp2> {
  using ClsType = GetLimitOp2;
  using IntType = uint64_t;
  static constexpr int BitWidth = 64;
  constexpr static IntType encode(ClsType cls) {
    uint64_t pack = 0;
    pack |= uint8_t(cls.forceEqual) << 16;
    pack |= uint8_t(cls.eagerMask) << 8;
    pack |= uint8_t(cls.endCondition.A.full) << 6;
    pack |= uint8_t(cls.endCondition.A.partial) << 4;
    pack |= uint8_t(cls.endCondition.B.full) << 2;
    pack |= uint8_t(cls.endCondition.B.partial) << 0;
    return pack;
  }

  constexpr static ClsType decode(IntType pack) {
    GetLimitOp2 packStruct = {};
    packStruct.forceEqual = ForceEq(getBits(pack, 16, 1));
    packStruct.eagerMask = uint8_t(getBits(pack, 8, 8));
    packStruct.endCondition.A.full = Next(getBits(pack, 6, 2));
    packStruct.endCondition.A.partial = Next(getBits(pack, 4, 2));
    packStruct.endCondition.B.full = Next(getBits(pack, 2, 2));
    packStruct.endCondition.B.partial = Next(getBits(pack, 0, 2));
    return packStruct;
  }
};

template <class _IntType, class... T>
struct PackTuple {
  using IntType = _IntType;
  using TupleType = std::tuple<T...>;
  static constexpr int BitWidth = (packW<T> + ...);
  static constexpr int TupleElementNum = std::tuple_size_v<TupleType>;

  constexpr static IntType encodeMany(T... args) {
    IntType val = 0;
    int packPos = 0;
    ((val |= (pack(args) & ((1 << packW<T>)-1)) << packPos,
      packPos += packW<T>),
     ...);
    return val;
  }

  template <std::size_t... I>
  constexpr static TupleType decode2Tuple_impl(IntType val,
                                               std::index_sequence<I...>) {
    TupleType t = {};
    int packPos = 0;
    ((
         // std::get<I>(t) = unpack((val >> packPos) & ((1 <<
         // packW<decltype(std::get<I>)>)-1))),
         std::get<I>(t) = Pack<std::tuple_element_t<I, TupleType>>::decode(
             (val >> packPos) &
             ((1 << packW<std::tuple_element_t<I, TupleType>>)-1)),
         packPos += packW<T>),
     ...);
    return t;
  }

  constexpr static TupleType decode2Tuple(IntType val) {
    return decode2Tuple_impl(val, std::make_index_sequence<TupleElementNum>{});
  }
};

template <>
struct Pack<State> : PackTuple<uint8_t, OpSrc, Delta> {
  using ClsType = State;

  constexpr static IntType encode(ClsType cls) {
    return encodeMany(cls.lastSrc, cls.lastDelta);
  }
  constexpr static ClsType decode(IntType val) {
    auto [lastSrc, lastDelta] = decode2Tuple(val);
    return {lastSrc, lastDelta};
  }
};

template <>
struct Pack<SEPart> : PackTuple<uint8_t, OpSrc, LRPart> {
  using ClsType = SEPart;

  constexpr static IntType encode(ClsType cls) {
    return encodeMany(cls.operand, cls.lrPart);
  }
  constexpr static ClsType decode(IntType val) {
    auto [operand, lrPart] = decode2Tuple(val);
    return {operand, lrPart};
  }
};

template <>
struct Pack<Limit> {
  using ClsType = Limit;
  using IntType = uint64_t;
  static constexpr int BitWidth = 64;
  constexpr static IntType encode(ClsType cls) {
    uint64_t val = 0;
    val |= uint64_t(cls.logRun) << 0;
    val |= uint64_t(pack(cls.lastState)) << 16;
    val |= uint64_t(cls.consume.A) << 32;
    val |= uint64_t(cls.consume.B) << 40;
    val |= uint64_t(cls.generate.A) << 48;
    val |= uint64_t(cls.generate.B) << 56;
    return val;
  }

  constexpr static ClsType decode(IntType val) {
    Limit limit = {};
    limit.logRun = getBits(val, 0, 8);
    limit.lastState = unpack<State>(getBits(val, 16, 2));
    limit.consume.A = getBits(val, 32, 8);
    limit.consume.B = getBits(val, 40, 8);
    limit.generate.A = getBits(val, 48, 8);
    limit.generate.B = getBits(val, 56, 8);
    return limit;
  }
};

template <FullPack T>
constexpr inline bool checkPackIdentical(T cls) {
  T redecode = Pack<T>::decode(Pack<T>::encode(cls));
  bool equal = cls == redecode;
  if (!equal) {
    throw("checkPackIdentical fail!");
  }
  return equal;
}

};  // namespace SpSpEncodeDecode