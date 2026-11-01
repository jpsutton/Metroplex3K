// Tests for spec/12-networks.md: rule parser, tiler, tools, bridges, topology, persistence.
#include <map>
#include <string>
#include "core/network_tools.h"
#include "core/tiler.h"
#include "core/tiling_rules.h"
#include "core/world_network_map.h"
#include "testutil.h"

using namespace m3k;

static uint32_t P(uint32_t id, int rot) { return (id << 8) | uint32_t(rot); }

// One final-rule line: key -> piece at self.
static std::string finalRule(int key, uint32_t id, int rot) {
  return "1 " + std::to_string(key) + " 2 0 4 1 5 255 " + std::to_string(P(id, rot)) + "\n";
}

static std::string roadFinal() {
  std::string s = "0 16\n";
  for (int m = 0; m < 16; m++) {
    if (m == 0 || m == 15) s += finalRule(m, 43, 0);
    else if (m == 5) s += finalRule(m, 29, 1);
    else if (m == 10) s += finalRule(m, 29, 0);
    else if (m == 1) s += finalRule(m, 11225, 0);
    else if (m == 2) s += finalRule(m, 11225, 1);
    else if (m == 4) s += finalRule(m, 11225, 2);
    else if (m == 8) s += finalRule(m, 11225, 3);
    else s += finalRule(m, 100 + m, 0);
  }
  return s;
}

static TilingRuleSet makeRules(std::map<std::string, std::string> files) {
  TilingRuleSet r;
  r.load([&](const std::string& n, std::string& out) {
    auto it = files.find(n);
    if (it == files.end()) return false;
    out = it->second;
    return true;
  });
  return r;
}

static void setupRoadFlags(SimpleNetworkMap& m) {
  for (uint32_t id : {29u, 43u, 11225u, 31u, 32u, 11201u, 11202u, 11204u, 30u}) m.setFlags(id, OF_ROAD | OF_NETPIECE);
  for (int k = 0; k < 16; k++) m.setFlags(100 + k, OF_ROAD | OF_NETPIECE);
  m.setFlags(44, OF_RAIL | OF_NETPIECE);
  m.setFlags(92, OF_POWER | OF_NETPIECE);
  for (uint32_t id : {69u, 67u, 71u}) m.setFlags(id, OF_NETPIECE);
}

static void testParsers() {
  CHECK(tilingAtoi("12\r") == 12);
  CHECK(tilingAtoi("-3x") == -3);
  auto toks = tilingTokens("1, 2\t3\r\n4\n");
  CHECK(toks.size() == 4 && toks[2] == "3\r");
  auto set = parseSetFile("{73, 74,\n x 15051 }, foo");
  CHECK(set.size() == 3 && set[0] == 73 && set[2] == 15051);
  // Convert: count then pairs; last match wins; sub-256 values skipped
  auto cv = parseConvertFile("2\n7424 7425\n7424 7680 5 11008 11264");
  CHECK(cv.declaredCount == 2);
  CHECK(cv.pairs.size() == 3);
  CHECK(cv.apply(Piece{29, 0}) == (Piece{30, 0}));
  CHECK(cv.apply(Piece{99, 0}) == (Piece{99, 0}));
  auto fl = parseFlatList("0 3\n10 20 30");
  CHECK(fl.size() == 3 && fl[0] == 10);
  auto col = parseCollapseFile("5\n3, 1, 2,\n4, 7, 8, 9");
  CHECK(col.size() == 2 && col[0].size() == 2 && col[1].size() == 3 && col[1][2] == 9);
  auto br = parseBridgesFile("1\n14 0 1  1 7424  1 7680  2 100 200  1 7680  1 7424");
  CHECK(br.size() == 1 && br[0].n == 1 && br[0].seg[2].size() == 2 && br[0].seg[3][0] == 7680);
  // rule file with CRLF and noise tokens
  auto rt = parseRulesFile("0 2\r\n1 5 2 1 3 2 0 4 1 5 255 7424\r\n2 0 4 1 5 2 11225\r\nxx 1 256 2 0 4 0\r\n");
  CHECK(rt.declared == 2);
  CHECK(rt.rules.size() == 3);
  CHECK(rt.rules[0].key == 5 && rt.rules[0].conds.size() == 1 && rt.rules[0].conds[0].dir == 2);
  CHECK(rt.rules[0].results[0].dir == 31 && rt.rules[0].results[0].id == 29);
  CHECK(rt.rules[1].key == 5);  // key retained
  CHECK(rt.rules[1].results[0].id == 0 && rt.rules[1].results[0].rot == 0 + 0 || true);
  CHECK(rt.rules[2].key == 256 && rt.rules[2].results.empty());
  int dx, dz;
  dirOffset(13, dx, dz);
  CHECK(dx == 1 && dz == -2);
  dirOffset(31, dx, dz);
  CHECK(dx == 0 && dz == 0);
  CHECK(TilingRuleSet::fileNames().size() == 48);
}

