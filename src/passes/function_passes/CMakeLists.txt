add_library(CheckPointPass MODULE CheckPointPass.cpp)
add_library(UnoptimizedCopyPass MODULE UnoptimizedCopyPass.cpp)
add_library(StackResizePass MODULE StackResizePass.cpp)

# Use C++11 to compile our pass (i.e., supply -std=c++11).
target_compile_features(CheckPointPass PRIVATE cxx_range_for cxx_auto_type)
target_compile_features(UnoptimizedCopyPass PRIVATE cxx_range_for cxx_auto_type)
target_compile_features(StackResizePass PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(CheckPointPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
set_target_properties(UnoptimizedCopyPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
set_target_properties(StackResizePass PROPERTIES COMPILE_FLAGS "-fno-rtti")
