#include <iostream>
#include "SpSpInst/SpSpInterface.hpp"
#include <memory>
#include <bit>
#include <random>
#include <unordered_set>
#include <functional>
#include <map>
using namespace SpSpInst;
// #include "SpSpInst/Gem5Interface.hpp"

auto policyFactory = SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::UniqueOR);
auto policyMask = policyFactory.policyMask();

enum struct SortState{
    Init,
    LeftIssued,
    RightIssued
};

struct Task{
    uint8_t srcBuf,destBuf;
    SortState state;
    int start,len;
};


constexpr uint64_t shortLimit = pack(Limit{0,{OpSrc::B,Delta::NotEqual}});
// Load data from~from+len, sort, put the result into to~to+len
// The data in to~to+len is sorted, element comes from from~from+len before call this function
template<CPMethod method, Size32 T>
inline void shortSort(T *from, T*to, int len){
    // printf("From: ");
    // for(int i=0; i<len; ++i){
    //     printf("%d\t",from[i]);
    // }
    // printf("\n");

    auto predLeft = whilelt(0,len);
    auto predRight = whilelt(cpu.v,len);
    auto idxLeft = load(predLeft,from);
    auto idxRight = load(predRight,from + cpu.v);

    uint32_t mid[100];

    for(int st = 0; st < cpu.logV2; ++st){
        // printf("I am here.\n");
        VBigCmp bigCmp = InitBigCmp(shortLimit + st,predLeft,predRight);
        bigCmp = KeyCombine<method>(bigCmp,idxLeft,idxRight);
        auto newIdxLeft = BFPermute<LRPart::Left>(bigCmp,idxLeft,idxRight);
        auto newIdxRight = BFPermute<LRPart::Right>(bigCmp,idxLeft,idxRight);
        idxLeft = newIdxLeft;
        idxRight = newIdxRight;
        
    }

    store(predLeft,to,idxLeft);
    store(predRight,to+cpu.v,idxRight);
    // printf("To: ");
    // for(int i=0; i<len; ++i){
    //     printf("%d\t",to[i]);
    // }
    // printf("\n");
}

constexpr auto sortFactory = SpSpPolicyFactory::PolicyFactory(SpSPPredefPolicy::SORT);
constexpr auto sortSimMask = sortFactory.simPolicyMask();
constexpr auto sortEagerMask = sortFactory.eagerMask();
constexpr auto sortOp2 = pack(GetLimitOp2{ForceEq::Yes,sortEagerMask,{{Next::Same,Next::Inf},{Next::Same,Next::Inf}}});
const uint64_t longLimit = pack(Limit{cpu.logV,{OpSrc::B,Delta::NotEqual}});
// The data in to~to+ALen+Blen is sorted, the lements from AFrom~AFrom+ALen, BFrom~BFrom+BLen
// AFrom+ALen and to+to+ALen+BLen shall have no overlap, otherwise this function return _|_

template<CPMethod method, Size32 T>
inline void mergeSort(T *AFrom, int ALen, T *BFrom, int BLen, T *to){
    int pa,pb,pc;
    pa = pb = pc = 0;

    while(pa < ALen && pb < BLen){
        auto predA = whilelt(pa,ALen);
        auto predB = whilelt(pb,BLen);
        auto idxA = load(predA,AFrom + pa);
        auto idxB = load(predB,BFrom + pb);
        VBigCmp bigCmp = InitBigCmp(longLimit,predA,predB);
        bigCmp = KeyCombine<method>(bigCmp,idxA,idxB);
        auto idxLeft = BFPermute<LRPart::Left>(bigCmp,idxA,idxB);
        auto idxRight = BFPermute<LRPart::Right>(bigCmp,idxA,idxB);

        uint64_t newLimit = GetLimit(bigCmp,sortSimMask,sortOp2);
        Limit unpackLimit = unpack<Limit>(newLimit);

        auto predCLow = whilelt(0,unpackLimit.generate.A);
        store(predCLow,to+pc,idxLeft);
        if(unpackLimit.generate.A > cpu.v){
            auto predCHigh = whilelt(cpu.v,unpackLimit.generate.A);
            store(predCHigh,to + pc + cpu.v, idxRight);
        }
        pa += unpackLimit.consume.A;
        pb += unpackLimit.consume.B;
        pc += unpackLimit.generate.A;
    }
    if(pa<ALen){
        std::copy(AFrom+pa,AFrom+ALen,to+pc);
        pc += ALen - pa;
    }
    if(pb<BLen){
        std::copy(BFrom+pb,BFrom+BLen,to+pc);
        pc += BLen - pb;
    }
    return;
}

