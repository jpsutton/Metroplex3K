#include "core/occ_defaults.h"
#include <algorithm>

namespace m3k {

namespace {
void ints(Occupant& o, std::initializer_list<std::pair<uint32_t, uint32_t>> l) {
  o.intProps.assign(l.begin(), l.end());
  std::sort(o.intProps.begin(), o.intProps.end());
}
}  // namespace

bool applyNewBuildingDefaults(Occupant& o, int year, int dayOfYear, int lifeMax, int declineAge) {
  o.intProps.clear(); o.floatProps.clear(); o.strProps.clear(); o.hasDate = false;
  auto date = [&] { o.hasDate = true; o.dateYear = year; o.dateDay = dayOfYear; };
  switch (o.agentType) {
    // Power plants: overwork months, maximum life, decline age, expired flag. Plants keep the
    // lifespan rolled at construction (spec 16 2.3).
    case 12111: case 12112: case 12113: case 12115: case 12117: case 12118:
      date(); ints(o, {{0x2351FAF8, 0}, {0x2351FAF9, uint32_t(lifeMax)}, {0x2351FAFA, uint32_t(declineAge)}, {0x2351FAFB, 0}});
      return true;
    case 12810:  // water pumping station
      date(); ints(o, {{0x8351FDF3, 1500}, {0x8351FDF4, 196608}, {0x8351FDF5, 3}, {0x8351FDF6, 0}}); return true;
    case 12840:  // water tower
      date(); ints(o, {{0x8351FDF3, 600}, {0x8351FDF4, 131072}, {0x8351FDF5, 0}, {0x8351FDF6, 0}}); return true;
    case 12830:  // desalination plant
      date(); ints(o, {{0x8351FDF3, 5000}, {0x8351FDF4, 327680}, {0x8351FDF5, 3}, {0x8351FDF6, 0}}); return true;
    case 12820:  // water treatment plant: all four properties are 0 when new
      date(); ints(o, {{0x0351D624, 0}, {0x0351D625, 0}, {0x0351D626, 0}, {0x0351D627, 0}}); return true;
    case 13130:  // police station
      ints(o, {{0xA352C8E8, 1265437}, {0xA352C8E9, 30}, {0xA352C8EA, 30}}); return true;
    case 13002:  // fire station
      ints(o, {{0x2352C88D, 13092632}, {0x2352C88E, 30}, {0x2352C88F, 30}}); return true;
    case 13310:  // hospital
      ints(o, {{0xA352D404, 1500}, {0xA352D406, 50}, {0xA352D407, 100}}); o.floatProps = {{0xA352D405, 0.12f}}; return true;
    case 13010:  // K-12 school
      ints(o, {{0x8352D5E9, 100}, {0x8352D5EA, 30}}); return true;
    case 13873:  // city college
      ints(o, {{0x2352D114, 150}, {0x2352D116, 125}}); o.floatProps = {{0x2352D115, 0.25f}}; return true;
    case 14130:  // library
      ints(o, {{0xA3544E00, 50}}); return true;
    case 12210:  // bus stop
      ints(o, {{0x6352CCCF, 5}, {0x6352CCD0, 5}}); return true;
    case 12361:  // rail/subway station
      ints(o, {{0x6352CCCF, 10}, {0x6352CCD0, 10}}); return true;
    default: return false;  // incl. small park 14311 and plopped landmarks: no properties
  }
}

}  // namespace m3k
