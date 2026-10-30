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
    // Classes below were not placed fresh in the original; values are the most common ones in the shipped cities [OBSERVED
    // statistics over all 60 saves, spec 29 1.4 'write the most common shipped value'].
    case 12851:  // recycling centre (all 126 shipped centres carry exactly these)
      date(); ints(o, {{0x4352C330, 45}, {0x4352C331, 50000}, {0x4352C332, 0}}); return true;
    case 12852:  // incinerator: per-run burn capacity 1,500 and monthly 45,000 (= 30 runs), nothing burnt yet
      date(); ints(o, {{0xE352C57C, 1500}, {0xE352C57D, 0}, {0xE352C57E, 45000}, {0xE352C57F, 0}}); return true;
    case 12853:  // waste-to-energy plant: power-plant words (no life limit seen) plus capacity 1,750 / 52,500
      date(); ints(o, {{0x2351FAF8, 0}, {0x2351FAF9, 0}, {0x2351FAFA, 0}, {0x2351FAFB, 0}, {0xA352179A, 1750}, {0xA352179B, 0}, {0xA352179C, 52500}}); return true;
    case 12230:  // rail-only station
      ints(o, {{0x6352CCCF, 10}, {0x6352CCD0, 10}}); return true;
    case 12360:  // subway-only station
      ints(o, {{0x6352CCCF, 10}, {0x6352CCD0, 12}}); return true;
    case 13881:  // jail: inmates, second word (75 is the commonest)
      ints(o, {{0x0352FBBD, 0}, {0x0352FBBE, 75}}); return true;
    case 14140:  // museum
      ints(o, {{0x63544FDD, 75}}); return true;
    case 10120: case 11002: case 13871: case 14370:  // city hall, military base, university, golf course: build date only
      date(); return true;
    default: return false;  // incl. small park 14311, plopped landmarks (no properties) and stadiums (team name needs a prompt)
  }
}

}  // namespace m3k
