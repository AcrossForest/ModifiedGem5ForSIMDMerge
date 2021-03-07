#ifndef __SPSPINST_HH__
#define __SPSPINST_HH__
#include <stdint.h>

#include <algorithm>
#include <tuple>
#include <vector>

#include "base/logging.hh"

// Packed <- source
// Packed -> dest

// Content of table
// 1. Enums
// 2. Util functions
// 3.

#define PRINTHERE() printf("Error %s:%d\n", __FILE__, __LINE__)

namespace SPSPINST {

enum EndType { endType_continue = 0, endType_finished = 1 };

enum IndexMatchMethod { IdxMat_AND = 0, IdxMat_OR = 1, IdxMat_SORT = 2 };

enum MatchResultPart { MatRes_A0, MatRes_A1, MatRes_B0, MatRes_B1 };

enum MatchResultLength {
  GetLength_consumedA,
  GetLength_consumedB,
  GetLength_outputLen0,
  GetLength_outputLen1,
  GetLength_outputLenSum,
};

enum MatchPredResult {
  MatPred_A0,
  MatPred_A1,
  MatPred_B0,
  MatPred_B1,
  MatPred_ConsumeA,
  MatPred_ConsumeB,
  MatPred_Out0,
  MatPred_Out1
};

enum CompareState {
  weakLE = 0,   // weak (left <= right)
  weakGT = 1,   // weak (left > right)
  strongLT = 2, // Strong (left < right)
  strongGT = 3  // Strong (left > right)
};

inline bool isPowerOf2(int n) { return (n != 0) && ((n & (n - 1)) == 0); }
inline int intLog2(int n) {
  int targetlevel = 0;
  while (n >>= 1)
    ++targetlevel;
  return targetlevel;
}
#define GETMASK(n) ((1ull << (n)) - 1)
#define GETLOWBIT(x, n) (GETMASK(n) & (x))
inline uint64_t pack(uint64_t va, uint64_t vb) {
  return (GETLOWBIT(va, 32) << 32) | GETLOWBIT(vb, 32);
}

inline uint32_t getBits(uint32_t packed, int low, int width) {
  return (packed >> low) & ((1ull << width) - 1);
}
inline uint32_t insertBits(uint32_t packed, uint32_t source, int low,
                           int width) {
  return packed | ((source & ((1ull << width) - 1)) << low);
}

template <class T>
inline void getVectorBits(const std::vector<uint32_t> &packed,
                          std::vector<T> &dest, int low, int width,
                          int multi = 1) {
  for (int m = 0; m < multi; ++m) {
    for (int i = 0; i < packed.size(); ++i) {
      dest[i + m * packed.size()] = getBits(packed[i], low + m * width, width);
    }
  }
}

template <class T>
inline void insertVectorBits(std::vector<uint32_t> &packed,
                             const std::vector<T> &source, int low, int width,
                             int multi = 1) {
  for (int m = 0; m < multi; ++m) {
    for (int i = 0; i < packed.size(); ++i) {
      packed[i] = insertBits(packed[i], source[i + m * packed.size()],
                             low + m * width, width);
    }
  }
}

inline int countVector1Bits(const std::vector<uint32_t> &packed, int pos) {
  int count = 0;
  for (int i = 0; i < packed.size(); ++i) {
    count += getBits(packed[i], pos, 1);
  }
  return count;
}

inline void insertIntAsVector1Bits(std::vector<uint32_t> &packed, int pos,
                                   int value) {
  // if value > packed.size(), then it is truncated to packed.size().
  // No error will be reported.
  // If value is a negative value, then it is treated as zero.
  // No error will be reported.
  for (int i = 0; i < packed.size() && i < value; ++i) {
    packed[i] = insertBits(packed[i], 1, pos, 1);
  }
}

class PackedTool {
public:
  int vecLen;
  std::vector<uint32_t> packed;
  int lowbit;
  PackedTool(int vecLen) : vecLen(vecLen), packed(vecLen, 0), lowbit(0) {}
  PackedTool(std::vector<uint32_t> packed)
      : vecLen(packed.size()), packed(packed), lowbit(0) {}

  void setNewVecLen(int _vecLen) {
    vecLen = _vecLen;
    packed.assign(_vecLen, 0);
    lowbit = 0;
  }

  void setNewPacked(const std::vector<uint32_t> &_packed) {
    vecLen = _packed.size();
    packed = _packed;
    lowbit = 0;
  }

