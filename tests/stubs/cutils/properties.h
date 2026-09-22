// Copyright 2026 The DiamaneOS Project
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdlib>
#define PROPERTY_VALUE_MAX 92
// These tests must never exercise Android property-dependent device discovery.
inline int property_get(const char*, char*, const char*) { std::abort(); }
