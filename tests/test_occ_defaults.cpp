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
  TEST_MAIN_END();
}
