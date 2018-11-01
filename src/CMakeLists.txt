# list of source files
set(
  libsrc
  murmur3.cpp
  cpc/cpc_sketch.cpp
  cpc/fm85.c
  cpc/fm85Compression.c
  cpc/fm85Confidence.c
  cpc/fm85Merging.c
  cpc/fm85Util.c
  cpc/iconEstimator.c
  cpc/u32Table.c
  hll/AuxHashMap.cpp
  hll/CompositeInterpolationXTable.cpp
  hll/Conversions.cpp
  hll/CouponHashSet.cpp
  hll/CouponList.cpp
  hll/CubicInterpolation.cpp
  hll/HarmonicNumbers.cpp
  hll/Hll4Array.cpp
  hll/Hll6Array.cpp
  hll/Hll8Array.cpp
  hll/HllArray.cpp
  hll/HllPairIterator.cpp
  hll/HllSketch.cpp
  hll/HllSketchImpl.cpp
  hll/HllUnion.cpp
  hll/HllUtil.cpp
  hll/IntArrayPairIterator.cpp
  hll/RelativeErrorTables.cpp
)

# this is the "object library" target: compiles the sources only once
add_library(objlib OBJECT ${libsrc})

# shared and static libraries built from the same object files
add_library(sketchescore_shared SHARED $<TARGET_OBJECTS:objlib>)
add_library(sketchescore_static STATIC $<TARGET_OBJECTS:objlib>)

target_link_libraries(sketchescore_shared)
target_link_libraries(sketchescore_static)

SET_TARGET_PROPERTIES(
   sketchescore_static
   PROPERTIES
   OUTPUT_NAME sketchescore
   CLEAN_DIRECT_OUTPUT ON
)

SET_TARGET_PROPERTIES(
   sketchescore_shared
   PROPERTIES
   OUTPUT_NAME sketchescore
   CLEAN_DIRECT_OUTPUT ON
)

install(
  TARGETS sketchescore_shared sketchescore_static
  ARCHIVE DESTINATION /usr/local/lib
  LIBRARY DESTINATION /usr/local/lib
)