  void incLowBit(int width) {
    lowbit += width;
    fatal_if(lowbit > 32, "Used more bits than 32. Cannot encoding.");
  }

  // For packed bits creation
  template <class T>
  void insertBits_t(const std::vector<T> &source, int width, int multi = 1) {
    insertVectorBits<T>(packed, source, lowbit, width, multi);
    incLowBit(width * multi);
  }
  void insertInt_t(int value) {
    insertIntAsVector1Bits(packed, lowbit, value);
    incLowBit(1);
  }

  // For extracting bits
  template <class T>
  void getBits_t(std::vector<T> &dest, int width, int multi = 1) {
    getVectorBits<T>(packed, dest, lowbit, width, multi);
    incLowBit(width * multi);
  }

  int getInt_t() {
    int output = countVector1Bits(packed, lowbit);
    incLowBit(1);
    return output;
  }

  std::vector<uint32_t> getPackedResult() { return packed; }
};

// template <class T>
// void resetVector(std::vector<T> &vec, int size, T val)
//{
//    vec.resize(size);
//    for (int i = 0; i < size; ++i)
//    {
//        vec[i] = val;
//    }
//}

class IndexCompressionTool {
public:
  int vecLen;
  int vecIdxBits;
  int stages;

  // Dynamic information
  std::vector<std::vector<uint8_t>> stageStates;
  int maxLenA, maxLenB;
  std::vector<uint8_t> diff;
  std::vector<uint8_t> src; // A =0, B = 1
  std::vector<uint8_t> idx;

  void setNewVecLen(int newVecLen) {
    vecLen = newVecLen;
    vecIdxBits = intLog2(vecLen);
    stages = vecIdxBits + 1;

    stageStates.resize(stages);
    for (int st = 0; st < stages; ++st) {
      stageStates[st].assign(vecLen, 0);
      // resetVector<uint8_t>(stageStates[st], vecLen, 0);
    }
    maxLenA = vecLen;
    maxLenB = vecLen;
    diff.assign(2 * vecLen, 0);
    src.assign(2 * vecLen, 0);
    idx.assign(2 * vecLen, 0);
    // resetVector<uint8_t>(diff, 2 * vecLen, 0);
    // resetVector<uint8_t>(src, 2 * vecLen, 0);
    // resetVector<uint8_t>(idx, 2 * vecLen, 0);
  }

  IndexCompressionTool(int _vecLen) { setNewVecLen(_vecLen); }

  std::vector<uint32_t> encode() {
    static PackedTool packTool(vecLen);
    packTool.setNewVecLen(vecLen);
    for (int st = 0; st < stages; ++st) {
      packTool.insertBits_t(stageStates[st], 2);
    }                                          // 2 * 7
    packTool.insertInt_t(maxLenA);             // 1
    packTool.insertInt_t(maxLenB);             // 1
    packTool.insertBits_t(diff, 1, 2);         // 2
    packTool.insertBits_t(src, 1, 2);          // 2
    packTool.insertBits_t(idx, vecIdxBits, 2); // 2 * 6
    // Total = 32 bits
    // 14 + 2 + 4 + 12 = 32
    return packTool.getPackedResult();
  }

  void decode(const std::vector<uint32_t> &packed) {
    static PackedTool packTool(packed);
    packTool.setNewPacked(packed);
    for (int st = 0; st < stages; ++st) {
      packTool.getBits_t(stageStates[st], 2);
    }
    maxLenA = packTool.getInt_t();
    maxLenB = packTool.getInt_t();
    packTool.getBits_t(diff, 1, 2);
    packTool.getBits_t(src, 1, 2);
    packTool.getBits_t(idx, vecIdxBits, 2);
  }

  void setBoundary(int maxA, int maxB) {
    maxLenA = maxA;
    maxLenB = maxB;
  }