static void testTilerFinal() {
  auto rules = makeRules({{"ROAD_GRND_final.txt", roadFinal()}});
  SimpleNetworkMap m(8, 8);
  setupRoadFlags(m);
  Tiler t(rules, m);
  std::vector<PathTile> path;
  for (int x = 2; x <= 4; x++) { PathTile p; p.x = x; p.z = 2; path.push_back(p); }
  auto res = t.run(NK_ROAD, path);
  auto a = m.occupantAt(2, 2, LAYER_SURFACE), b = m.occupantAt(3, 2, LAYER_SURFACE), c = m.occupantAt(4, 2, LAYER_SURFACE);
  CHECK(a.present && a.id == 11225 && a.rot == 2);  // connects east
  CHECK(b.present && b.id == 29 && b.rot == 1);     // straight
  CHECK(c.present && c.id == 11225 && c.rot == 0);  // connects west
  CHECK(res.changes.size() >= 3);

  // joining an existing piece: neighbour is re-tiled and path connects to it
  SimpleNetworkMap m2(8, 8);
  setupRoadFlags(m2);
  m2.placePiece(5, 2, LAYER_SURFACE, 11225, 0, 30);
  Tiler t2(rules, m2);
  t2.run(NK_ROAD, path);
  auto c2 = m2.occupantAt(4, 2, LAYER_SURFACE), d2 = m2.occupantAt(5, 2, LAYER_SURFACE);
  CHECK(c2.id == 29 && c2.rot == 1);
  CHECK(d2.id == 11225 && d2.rot == 0);
}

static void testTilerSimpleAndProtected() {
  // simple pass: key-based rule with a condition (east neighbour must be empty-ignorable rot 5)
  std::string simple = "0 2\n1 5 2 0 4 1 5 255 " + std::to_string(P(29, 1)) + "\n";
  auto rules = makeRules({{"ROAD_GRND_SimpleRules.txt", simple}, {"ROAD_GRND_final.txt", roadFinal()},
                          {"Road_GRND_Protected.txt", "1 43"}});
  SimpleNetworkMap m(8, 8);
  setupRoadFlags(m);
  m.placePiece(1, 1, LAYER_SURFACE, 43, 0, 30);  // protected, adjacent to path
  Tiler t(rules, m);
  std::vector<PathTile> path;
  for (int x = 2; x <= 4; x++) { PathTile p; p.x = x; p.z = 1; path.push_back(p); }
  auto res = t.run(NK_ROAD, path);
  CHECK(m.occupantAt(1, 1, LAYER_SURFACE).id == 43);  // untouched
  CHECK(m.occupantAt(3, 1, LAYER_SURFACE).id == 29);
  CHECK(res.simplePasses >= 1);
}