// Reqire: len > cpu.v, then both left and right will not be zero. 
// left + right = len, left = n * cpu.v,  (len - modV(len))/2 <= left <= (len - modV(len)+cpu.v)/2
// (len + modV(len))/2 >= right >= (len + modV(len) - cpu.v)/2
// Usually only one choice. When two boundary are k*cpu.v and (k+1)*cpu.v, then left k*cpu.v (2kv ~ 2kv+v-1, left = kv) 
inline std::pair<int,int> getChildLen(int len){
    int x = len >> cpu.logV2; // x = (len - modV(len)) >> cpu.v
    int xv2 = x >> 1; // xv2 = (x - mod2(x)) >> 1
    int left = (x - xv2) << cpu.logV2; // (x - xv2) = (x + mod2(x)) >> 1, 
    // left =   ( ( (len - modV(len)) >> cpu.v + mod2( (len - modV(len)) >> cpu.v ) >> 1 << cpu.v
    // left  << 1 =  (len - modV(len))  +  mod2((len - modV(len)) >> cpu.v)  << cpu.v
    // left << 1 >= len - modV(len)
    // left << 1 <= len - modV(len) + cpu.v
    
    int right = len - left;
    return std::make_pair(left,right);
}

template<CPMethod method, Size32 T>
void sorting(T *a, int len){
    std::unique_ptr<Task[]> ts = std::make_unique<Task[]>(64);
    std::unique_ptr<T[]> extraBuffer = std::make_unique<T[]>(len);

    T *buf[2] = {a,extraBuffer.get()};
    Task *top,*bottom;
    top = bottom = ts.get();

    bottom->srcBuf = 0;
    bottom->destBuf = 0;
    bottom->start = 0;
    bottom->len = len;
    bottom->state = SortState::Init;

    while(true){
        if(top < bottom){
            return;
        }
        switch (top->state)
        {
        case SortState::Init:
            if(top->len <= cpu.v2){
                shortSort<method,T>(buf[top->srcBuf] + top->start, buf[top->destBuf] + top->start, top->len);
                --top;
                continue;
            } else {
                auto newTop = top + 1;
                auto [left,right] = getChildLen(top->len);

                newTop->srcBuf = top->srcBuf;
                newTop->destBuf = 1 - top->destBuf;
                newTop->start = top->start;
                newTop->len = left;
                newTop->state = SortState::Init;
                top->state = SortState::LeftIssued;
                ++top;
                continue;
            }
            break;
        case SortState::LeftIssued:
        {
            auto newTop = top + 1;
            auto [left,right] = getChildLen(top->len);

            newTop->srcBuf = top->srcBuf;
            newTop->destBuf = 1 - top->destBuf;
            newTop->start = top->start + left;
            newTop->len = right;
            newTop->state = SortState::Init;
            top->state = SortState::RightIssued;
            ++top;
            continue;
        }
            break;
        case SortState::RightIssued:
        {
            auto [left,right] = getChildLen(top->len);
            mergeSort<method,T>(buf[1 - top->destBuf]+top->start, left, buf[1-top->destBuf]+top->start+left,right,buf[top->destBuf]+top->start);
            --top;
            continue;
        }
            break;
        default:
            break;
        }
    }
    return;
}

