// Per-class property values written for a newly placed building (spec 10 6.2.5, open issue 1.2).
#pragma once
#include "core/world.h"

namespace m3k {

// Fills the date pair and property lists of a freshly placed occupant from the values observed in
// saves written by the original [OBSERVED]. Classes not observed are left without properties.
// `year`/`dayOfYear` is the build date. `lifeMax`/`declineAge` (months) are the plant's rolled
// lifespan (spec 16 2.3) and only used for power plants. Returns true if the class has defaults.
bool applyNewBuildingDefaults(Occupant& o, int year, int dayOfYear, int lifeMax, int declineAge);

}  // namespace m3k
