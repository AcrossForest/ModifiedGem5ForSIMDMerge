#pragma once
#include <vector>
#include "../SpSpCommon.hpp"

namespace SpSpUserCommon {
using namespace SpSpEnum;

using ScaIdx = uint32_t;
using ScaInt = int32_t;
using ScaFlt = float;

template <class T>
using VReg = std::vector<T>;

using VecIdx = VReg<ScaIdx>;
using VecInt = VReg<ScaInt>;
using VecFlt = VReg<ScaFlt>;
using VecBool = VReg<uint8_t>;
using VecPred = VReg<uint8_t>;

using VBigCmp = VReg<uint32_t>;
using VMatRes = VReg<uint32_t>;

// inline CPUInfo defaultCPU = CPUInfo(4);

}  // namespace SpSpUserCommon