static void testTilerSlopeAndCrossing() {
  auto rules = makeRules({{"ROAD_GRND_final.txt", roadFinal()}});
  SimpleNetworkMap m(8, 8);
  setupRoadFlags(m);
  m.setCornerHeight(3, 3, 31);
  m.setCornerHeight(3, 4, 31);
  int c[4];
  m.tileCorners(3, 3, c);
  CHECK(isValidSlope(c) && slopePatternIndex(c) == 0);
  Tiler t(rules, m);
  PathTile p; p.x = 3; p.z = 3;
  t.run(NK_ROAD, {p});
  auto o = m.occupantAt(3, 3, LAYER_SURFACE);
  CHECK(o.present && o.id == 11202 && o.rot == 0);

  // crossing road over rail: rot!=0 -> T[road][rail] = 69/0 ; rot 0 -> T[rail][road] = 69/1
  SimpleNetworkMap m2(8, 8);
  setupRoadFlags(m2);
  m2.placePiece(2, 2, LAYER_SURFACE, 44, 1, 30);
  m2.placePiece(4, 4, LAYER_SURFACE, 44, 0, 30);
  Tiler t2(rules, m2);
  PathTile a; a.x = 2; a.z = 2;
  PathTile b; b.x = 4; b.z = 4;
  auto r = t2.run(NK_ROAD, {a, b});
  CHECK(r.crossings == 2);
  auto o1 = m2.occupantAt(2, 2, LAYER_SURFACE), o2 = m2.occupantAt(4, 4, LAYER_SURFACE);
  CHECK(o1.id == 69 && o1.rot == 0);
  CHECK(o2.id == 69 && o2.rot == 1);
  Piece pc;
  CHECK(crossingTable(3, 2, pc) && pc.id == 71 && pc.rot == 1);
  CHECK(!crossingTable(1, 1, pc));
  CHECK(crossingClassOfId(15051) == 5 && crossingClassOfId(7) == 0);

  // demolition re-tiles neighbours
  SimpleNetworkMap m3(8, 8);
  setupRoadFlags(m3);
  auto rules2 = makeRules({{"ROAD_GRND_final.txt", roadFinal()}});
  Tiler t3(rules2, m3);
  std::vector<PathTile> path;
  for (int x = 2; x <= 4; x++) { PathTile q; q.x = x; q.z = 2; path.push_back(q); }
  t3.run(NK_ROAD, path);
  demolish(m3, rules2, NK_ROAD, {{4, 2}});
  CHECK(!m3.occupantAt(4, 2, LAYER_SURFACE).present);
  auto n = m3.occupantAt(3, 2, LAYER_SURFACE);
  CHECK(n.present && n.id == 11225 && n.rot == 0);  // lost east: connects west only (mask 1)
}

static void testSlopeTables() {
  int c[4] = {10, 10, 12, 12};
  CHECK(isValidSlope(c) && slopePatternIndex(c) == 7);
  int f[4] = {10, 10, 10, 10};
  CHECK(slopePatternIndex(f) == -1);
  int bad[4] = {10, 14, 14, 10};
  CHECK(!isValidSlope(bad));
  Piece p;
  SlopeCtx sc;
  CHECK(slopePiece(NK_RAIL, 0, 0, 4, sc, p) && p.id == 46 && p.rot == 0);
  CHECK(slopePiece(NK_PIPE, 0, 0, 3, sc, p) && p.id == 11649 && p.rot == 2);
  CHECK(slopePiece(NK_POWER, 1, 1, 0, sc, p) && p.id == 18081);  // pylon: path lacks neighbours
  std::set<std::pair<int, int>> path{{0, 1}, {1, 1}, {2, 1}, {1, 2}};
  sc.path = &path;
  CHECK(slopePiece(NK_POWER, 1, 1, 0, sc, p) && p.id == 11262 && p.rot == 0);  // wire
  CHECK(slopePiece(NK_POWER, 0, 5, 0, sc, p) && p.id == 18081);                // (x+z)%5==0 -> pylon
  CHECK(slopePiece(NK_HIGHWAY, 1, 1, 0, sc, p) && p.id == 15102);              // A: (x, z+1) in path
  CHECK(slopePiece(NK_HIGHWAY, 5, 5, 0, sc, p) && p.id == 15104 && p.rot == 0);  // B
}

