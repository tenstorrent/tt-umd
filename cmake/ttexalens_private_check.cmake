include(ExternalProject)

ExternalProject_Add(
    ttexalens_private
    GIT_REPOSITORY ${TTEXALENS_PRIVATE_GIT_REPOSITORY}
    GIT_TAG ${TTEXALENS_PRIVATE_GIT_TAG}
    GIT_SHALLOW TRUE
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/ttexalens_private
    CONFIGURE_COMMAND
        ""
    BUILD_COMMAND
        ""
    INSTALL_COMMAND
        ""
)
