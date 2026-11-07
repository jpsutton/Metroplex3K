// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// RefPack (QFS) decompression and the canonical compressor. See spec/02-containers.md section 6.
#pragma once
#include "core/bytes.h"

namespace m3k {

// Decode a RefPack stream. `rawSizeOut` (optional) receives the header's declared size.
// Throws FormatError on malformed input.
Bytes refpackDecode(const uint8_t* data, size_t size);
inline Bytes refpackDecode(const Bytes& b) { return refpackDecode(b.data(), b.size()); }

// Encode with the original's canonical algorithm (spec 6.4). Header is `10 FB` + 3-byte raw size.
Bytes refpackEncode(const uint8_t* data, size_t size);
inline Bytes refpackEncode(const Bytes& b) { return refpackEncode(b.data(), b.size()); }

}  // namespace m3k
