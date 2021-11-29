#pragma once
#include "PolicyFactory.hpp"
#include "PredefPolicy.hpp"
#include "SpSpCommon.hpp"

#ifdef __SPSP_USE_ARM__
#include "realInst/UserInterface.hpp"
#else
#include "simInst/UnserInterface.hpp"

#endif  // __SPSP_USE_ARM__