  void addComparision(const std::vector<uint32_t> &idxA,
                      const std::vector<uint32_t> &idxB) {
    // Create tuple list
    int fullRange = 2 * vecLen;
    static std::vector<uint32_t> tempVal;
    static std::vector<uint8_t> tempSrc;
    static std::vector<uint8_t> tempIdx;
    tempVal.assign(fullRange, 0);
    tempSrc.assign(fullRange, 0);
    tempIdx.assign(fullRange, 0);

    const uint32_t invalidIdx = (1ull << 32) - 1;
    for (int i = 0; i < vecLen; ++i) {
      int ia = i;
      int ib = 2 * vecLen - 1 - i;
      tempVal[ia] = i < maxLenA ? idxA[i] : invalidIdx;
      tempVal[ib] = i < maxLenB ? idxB[i] : invalidIdx;
      tempSrc[ia] = 0;
      tempSrc[ib] = 1;
      tempIdx[ia] = i;
      tempIdx[ib] = i;
    }

    for (int st = 0; st < stages; ++st) {
      int blkRange = fullRange >> st;
      int halfBlk = blkRange >> 1;
      for (int blkBase = 0; blkBase < fullRange; blkBase += blkRange) {
        for (int blkOffset = 0; blkOffset < halfBlk; ++blkOffset) {
          int left = blkBase + blkOffset;
          int right = blkBase + blkOffset + halfBlk;

          uint8_t oldState = stageStates[st][(blkBase >> 1) + blkOffset];

          uint8_t newState;

          // Encoding of state
          // 0: Weak (left <= right)
          // 1: Weak (left > right)
          // 2: Strong (left <= right)
          // 3: Strong (left > right)
          switch (oldState) {
          case weakLE: // Weak (left <= right)
          case weakGT: // Weak (left > right)
            if (tempVal[left] < tempVal[right]) {
              // Next: Strong (left < right)
              newState = strongLT;
            } else if (tempVal[left] > tempVal[right]) {
              // Next: Strong (left > right)
              newState = strongGT;
            } else {
              // val is the same, so next is weak state
              if (tempSrc[left] < tempSrc[right])
                newState = weakLE;
              else if (tempSrc[left] > tempSrc[right])
                newState = weakGT;
              else if (tempIdx[left] <= tempIdx[right])
                newState = weakLE;
              else // tempIdx[left] > tempIdx[right]
                newState = weakGT;
            }
            break;
          case strongGT:
          case strongLT:
            newState = oldState;
            break;
          default:
            // Impossible to be here.
            PRINTHERE();
            fatal("Unkown compare state. Should not reach here.");
            break;
          }

          stageStates[st][(blkBase >> 1) + blkOffset] = newState;
          if (newState == weakGT || newState == strongGT) {
            std::swap(tempVal[left], tempVal[right]);
            std::swap(tempSrc[left], tempSrc[right]);
            std::swap(tempIdx[left], tempIdx[right]);
          }
        }
      }
    }

    diff[0] = 1;
    for (int i = 1; i < fullRange; ++i) {
      fatal_if(tempVal[i - 1] > tempVal[i],
               "tempVal should be already sorted.\n");
      diff[i] |= tempVal[i - 1] != tempVal[i];
    }
    for (int i = 0; i < fullRange; ++i) {
      src[i] = tempSrc[i];
      idx[i] = tempIdx[i];
    }
  }
};

class MatchResult {
public:
  int vecLen;
  int srcIdxBits;
  // Dynamic Information
  int consumedA;
  int consumedB;
  int outputLen;
  std::vector<uint8_t> validA;
  std::vector<uint8_t> regIdxA;
  std::vector<uint8_t> validB;
  std::vector<uint8_t> regIdxB;

  void setNewVecLen(int newVecLen) {
    vecLen = newVecLen;
    srcIdxBits = intLog2(vecLen);

    consumedA = 0;
    consumedB = 0;
    outputLen = 0;
    validA.assign(2 * vecLen, 0);
    regIdxA.assign(2 * vecLen, 0);
    validB.assign(2 * vecLen, 0);
    regIdxB.assign(2 * vecLen, 0);
    // resetVector<uint8_t>(validA, 2 * vecLen, 0);
    // resetVector<uint8_t>(regIdxA, 2 * vecLen, 0);
    // resetVector<uint8_t>(validB, 2 * vecLen, 0);
    // resetVector<uint8_t>(regIdxB, 2 * vecLen, 0);
  }
  MatchResult(int vecLen) { setNewVecLen(vecLen); }
  std::vector<uint32_t> encode() {
    static PackedTool tool(vecLen);
    tool.setNewVecLen(vecLen);
    tool.insertInt_t(consumedA);
    tool.insertInt_t(consumedB);

    // Use the feature of insertInt_t. It will behave correctly when
    // both outputLen in [0,vecLen], and [vecLen+1,2*vecLen]
    tool.insertInt_t(outputLen);
    tool.insertInt_t(outputLen - vecLen);

    tool.insertBits_t(validA, 1, 2);
    tool.insertBits_t(regIdxA, srcIdxBits, 2);
    tool.insertBits_t(validB, 1, 2);
    tool.insertBits_t(regIdxB, srcIdxBits, 2);
    return tool.getPackedResult();
  }

