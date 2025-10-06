FROM alpine:3.22

RUN apk update && apk add \
    build-base\
    git-lfs\
    wget\
    python3-dev\
    py3-pip\
    xxd\
    ninja-build\
    gtest-dev\
    hwloc-dev\
    yamllint\
    yaml-dev\
    boost-dev\
    cmake\
    clang17-dev \
    py3-pytest

WORKDIR /run
