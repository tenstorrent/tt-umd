include("/workspace/tt_metal/third_party/umd/.cpmcache/cpm/CPM_0.40.2.cmake")
CPMAddPackage("NAME;spdlog;GITHUB_REPOSITORY;gabime/spdlog;VERSION;1.15.2;OPTIONS;CMAKE_MESSAGE_LOG_LEVEL NOTICE;SPDLOG_FMT_EXTERNAL_HO ON;SPDLOG_INSTALL ON")
set(spdlog_FOUND TRUE)