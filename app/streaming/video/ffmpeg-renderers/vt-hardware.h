// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Qualify the actual HEVC parameter sets and first image, without model guesses
// or VideoToolbox's permitted HEVC software fallback. No screen/window access.
bool dp_vt_hevc_hardware_probe(const uint8_t *annexb, size_t size);
#ifdef __cplusplus
}
#endif
