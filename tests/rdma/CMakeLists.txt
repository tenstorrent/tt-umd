# RDMA dma-buf loopback test.
#
# Deliberately not part of the default build, of the umd_tests aggregate target, or of CI: it needs
# libibverbs at build time and an ACTIVE RoCEv2 NIC at run time. Build it explicitly with
# `cmake --build <dir> --target rdma_tests` on a machine that has libibverbs-dev installed.
find_library(IBVERBS_LIBRARY NAMES ibverbs)
find_path(IBVERBS_INCLUDE_DIR NAMES infiniband/verbs.h)

if(NOT (IBVERBS_LIBRARY AND IBVERBS_INCLUDE_DIR))
    message(STATUS "libibverbs not found; skipping the rdma_tests target")
    return()
endif()

add_executable(
    rdma_tests
    EXCLUDE_FROM_ALL
    rdma_loopback.cpp
    test_dmabuf_loopback.cpp
)
target_link_libraries(
    rdma_tests
    PRIVATE
        test_common
        ${IBVERBS_LIBRARY}
)
target_include_directories(rdma_tests PRIVATE ${IBVERBS_INCLUDE_DIR})

set_target_properties(
    rdma_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/test/umd/rdma
        OUTPUT_NAME
            rdma_tests
)
