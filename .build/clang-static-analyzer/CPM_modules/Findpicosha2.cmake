include("/workspace/tt_metal/third_party/umd/.cpmcache/cpm/CPM_0.40.2.cmake")
CPMAddPackage("NAME;picosha2;GITHUB_REPOSITORY;okdshin/PicoSHA2;GIT_TAG;v1.0.1;OPTIONS;CMAKE_MESSAGE_LOG_LEVEL NOTICE;DOWNLOAD_ONLY;YES")
set(picosha2_FOUND TRUE)