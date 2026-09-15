# libibverbs discovery for the RDMA dma-buf test and example, both gated behind TT_UMD_BUILD_RDMA.
#
# Included once from the top level when that option is ON, and defines an imported target
# `ibverbs::ibverbs` carrying both the library and its include directory. Consumers just link it.
#
# Everything here is a hard error rather than a silent skip: the option is opt-in, so a host that
# cannot satisfy it should say what to install instead of quietly building nothing.

# Both the library and the headers are located. libibverbs1 without libibverbs-dev would pass a
# library-only check and then fail the build on a missing header.
find_library(IBVERBS_LIBRARY NAMES ibverbs)
find_path(IBVERBS_INCLUDE_DIR NAMES infiniband/verbs.h)
if(NOT (IBVERBS_LIBRARY AND IBVERBS_INCLUDE_DIR))
    message(
        FATAL_ERROR
        "TT_UMD_BUILD_RDMA is ON but libibverbs was not found (library: ${IBVERBS_LIBRARY}, headers: ${IBVERBS_INCLUDE_DIR}). Install libibverbs-dev, or configure with -DTT_UMD_BUILD_RDMA=OFF."
    )
endif()

# The headers must also be new enough: ibv_reg_dmabuf_mr() needs rdma-core >= v33 and
# ibv_query_gid_ex() >= v32, and both are called unconditionally. Without this check an older
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
        FATAL_ERROR
        "TT_UMD_BUILD_RDMA is ON but libibverbs is too old: need rdma-core >= v33 for ibv_reg_dmabuf_mr and >= v32 for ibv_query_gid_ex."
    )
endif()

add_library(ibverbs::ibverbs UNKNOWN IMPORTED)
set_target_properties(
    ibverbs::ibverbs
    PROPERTIES
        IMPORTED_LOCATION
            "${IBVERBS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${IBVERBS_INCLUDE_DIR}"
)