  void decode(const std::vector<uint32_t> &packed) {
    static PackedTool tool(packed);
    tool.setNewPacked(packed);
    consumedA = tool.getInt_t();
    consumedB = tool.getInt_t();
    int firstHalf = tool.getInt_t();
    int secondHalf = tool.getInt_t();
    outputLen = firstHalf + secondHalf;

    tool.getBits_t(validA, 1, 2);
    tool.getBits_t(regIdxA, srcIdxBits, 2);
    tool.getBits_t(validB, 1, 2);
    tool.getBits_t(regIdxB, srcIdxBits, 2);
  }

  void consume(int src) {
    if (src == 0) {
      consumedA++;
    } else if (src == 1) {
      consumedB++;
    } else {
      PRINTHERE();
      fatal("Unkown src. Should not reach here.");
    }
  }

  void writeOutput(int src, int idx) {
    if (src == 0) {
      validA[outputLen] = 1;
      regIdxA[outputLen] = idx;
    } else if (src == 1) {
      validB[outputLen] = 1;
      regIdxB[outputLen] = idx;
    } else {
      PRINTHERE();
      fatal("Unkown src. Should not be here");
    }
  }

  void advanceOutput() { outputLen += 1; }

  std::tuple<std::vector<uint8_t>::iterator, std::vector<uint8_t>::iterator>
  getIterator(MatchResultPart part) {

    std::vector<uint8_t>::iterator validIter;
    std::vector<uint8_t>::iterator idxIter;

    switch (part) {
    case MatRes_A0:
      validIter = validA.begin();
      idxIter = regIdxA.begin();
      break;
    case MatRes_A1:
      validIter = validA.begin() + vecLen;
      idxIter = regIdxA.begin() + vecLen;
      break;
    case MatRes_B0:
      validIter = validB.begin();
      idxIter = regIdxB.begin();
      break;
    case MatRes_B1:
      validIter = validB.begin() + vecLen;
      idxIter = regIdxB.begin() + vecLen;
      break;
    default:
      PRINTHERE();
      fatal("Unkown MatchResultPart. Should not reach here.\n");
    }

    return std::make_tuple(validIter, idxIter);
  }
};

class Machine {
public:
  int vecLen;
  Machine(int vecLen) : vecLen(vecLen) {
    fatal_if(!isPowerOf2(vecLen), "vecLen should be the power of 2");
  }

  // Normal interface for vector
  // std::vector<uint32_t> for z0.s
  // std::vector<uint8_t> for p0.s

  uint64_t pack(uint64_t a, uint64_t b) {
    uint64_t truncatedA = std::min<uint64_t>(vecLen, a);
    uint64_t truncatedB = std::min<uint64_t>(vecLen, b);
    return (truncatedA << 32) | ((truncatedB << 32) >> 32);
  }

  std::vector<uint32_t>
  indexCompressionInitM1(const std::vector<uint32_t> &idxA,
                         const std::vector<uint32_t> &idxB, uint64_t ab) {
    int maxLenA = GETLOWBIT(ab >> 32, 32);
    int maxLenB = GETLOWBIT(ab, 32);
    static IndexCompressionTool idxCompTool(vecLen);
    idxCompTool.setNewVecLen(vecLen);
    idxCompTool.setBoundary(maxLenA, maxLenB);
    idxCompTool.addComparision(idxA, idxB);
    return idxCompTool.encode();
  }

  std::vector<uint32_t> indexCompressionInitM2(int maxLenA, int maxLenB) {
    static IndexCompressionTool idxCompTool(vecLen);
    idxCompTool.setNewVecLen(vecLen);
    idxCompTool.setBoundary(maxLenA, maxLenB);
    return idxCompTool.encode();
  }

  std::vector<uint32_t>
  indexCompression(const std::vector<uint32_t> &oldPackState,
                   const std::vector<uint32_t> &idxA,
                   const std::vector<uint32_t> &idxB) {
    static IndexCompressionTool idxCompTool(vecLen);
    idxCompTool.setNewVecLen(vecLen);
    idxCompTool.decode(oldPackState);
    idxCompTool.addComparision(idxA, idxB);
    return idxCompTool.encode();
  }

