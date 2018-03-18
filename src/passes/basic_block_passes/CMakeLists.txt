add_library(BarrierPass MODULE BarrierPass.cpp ../Utils/Utils.cpp)

target_compile_features(BarrierPass PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(BarrierPass PROPERTIES COMPILE_FLAGS "-fno-rtti")
