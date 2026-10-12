# IP layouts for tests

Each directory is an RTL simulator build directory as far as `RtlSimIpLayout` is concerned: it holds
one `ip_layout.yaml`, and its SoC descriptor paths are relative to it. `test_rtl_sim_ip_layout.cpp`
reads them in place.