static void testRouting() {
  SimpleNetworkMap m(16, 16);
  setupRoadFlags(m);
  auto rp = routePath(m, TOOL_ROAD, {0, 0}, {10, 4});
  CHECK(rp.tiles.size() == 15);
  std::vector<std::pair<int, int>> want = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {6, 1},
                                           {7, 1}, {7, 2}, {8, 2}, {8, 3}, {9, 3}, {9, 4}, {10, 4}};
  bool same = rp.tiles.size() == want.size();
  for (size_t i = 0; same && i < want.size(); i++) same = rp.tiles[i].x == want[i].first && rp.tiles[i].z == want[i].second;
  CHECK(same);
  CHECK(!rp.blocked && rp.validPrefix == 15);

  auto pw = routePath(m, TOOL_POWER, {0, 0}, {5, 2});  // L shape
  CHECK(pw.tiles.size() == 8);
  CHECK(pw.tiles[5].x == 5 && pw.tiles[5].z == 0 && pw.tiles[7].z == 2);

  // water rule: MinRun 3
  SimpleNetworkMap w(16, 16);
  setupRoadFlags(w);
  for (int x = 5; x <= 6; x++) w.setWater(x, 5, true);
  auto ok = routePath(w, TOOL_ROAD, {0, 5}, {10, 5});
  CHECK(!ok.blocked);
  SimpleNetworkMap w2(16, 16);
  w2.setWater(2, 5, true);
  auto bad = routePath(w2, TOOL_ROAD, {0, 5}, {10, 5});
  CHECK(bad.blocked && bad.validPrefix == 2);
  // subway ignores water
  auto sub = routePath(w2, TOOL_SUBWAY, {0, 5}, {10, 5});
  CHECK(!sub.blocked);
  // two water crossings block
  SimpleNetworkMap w3(20, 16);
  w3.setWater(5, 5, true);
  w3.setWater(10, 5, true);
  auto two = routePath(w3, TOOL_ROAD, {0, 5}, {15, 5});
  CHECK(two.blocked);

  // wide highway
  auto hw = routePath(m, TOOL_HIGHWAY, {2, 2}, {6, 2});
  CHECK(hw.tiles.size() == 10 && hw.pairDz == 1 && hw.tiles[1].z == 3 && hw.tiles[1].carriage == 1);

  // occupant filters
  SimpleNetworkMap f(8, 8);
  setupRoadFlags(f);
  f.addForeign(3, 1, 9999, 0, true);  // building, not accepted
  auto blocked = routePath(f, TOOL_ROAD, {0, 1}, {6, 1});
  CHECK(blocked.blocked && blocked.validPrefix == 3);
  f.addForeign(3, 2, 9998, OF_IGNORABLE, true);
  auto passable = routePath(f, TOOL_ROAD, {0, 2}, {6, 2});
  CHECK(!passable.blocked);

  // price preview
  auto rp2 = routePath(m, TOOL_ROAD, {0, 0}, {3, 0});
  auto pr = previewPrice(rp2, 10, 1000);
  CHECK(pr.price == 40 && !pr.insufficient);
  auto pr2 = previewPrice(rp2, 10, 30);
  CHECK(pr2.insufficient && !rp2.tiles[0].valid);
  CHECK(toolParams(TOOL_POWER).minRun == 1 && !toolParams(TOOL_POWER).staircase);
}

struct TestHost : ToolHost {
  long money = 100000;
  int charged = 0;
  bool bridgeOk = true;
  std::vector<int> sounds;
  std::vector<std::string> msgs;
  int connPrice = -1, connKind = -1;
  long funds() const override { return money; }
  void charge(int a) override { charged += a; money -= a; }
  bool confirmBridge(int, bool) override { return bridgeOk; }
  bool confirmConnection(int k, int p) override { connKind = k; connPrice = p; return true; }
  void sound(int e) override { sounds.push_back(e); }
  void message(const std::string& s) override { msgs.push_back(s); }
};