int testSetOperation(PolicyStruct policy, uint32_t *fromA, int lenA, uint32_t *fromB, int lenB, uint32_t* toC, uint32_t *toA, uint32_t *toB){
    Limit limit = {cpu.logV,{OpSrc::B,Delta::NotEqual}};
    uint64_t limitMask = pack(limit);
    auto policyMask = policy.policyMask;
    auto simPolicyMask = policy.simPolicyMask;
    auto limitOp2 = GetLimitOp2{ForceEq::Yes,policy.eagerMask,{{Next::Same,Next::Inf},{Next::Same,Next::Inf}}};
    uint64_t getLimitOp2Mask = pack(limitOp2);
    
    int pa,pb,pc;
    pa = pb = pc = 0;

    uint32_t lastA, lastB;
    lastA = lastB = 0;

    auto predTrue = whilelt(0,cpu.v);

    while(pa < lenA || pb < lenB){
        auto predA = whilelt(pa,lenA);
        auto predB = whilelt(pb,lenB);
        auto idxA = load(predA,fromA + pa);
        auto idxB = load(predB,fromB + pb);


        auto bigCmp = InitBigCmp(limitMask,predA,predB);
        bigCmp = KeyCombine(bigCmp,idxA,idxB);

        auto matRes = Match(bigCmp,policyMask.A,policyMask.B);
        auto newLimit = GetLimit(bigCmp,simPolicyMask,getLimitOp2Mask);

        auto unpackLimit = unpack<Limit>(newLimit);

        auto genC = unpackLimit.generate.A;

        auto predCLeft = whilelt(0,genC);
        auto idxALeft = SEPermute<SEPart{OpSrc::A,LRPart::Left}>(matRes,idxA,SEPair{0u,lastA});
        auto idxBLeft = SEPermute<SEPart{OpSrc::B,LRPart::Left}>(matRes,idxB,SEPair{0u,lastB});
        store(predCLeft,toA+pc,idxALeft);
        store(predCLeft,toB+pc,idxBLeft);
        store(predCLeft,toC+pc,simd_or(predTrue,idxALeft,idxBLeft));
        lastA = lastActiveElem(predCLeft,idxALeft,lastA);
        lastB = lastActiveElem(predCLeft,idxBLeft,lastB);

        if(genC > cpu.v){
            auto predCRight = whilelt(cpu.v,genC);
            auto idxARight = SEPermute<SEPart{OpSrc::A,LRPart::Right}>(matRes,idxA,SEPair{0u,lastA});
            auto idxBRight = SEPermute<SEPart{OpSrc::B,LRPart::Right}>(matRes,idxB,SEPair{0u,lastB});
            store(predCRight,toA+pc+cpu.v,idxARight);
            store(predCRight,toB+pc+cpu.v,idxBRight);
            store(predCRight,toC+pc+cpu.v, simd_or(predTrue,idxARight,idxBRight));
            lastA = lastActiveElem(predCRight,idxBRight,lastA);
            lastB = lastActiveElem(predCRight,idxBRight,lastB);
        }

        pa += unpackLimit.consume.A;
        pb += unpackLimit.consume.B;
        pc += unpackLimit.generate.A;
        limitMask = newLimit;
    }
    return pc;
}


template<CPMethod method, Size32 T>
bool testSort(int seed, int len){
    // printf("cpu.v = %d, cpu.logV = %d\n",cpu.v,cpu.logV);
    std::vector<T> oldArr;
    std::minstd_rand0 gen(seed);
    std::uniform_real_distribution<float> dist(-16*len,16*len);
    // std::uniform_int_distribution<uint32_t> dist(0,16*len);
    for(int i=0; i<len;++i){
        auto v = dist(gen);
        oldArr.emplace_back(reinterpret_cast<T&>(v));
    }

    auto toSort1 = oldArr;
    std::sort(toSort1.begin(),toSort1.end());
    auto toSort2 = oldArr;
    sorting<method,T>(toSort2.data(),toSort2.size());
    // for(int i=0; i<len; ++i){
    //     printf("%4d\t%4d\t%4d\t%4d\n",i,oldArr[i],toSort1[i],toSort2[i]);
    // }

    bool equal = toSort1 == toSort2;
    
    printf("Test seed = %4d, len = %4d, result = %4d\n",seed,len,equal);
    fflush(stdout);
    return equal;
}

struct TestSet{
    std::vector<uint32_t> a,b,ca,cb,c;

