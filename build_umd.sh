ARCH_NAME=blackhole cmake -B build -G Ninja -DTT_UMD_BUILD_TESTS=ON
ARCH_NAME=blackhole ninja umd_tests -C build