  std::vector<uint32_t> indexMatchM1(const std::vector<uint32_t> &oldPackState,
                                     uint64_t endAB, IndexMatchMethod imm) {
    EndType endA = EndType(endAB >> 32);
    EndType endB = EndType(GETLOWBIT(endAB, 32));
    return indexMatchM2(oldPackState, endA, endB, imm);
  }

  uint64_t getLength(const std::vector<uint32_t> &packedMatchResult,
                     MatchResultLength imm) {
    static MatchResult matchResult(vecLen);
    matchResult.setNewVecLen(vecLen);
    matchResult.decode(packedMatchResult);
    switch (imm) {
    case GetLength_consumedA:
      return matchResult.consumedA;
      break;
    case GetLength_consumedB:
      return matchResult.consumedB;
      break;
    case GetLength_outputLen0:
      return matchResult.outputLen > vecLen ? vecLen : matchResult.outputLen;
      break;
    case GetLength_outputLen1:
      return matchResult.outputLen > vecLen ? matchResult.outputLen - vecLen
                                            : 0;
    case GetLength_outputLenSum:
      return matchResult.outputLen;
      break;
    default:
      PRINTHERE();
      fatal("Uunkown MatchResultLength. Should not reach here.\n");
      return 0;
      break;
    }
  }
  std::vector<uint32_t> indexMatchM2(const std::vector<uint32_t> &oldPackState,
                                     EndType endA, EndType endB,
                                     IndexMatchMethod imm) {
    static IndexCompressionTool idxCompTool(vecLen);
    idxCompTool.setNewVecLen(vecLen);
    idxCompTool.decode(oldPackState);
    bool incompleteA =
        (idxCompTool.maxLenA == vecLen) || (endA == endType_continue);
    bool incompleteB =
        (idxCompTool.maxLenB == vecLen) || (endB == endType_continue);

    idxCompTool.diff[0] = 1;
    if (imm == IdxMat_SORT) {
      for (int i = 0; i < 2 * vecLen; ++i) {
        idxCompTool.diff[i] = 1;
      }
    }

    int start = 0;
    int lastMeetIdx[] = {0, 0};
    static MatchResult result(vecLen);
    result.setNewVecLen(vecLen);
    while (start < 2 * vecLen) {
      // find group end
      int end = start + 1;
      while (end < 2 * vecLen && idxCompTool.diff[end] == 0)
        end++;

      // The group is [start,end)
      // break early if this group is invalide group.
      // We know that the first element of this group must be
      // (src=0,idx=maxLenA)
      if (idxCompTool.src[start] == 0 &&
          idxCompTool.idx[start] == idxCompTool.maxLenA) {
        break;
      }

      // Normal process goes here
      switch (imm) {
      case IdxMat_AND:
        if (end - start == 1)
          break;
        goto process;
      case IdxMat_OR:
      case IdxMat_SORT:
      process:
        for (int i = start; i < end; ++i) {
          result.writeOutput(idxCompTool.src[i], idxCompTool.idx[i]);
        }
        result.advanceOutput();
        break;
      default:
        PRINTHERE();
        fatal("Unkown IndexMatchMethod. Should not reach here.\n");
      }

      // All elements in this group are consumed
      for (int i = start; i < end; ++i) {
        result.consume(idxCompTool.src[i]);
      }
      // Finally, if the "lastVisibleButNotLastElement" is meet,
      // then break.
      for (int i = start; i < end; ++i) {
        lastMeetIdx[idxCompTool.src[i]] = idxCompTool.idx[i];
      }
      if ((incompleteA && lastMeetIdx[0] >= idxCompTool.maxLenA - 1) ||
          (incompleteB && lastMeetIdx[1] >= idxCompTool.maxLenB - 1)) {
        break;
      }
      // Move to the next group by setting the start to be the end
      // of this group
      start = end;
    }

    return result.encode();
  }

  template <class T>
  std::vector<T> customPermute(const std::vector<uint32_t> &packedMatchResult,
                               const std::vector<T> &source,
                               MatchResultPart part) {
    static MatchResult matchResult(vecLen);
    matchResult.setNewVecLen(vecLen);
    matchResult.decode(packedMatchResult);

    std::vector<uint8_t>::iterator validIter;
    std::vector<uint8_t>::iterator idxiter;

    std::tie(validIter, idxiter) = matchResult.getIterator(part);
    std::vector<T> dest(vecLen, 0);

    for (int i = 0; i < vecLen; ++i) {
      if (validIter[i]) {
        dest[i] = source[idxiter[i]];
      }
    }
    return dest;
  }

