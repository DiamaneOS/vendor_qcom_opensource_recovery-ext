#!/bin/sh
# Copyright 2026 The DiamaneOS Project
# SPDX-License-Identifier: Apache-2.0
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${CXX:-c++}" ${CPPFLAGS:-} ${CXXFLAGS:-} -std=c++17 -O1 -g -ffunction-sections -fdata-sections \
  -I"$root/tests/stubs" "$root/tests/gpt_io_test.cpp" ${LDFLAGS:-} ${LDLIBS--lz} \
  -Wl,--gc-sections -Wl,--wrap=read -Wl,--wrap=write \
  -Wl,--wrap=lseek64 -Wl,--wrap=fsync -Wl,--wrap=ioctl \
  -Wl,--wrap=open -Wl,--wrap=close -o "$work/gpt-io-test"
"$work/gpt-io-test"
