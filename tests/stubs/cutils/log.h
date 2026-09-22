// Copyright 2026 The DiamaneOS Project
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdio>
#define ALOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ALOGI(...) do {} while (0)
#define ALOGV(...) do {} while (0)
#define ALOGD(...) do {} while (0)