  std::vector<uint8_t>
  getPredicate(const std::vector<uint32_t> &packedMatchResult,
               MatchPredResult predPart) {
    static MatchResult matchResult(vecLen);
    matchResult.setNewVecLen(vecLen);
    matchResult.decode(packedMatchResult);
    int len;
    std::vector<uint8_t>::iterator validIter;
    std::vector<uint8_t>::iterator idxiter;
    switch (predPart) {
    case MatPred_A0:
    case MatPred_A1:
    case MatPred_B0:
    case MatPred_B1:
      std::tie(validIter, idxiter) =
          matchResult.getIterator(MatchResultPart(predPart));
      return std::vector<uint8_t>(validIter, validIter + vecLen);
      break;
    case MatPred_ConsumeA:
      len = matchResult.consumedA;
      break;
    case MatPred_ConsumeB:
      len = matchResult.consumedB;
      break;
    case MatPred_Out0:
      len = matchResult.outputLen > vecLen ? vecLen : matchResult.outputLen;
      break;
    case MatPred_Out1:
len = matchResult.outputLen > vecLen ? matchResult.outputLen - vecLen : 0;
      break;
    default:
      PRINTHERE();
      fatal("Unkown MatchPredResult. Should not reach here.\n");
      break;
    }

    std::vector<uint8_t> temp(vecLen, 0);
    for (int i = 0; i < len; ++i)
      temp[i] = 1;
    return temp;
  }

  std::vector<uint32_t> singleSideSortInit(const std::vector<uint32_t> &idx,
                                           const std::vector<uint8_t> &pred) {
    static std::vector<uint32_t> permute(vecLen, 0);
    permute.assign(vecLen, 0);
    for (int i = 0; i < vecLen; ++i) {
      permute[i] = i;
    }
    return singleSideSort(permute, idx, pred);
  }

  std::vector<uint32_t> singleSideSort(const std::vector<uint32_t> &prepermute,
                                       const std::vector<uint32_t> &idx,
                                       const std::vector<uint8_t> &pred) {
    static std::vector<std::tuple<uint32_t, uint32_t>> permutedTuple;
    permutedTuple.assign(vecLen, std::make_tuple(0, 0));

    const uint32_t invalidIdx = (1ull << 32) - 1;
    for (int i = 0; i < vecLen; ++i) {
      if (pred[i] == 1) {
        permutedTuple[i] = std::make_tuple(idx[prepermute[i]], prepermute[i]);
      } else {
        permutedTuple[i] = std::make_tuple(invalidIdx, prepermute[i]);
      }
    }
    std::stable_sort(
        permutedTuple.begin(), permutedTuple.end(),
[](std::tuple<uint32_t, uint32_t> a, std::tuple<uint32_t, uint32_t> b) {
          return std::get<0>(a) < std::get<0>(b);
        });

    std::vector<uint32_t> out(vecLen, 0);
    for (int i = 0; i < vecLen; ++i) {
      out[i] = std::get<1>(permutedTuple[i]);
    }
    return out;
  }

  std::vector<uint8_t> diff(const std::vector<uint32_t> &valArray,
                            uint32_t zeroVal) {
    std::vector<uint8_t> diffPred(vecLen, 0);
    diffPred[0] = valArray[0] != zeroVal;
    for (int i = 1; i < vecLen; ++i) {
      diffPred[i] = valArray[i] != valArray[i - 1];
    }
    return diffPred;
  }

  std::vector<uint32_t> encoding(const std::vector<uint8_t> &pred) {
    std::vector<uint32_t> vecRegidx(vecLen, 0);
    int used = 0;
    for (int i = 0; i < vecLen; ++i) {
      if (pred[i]) {
        vecRegidx[used] = i;
        used++;
      }
    }
    return vecRegidx;
  }

  template <class T>
  T getLastActiveElem(const std::vector<uint32_t> &packedMatchResult,
                      const std::vector<T> &source, MatchResultPart part) {
    static MatchResult matchResult(vecLen);
    matchResult.setNewVecLen(vecLen);
    matchResult.decode(packedMatchResult);

    std::vector<uint8_t>::iterator validIter;
    std::vector<uint8_t>::iterator idxiter;

    std::tie(validIter, idxiter) = matchResult.getIterator(part);
    T dest = 0;

    for (int i = 0; i < vecLen; ++i) {
      if (validIter[i]) {
        dest = source[validIter[i]];
      }
    }
    return dest;
  }
};
} // namespace SPSPINST

#endif
