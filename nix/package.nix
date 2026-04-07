{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  python3,
  hwloc,
  yaml-cpp,
  fmt,
  spdlog,
  cxxopts,
  nanobind,
  # Header-only deps provided as source trees
  picosha2-src,
  asio-src,
  tt-logger,
}:

stdenv.mkDerivation {
  pname = "tt-umd";
  version =
    let
      versionFile = builtins.readFile ../VERSION;
      # Strip prerelease suffix (e.g. "0.9.5-dev.260402" -> "0.9.5")
      cleaned = builtins.head (builtins.split "-" (lib.removeSuffix "\n" versionFile));
    in
    cleaned;

  src = lib.cleanSource ./..;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    python3
  ];

  buildInputs = [
    hwloc
    yaml-cpp
    fmt
    spdlog
    cxxopts
    nanobind
    tt-logger
  ];

  cmakeFlags = [
    (lib.cmakeBool "CPM_LOCAL_PACKAGES_ONLY" true)
    # Tell the build where to find header-only deps that CPM would normally download
    (lib.cmakeFeature "picosha2_SOURCE_DIR" "${picosha2-src}")
    (lib.cmakeFeature "umd_asio_SOURCE_DIR" "${asio-src}")
    # Disable optional components that pull in extra deps
    (lib.cmakeBool "TT_UMD_BUILD_TESTS" false)
    (lib.cmakeBool "TT_UMD_BUILD_SIMULATION" false)
    (lib.cmakeBool "TT_UMD_BUILD_PYTHON" false)
    (lib.cmakeBool "TT_UMD_BUILD_EXAMPLES" false)
    (lib.cmakeBool "TT_UMD_BUILD_TOOLS" true)
    (lib.cmakeBool "TT_UMD_ENABLE_CLANG_TIDY" false)
    (lib.cmakeBool "TT_UMD_ENABLE_TRACY" false)
  ];

  meta = {
    description = "Tenstorrent User Mode Driver - C++ library for interfacing with Tenstorrent hardware";
    homepage = "https://github.com/tenstorrent/tt-umd";
    license = lib.licenses.asl20;
    platforms = lib.platforms.linux;
    maintainers = [ ];
  };
}
