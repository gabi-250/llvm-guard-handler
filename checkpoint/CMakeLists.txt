add_library(CheckPointPass MODULE CheckPointPass.cpp)

# Use C++11 to compile our pass (i.e., supply -std=c++11).
target_compile_features(CheckPointPass PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(CheckPointPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
