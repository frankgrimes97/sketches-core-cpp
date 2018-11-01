/*
 * Copyright 2018, Yahoo! Inc. Licensed under the terms of the
 * Apache License 2.0. See LICENSE file at the project root for terms.
 */

#include "HllSketchImpl.hpp"
#include "HllArray.hpp"
#include "CouponList.hpp"
#include "CouponHashSet.hpp"

namespace datasketches {

#ifdef DEBUG
static int numImpls = 0;
#endif 

HllSketchImpl::HllSketchImpl(const int lgConfigK, const TgtHllType tgtHllType, const CurMode curMode)
  : lgConfigK(lgConfigK),
    tgtHllType(tgtHllType),
    curMode(curMode)
{
#ifdef DEBUG
  std::cerr << "Num impls: " << ++numImpls << "\n";
#endif
}

HllSketchImpl::~HllSketchImpl() {
#ifdef DEBUG
  std::cerr << "Num impls: " << --numImpls << "\n";
#endif
}

HllSketchImpl* HllSketchImpl::deserialize(std::istream& is) {
  // we'll hand off the sketch based on PreInts so we don't need
  // to move the stream pointer back and forth -- perhaps somewhat fragile?
  const int preInts = is.peek();
  if (preInts == HllUtil::HLL_PREINTS) {
    return HllArray::newHll(is);
  } else if (preInts == HllUtil::HASH_SET_PREINTS) {
    return CouponHashSet::newSet(is);
  } else if (preInts == HllUtil::LIST_PREINTS) {
    return CouponList::newList(is);
  } else {
    throw std::invalid_argument("Attempt to deserialize Unknown object type");
  }
}

TgtHllType HllSketchImpl::extractTgtHllType(const uint8_t modeByte) {
  switch ((modeByte >> 2) & 0x3) {
  case 0:
    return TgtHllType::HLL_4;
  case 1:
    return TgtHllType::HLL_6;
  case 2:
    return TgtHllType::HLL_8;
  default:
    throw std::invalid_argument("Invalid current sketch mode");
  }
}

CurMode HllSketchImpl::extractCurMode(const uint8_t modeByte) {
  switch (modeByte & 0x3) {
  case 0:
    return CurMode::LIST;
  case 1:
    return CurMode::SET;
  case 2:
    return CurMode::HLL;
  default:
    throw std::invalid_argument("Invalid current sketch mode");
  }
}

uint8_t HllSketchImpl::makeFlagsByte(const bool compact) const {
  uint8_t flags(0);
  flags |= (isEmpty() ? HllUtil::EMPTY_FLAG_MASK : 0);
  flags |= (compact ? HllUtil::COMPACT_FLAG_MASK : 0);
  flags |= (isOutOfOrderFlag() ? HllUtil::OUT_OF_ORDER_FLAG_MASK : 0);
  return flags;
}

// lo2bits = curMode, next 2 bits = tgtHllType
// Dec  Lo4Bits TgtHllType, CurMode
//   0     0000      HLL_4,    LIST
//   1     0001      HLL_4,     SET
//   2     0010      HLL_4,     HLL
//   4     0100      HLL_6,    LIST
//   5     0101      HLL_6,     SET
//   6     0110      HLL_6,     HLL
//   8     1000      HLL_8,    LIST
//   9     1001      HLL_8,     SET
//  10     1010      HLL_8,     HLL
uint8_t HllSketchImpl::makeModeByte() const {
  uint8_t byte;

  switch (curMode) {
  case SET:
    byte = 1;
    break;
  case HLL:
    byte = 2;
    break;
  case LIST:
  default:
    byte = 0;
  }

  switch (tgtHllType) {
  case HLL_4:
    byte |= (0 << 2);  // for completeness
    break;
  case HLL_6:
    byte |= (1 << 2);
    break;
  case HLL_8:
    byte |= (2 << 2); 
    break;
  }

  return byte;
}

TgtHllType HllSketchImpl::getTgtHllType() const {
  return tgtHllType;
}

int HllSketchImpl::getLgConfigK() const {
  return lgConfigK;
}

CurMode HllSketchImpl::getCurMode() const {
  return curMode;
}

}
