from __future__ import print_function
from __future__ import absolute_import

import m5
from m5.defines import buildEnv
from m5.objects import *

from common.Benchmarks import *
from common import ObjectList
import math


class SpSpLatencySetting:
    def __init__(self, vecLen=16, KeyCombineLat=3,
                 MatchLat=1, SEPermuteLat=1) -> None:
        super().__init__()
        self.setV(vecLen)

        self.KeyCombineLat = KeyCombineLat
        self.MatchLat = MatchLat
        self.SEPermuteLat = SEPermuteLat

    def setV(self, v: int):
        self.v = v
        self.v2 = self.v * 2
        self.logV = int(
            math.log2(self.v))
        self.logV2 = self.logV + 1


def spspLatencyPart(spspLatObj):
    LatCmp32Bit = spspLatObj.KeyCombineLat
    LatMatch = spspLatObj.MatchLat
    LatPermute = spspLatObj.SEPermuteLat

    spspVecLen = spspLatObj.v
    logV = spspLatObj.logV
    logV2 = spspLatObj.logV2
    spspAllsortStgs = max(
        1, int(logV * (logV+1) / 2))
    return [
        ######################################################
        # SpSp Instructions version 1
        #####################################################
        OpDesc(opClass='SpSpIndexCompressInit',
                       opLat=LatCmp32Bit * logV2),
        # SpSpGetLength just extract bits, do nothing. 1 cycle
        OpDesc(
            opClass='SpSpGetLength', opLat=1),
        # Note: SpSpIndexCompression true latency = spsp32CmpLat * logV2,
        #  but it allways follow
        # the SpSpIndexCompressInit and it can be
        #  'specially pipelined' with that instruction
        # So it is OK to set its latency to be 1
        OpDesc(opClass='SpSpIndexCompression',
                       opLat=1),
        # Butterfly's implementation
        OpDesc(opClass='SpSpIndexMatch',
                       opLat=LatMatch * logV2),
        OpDesc(opClass='SpSpCustPerm',
               opLat=LatPermute * logV2),
        # SpSpGetPred just extract bits, latency = 1
        OpDesc(
            opClass='SpSpGetPred', opLat=1),
        # SpSpSingleSideSortInit: (... 3 + 2 + 1) * spsp32CmpLat
        OpDesc(opClass='SpSpSingleSideSortInit',
                       opLat=LatCmp32Bit * spspAllsortStgs,
                       pipelined=False),
        # according to above, svtbl(i.e. permute in x86) is SimdMisc,
        # So permute instruction have 3 cycle latency
        OpDesc(opClass='SpSpSingleSideSort',
                       opLat=3 + LatCmp32Bit * spspAllsortStgs,
                       pipelined=False),
        ######################################################
        # SpSp Instructions Version 2
        #####################################################
        OpDesc(opClass='SpSpInitBigCmp',
                       opLat=LatCmp32Bit * logV2),
        OpDesc(opClass='SpSpNextBigCmpFromMatRes',
                       opLat=LatCmp32Bit * logV2),
        # Note: The true latency is LatCmp32Bit * logV2, however,
        # it is 'pipelined by fragments'
        # and it will always follows either SpSpInitBigCmp or
        # SpSpNextBigCmpFromMatRes, so we should set its latency to be 1 due to the perfect pipeline. The extra latency is moved to SpSpInitBigCmp (which is set to LatCmp32Bit * logV2 while in practice 1).  So the total latency is the same as real.
        OpDesc(opClass='SpSpKeyCombine',
                       opLat=1),
        OpDesc(opClass='SpSpMatch',
                       opLat=LatMatch * logV2),
        OpDesc(opClass='SpSpGetLimit',
                       opLat=LatMatch * logV2),
        OpDesc(opClass='SpSpBFPermute',
                       opLat=LatPermute * logV2),
                    #    opLat= 1), # the real latency is LatPermute * logV2, 
                    #    # but it will always follow KeyCombine and therefore SpSpInitBigCmp
                    #    # There are special pipeline enabled for them.
        OpDesc(opClass='SpSpSEPermute',
                       opLat=LatPermute * logV2),
    ]


def resetSpSpLatency(fp, spspLatObj):
    newCostList = spspLatencyPart(
        spspLatObj)
    for i in range(len(newCostList)):
        fp.opList[-len(newCostList) +
                  i].opLat = newCostList[i].opLat
    print("Reset SpSpLatency!")
    print(spspLatObj.__dict__)


# globalSpSpLatObj = SpSpLatencySetting()

# def addSpSpLatencyParser(parser):
#   parser.add_option("--SpSp-vecLen",type="int",default=16)
#   parser.add_option("--SpSp-KeyCombineLat",type="float",default=3)
#   parser.add_option("--SpSp-MatchLat",type="float",default=1)
#   parser.add_option("--SpSp-SEPermuteLat",type="float",default=1)

# def applySpSpOptions(options):
#   if options.SpSp_vecLen:
#     globalSpSpLatObj.setV(options.SpSp_vecLen)
#   if options.SpSp_KeyCombineLat:
#     globalSpSpLatObj.KeyCombineLat = options.SpSp_KeyCombineLat
#   if options.SpSp_MatchLat:
#     globalSpSpLatObj.MatchLat = options.SpSp_MatchLat
#   if options.SpSp_SEPermuteLat:
#     globalSpSpLatObj.SEPermuteLat = options.SpSp_SEPermuteLat
#   print("Set spsp parameters as follow:\n")
#   print(globalSpSpLatObj.__dict__)
