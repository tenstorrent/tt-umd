export CMAKE_C_COMPILER=/usr/bin/gcc
export CMAKE_CXX_COMPILER=/usr/bin/g++
ARCH_NAME=blackhole cmake -B build -G Ninja
ARCH_NAME=blackhole ninja -C build
./build/test/umd/blackhole/unit_tests --gtest_filter=SiliconDriverBH.Create*
/home/vradhakrishnan/work/syseng/src/t6ifc/t6py/packages/tenstorrent/scripts/gddr/dump-bh-regs --noc_x 0 --noc_y 0 --noc_id 0 --skip-enable-axi > output.txt