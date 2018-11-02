cmake_minimum_required(VERSION 3.0)
set(PROJECT_NAME sketches-core-cpp)
set(RELEASE_DATE 2018-10-18)
project(${PROJECT_NAME})

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z")
  add_compile_options(-Wno-tautological-constant-out-of-range-compare)
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()

link_directories("/usr/local/lib")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-c -g -Wall -O3)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake")
find_package(cppunit REQUIRED)

include_directories(
    include
    include/cpc
    include/hll
    include/kll
)
add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)
