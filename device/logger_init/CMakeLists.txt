add_library(umd-logger-init OBJECT logger_init.cpp)
add_library(umd::Logger::Init ALIAS umd-logger-init)

target_link_libraries(umd-logger-init PRIVATE tt-logger::tt-logger)
