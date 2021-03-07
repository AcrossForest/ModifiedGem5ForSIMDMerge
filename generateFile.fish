#!/usr/bin/env fish


scons ./build/(string upper $argv[1])/arch/(string lower $argv[1])/generated/decoder.hh -j64 CC=gcc-10 CXX=g++-10 MARSHAL_LDFLAGS_EXTRA=-fno-lto LDFLAGS_EXTRA=-fno-lto
