# This example drives real RDMA verbs, so it needs libibverbs. It is skipped rather than failing
# the configure step when libibverbs-dev is not installed, so enabling TT_UMD_BUILD_EXAMPLES stays
# safe on hosts without an RDMA stack.
#
# Both the library and the headers are located: a host with libibverbs1 but not libibverbs-dev passes
# find_library and would then hard-fail the build on "#include <infiniband/verbs.h>: No such file or
# directory" — exactly the case the skip is meant to cover.
find_library(IBVERBS_LIBRARY NAMES ibverbs)
find_path(IBVERBS_INCLUDE_DIR NAMES infiniband/verbs.h)
if(NOT (IBVERBS_LIBRARY AND IBVERBS_INCLUDE_DIR))
    message(STATUS "rdma_dmabuf_p2p example: libibverbs library or headers not found, skipping")
    return()
endif()

# The headers also have to be new enough: ibv_reg_dmabuf_mr() needs rdma-core >= v33 and
# ibv_query_gid_ex() >= v32, and both are called unconditionally below. Without this check an older
# rdma-core fails with a bare "was not declared in this scope" that says nothing about the cause.
include(CheckCXXSymbolExists)
set(CMAKE_REQUIRED_INCLUDES ${IBVERBS_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${IBVERBS_LIBRARY})
check_cxx_symbol_exists(
    ibv_reg_dmabuf_mr
    "infiniband/verbs.h"
    HAVE_IBV_REG_DMABUF_MR
)
check_cxx_symbol_exists(
    ibv_query_gid_ex
    "infiniband/verbs.h"
    HAVE_IBV_QUERY_GID_EX
)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT (HAVE_IBV_REG_DMABUF_MR AND HAVE_IBV_QUERY_GID_EX))
    message(
        STATUS
        "rdma_dmabuf_p2p example: libibverbs is too old (need rdma-core >= v33 for ibv_reg_dmabuf_mr and >= v32 for ibv_query_gid_ex), skipping"
    )
    return()
endif()

add_executable(dmabuf_target dmabuf_target.cpp)

target_link_libraries(
    dmabuf_target
    PRIVATE
        umd::tt-umd
        tt-logger::tt-logger
        ${IBVERBS_LIBRARY}
)
target_include_directories(dmabuf_target PRIVATE ${IBVERBS_INCLUDE_DIR})

# The initiator only speaks RDMA against a plain host buffer, so it does not link UMD.
add_executable(dmabuf_initiator dmabuf_initiator.cpp)

target_link_libraries(dmabuf_initiator PRIVATE ${IBVERBS_LIBRARY})
target_include_directories(dmabuf_initiator PRIVATE ${IBVERBS_INCLUDE_DIR})

set_target_properties(
    dmabuf_target
    dmabuf_initiator
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/examples/rdma_dmabuf_p2p/
)
