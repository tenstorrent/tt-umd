include_guard(GLOBAL)

option(ENABLE_ASAN "Enable build with AddressSanitizer" OFF)
message(STATUS "Build with ASAN: ${ENABLE_ASAN}")

set(SANITIZER_ENABLED ${ENABLE_ASAN})

option(ENABLE_MSAN "Enable build with MemorySanitizer" OFF)
message(STATUS "Build with MSAN: ${ENABLE_MSAN}")

if(SANITIZER_ENABLED AND ENABLE_MSAN)
    message(FATAL_ERROR "Multiple sanitizers are not supported")
elseif(ENABLE_MSAN)
    set(SANITIZER_ENABLED ${ENABLE_MSAN})
endif()

option(ENABLE_TSAN "Enable build with ThreadSanitizer" OFF)
message(STATUS "Build with TSAN: ${ENABLE_TSAN}")

if(SANITIZER_ENABLED AND ENABLE_TSAN)
    message(FATAL_ERROR "Multiple sanitizers are not supported")
elseif(ENABLE_TSAN)
    set(SANITIZER_ENABLED ${ENABLE_TSAN})
endif()

option(ENABLE_UBSAN "Enable build with UndefinedBehaviorSanitizer" OFF)
message(STATUS "Build with UBSAN: ${ENABLE_UBSAN}")

if(SANITIZER_ENABLED AND ENABLE_UBSAN)
    message(FATAL_ERROR "Multiple sanitizers are not supported")
endif()

unset(SANITIZER_ENABLED)

add_compile_options(
    $<$<BOOL:${ENABLE_ASAN}>:-fsanitize=address>
    $<$<BOOL:${ENABLE_MSAN}>:-fsanitize=memory>
    $<$<BOOL:${ENABLE_TSAN}>:-fsanitize=thread>
    $<$<BOOL:${ENABLE_UBSAN}>:-fsanitize=undefined>
)
add_link_options(
    $<$<BOOL:${ENABLE_ASAN}>:-fsanitize=address>
    $<$<BOOL:${ENABLE_MSAN}>:-fsanitize=memory>
    $<$<BOOL:${ENABLE_TSAN}>:-fsanitize=thread>
    $<$<BOOL:${ENABLE_UBSAN}>:-fsanitize=undefined>
)