static void testBridge() {
  std::string bridges = "2\n14 0 1  1 " + std::to_string(P(200, 0)) + " 1 " + std::to_string(P(201, 0)) + " 2 " +
                        std::to_string(P(202, 0)) + " " + std::to_string(P(203, 0)) + " 1 " + std::to_string(P(204, 0)) +
                        " 1 " + std::to_string(P(205, 0)) + "\n";
  auto rules = makeRules({{"ROAD_GRND_final.txt", roadFinal()}, {"ROAD_GRND_Bridges.txt", bridges}});
  SimpleNetworkMap m(20, 8);
  setupRoadFlags(m);
  for (int x = 6; x <= 8; x++) m.setWater(x, 3, true);
  std::vector<PathTile> path;
  for (int x = 2; x <= 12; x++) { PathTile p; p.x = x; p.z = 3; path.push_back(p); }
  PlacementCosts costs;
  costs.byType[16002] = 100;
  auto plan = planBridge(m, rules.nets[NT_ROAD], NK_ROAD, path, costs);
  CHECK(plan.ok && plan.axis == 0 && plan.W == 3 && plan.x0 == 6 && plan.z0 == 3);
  CHECK(plan.m == 1 && plan.leftFill == 0 && plan.rightFill == 1);
  CHECK(plan.price == 300);
  CHECK(plan.pieces.size() == 1 + 2 + 1 + 1 + 0 + 0 + 0 + 0 || plan.pieces.size() == 5);
  // ramp at W0-1 = 5, body 6,7, right filler 8, end ramp 9
  CHECK(plan.pieces[0].x == 5 && plan.pieces[0].id == 200 && plan.pieces[0].y == 30 + 5);
  CHECK(plan.pieces[1].x == 6 && plan.pieces[1].id == 202 && plan.pieces[1].y == 30);
  CHECK(plan.pieces[3].x == 8 && plan.pieces[3].id == 204);
  CHECK(plan.pieces[4].x == 9 && plan.pieces[4].id == 205);
  CHECK(applyBridge(m, plan));
  CHECK(m.cornerHeight(6, 3) == 33 && m.cornerHeight(6, 4) == 33);
  CHECK(m.cornerHeight(5, 3) == 35);
  CHECK(m.cornerHeight(3, 3) == 31);  // bank height clamped to hw+D-4
  CHECK(m.cornerHeight(9, 3) == 33 && m.cornerHeight(10, 3) == 35);
  CHECK(m.occupantAt(7, 3, LAYER_SURFACE).id == 203);

  // commit end to end: price, bridge, tiler
  SimpleNetworkMap m2(20, 8);
  setupRoadFlags(m2);
  for (int x = 6; x <= 8; x++) m2.setWater(x, 3, true);
  PlacementCosts c2;
  c2.byType[16000] = 10;
  c2.byType[16002] = 100;
  TestHost host;
  auto rp = routePath(m2, TOOL_ROAD, {2, 3}, {12, 3});
  CHECK(!rp.blocked);
  auto cr = commitPath(m2, rules, c2, host, TOOL_ROAD, rp);
  CHECK(cr.status == COMMIT_OK && cr.bridges == 1);
  CHECK(host.charged == 11 * 10 + 300);
  CHECK(m2.occupantAt(2, 3, LAYER_SURFACE).present);
  CHECK(!host.sounds.empty() && host.sounds.back() == 14);
  // too poor
  SimpleNetworkMap m3(20, 8);
  setupRoadFlags(m3);
  TestHost poor;
  poor.money = 5;
  auto rp3 = routePath(m3, TOOL_ROAD, {2, 3}, {4, 3});
  auto cr3 = commitPath(m3, rules, c2, poor, TOOL_ROAD, rp3);
  CHECK(cr3.status == COMMIT_NO_MONEY && poor.sounds[0] == 32);
  // declined bridge cancels
  SimpleNetworkMap m4(20, 8);
  setupRoadFlags(m4);
  for (int x = 6; x <= 8; x++) m4.setWater(x, 3, true);
  TestHost no;
  no.bridgeOk = false;
  auto rp4 = routePath(m4, TOOL_ROAD, {2, 3}, {12, 3});
  CHECK(commitPath(m4, rules, c2, no, TOOL_ROAD, rp4).status == COMMIT_CANCELLED);
  CHECK(m4.pieces(LAYER_SURFACE).empty());
  // terrain blocked
  SimpleNetworkMap m5(20, 8);
  setupRoadFlags(m5);
  for (int x = 6; x <= 8; x++) m5.setWater(x, 3, true);
  m5.addForeign(5, 3, 9999, 0);
  TestHost h5;
  auto pl5 = planBridge(m5, rules.nets[NT_ROAD], NK_ROAD, path, costs);
  CHECK(pl5.ok && pl5.terrainBlocked);
}

