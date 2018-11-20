/*
 * Copyright 2018, Yahoo! Inc. Licensed under the terms of the
 * Apache License 2.0. See LICENSE file at the project root for terms.
 */

#include "HllArray.hpp"
#include "HllUtil.hpp"
#include "HarmonicNumbers.hpp"
#include "CubicInterpolation.hpp"
#include "CompositeInterpolationXTable.hpp"
#include "RelativeErrorTables.hpp"
#include "CouponList.hpp"
#include "Hll8Array.hpp"
#include "Hll6Array.hpp"
#include "Hll4Array.hpp"
#include "Conversions.hpp"

#include <cstring>
#include <cmath>

namespace datasketches {

HllArray::HllArray(const int lgConfigK, const TgtHllType tgtHllType)
  : HllSketchImpl(lgConfigK, tgtHllType, CurMode::HLL) {
  hipAccum = 0.0;
  kxq0 = 1 << lgConfigK;
  kxq1 = 0.0;
  curMin = 0;
  numAtCurMin = 1 << lgConfigK;
  oooFlag = false;
  hllByteArr = nullptr; // allocated in derived class
}

HllArray::HllArray(const HllArray& that)
  : HllSketchImpl(that.lgConfigK, that.tgtHllType, CurMode::HLL) {
  hipAccum = that.getHipAccum();
  kxq0 = that.getKxQ0();
  kxq1 = that.getKxQ1();
  curMin = that.getCurMin();
  numAtCurMin = that.getNumAtCurMin();
  oooFlag = that.isOutOfOrderFlag();

  // can determine length, so allocate here
  int arrayLen = that.getHllByteArrBytes();
  hllByteArr = new uint8_t[arrayLen];
  std::copy(that.hllByteArr, that.hllByteArr + arrayLen, hllByteArr);
}

HllArray::~HllArray() {
  delete [] hllByteArr;
}

HllArray* HllArray::copyAs(const TgtHllType tgtHllType) const {
  if (tgtHllType == getTgtHllType()) {
    return (HllArray*) copy();
  }
  if (tgtHllType == TgtHllType::HLL_4) {
    return Conversions::convertToHll4(*this);
  } else if (tgtHllType == TgtHllType::HLL_6) {
    return Conversions::convertToHll6(*this);
  } else { // tgtHllType == HLL_8
    return Conversions::convertToHll8(*this);
  }
}

HllArray* HllArray::newHll(const int lgConfigK, const TgtHllType tgtHllType) {
  switch (tgtHllType) {
    case HLL_8:
      return (HllArray*) new Hll8Array(lgConfigK);
    case HLL_6:
      return (HllArray*) new Hll6Array(lgConfigK);
    case HLL_4:
      return (HllArray*) new Hll4Array(lgConfigK);
    default:
      throw std::invalid_argument("Impossible HLL type");
  }
}

HllArray* HllArray::newHll(std::istream& is) {
  uint8_t listHeader[8];
  is.read((char*)listHeader, 8 * sizeof(uint8_t));

  if (listHeader[0] != HllUtil::HLL_PREINTS) {
    throw std::invalid_argument("Incorrect number of preInts in input stream");
  }
  if (listHeader[1] != HllUtil::SER_VER) {
    throw std::invalid_argument("Wrong ser ver in input stream");
  }
  if (listHeader[2] != HllUtil::FAMILY_ID) {
    throw std::invalid_argument("Input stream is not an HLL sketch");
  }

  CurMode curMode = extractCurMode(listHeader[7]);
  if (curMode != HLL) {
    throw std::invalid_argument("Calling HLL construtor with non-HLL mode data");
  }

  TgtHllType tgtHllType = extractTgtHllType(listHeader[7]);
  bool oooFlag = ((listHeader[5] & HllUtil::OUT_OF_ORDER_FLAG_MASK) ? true : false);
  bool comapctFlag = ((listHeader[5] & HllUtil::COMPACT_FLAG_MASK) ? true : false);

  const int lgK = (int) listHeader[3];
  const int curMin = (int) listHeader[6];

  HllArray* sketch = newHll(lgK, tgtHllType);
  sketch->putCurMin(curMin);
  sketch->putOutOfOrderFlag(oooFlag);

  double hip, kxq0, kxq1;
  is.read((char*)&hip, sizeof(hip));
  is.read((char*)&kxq0, sizeof(kxq0));
  is.read((char*)&kxq1, sizeof(kxq1));
  sketch->putHipAccum(hip);
  sketch->putKxQ0(kxq0);
  sketch->putKxQ1(kxq1);

  int numAtCurMin, auxCount;
  is.read((char*)&numAtCurMin, sizeof(numAtCurMin));
  is.read((char*)&auxCount, sizeof(auxCount));
  sketch->putNumAtCurMin(numAtCurMin);
  
  is.read((char*)sketch->hllByteArr, sketch->getHllByteArrBytes());
  
  if (auxCount > 0) { // necessarily TgtHllType == HLL_4
    int auxLgIntArrSize = (int) listHeader[4];
    AuxHashMap* auxHashMap = AuxHashMap::deserialize(is, lgK, auxCount, auxLgIntArrSize, comapctFlag);
    ((Hll4Array*)sketch)->putAuxHashMap(auxHashMap);
  }

  return sketch;
}

void HllArray::serialize(std::ostream& os, const bool compact) const {
  // header
  const uint8_t preInts(getPreInts());
  os.write((char*)&preInts, sizeof(preInts));
  const uint8_t serialVersion(HllUtil::SER_VER);
  os.write((char*)&serialVersion, sizeof(serialVersion));
  const uint8_t familyId(HllUtil::FAMILY_ID);
  os.write((char*)&familyId, sizeof(familyId));
  const uint8_t lgKByte((uint8_t) lgConfigK);
  os.write((char*)&lgKByte, sizeof(lgKByte));

  AuxHashMap* auxHashMap = getAuxHashMap();
  uint8_t lgArrByte(0);
  if (auxHashMap != nullptr) {
    lgArrByte = auxHashMap->getLgAuxArrInts();
  }
  os.write((char*)&lgArrByte, sizeof(lgArrByte));

  const uint8_t flagsByte(makeFlagsByte(compact));
  os.write((char*)&flagsByte, sizeof(flagsByte));
  const uint8_t curMinByte((uint8_t) curMin);
  os.write((char*)&curMinByte, sizeof(curMinByte));
  const uint8_t modeByte(makeModeByte());
  os.write((char*)&modeByte, sizeof(modeByte));

  // estimator data
  os.write((char*)&hipAccum, sizeof(hipAccum));
  os.write((char*)&kxq0, sizeof(kxq0));
  os.write((char*)&kxq1, sizeof(kxq1));

  // array data
  os.write((char*)&numAtCurMin, sizeof(numAtCurMin));

  const int auxCount = (auxHashMap == nullptr ? 0 : auxHashMap->getAuxCount());
  os.write((char*)&auxCount, sizeof(auxCount));
  os.write((char*)hllByteArr, getHllByteArrBytes());

  // aux map if HLL_4
  if (tgtHllType == HLL_4) {
    if (auxHashMap != nullptr) {
      if (compact) {
        std::unique_ptr<PairIterator> itr = auxHashMap->getIterator();
        while (itr->nextValid()) {
          const int pairValue = itr->getPair();
          os.write((char*)&pairValue, sizeof(pairValue));
        }
      } else {
        os.write((char*)auxHashMap->getAuxIntArr(), auxHashMap->getUpdatableSizeBytes());
      }
    } else if (!compact) {
      // if updatable, we write even if currently unused so the binary can be wrapped      
      int auxBytes = 4 << HllUtil::LG_AUX_ARR_INTS[lgConfigK];
      std::fill_n(std::ostreambuf_iterator<char>(os), auxBytes, 0);
    }
  }
}

HllSketchImpl* HllArray::couponUpdate(const int coupon) { // used by HLL_8 and HLL_6
  const int configKmask = (1 << getLgConfigK()) - 1;
  const int slotNo = HllUtil::getLow26(coupon) & configKmask;
  const int newVal = HllUtil::getValue(coupon);
  assert(newVal > 0);

  const int curVal = getSlot(slotNo);
  if (newVal > curVal) {
    putSlot(slotNo, newVal);
    hipAndKxQIncrementalUpdate(*this, curVal, newVal);
    if (curVal == 0) {
      decNumAtCurMin(); // interpret numAtCurMin as num zeros
      assert(getNumAtCurMin() >= 0);
    }
  }
  return this;
}

HllSketchImpl* HllArray::reset() {
  return new CouponList(lgConfigK, tgtHllType, CurMode::LIST);
}

double HllArray::getEstimate() const {
  if (oooFlag) {
    return getCompositeEstimate();
  }
  return getHipAccum();
}

// HLL UPPER AND LOWER BOUNDS

/*
 * The upper and lower bounds are not symmetric and thus are treated slightly differently.
 * For the lower bound, when the unique count is <= k, LB >= numNonZeros, where
 * numNonZeros = k - numAtCurMin AND curMin == 0.
 *
 * For HLL6 and HLL8, curMin is always 0 and numAtCurMin is initialized to k and is decremented
 * down for each valid update until it reaches 0, where it stays. Thus, for these two
 * isomorphs, when numAtCurMin = 0, means the true curMin is > 0 and the unique count must be
 * greater than k.
 *
 * HLL4 always maintains both curMin and numAtCurMin dynamically. Nonetheless, the rules for
 * the very small values <= k where curMin = 0 still apply.
 */
double HllArray::getLowerBound(const int numStdDev) const {
  HllUtil::checkNumStdDev(numStdDev);
  const int configK = 1 << lgConfigK;
  const double numNonZeros = ((curMin == 0) ? (configK - numAtCurMin) : configK);

  double estimate;
  double rseFactor;
  if (oooFlag) {
    estimate = getCompositeEstimate();
    rseFactor = HllUtil::HLL_NON_HIP_RSE_FACTOR;
  } else {
    estimate = hipAccum;
    rseFactor = HllUtil::HLL_HIP_RSE_FACTOR;
  }

  double relErr;
  if (lgConfigK > 12) {
    relErr = (numStdDev * rseFactor) / sqrt(configK);
  } else {
    relErr = RelativeErrorTables::getRelErr(false, oooFlag, lgConfigK, numStdDev);
  }
  return fmax(estimate / (1.0 + relErr), numNonZeros);
}

double HllArray::getUpperBound(const int numStdDev) const {
  HllUtil::checkNumStdDev(numStdDev);
  const int configK = 1 << lgConfigK;

  double estimate;
  double rseFactor;
  if (oooFlag) {
    estimate = getCompositeEstimate();
    rseFactor = HllUtil::HLL_NON_HIP_RSE_FACTOR;
  } else {
    estimate = hipAccum;
    rseFactor = HllUtil::HLL_HIP_RSE_FACTOR;
  }

  double relErr;
  if (lgConfigK > 12) {
    relErr = (-1.0) * (numStdDev * rseFactor) / sqrt(configK);
  } else {
    relErr = RelativeErrorTables::getRelErr(true, oooFlag, lgConfigK, numStdDev);
  }
  return estimate / (1.0 + relErr);
}

/**
 * This is the (non-HIP) estimator.
 * It is called "composite" because multiple estimators are pasted together.
 * @param absHllArr an instance of the AbstractHllArray class.
 * @return the composite estimate
 */
// Original C: again-two-registers.c hhb_get_composite_estimate L1489
double HllArray::getCompositeEstimate() const {
  const double rawEst = getHllRawEstimate(lgConfigK, kxq0 + kxq1);

  const double* xArr = CompositeInterpolationXTable::get_x_arr(lgConfigK);
  const int xArrLen = CompositeInterpolationXTable::get_x_arr_length(lgConfigK);
  const double yStride = CompositeInterpolationXTable::get_y_stride(lgConfigK);

  if (rawEst < xArr[0]) {
    return 0;
  }

  const int xArrLenM1 = xArrLen - 1;

  if (rawEst > xArr[xArrLenM1]) {
    double finalY = yStride * xArrLenM1;
    double factor = finalY / xArr[xArrLenM1];
    return rawEst * factor;
  }

  double adjEst = CubicInterpolation::usingXArrAndYStride(xArr, xArrLen, yStride, rawEst);

  // We need to completely avoid the linear_counting estimator if it might have a crazy value.
  // Empirical evidence suggests that the threshold 3*k will keep us safe if 2^4 <= k <= 2^21.

  if (adjEst > (3 << lgConfigK)) { return adjEst; }
  //Alternate call
  //if ((adjEst > (3 << lgConfigK)) || ((curMin != 0) || (numAtCurMin == 0)) ) { return adjEst; }

  const double linEst =
      getHllBitMapEstimate(lgConfigK, curMin, numAtCurMin);

  // Bias is created when the value of an estimator is compared with a threshold to decide whether
  // to use that estimator or a different one.
  // We conjecture that less bias is created when the average of the two estimators
  // is compared with the threshold. Empirical measurements support this conjecture.

  const double avgEst = (adjEst + linEst) / 2.0;

  // The following constants comes from empirical measurements of the crossover point
  // between the average error of the linear estimator and the adjusted hll estimator
  double crossOver = 0.64;
  if (lgConfigK == 4)      { crossOver = 0.718; }
  else if (lgConfigK == 5) { crossOver = 0.672; }

  return (avgEst > (crossOver * (1 << lgConfigK))) ? adjEst : linEst;
}

double HllArray::getKxQ0() const {
  return kxq0;
}

double HllArray::getKxQ1() const {
  return kxq1;
}

double HllArray::getHipAccum() const {
  return hipAccum;
}

int HllArray::getCurMin() const {
  return curMin;
}

int HllArray::getNumAtCurMin() const {
  return numAtCurMin;
}

CurMode HllArray::getCurMode() const {
  return curMode;
}

void HllArray::putKxQ0(const double kxq0) {
  this->kxq0 = kxq0;
}

void HllArray::putKxQ1(const double kxq1) {
  this->kxq1 = kxq1;
}

void HllArray::putHipAccum(const double hipAccum) {
  this->hipAccum = hipAccum;
}

void HllArray::putCurMin(const int curMin) {
  this->curMin = curMin;
}

void HllArray::putNumAtCurMin(const int numAtCurMin) {
  this->numAtCurMin = numAtCurMin;
}

void HllArray::decNumAtCurMin() {
  --numAtCurMin;
}

void HllArray::addToHipAccum(const double delta) {
  hipAccum += delta;
}

bool HllArray::isCompact() const {
  return false;
}

bool HllArray::isEmpty() const {
  const int configK = 1 << getLgConfigK();
  return (getCurMin() == 0) && (getNumAtCurMin() == configK);
}

void HllArray::putOutOfOrderFlag(bool flag) {
  oooFlag = flag;
}

bool HllArray::isOutOfOrderFlag() const {
  return oooFlag;
}

int HllArray::hll4ArrBytes(const int lgConfigK) {
  return 1 << (lgConfigK - 1);
}

int HllArray::hll6ArrBytes(const int lgConfigK) {
  const int numSlots = 1 << lgConfigK;
  return ((numSlots * 3) >> 2) + 1;
}

int HllArray::hll8ArrBytes(const int lgConfigK) {
  return 1 << lgConfigK;
}

int HllArray::getMemDataStart() const {
  return HllUtil::HLL_BYTE_ARR_START;
}

int HllArray::getUpdatableSerializationBytes() const {
  return HllUtil::HLL_BYTE_ARR_START + getHllByteArrBytes();
}

int HllArray::getCompactSerializationBytes() const {
  AuxHashMap* auxHashMap = getAuxHashMap();
  const int auxCountBytes = ((auxHashMap == nullptr) ? 0 : auxHashMap->getCompactSizeBytes());
  return HllUtil::HLL_BYTE_ARR_START + getHllByteArrBytes() + auxCountBytes;
}

int HllArray::getPreInts() const {
  return HllUtil::HLL_PREINTS;
}

std::unique_ptr<PairIterator> HllArray::getAuxIterator() const {
  return nullptr;
}

AuxHashMap* HllArray::getAuxHashMap() const {
  return nullptr;
}

void HllArray::hipAndKxQIncrementalUpdate(HllArray& host, const int oldValue, const int newValue) {
  assert(newValue > oldValue);

  const int configK = 1 << host.getLgConfigK();
  // update hipAccum BEFORE updating kxq0 and kxq1
  double kxq0 = host.getKxQ0();
  double kxq1 = host.getKxQ1();
  host.addToHipAccum(configK / (kxq0 + kxq1));
  // update kxq0 and kxq1; subtract first, then add
  if (oldValue < 32) { host.putKxQ0(kxq0 -= HllUtil::invPow2(oldValue)); }
  else               { host.putKxQ1(kxq1 -= HllUtil::invPow2(oldValue)); }
  if (newValue < 32) { host.putKxQ0(kxq0 += HllUtil::invPow2(newValue)); }
  else               { host.putKxQ1(kxq1 += HllUtil::invPow2(newValue)); }
}

/**
 * Estimator when N is small, roughly less than k log(k).
 * Refer to Wikipedia: Coupon Collector Problem
 * @return the very low range estimate
 */
//In C: again-two-registers.c hhb_get_improved_linear_counting_estimate L1274
double HllArray::getHllBitMapEstimate(const int lgConfigK, const int curMin, const int numAtCurMin) const {
  const  int configK = 1 << lgConfigK;
  const  int numUnhitBuckets =  ((curMin == 0) ? numAtCurMin : 0);

  //This will eventually go away.
  if (numUnhitBuckets == 0) {
    return configK * log(configK / 0.5);
  }

  const int numHitBuckets = configK - numUnhitBuckets;
  return HarmonicNumbers::getBitMapEstimate(configK, numHitBuckets);
}

//In C: again-two-registers.c hhb_get_raw_estimate L1167
double HllArray::getHllRawEstimate(const int lgConfigK, const double kxqSum) const {
  const int configK = 1 << lgConfigK;
  double correctionFactor;
  if (lgConfigK == 4) { correctionFactor = 0.673; }
  else if (lgConfigK == 5) { correctionFactor = 0.697; }
  else if (lgConfigK == 6) { correctionFactor = 0.709; }
  else { correctionFactor = 0.7213 / (1.0 + (1.079 / configK)); }
  const double hyperEst = (correctionFactor * configK * configK) / kxqSum;
  return hyperEst;
}

}
