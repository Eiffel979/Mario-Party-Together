# adapters/mp4: Mp4Adapter (host side). Added by runtimes/gc_wii/mp4/host.
add_library(mpt_mp4_adapter STATIC src/mp4_adapter.cpp include/mpt/mp4/adapter.h)
add_library(mpt::mp4_adapter ALIAS mpt_mp4_adapter)
target_include_directories(mpt_mp4_adapter PUBLIC include)
target_link_libraries(mpt_mp4_adapter PUBLIC mpt::mp4_runtime)
mpt_set_warnings(mpt_mp4_adapter)