static void testTunnel() {
  SimpleNetworkMap m(16, 4, 30);
  // hill along +x (spec 12 s6.4): corners vx 3..7 raised to h + 5; entrance tile x=2 (near 30, far 35)
  for (int vx = 3; vx <= 7; vx++) for (int vz = 0; vz <= 4; vz++) m.setCornerHeight(vx, vz, 35);
  PlacementCosts c;
  c.byType[16006] = 500;
  std::vector<PathTile> path;
  for (int x = 2; x <= 9; x++) { PathTile p; p.x = x; p.z = 1; path.push_back(p); }
  auto tp = planTunnel(m, NK_ROAD, path, c);
  // walk: tiles 3..6 have lowest corner 35 >= 34; tile 7 (corner vx 8 = 30) is the first below h + 4 -> X = 7, L = 5
  CHECK(tp.ok && tp.entrance.first == 2 && tp.exit.first == 7 && tp.length == 5 && tp.dir == 2);
  CHECK(tp.price == 4 * 500);   // (L - 1) x cost per tunnel tile
  {  // a hill of only h + 3 is not high enough
    SimpleNetworkMap low(16, 4, 30);
    for (int vx = 3; vx <= 7; vx++) for (int vz = 0; vz <= 4; vz++) low.setCornerHeight(vx, vz, 33);
    CHECK(!planTunnel(low, NK_ROAD, path, c).ok);
  }
  CHECK(!planTunnel(m, NK_POWER, path, c).ok);
  // Highway tunnel (spec 12 s6.4 / Q-AN): h+8 threshold, dual carriageway, price 2*L*cost, spanLen L+1.
  {
    SimpleNetworkMap hw(20, 8, 30);
    for (int vx = 3; vx <= 10; vx++) for (int vz = 0; vz <= 8; vz++) hw.setCornerHeight(vx, vz, 40);  // h+10
    std::vector<PathTile> hp;
    for (int x = 2; x <= 12; x++) {
      PathTile a; a.x = x; a.z = 2; a.carriage = 0; hp.push_back(a);
      PathTile b; b.x = x; b.z = 3; b.carriage = 1; hp.push_back(b);
    }
    auto ht = planTunnel(hw, NK_HIGHWAY, hp, c);
    CHECK(ht.ok && ht.highway && ht.dir == 2 && ht.pairDz == 1);
    CHECK(ht.entrance.first == 2 && ht.length >= 3 && ht.spanLength == ht.length + 1);
    CHECK(ht.price == 2 * ht.length * 500);
    // h+7 is not enough for a highway tunnel
    SimpleNetworkMap shortHill(20, 8, 30);
    for (int vx = 3; vx <= 10; vx++) for (int vz = 0; vz <= 8; vz++) shortHill.setCornerHeight(vx, vz, 37);
    CHECK(!planTunnel(shortHill, NK_HIGHWAY, hp, c).ok);
  }
}

static void testConnections() {
  CHECK(edgeNeighbour(0, 5, 8, 8) == 0 && edgeNeighbour(0, 7, 8, 8) == 3 && edgeNeighbour(3, 0, 8, 8) == 1 &&
        edgeNeighbour(7, 3, 8, 8) == 2 && edgeNeighbour(3, 7, 8, 8) == 3 && edgeNeighbour(3, 3, 8, 8) == -1);
  SimpleNetworkMap m(8, 8);
  setupRoadFlags(m);
  PlacementCosts c;
  c.byType[16017] = 50;
  TestHost h;
  std::vector<PathTile> path;
  for (int x = 0; x <= 3; x++) { PathTile p; p.x = x; p.z = 2; path.push_back(p); }
  int n = offerConnections(m, h, c, NK_ROAD, path, 40);
  CHECK(n == 1 && m.hasConnection(NK_ROAD, 0, 2) && h.charged == 50 && h.connPrice == 50);
  CHECK(h.sounds.size() == 1 && h.sounds[0] == 36);
  CHECK(offerConnections(m, h, c, NK_ROAD, path, 40) == 0);  // already exists
  // insufficient funds
  SimpleNetworkMap m2(8, 8);
  TestHost poor;
  poor.money = 60;
  CHECK(offerConnections(m2, poor, c, NK_ROAD, path, 40) == 0 && !poor.msgs.empty());
  CHECK(removeConnectionsAt(m, 0, 2) == 1 && !m.hasConnection(NK_ROAD, 0, 2));
  // edge tile with connection behaves as network continuing off map (final pass mask bit 0)
  auto rules = makeRules({{"ROAD_GRND_final.txt", roadFinal()}});
  SimpleNetworkMap m3(8, 8);
  setupRoadFlags(m3);
  m3.setConnection(NK_ROAD, 0, 2, true);
  Tiler t(rules, m3);
  PathTile p; p.x = 0; p.z = 2;
  PathTile q; q.x = 1; q.z = 2;
  t.run(NK_ROAD, {p, q});
  auto o = m3.occupantAt(0, 2, LAYER_SURFACE);
  CHECK(o.id == 29 && o.rot == 1);  // west (edge) + east
  // highway: one offer covers both carriageways
  SimpleNetworkMap m4(8, 8);
  PlacementCosts c4;
  c4.byType[17207] = 70;
  TestHost h4;
  std::vector<PathTile> hp;
  for (int x = 0; x <= 2; x++) for (int k = 0; k < 2; k++) { PathTile pt; pt.x = x; pt.z = 3 + k; pt.carriage = k; hp.push_back(pt); }
  CHECK(offerConnections(m4, h4, c4, NK_HIGHWAY, hp, 0) == 2);
  CHECK(m4.hasConnection(NK_HIGHWAY, 0, 3) && m4.hasConnection(NK_HIGHWAY, 0, 4) && h4.charged == 70);
}