    TestSet(int len){
        std::minstd_rand0 gen;
        std::uniform_int_distribution<uint32_t> dist;
        // std::vector<uint32_t> keySet;

        std::unordered_set<uint32_t> keySet;
        while(keySet.size() < 2*len){
            keySet.insert(dist(gen));
        }
        a.insert(a.end(),keySet.begin(),keySet.end());
        std::shuffle(a.begin(),a.end(),gen);
        a.resize(len);
        std::sort(a.begin(),a.end());

        b.insert(b.end(),keySet.begin(),keySet.end());
        std::shuffle(b.begin(),b.end(),gen);
        b.resize(len);
        std::sort(b.begin(),b.end());

        // std::uniform_int_distribution<uint32_t> distSelect(0,keySet.size()-1);
        // for(int i=0; i<len; ++i){
        //     a.emplace_back(keySet[distSelect(gen)]);
        //     b.emplace_back(keySet[distSelect(gen)]);
        // }
        ca.resize(2*len);
        cb.resize(2*len);
        c.resize(2*len);
    }



    void run(PolicyStruct policy){
        int lenC = testSetOperation(policy,a.data(),a.size(),b.data(),b.size(),c.data(),ca.data(),cb.data());
        c.resize(lenC);
        ca.resize(lenC);
        cb.resize(lenC);
    }

};


bool testSetGeneral(int len,PolicyStruct policy, std::function<bool(bool,bool)> fun){
    TestSet t(len);
    t.run(policy);
    std::unordered_set<uint32_t> sa,sb,sunion,sresult;
    sa.insert(t.a.begin(),t.a.end());
    sb.insert(t.b.begin(),t.b.end());

    sunion.insert(t.a.begin(),t.a.end());
    sunion.insert(t.b.begin(),t.b.end());


    for(auto x:sunion){
        bool ba = sa.contains(x);
        bool bb = sb.contains(x);
        if(fun(ba,bb)){
            sresult.insert(x);
        }
    }

    std::unordered_set<uint32_t> s2;
    s2.insert(t.c.begin(),t.c.end());

    bool result = sresult == s2;
    if(!result){
        throw("What is the fuck");
    }
    printf("Check %d - %d\n",len,result);
    fflush(stdout);

    return result;
}

