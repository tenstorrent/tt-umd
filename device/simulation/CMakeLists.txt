
add_library(simulation SHARED 
    tt_simulation_host.cpp
    tt_simulation_device.cpp
)
target_link_libraries(simulation
    PUBLIC umd_common_directories nng uv
    PRIVATE fmt::fmt-header-only
)
target_include_directories(simulation PUBLIC
    ${flatbuffers_SOURCE_DIR}/include
    ${nanomsg_SOURCE_DIR}/include
    ${libuv_SOURCE_DIR}/include
)