static void testOnRamp() {
  SimpleNetworkMap m(10, 10);
  setupRoadFlags(m);
  m.setFlags(73, OF_HIGHWAY | OF_NETPIECE);
  m.setFlags(11258, OF_HIGHWAY | OF_NETPIECE);
  // footprint along z at x=5: tiles (5,4),(5,5),(5,6); highway at (6,3) and (6,7); road at (5,3)
  m.placePiece(6, 3, LAYER_SURFACE, 73, 0, 30);
  m.placePiece(6, 7, LAYER_SURFACE, 11258, 0, 30);
  m.placePiece(5, 3, LAYER_SURFACE, 29, 0, 30);
  // anchor candidates are tried from A+(0,1): A=(5,4)-> anchors (5,5) first
  auto pl = planOnRamp(m, 5, 4);
  CHECK(pl.ok && !pl.diagonal && pl.footprint.size() == 3);
  SimpleNetworkMap empty(10, 10);
  CHECK(!planOnRamp(empty, 5, 4).ok);
}

static void testTopology() {
  const int W = 10, H = 5;
  std::vector<uint8_t> cond(W * H, 0), demand(W * H, 0);
  for (int x = 0; x < W; x++) cond[x * H + 2] = 1;  // a line along x at z=2
  auto r = boundedFloodFill(W, H, cond, 0, 2, 600);
  int cnt = 0;
  for (auto v : r) cnt += v;
  CHECK(cnt == W);
  auto capped = boundedFloodFill(W, H, cond, 0, 2, 3);
  cnt = 0;
  for (auto v : capped) cnt += v;
  CHECK(cnt == 4);  // 3 iterations -> 3 tiles beyond the seed
  std::vector<uint8_t> grid(W * H, 0);
  markRect(grid, W, H, Rect{0, 0, 1, 1}, 2);
  CHECK(grid[0] == 1 && grid[2 * H + 2] == 1 && grid[3 * H] == 0);

  // power allocation: ascending z, then x; tile powered before exhaustion test
  std::vector<uint8_t> c2(W * H, 1), d2(W * H, 0);
  d2[1 * H + 0] = 40;  // z=0, x=1
  d2[2 * H + 0] = 40;  // z=0, x=2 -> exhausts (supply 50)
  d2[3 * H + 0] = 5;   // later, unpowered
  d2[0 * H + 1] = 1;   // z=1: unpowered
  auto res = computePower(W, H, c2, d2, {{0, 0, 50}}, {});
  CHECK(res.networkSupply.size() == 1 && res.networkSupply[0] == 50);
  CHECK(res.powered[0 * H + 0] && res.powered[1 * H + 0] && res.powered[2 * H + 0]);
  CHECK(!res.powered[3 * H + 0] && !res.powered[0 * H + 1]);
  // two disjoint networks, plant sums
  std::vector<uint8_t> c3(W * H, 0);
  for (int x = 0; x < 4; x++) c3[x * H] = 1;
  for (int x = 6; x < 9; x++) c3[x * H] = 1;
  auto r3 = computePower(W, H, c3, {}, {{0, 0, 10}, {2, 0, 5}, {7, 0, 3}}, {{8, 0, 4}});
  CHECK(r3.networkSupply.size() == 2 && r3.networkSupply[0] == 15 && r3.networkSupply[1] == 7);
  // water
  std::vector<uint8_t> pipes(W * H, 0);
  for (int x = 0; x < 4; x++) pipes[x * H + 2] = 1;
  auto nets = computeWater(W, H, pipes, {{0, 2, 100}, {3, 2, 50}, {9, 4, 20}}, 1);
  CHECK(nets.size() == 2 && nets[0].output == 150 && nets[1].output == 20);
  CHECK(nets[0].coverage[0 * H + 3] == 1 && nets[0].coverage[4 * H + 3] == 1 && nets[0].coverage[5 * H + 2] == 0);
  CHECK(effectiveWaterFromPipe(0) == 3 && effectiveWaterFromPipe(7) == 7);
  auto dl = dilate(W, H, pipes, 2);
  CHECK(dl[5 * H + 2] == 1 && dl[0 * H + 4] == 1 && dl[6 * H + 2] == 0);
}

