// Tests for spec/18-sim-transport.md using synthetic fixtures.
#include <cmath>
#include "core/traffic.h"
#include "testutil.h"

using namespace m3k;

static void testParams() {
  TrafficParams d;
  CHECK(d.cellCostRoad == 4 && d.annoyanceLevel == 150 && d.dest[0][0] == 10 && d.dest[3][1] == 66);
  auto ini = IniFile::fromText(
      "[TrafficLayer]\nTripCellCostBaseRoad=7;\nTripMaxDistanceToRoadInd=9\nTripMaxDistanceToRoadDefault=5\n"
      "MonthlyDensityDecrement=75\nMysticalERoads=12.5\n"
      "[TrafficTripDestinationThresholds]\nFromResidential=1,2,3\nFromCommercial=1,2\n");
  auto p = TrafficParams::fromIni(ini);
  CHECK(p.cellCostRoad == 7);
  CHECK(p.maxDistInd == 5 && p.maxDistDefault == 6);  // defect reproduced
  CHECK(p.monthlyDensityDecrement == 75 && p.mysticalERoads == 12.5f);
  CHECK(p.dest[0][0] == 1 && p.dest[0][2] == 3);
  CHECK(p.dest[1][0] == 70);  // malformed -> built-in
}

static void testMisc() {
  TrafficRng r(12345);
  uint64_t v = uint64_t(12345) * 0x41C64E6Du + 0x3039u;
  CHECK(r.next() == uint32_t(v >> 16));
  CHECK(r.state() == uint32_t(v));
  CHECK(TrafficSim::slotFires(0, 1) && TrafficSim::slotFires(0, 6) && !TrafficSim::slotFires(0, 7));
  CHECK(TrafficSim::slotFires(1, 7) && TrafficSim::slotFires(3, 21) && !TrafficSim::slotFires(3, 28));
  CHECK(categoryOfTypeChain({3300, 3000, 100}) == CAT_RES);
  CHECK(categoryOfTypeChain({12330, 8000}) == CAT_IND);
  CHECK(categoryOfTypeChain({12310}) == CAT_COM && categoryOfTypeChain({4444}) == -1);
  CHECK(stationFlagsForType(kSubwayRailType) == 0x3C && stationFlagsForType(1) == 0);
  CHECK(efficiencyCurve(50, 120, 30) == 0.5f);
  float e = efficiencyCurve(120, 120, 30);
  CHECK(std::fabs(e - 1.1667f) < 0.001f);
  CHECK(std::fabs(efficiencyCurve(100, 120, 30) - 1.0f) < 0.0001f);
  CHECK(efficiencyCurve(200, 100, 30) == 1.0f);  // M == 1 fallback
  TrafficParams p;
  CHECK(TrafficSim::roadUpkeep(p, 25, 3) == 3 + 2);  // ceil(2.5) + ceil(1.5)
  CHECK(TrafficSim::transitUpkeep(p, 7, 11, 1) == 7 + 2 + 1);
}