int shortAdd(uint32_t *from, int* fromVal, uint32_t* to, int* toVal, int len){
    auto predLeft = whilelt(0,len);
    auto predRight = whilelt(cpu.v,len);
    auto idxLeft = load(predLeft,from);
    auto idxRight = load(predRight,from+cpu.v);
    auto valLeft = load(predLeft,fromVal);
    auto valRight = load(predRight,fromVal+cpu.v);
    auto allTrue = whilelt(0,cpu.v);


    static constexpr uint64_t getLimitOp2 = pack(GetLimitOp2{ForceEq::Yes,PolicyOR.eagerMask,{{Next::Inf,Next::Inf},{Next::Inf,Next::Inf}}}); 

    VBigCmp bigCmp = InitBigCmp(shortLimit,predLeft,predRight);

    for(int st=0; st < cpu.logV; ++st){
        bigCmp = KeyCombine(bigCmp,idxLeft,idxRight);
        VMatRes matRes = Match(bigCmp,PolicyOR.policyMask.A,PolicyOR.policyMask.B);

        auto idxLeftA = SEPermute<SEPart{OpSrc::A,LRPart::Left}>(matRes,idxLeft,SEPair{0u,0u});
        auto idxLeftB = SEPermute<SEPart{OpSrc::B,LRPart::Left}>(matRes,idxLeft,SEPair{0u,0u});
        auto newIdxLeft = simd_or(allTrue,idxLeftA,idxLeftB);
        auto valLeftA = SEPermute<SEPart{OpSrc::A,LRPart::Left}>(matRes,valLeft,SEPair{0,0});
        auto valLeftB = SEPermute<SEPart{OpSrc::B,LRPart::Left}>(matRes,valLeft,SEPair{0,0});
        auto newValLeft = simd_add(allTrue,valLeftA,valLeftB);

        auto idxRightA = SEPermute<SEPart{OpSrc::A,LRPart::Right}>(matRes,idxRight,SEPair{0u,0u});
        auto idxRightB = SEPermute<SEPart{OpSrc::B,LRPart::Right}>(matRes,idxRight,SEPair{0u,0u});
        auto newIdxRight = simd_or(allTrue,idxRightA,idxRightB);
        auto valRightA = SEPermute<SEPart{OpSrc::A,LRPart::Right}>(matRes,valRight,SEPair{0,0});
        auto valRightB = SEPermute<SEPart{OpSrc::B,LRPart::Right}>(matRes,valRight,SEPair{0,0});
        auto newValRight = simd_add(allTrue,valRightA,valRightB);

        idxLeft = newIdxLeft;
        idxRight = newIdxRight;
        valLeft = newValLeft;
        valRight = newValRight;

        bigCmp = NextBigCmpFromMatRes(shortLimit+st+1,matRes);
    }
    
    auto idxA = idxLeft;
    auto idxB = idxRight;
    auto valA = valLeft;
    auto valB = valRight;

    bigCmp = KeyCombine(bigCmp,idxA,idxB);
    VMatRes matRes = Match(bigCmp,PolicyOR.policyMask.A,PolicyOR.policyMask.B);
    uint64_t newLimit = GetLimit(bigCmp,PolicyOR.simPolicyMask,getLimitOp2);
    Limit unpackLimit = unpack<Limit>(newLimit);
    int genC = unpackLimit.generate.A;

    {
        auto predLeft = whilelt(0,genC);
        auto idxLeftA = SEPermute<SEPart{OpSrc::A,LRPart::Left}>(matRes,idxA,SEPair{0u,0u});
        auto idxLeftB = SEPermute<SEPart{OpSrc::B,LRPart::Left}>(matRes,idxB,SEPair{0u,0u});
        auto idxLeft = simd_or(predLeft,idxLeftA,idxLeftB);
        auto valLeftA = SEPermute<SEPart{OpSrc::A,LRPart::Left}>(matRes,valA,SEPair{0,0});
        auto valLeftB = SEPermute<SEPart{OpSrc::B,LRPart::Left}>(matRes,valB,SEPair{0,0});
        auto valLeft = simd_add(predLeft,valLeftA,valLeftB);

        store(predLeft,to,idxLeft);
        store(predLeft,toVal,valLeft);
    }

    if(genC > cpu.v){
        auto predRight = whilelt(cpu.v,genC);
        auto idxRightA = SEPermute<SEPart{OpSrc::A,LRPart::Right}>(matRes,idxA,SEPair{0u,0u});
        auto idxRightB = SEPermute<SEPart{OpSrc::B,LRPart::Right}>(matRes,idxB,SEPair{0u,0u});
        auto idxRight = simd_or(predRight,idxRightA,idxRightB);
        auto valRightA = SEPermute<SEPart{OpSrc::A,LRPart::Right}>(matRes,valA,SEPair{0,0});
        auto valRightB = SEPermute<SEPart{OpSrc::B,LRPart::Right}>(matRes,valB,SEPair{0,0});
        auto valRight = simd_add(predRight,valRightA,valRightB);
        store(predRight,to+cpu.v,idxRight);
        store(predRight,toVal+cpu.v,valRight);
    }
    
    return genC;
}

