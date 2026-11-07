// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/occ_defaults.h"
#include "testutil.h"

using namespace m3k;

int main() {
  Occupant h; h.agentType = 13310;
  CHECK(applyNewBuildingDefaults(h, 2000, 1, 0, 0) && !h.hasDate && h.intProps.size() == 3 && h.floatProps.size() == 1);
  CHECK(h.intProps[0].first == 0xA352D404 && h.intProps[0].second == 1500 && h.floatProps[0].second == 0.12f);
  Occupant pump; pump.agentType = 12810;
  CHECK(applyNewBuildingDefaults(pump, 1900, 1, 0, 0) && pump.hasDate && pump.dateYear == 1900 && pump.dateDay == 1);
  CHECK(pump.intProps.size() == 4 && pump.intProps[1].second == 196608);
  Occupant coal; coal.agentType = 12111;
  CHECK(applyNewBuildingDefaults(coal, 1900, 1, 732, 586) && coal.intProps[1].second == 732 && coal.intProps[2].second == 586);
  Occupant park; park.agentType = 14311; park.intProps = {{1, 2}};
  CHECK(!applyNewBuildingDefaults(park, 2000, 1, 0, 0) && park.intProps.empty() && !park.hasDate);
  Occupant inc; inc.agentType = 12852;
  CHECK(applyNewBuildingDefaults(inc, 2000, 1, 0, 0) && inc.hasDate && inc.intProps.size() == 4 && inc.intProps[0].second == 1500);
  Occupant rec; rec.agentType = 12851;
  CHECK(applyNewBuildingDefaults(rec, 2000, 1, 0, 0) && rec.hasDate && rec.intProps.size() == 3 && rec.intProps[1].second == 50000);
  Occupant wte; wte.agentType = 12853;
  CHECK(applyNewBuildingDefaults(wte, 2000, 1, 0, 0) && wte.intProps.size() == 7 && wte.intProps[6].second == 52500);
  Occupant jail; jail.agentType = 13881;
  CHECK(applyNewBuildingDefaults(jail, 2000, 1, 0, 0) && !jail.hasDate && jail.intProps.size() == 2);
  Occupant hall; hall.agentType = 10120;
  CHECK(applyNewBuildingDefaults(hall, 2001, 5, 0, 0) && hall.hasDate && hall.dateYear == 2001 && hall.intProps.empty());
  TEST_MAIN_END();
}
