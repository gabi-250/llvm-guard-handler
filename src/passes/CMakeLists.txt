cmake_minimum_required(VERSION 3.7)

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(Utils)
link_directories(${LLVM_LIBRARY_DIRS})

add_subdirectory(function_passes)
add_subdirectory(basic_block_passes)
