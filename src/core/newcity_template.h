// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Observed initial state of a brand-new city (spec/30-new-city-template.md). The original was made to save a city
// right after New City, before the simulator ran; fourteen such saves (four map sizes, three difficulties, three
// start years) were compared byte for byte.
#pragma once
#include <cstdint>
#include <vector>
#include "core/bytes.h"
#include "core/records.h"

namespace m3k {

struct NewCityRecord {
  ResKey key;
  Bytes data;
};

// Inner records that are identical in every new city (spec 30 s4), plus the twelve zone-developer class words
// 206C6E7C:409FF3BA:<n>. Sizes include the 8-byte record header where the record has one.
const std::vector<NewCityRecord>& newCityConstantRecords();
const NewCityRecord* newCityConstantRecord(const ResKey& key);

// Size-dependent layer records of spec 30 s5 that the simulation does not write on its own or that it would
// otherwise compute before the first simulation step: the spacing grid 01FD7A8C (all zero) and the land-value
// layer C0AB8A88 (zero grid; scalars as in the template).
std::vector<NewCityRecord> newCityLayerRecords(int W, int H);

// Adds every constant and layer record that `store` lacks (records already present, e.g. from a terrain file,
// win). Returns the number of records added.
size_t seedNewCityRecords(RawRecordStore& store, int W, int H);

}  // namespace m3k
