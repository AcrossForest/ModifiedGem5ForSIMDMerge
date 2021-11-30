#!/usr/bin/env fish
# conda activate gem5
scons ./build/(string upper $argv[1])/gem5.$argv[2] -j(nproc) --ignore-style CC=gcc-10 CXX=g++-10 CCFLAGS_EXTRA=-std=c++20 MARSHAL_LDFLAGS_EXTRA=-fno-lto LDFLAGS_EXTRA=-fno-lto PYTHON_CONFIG=/usr/bin/python3-config