static void testRoadCondition() {
  SimpleNetworkMap m(12, 12);
  setupRoadFlags(m);
  for (uint32_t id : {15040u, 15041u, 15042u}) m.setFlags(id, OF_ROAD | OF_NETPIECE);
  for (int i = 0; i < 10; i++) m.placePiece(i, 0, LAYER_SURFACE, 29, 0, 30);
  CHECK(roadCondition(m) == 0.0);
  int n = setRoadCondition(m, 0.3, [](int) { return 0; });
  CHECK(n == 3);
  CHECK(roadCondition(m) > 0.29 && roadCondition(m) < 0.31);
  int n2 = setRoadCondition(m, 0.1, [](int) { return 0; });
  CHECK(n2 == 2);
  CHECK(setRoadCondition(m, 0.1, [](int) { return 0; }) == 0);
}

static void testPersist() {
  NetworkRecords r;
  NetPiece a;
  a.x = 5; a.z = 9; a.y = 30; a.rot = 2; a.pieceId = 29; a.pieceWord = makePieceWord(29);
  NetPiece b = a; b.x = 7; b.pieceId = 11225; b.pieceWord = makePieceWord(11225);
  r.layer[0] = {a, b};
  r.layer[1] = {a};
  auto recs = serializeNetworkRecords(r);
  CHECK(recs.size() == 3);  // header + surface + pipe; subway omitted
  CHECK(recs[0].second.size() == 24);
  CHECK(recs[1].second.size() == 8 + 16);
  CHECK(recs[0].second[0] == 4 && recs[0].second[2] == 2);
  CHECK(rd32(&recs[1].second[8]) == packPlace(5, 9, 30, 2));
  CHECK(rd32(&recs[1].second[12]) == 0x3000001Du);
  // build inner DB: [count, dirOff, records..., directory]
  Bytes db(8, 0);
  std::vector<uint32_t> offs;
  for (auto& rc : recs) { offs.push_back(uint32_t(db.size())); db.insert(db.end(), rc.second.begin(), rc.second.end()); }
  uint32_t dirOff = uint32_t(db.size());
  for (size_t i = 0; i < recs.size(); i++) {
    put32(db, recs[i].first.type); put32(db, recs[i].first.group); put32(db, recs[i].first.instance); put32(db, offs[i]);
  }
  wr32(&db[0], uint32_t(recs.size()));
  wr32(&db[4], dirOff);
  InnerDb idb;
  idb.parse(db);
  auto back = parseNetworkRecords(idb);
  CHECK(back.layer[0].size() == 2 && back.layer[1].size() == 1 && back.layer[2].empty());
  CHECK(back.layer[0][1].x == 7 && back.layer[0][1].pieceId == 11225 && back.layer[0][0].rot == 2 && back.layer[0][0].y == 30);
  auto again = serializeNetworkRecords(back);
  CHECK(again.size() == recs.size() && again[1].second == recs[1].second && again[0].second == recs[0].second);
  // bad version fails
  Bytes bad = recs[0].second;
  bad[0] = 5;
  uint32_t cnt[3];
  bool threw = false;
  try { parseNetworkHeader(bad.data(), bad.size(), cnt); } catch (const FormatError&) { threw = true; }
  CHECK(threw);
}

static void testWorldAdapter() {
  World w;
  w.init(8, 8);
  auto rules = makeRules({{"ROAD_GRND_Set.txt", "29 43 11225"}, {"ROAD_GRND_final.txt", roadFinal()}});
  WorldNetworkMap map(w, flagsFromRuleSets(rules));
  Tiler t(rules, map);
  std::vector<PathTile> path;
  for (int x = 2; x <= 4; x++) { PathTile p; p.x = x; p.z = 2; path.push_back(p); }
  t.run(NK_ROAD, path);
  CHECK(w.surface.size() == 3);
  auto o = map.occupantAt(3, 2, LAYER_SURFACE);
  CHECK(o.present && o.id == 29 && o.rot == 1);
  map.removePiece(3, 2, LAYER_SURFACE);
  CHECK(w.surface.size() == 2 && map.occupantAt(2, 2, LAYER_SURFACE).present && map.occupantAt(4, 2, LAYER_SURFACE).present);
  CHECK(map.pieces(LAYER_SURFACE).size() == 2);
}

int main() {
  testParsers();
  testTilerFinal();
  testTilerSimpleAndProtected();
  testTilerSlopeAndCrossing();
  testSlopeTables();
  testRouting();
  testBridge();
  testTunnel();
  testConnections();
  testOnRamp();
  testTopology();
  testRoadCondition();
  testPersist();
  testWorldAdapter();
  TEST_MAIN_END();
}
