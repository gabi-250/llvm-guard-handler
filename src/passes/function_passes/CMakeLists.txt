add_library(CheckPointPass MODULE CheckPointPass.cpp)
add_library(UnoptimizedCopyPass MODULE UnoptimizedCopyPass.cpp Utils/Utils.cpp)
add_library(LiveVariablesPass MODULE LiveVariablesPass.cpp Utils/Utils.cpp)

# Use C++11 to compile our pass (i.e., supply -std=c++11).
target_compile_features(CheckPointPass PRIVATE cxx_range_for cxx_auto_type)
target_compile_features(UnoptimizedCopyPass PRIVATE cxx_range_for cxx_auto_type)
target_compile_features(LiveVariablesPass PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(CheckPointPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
set_target_properties(UnoptimizedCopyPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
set_target_properties(LiveVariablesPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
