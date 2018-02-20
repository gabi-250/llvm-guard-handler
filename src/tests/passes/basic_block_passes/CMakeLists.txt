add_library(MarkUnoptimizedPass MODULE MarkUnoptimizedPass.cpp)

target_compile_features(MarkUnoptimizedPass PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(MarkUnoptimizedPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