int longAdd(uint32_t *fromA, int *fromValA, int lenA, uint32_t *fromB, int *fromValB, int lenB, uint32_t *toC, int *toValC){
    int pa,pb,pc;
    pa = pb = pc = 0;

    static constexpr uint64_t getLimitOp2 = pack(GetLimitOp2{ForceEq::Yes,PolicyOR.eagerMask,{{Next::Epsilon,Next::Inf},{Next::Epsilon,Next::Inf}}}); 

    while(pa < lenA || pb < lenB){
        auto predA = whilelt(pa,lenA);
        auto predB = whilelt(pb,lenB);
        auto idxA = load(predA,fromA+pa);
        auto idxB = load(predB,fromB+pb);
        auto valA = load(predA,fromValA+pa);
        auto valB = load(predB,fromValB+pb);

        VBigCmp bigCmp = InitBigCmp(longLimit,predA,predB);
        bigCmp = KeyCombine(bigCmp,idxA,idxB);
        VMatRes matRes = Match(bigCmp,PolicyOR.policyMask.A,PolicyOR.policyMask.B);
        uint64_t newLimit = GetLimit(bigCmp,PolicyOR.simPolicyMask,getLimitOp2);
        Limit unpackLimit = unpack<Limit>(newLimit);
        int genC = unpackLimit.generate.A;

        auto predLeft = whilelt(0,genC);
        auto idxLeftA = SEPermute<SEPart{OpSrc::A,LRPart::Left}>(matRes,idxA,SEPair{0u,0u});
        auto idxLeftB = SEPermute<SEPart{OpSrc::B,LRPart::Left}>(matRes,idxB,SEPair{0u,0u});
        auto idxLeft = simd_or(predLeft,idxLeftA,idxLeftB);
        auto valLeftA = SEPermute<SEPart{OpSrc::A,LRPart::Left}>(matRes,valA,SEPair{0,0});
        auto valLeftB = SEPermute<SEPart{OpSrc::B,LRPart::Left}>(matRes,valB,SEPair{0,0});
        auto valLeft = simd_add(predLeft,valLeftA,valLeftB);

        store(predLeft,toC + pc,idxLeft);
        store(predLeft,toValC + pc,valLeft);

        if(genC > cpu.v){
            auto predRight = whilelt(cpu.v,genC);
            auto idxRightA = SEPermute<SEPart{OpSrc::A,LRPart::Right}>(matRes,idxA,SEPair{0u,0u});
            auto idxRightB = SEPermute<SEPart{OpSrc::B,LRPart::Right}>(matRes,idxB,SEPair{0u,0u});
            auto idxRight = simd_or(predRight,idxRightA,idxRightB);
            auto valRightA = SEPermute<SEPart{OpSrc::A,LRPart::Right}>(matRes,valA,SEPair{0,0});
            auto valRightB = SEPermute<SEPart{OpSrc::B,LRPart::Right}>(matRes,valB,SEPair{0,0});
            auto valRight = simd_add(predRight,valRightA,valRightB);
            store(predRight,toC+ + pc +cpu.v,idxRight);
            store(predRight,toValC+ pc + cpu.v,valRight);
        }

        pa += unpackLimit.consume.A;
        pb += unpackLimit.consume.B;
        pc += unpackLimit.generate.A;
    }
    return pc;
}

struct Loc{
    int start,len;
    uint8_t bufId;
};
struct StackTerm : public Loc{
    uint8_t power;
};


int addAggregation(uint32_t* idx, int* val, int len){
    std::vector<uint32_t> idx2(len);
    std::vector<int> val2(len);
    uint32_t * bufIdx[2] = {idx,idx2.data()};
    int * bufVal[2] = {val,val2.data()};

//

    std::vector<StackTerm> stk(100);
    StackTerm *top,*bottom;
    stk[0].power = 103;
    stk[1].power = 102;
    stk[2].power = 101;
    stk[2].start = 0;
    stk[2].len = 0;
    top = bottom = stk.data() + 2;

    // Aggregation stack machine
    auto mergeOneAndTop = [&](StackTerm *upper, StackTerm *lower){
        StackTerm dest;
        dest.bufId = 1 - upper->bufId;
        dest.start = upper->start;
        dest.len = longAdd(
            bufIdx[upper->bufId]+upper->start,bufVal[upper->bufId]+upper->start,upper->len,
            bufIdx[lower->bufId]+lower->start,bufVal[lower->bufId]+lower->start,lower->len,
            bufIdx[dest.bufId] + dest.start, bufVal[dest.bufId]+dest.start
        );
        dest.power = std::bit_width(uint32_t(dest.len));
        *upper = dest;
    };
    
    
    auto extendTerm = [&](uint8_t bufId, int start, int len){
        ++top;
        top->bufId=0;
        top->start = (top-1)->start + (top-1)->len;
        int lc = shortAdd(bufIdx[bufId]+start,bufVal[bufId]+start,
                        bufIdx[top->bufId]+top->start,bufVal[top->bufId]+top->start,len);
        top->len = lc;
        top->power = std::bit_width( uint32_t(top->len));

        while(top != bottom+1){
            int p1,p2,p3;
            p1 = top->power;
            p2 = (top-1)->power;
            p3 = (top-2)->power;
            if(p1 >= p3 || p2 >= p3){
                mergeOneAndTop(top-2,top-1);
                *(top-1) = *top;
                --top;
            } else if (p1 >= p2){
                mergeOneAndTop(top-1,top);
                --top;
            } else {
                break;
            }
        }
    };

    auto finalReduce = [&](){
        while(top != bottom+1){
            mergeOneAndTop(top-1,top);
            --top;
        }
    };

    // Creater machine
    int pa =0;
    while(pa < len){
        int newLen = std::min(len-pa,int(cpu.v2));
        extendTerm(0,pa,newLen);
        pa += newLen;
    }

    finalReduce();

    if(top->bufId==1){
        std::copy(bufIdx[1]+top->start,bufIdx[1]+top->start+top->len,bufIdx[0]+top->start);
        std::copy(bufVal[1]+top->start,bufVal[1]+top->start+top->len,bufVal[0]+top->start);
    }
    return top->len;
}