static void testRecord() {
  const int W = 40, H = 33;  // exercises multi-word rows
  TrafficRecord r;
  r.reset(W, H);
  for (int x = 0; x < W; x++)
    for (int z = 0; z < H; z++) {
      size_t k = size_t(x) * H + z;
      r.snapSurface[k] = uint8_t(x + z); r.snapHighway[k] = uint8_t(x * 3); r.snapSubway[k] = uint8_t(z);
      r.liveSurface[k] = uint8_t(x ^ z); r.liveHighway[k] = uint8_t(x); r.liveSubway[k] = uint8_t(255 - z);
      r.tripOk[k] = (x + z) % 3 == 0; r.tripTried[k] = (x * z) % 5 == 0;
    }
  r.faresThisMonth = 12.5f; r.faresLastMonth = 3.25f;
  r.strikeState = STRIKE_STRIKING; r.strikeMonths = 4;
  for (int i = 0; i < 32; i++) r.history[i] = uint32_t(i * 7);
  r.tripsAttempted = 11; r.tripsFailed = 2; r.failureRatio = 0.25f; r.avgTransit = 9; r.avgRoad = 77;
  Bytes b = serializeTrafficRecord(r);
  CHECK(b.size() == trafficRecordSize(W, H));
  CHECK(b[0] == 9 && b[1] == 0 && b[2] == 2 && rd32(&b[4]) == 0xDEADBEEF);
  // spec: first grid byte is x = W-1, z = 0
  CHECK(b[8] == r.snapSurface[size_t(W - 1) * H + 0]);
  auto q = parseTrafficRecord(b.data(), b.size(), W, H);
  CHECK(q.snapSurface == r.snapSurface && q.snapHighway == r.snapHighway && q.snapSubway == r.snapSubway);
  CHECK(q.liveSurface == r.liveSurface && q.liveHighway == r.liveHighway && q.liveSubway == r.liveSubway);
  CHECK(q.tripOk == r.tripOk && q.tripTried == r.tripTried);
  CHECK(q.faresThisMonth == 12.5f && q.faresLastMonth == 3.25f && q.strikeState == STRIKE_STRIKING && q.strikeMonths == 4);
  CHECK(q.history[31] == 217 && q.graphMax == 220 && q.tripsFailed == 2 && q.avgRoad == 77 && q.failureRatio == 0.25f);
  CHECK(serializeTrafficRecord(q) == b);
  // size formula of the spec for a 256 map
  CHECK(trafficRecordSize(256, 256) == 409773);
  // truncated -> throws; older version (v2) accepted
  bool threw = false;
  try { parseTrafficRecord(b.data(), b.size() - 1, W, H); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
  Bytes v2;
  put16(v2, 2); v2.push_back(2); v2.push_back(0); put32(v2, 0xDEADBEEF);
  size_t G = size_t(W) * H, B = size_t(W) * 2 * 4;
  v2.insert(v2.end(), 3 * G + 2 * B, 7);
  auto o = parseTrafficRecord(v2.data(), v2.size(), W, H);
  CHECK(o.version == 2 && o.snapSurface[0] == 7 && o.strikeState == STRIKE_NORMAL && o.liveSurface[0] == 0);
  // marker strings (flags bit 0)
  Bytes mk(b.begin(), b.begin() + 4);
  mk[2] = 3;
  put32(mk, 0xDEADBEEF);
  put32(mk, 3); mk.push_back('a'); mk.push_back('b'); mk.push_back('c');
  mk.insert(mk.end(), b.begin() + 8, b.end());
  auto m2 = parseTrafficRecord(mk.data(), mk.size(), W, H);
  CHECK(m2.snapSurface == r.snapSurface);
}

// 14x10 map: a road along z=5 (x 0..13).
static void roadLine(TrafficSim& s, int z, int x0, int x1) {
  for (int x = x0; x <= x1; x++) s.flags(x, z) |= TF_ROAD;
}

static void testTrips() {
  TrafficParams p;
  TrafficSim s(14, 10, p, 99);
  roadLine(s, 5, 0, 13);
  std::vector<TripBuilding> bs;
  TripBuilding res; res.x = 2; res.z = 3; res.category = CAT_RES; res.slot = 0;
  TripBuilding com; com.x = 11; com.z = 3; com.category = CAT_COM; com.slot = 1;
  bs = {res, com};
  s.rebuildDestinations(bs);
  CHECK(s.destBit(11, 5, CAT_COM) && !s.destBit(2, 5, CAT_COM) && s.destBit(2, 5, CAT_RES));
  TripRequest req;
  req.origin = TripRect{2, 3, 2, 3};
  req.originCategory = CAT_RES;
  req.dest = CAT_COM;
  auto tr = s.routeTrip(req);
  CHECK(tr.success && tr.path.size() >= 2 && tr.path[0].mode == TM_START);
  // success => last node has a com destination bit in its rect
  auto& last = tr.path.back();
  CHECK(s.destBit(last.r.x0, last.r.z0, CAT_COM));
  // no such destination -> fail
  req.dest = CAT_IND;
  CHECK(!s.routeTrip(req).success);
  // counted trip adds density A = w*h*adder = 1 per road node tile
  req.dest = CAT_COM;
  TripResult ok = s.routeTrip(req);
  CHECK(ok.success);
  s.applyCountedTrip(req.origin, ok);
  int total = 0;
  for (int x = 0; x < 14; x++) total += s.liveSurface(x, 5);
  int roadNodes = 0;
  for (auto& n : ok.path) if (n.mode == TM_ROAD) roadNodes++;
  CHECK(total == roadNodes);
  // result bits
  CHECK(s.tripResult(2, 3) == 2);
  s.writeResult(req.origin, false);
  CHECK(s.tripResult(2, 3) == 1 && s.tripResultArea(TripRect{1, 2, 3, 4}) == 1);
  s.writeResult(req.origin, true);
  CHECK(s.tripResult(2, 3) == 0);
  s.clearLot(req.origin);
  CHECK(s.tripResult(2, 3) == 2);

  // neighbour connection absorbs any trip
  TrafficSim s2(14, 10, p, 5);
  roadLine(s2, 5, 0, 13);
  s2.flags(13, 5) |= TF_ROAD_EXIT;
  TripRequest r2;
  r2.origin = TripRect{2, 3, 2, 3};
  r2.dest = CAT_IND;
  auto t2 = s2.routeTrip(r2);
  CHECK(t2.success);
  // budget: tiny budget fails to reach exit
  r2.budgetFactor = 1;
  CHECK(!s2.routeTrip(r2).success);

  // negative density adder defect: 7x7 lot, adder 3 -> 147 -> -109 removes density
  TrafficParams p3;
  p3.densityAdder = 3;
  TrafficSim s3(20, 20, p3, 1);
  roadLine(s3, 10, 0, 19);
  s3.rec().liveSurface[size_t(5) * 20 + 10] = 200;
  TripResult fake;
  TripNode n; n.mode = TM_ROAD; n.r = TripRect{5, 10, 5, 10};
  fake.path = {n};
  s3.applyCountedTrip(TripRect{0, 0, 6, 6}, fake);
  CHECK(s3.liveSurface(5, 10) == 91);
  s3.applyCountedTrip(TripRect{0, 0, 6, 6}, fake);
  s3.applyCountedTrip(TripRect{0, 0, 6, 6}, fake);
  s3.applyCountedTrip(TripRect{0, 0, 6, 6}, fake);
  CHECK(s3.liveSurface(5, 10) == 0);  // clamped at 0

  // Transport-occupant gate (spec 18 §6.1): a node whose tiles have no network/station flag
  // credits neither density nor fare (no run-time 0x4000 occupant).
  TrafficSim s4(6, 6, p, 1);
  TripNode bare; bare.mode = TM_RAIL; bare.r = TripRect{2, 2, 2, 2};
  TripResult barePath; barePath.path = {bare};
  s4.applyCountedTrip(TripRect{0, 0, 0, 0}, barePath);
  CHECK(s4.liveSurface(2, 2) == 0 && s4.rec().faresThisMonth == 0.0f);
  s4.flags(2, 2) |= TF_RAIL;
  s4.applyCountedTrip(TripRect{0, 0, 0, 0}, barePath);
  CHECK(s4.liveSurface(2, 2) >= 1 && s4.rec().faresThisMonth > 0.0f);
}

static void testTransit() {
  TrafficParams p;
  TrafficSim s(14, 10, p, 3);
  // road tile (2,2) next to rail station (3,2); track (4..12,2); rail exit at (13,2)
  s.flags(2, 2) |= TF_ROAD;
  s.addStation(kRailStationType, TripRect{3, 2, 3, 2});
  for (int x = 4; x <= 13; x++) s.flags(x, 2) |= TF_RAIL;
  s.flags(13, 2) |= TF_RAIL_EXIT;
  TripRequest r;
  r.origin = TripRect{2, 0, 2, 0};
  r.dest = CAT_IND;
  auto t = s.routeTrip(r);
  CHECK(t.success);
  bool onRail = false;
  for (auto& n : t.path) if (n.mode == TM_RAIL) onRail = true;
  CHECK(onRail && t.path.back().mode == TM_RAIL);
  // rail forbidden: only the road is available and no destination -> fail
  r.rail = false;
  CHECK(!s.routeTrip(r).success);
  // applying the trip credits the rail fare and loads the track
  r.rail = true;
  auto t2 = s.routeTrip(r);
  s.applyCountedTrip(r.origin, t2);
  CHECK(s.rec().faresThisMonth > 0);
  CHECK(s.liveSurface(8, 2) >= 1);
  // station tile removal clears flags and densities
  s.rec().liveSurface[size_t(3) * 10 + 2] = 5;
  s.removeStation(kRailStationType, TripRect{3, 2, 3, 2});
  CHECK(!(s.flags(3, 2) & TF_STATION) && s.liveSurface(3, 2) == 0);
  // Spec 18 §11 piece removal: clearNetwork zeroes the piece's own layer and the listed flag bits;
  // span is never cleared.
  TrafficSim n(4, 4, p, 9);
  n.flags(1, 1) = uint16_t(TF_ROAD | TF_DAMAGED | TF_PROP_A | TF_ROAD_EXIT | TF_SPAN);
  n.rec().liveSurface[size_t(1) * 4 + 1] = 40;
  n.rec().snapSurface[size_t(1) * 4 + 1] = 30;
  n.clearNetwork(TripRect{1, 1, 1, 1}, uint16_t(TF_ROAD | TF_DAMAGED | TF_PROP_A | TF_ROAD_EXIT), true, false, false);
  CHECK(n.flags(1, 1) == TF_SPAN && n.liveSurface(1, 1) == 0 && n.snapSurface(1, 1) == 0);
  n.flags(2, 2) = uint16_t(TF_HIGHWAY | TF_ACCESS | TF_HWY_EXIT);
  n.rec().liveHighway[size_t(2) * 4 + 2] = 55;
  n.clearNetwork(TripRect{2, 2, 2, 2}, uint16_t(TF_HIGHWAY | TF_ACCESS | TF_DAMAGED | TF_PROP_A | TF_HWY_EXIT), false, true,
                 false);
  CHECK(!(n.flags(2, 2) & (TF_HIGHWAY | TF_ACCESS | TF_HWY_EXIT)) && n.liveHighway(2, 2) == 0);
}

static void testSpansAndHighway() {
  TrafficParams p;
  TrafficSim s(20, 6, p, 8);
  for (int x = 0; x <= 19; x++) s.flags(x, 3) |= TF_ROAD;
  s.flags(19, 3) |= TF_ROAD_EXIT;
  // a span piece at x=5 jumping 4 tiles along +x
  s.flags(5, 3) |= TF_SPAN;
  int calls = 0;
  s.spanOf = [&](int x, int z, SpanInfo& sp) { calls++; (void)z; if (x != 5) return false; sp.dir = 2; sp.len = 4; return true; };
  TripRequest r;
  r.origin = TripRect{0, 1, 0, 1};
  r.dest = CAT_IND;
  auto t = s.routeTrip(r);
  CHECK(t.success && calls > 0);
  bool spanNode = false;
  for (auto& n : t.path) if (n.r.x1 - n.r.x0 == 4) spanNode = true;
  CHECK(spanNode);
  // without a span reporter the step fails -> the trip cannot pass x=5
  TrafficSim s2(20, 6, p, 8);
  for (int x = 0; x <= 19; x++) s2.flags(x, 3) |= TF_ROAD;
  s2.flags(19, 3) |= TF_ROAD_EXIT;
  s2.flags(5, 3) |= TF_SPAN;
  CHECK(!s2.routeTrip(r).success);

  // highway via access tile: road(0..3) -> access tile (3,3) -> highway (4..9) with exit
  TrafficSim h(14, 6, p, 8);
  for (int x = 0; x <= 3; x++) h.flags(x, 3) |= TF_ROAD;
  h.flags(3, 3) |= TF_ACCESS;
  for (int x = 4; x <= 13; x++) h.flags(x, 3) |= TF_HIGHWAY;
  h.flags(13, 3) |= TF_HWY_EXIT;
  TripRequest hr;
  hr.origin = TripRect{0, 1, 0, 1};
  hr.dest = CAT_COM;
  auto ht = h.routeTrip(hr);
  CHECK(ht.success && ht.path.back().mode == TM_HWY);
  h.applyCountedTrip(hr.origin, ht);
  CHECK(h.liveHighway(8, 3) == 1);
}

static void testQueries() {
  TrafficParams p;
  TrafficSim s(6, 6, p, 1);
  s.flags(1, 1) |= TF_ROAD;
  s.flags(2, 1) |= TF_HIGHWAY;
  s.flags(3, 1) |= TF_SUBWAY;
  s.rec().snapSurface[size_t(1) * 6 + 1] = 100;
  s.rec().snapHighway[size_t(2) * 6 + 1] = 60;
  s.rec().snapSubway[size_t(3) * 6 + 1] = 40;
  CHECK(s.tileValue(1, 1, 0x01) == 100 && s.tileValue(1, 1, 0x02) == 0);
  CHECK(s.tileValue(2, 1, 0x02) == 60 && s.tileValue(3, 1, 0x08) == 40 && s.tileValue(3, 1, 0x01) == 0);
  {  // surface test is per bit (spec 18 7.1): a rail tile's surface value needs mask bit 0x04, not 0x01
    s.flags(4, 1) |= TF_RAIL;
    s.rec().snapSurface[size_t(4) * 6 + 1] = 70;
    CHECK(s.tileValue(4, 1, 0x03) == 0 && s.tileValue(4, 1, 0x04) == 70 && s.tileValue(1, 1, 0x03) == 100);
    s.flags(4, 1) = 0; s.rec().snapSurface[size_t(4) * 6 + 1] = 0;
  }
  int sum, cnt;
  s.areaSumCount(TripRect{0, 0, 5, 5}, 0x0B, sum, cnt);
  CHECK(sum == 200 && cnt == 3);
  CHECK(s.areaAverage(TripRect{0, 1, 3, 1}, 0x0B) == 200 / 4);
  // defect: subway compared against caller's initial value, not the running value
  s.flags(4, 1) |= TF_ROAD | TF_SUBWAY;
  s.rec().snapSurface[size_t(4) * 6 + 1] = 90;
  s.rec().snapSubway[size_t(4) * 6 + 1] = 40;
  CHECK(s.areaAverage(TripRect{4, 1, 4, 1}, 0x09, 0) == 40);   // 40 > initial 0 replaces 90
  CHECK(s.areaAverage(TripRect{4, 1, 4, 1}, 0x09, 50) == 90);  // 40 !> 50
}

static void testMonthly() {
  TrafficParams p;  // decrement 25 -> factor 0.25
  TrafficSim s(6, 6, p, 1);
  s.flags(1, 1) |= TF_ROAD;
  s.flags(2, 1) |= TF_ROAD | TF_STATION;
  s.rec().liveSurface[size_t(1) * 6 + 1] = 100;
  s.rec().liveSurface[size_t(2) * 6 + 1] = 100;
  s.rec().liveHighway[0] = 100;
  s.rec().faresThisMonth = 4;
  s.rec().tripsAttempted = 10; s.rec().tripsFailed = 3;
  MonthlyInput in;
  in.roadTiles = 1; in.highwayTiles = 0; in.month = 1; in.year = 2000;
  in.ord.crossingGuards = true;  // road retention +2%
  s.monthly(in);
  CHECK(s.liveSurface(1, 1) == 27 && s.snapSurface(1, 1) == 27);  // trunc(100*0.27)
  CHECK(s.liveSurface(2, 1) == 25);  // station: other factor
  CHECK(s.liveHighway(0, 0) == 25);
  CHECK(s.rec().faresLastMonth == 4 && s.rec().faresThisMonth == 0);
  CHECK(s.rec().tripsAttempted == 0 && std::fabs(s.rec().failureRatio - 0.3f) < 1e-6f);
  CHECK(s.rec().history[12] == s.rec().avgRoad && s.rec().history[22] == s.rec().avgRoad);

  // strike machine
  TrafficSim t(4, 4, p, 7);
  t.setTransitFunding(100);
  for (int i = 0; i < 200; i++) CHECK(t.strikeStep(10) == 0);
  t.setTransitFunding(50);
  int ev = 0, guard = 0;
  while (ev == 0 && guard++ < 10000) ev = t.strikeStep(10);
  CHECK(ev == 1 && t.rec().strikeState == STRIKE_THREATENED);
  CHECK(t.strikeStep(10) == 2 && t.rec().strikeState == STRIKE_STRIKING);
  for (int i = 0; i < 25; i++) CHECK(t.strikeStep(10) == 0);  // counter 0..24 -> 25 increments
  int e2 = t.strikeStep(10);
  CHECK(e2 == 3 && t.rec().strikeState == STRIKE_NORMAL);  // lasts at most MaxLength+1 months
  t.setTransitFunding(50);
  while (t.strikeStep(10) != 1) {}
  t.setTransitFunding(100);
  CHECK(t.strikeStep(10) == 3 && t.rec().strikeState == STRIKE_NORMAL);

  // road condition message
  t.setRoadFunding(50);
  CHECK(t.roadConditionMessage(0.10) == int((0.10 + 0.5 * 0.1) * 100));
  t.setRoadFunding(100);
  CHECK(t.roadConditionMessage(0.10) == -1);
  t.setRoadFunding(200);
  int msg = t.roadConditionMessage(0.10);
  CHECK(msg >= 0 && msg < 10);

  // ordinances: shuttle increases radii; subsidised transit changes chance and fares
  TrafficOrdinances o;
  o.shuttleService = true;
  t.applyOrdinances(o);
  CHECK(t.radius(CAT_RES) == 6 && t.radius(3) == 7);
  t.setTransitFunding(100);
  CHECK(t.massTransitChance() == 50);
  o.subsidizedTransit = true;
  t.applyOrdinances(o);
  CHECK(t.massTransitChance() == 100 && std::fabs(t.fareBus() - 0.0005f) < 1e-7f);
}

static void testGenerators() {
  TrafficParams p;
  TrafficSim s(14, 10, p, 4);
  roadLine(s, 5, 0, 13);
  s.flags(13, 5) |= TF_ROAD_EXIT;
  std::vector<TripBuilding> bs;
  for (int i = 0; i < 4; i++) {
    TripBuilding b; b.x = 1 + i * 3; b.z = 3; b.category = CAT_RES; b.slot = i;
    bs.push_back(b);
  }
  // day 1 -> slot 0 only; day 28 -> none
  int fired = 0;
  for (int rep = 0; rep < 60; rep++) fired += s.runWeekly(1, bs);
  CHECK(fired > 0);
  CHECK(s.tripResult(1, 3) != 2);   // slot 0 building routed at least once
  CHECK(s.tripResult(4, 3) == 2);   // slot 1 building never fired
  CHECK(s.runWeekly(28, bs) == 0);
  // destination thresholds respected
  TrafficSim d(4, 4, p, 11);
  int resN = 0;
  for (int i = 0; i < 4000; i++) if (d.chooseDestination(CAT_RES) == CAT_RES) resN++;
  CHECK(resN > 250 && resN < 550);  // ~10%
  // industry/other never use bus or subway
  bool rail, bus, sub, subj;
  TrafficSim::permissions(CAT_IND, rail, bus, sub, subj);
  CHECK(rail && !bus && !sub && !subj);
  TrafficSim::permissions(CAT_RES, rail, bus, sub, subj);
  CHECK(rail && bus && sub && subj);
}

int main() {
  testParams();
  testMisc();
  testRecord();
  testTrips();
  testTransit();
  testSpansAndHighway();
  testQueries();
  testMonthly();
  testGenerators();
  TEST_MAIN_END();
}