bool testAggregation(int len, int ratio, int valRange){
    std::minstd_rand0 gen;
    // std::uniform_int_distribution<uint32_t> dist0;
    std::uniform_int_distribution<uint32_t> dist0(0,len);
    std::unordered_set<uint32_t> distinctKey;
    while(distinctKey.size() < 1+(len/ratio)){
        distinctKey.insert(dist0(gen));
    }
    std::vector<uint32_t> keyVec(distinctKey.begin(),distinctKey.end());
    std::uniform_int_distribution<uint32_t> dist(0,len/ratio);
    std::vector<uint32_t> idxVec;
    idxVec.reserve(len);
    for(int i=0; i<len; ++i){
        idxVec.emplace_back(keyVec[dist(gen)]);
    }

    std::uniform_int_distribution<int> distInt(0,valRange);
    std::vector<int> valVec;
    valVec.reserve(len);
    for(int i=0; i<len;++i){
        valVec.emplace_back(distInt(gen));
    }

    std::map<uint32_t,int> theMap;
    for(int i=0; i<len; ++i){
        theMap[idxVec[i]] += valVec[i];
    }

    std::vector<uint32_t> outIdxVec;
    std::vector<int> outValVec;
    for(auto [k,v]:theMap){
        outIdxVec.emplace_back(k);
        outValVec.emplace_back(v);
    }
    
    auto idxVecUse = idxVec;
    auto valVecUse = valVec;
    int lenC = addAggregation(idxVecUse.data(),valVecUse.data(),idxVecUse.size());
    idxVecUse.resize(lenC);
    valVecUse.resize(lenC);

    bool idxEqual = idxVecUse == outIdxVec;
    bool valEqual = valVecUse == outValVec;
    
    bool bothEqual = idxEqual && valEqual;
    if(!bothEqual){
            auto idxVecUseT = idxVec;
            auto valVecUseT = valVec;
            int lenC = addAggregation(idxVecUseT.data(),valVecUseT.data(),idxVecUseT.size());
            idxVecUseT.resize(lenC);
            valVecUseT.resize(lenC);
        throw("What is the fuck! ");
    }
    printf("len = %4d\t%d\n",len,bothEqual);
    fflush(stdout);

    return bothEqual;
}

int main(){

    bool allPass = true;


    // for(int i=1; i<100;++i){
    for(int i=1; i<100;++i){
        allPass &= testSort<CPMethod::IsUInt,uint32_t>(i,i);
        allPass &= testSort<CPMethod::IsInt,int>(i,i);
        allPass &= testSort<CPMethod::IsFloat,float>(i,i);
    }
    printf("Total result: %d\n",allPass);


    for(int i=1; i<100; ++i){
        allPass &= testAggregation(i,2,3);
        if(!allPass){
            break;
        }
    }
    printf("Total result %d\n",allPass);


    for(int i=1; i<100;++i){
        allPass &= testSetGeneral(i,PolicyOR,[](bool a,bool b){
            return a || b;
        });
        allPass &= testSetGeneral(i,PolicyAND,[](bool a,bool b){
            return a && b;
        });
        allPass &= testSetGeneral(i,PolicyDiff,[](bool a,bool b){
            return a && (!b);
        });
        allPass &= testSetGeneral(i,PolicyXOR,[](bool a,bool b){
            return a^b;
        });
        if(!allPass){
            break;
        }
    }

    printf("Total result: %d\n",allPass);
    

}