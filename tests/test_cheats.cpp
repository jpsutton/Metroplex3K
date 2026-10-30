// Cheats: phrase -> ID table (spec 27 5.1a), matching, logging rule, ticker table (5.2), log records.
#include <map>
#include "core/cheats.h"
#include "testutil.h"
using namespace m3k;

int main() {
  // Full phrase -> ID table of spec 27 section 5.1a.
  static const struct { const char* phrase; uint32_t id; } ids[] = {
    {"I am weak", 0x62E8630B}, {"***", 0xE37FB7B8}, {"power to the masses", 0xC2FA8E82}, {"water in the desert", 0x42FA93FE},
    {"garbage in, garbage out", 0xC2FAA2F5}, {"so sue me", 0x23B4418F}, {"I love red tape", 0xC37A5AAA},
    {"call cousin Vinnie", 0xA3558FD6}, {"UFO Swarm", 0x6303EFA6}, {"Locust Swarm", 0xC5326321}, {"The Birds", 0x0367F938},
    {"Terrain one up", 0xA31CF2EF}, {"Terrain one down", 0xC31CF2F6}, {"Terrain ten up", 0xC31CF2FA},
    {"Terrain ten down", 0x831CF2FF}, {"traffic lights", 0x837961EA}, {"tuneup", 0xA42483B1},
    {"force petitioners to say", 0x031DCE55}, {"force Moe to say", 0x43110C27}, {"force Gus to say", 0xC310F7D4},
    {"stop forcing advice", 0x831109B4},
    {"fund", 0xC352E7AE}, {"advisor", 0x0352E7C2}, {"moremoney", 0x0352E7CC}, {"maxis", 0x8352E7D6}, {"1234", 0x4352E7E1},
    {"sc3k", 0x4352E7ED}, {"mayor", 0x4352E7F6}, {"hello", 0xA352E800}, {"simon says", 0x03546E10}, {"simcity", 0xE352E809},
    {"money", 0xA352E814}, {"ticker", 0xE352E822}, {"electronic arts", 0x4352E828}, {"erts", 0x0352E873},
    {"will wright", 0x0352E87C}, {"help", 0x0352E885}, {"sim", 0xE352E88F}, {"easter egg", 0x4352E897},
    {"llama", 0xC352E8A2}, {"scurk", 0xC352E8AC}, {"bat", 0xC352E8B4}, {"porntipsguzzardo", 0x8352E8BD},
    {"broccoli", 0x2352E8C7},
  };
  for (const auto& e : ids) {
    const CheatInfo* c = findCheatByPhrase(e.phrase);
    CHECK(c && c->id == e.id);
    CHECK(findCheatById(e.id) == c);
  }
  // [OPEN] IDs: present as phrases, ID 0, never logged.
  for (const char* p : {"pay tribute to your king", "zyxwvu", "let's make a deal", "nerdz rool", "Salt on", "Salt off", "Load terrain",
                        "force Mortimer to say", "force Constance to say", "force Maria to say", "force Randall to say", "force Karen to say"}) {
    const CheatInfo* c = findCheatByPhrase(p);
    CHECK(c && c->id == 0);
  }
  CHECK(!findCheatByPhrase("no such cheat") && !findCheatById(0xDEADBEEF) && !findCheatById(0));
  {
    std::map<uint32_t, int> seen;
    for (const auto& c : cheatTable()) if (c.id) CHECK(++seen[c.id] == 1);
  }

  // Case-insensitive prefix matching, longest phrase wins, argument is the rest.
  CHECK(matchCheat("I AM WEAK").info == findCheatByPhrase("I am weak"));
  CHECK(matchCheat("power TO the Masses").info == findCheatByPhrase("power to the masses"));
  CHECK(matchCheat("simon says  hi there").arg == "hi there");
  CHECK(matchCheat("simcity").info == findCheatByPhrase("simcity") && matchCheat("sim").info == findCheatByPhrase("sim"));
  CHECK(matchCheat("simon says x").info == findCheatByPhrase("simon says"));
  CHECK(matchCheat("traffic lights ON").arg == "ON" && trafficLightsArgOn(matchCheat("traffic lights ON")) &&
        trafficLightsArgOn(matchCheat("traffic lights on")) && !trafficLightsArgOn(matchCheat("traffic lights off")) &&
        !trafficLightsArgOn(matchCheat("traffic lights On")));
  CHECK(!matchCheat("xyzzy") && !matchCheat("") && !matchCheat("I am"));
  CHECK(matchCheat("MAXIS").allCaps && !matchCheat("Maxis").allCaps);
  CHECK(forcedMessageId(matchCheat("force Moe to say 123")) == 123 && forcedMessageId(matchCheat("force Moe to say 0x10")) == 16 &&
        forcedMessageId(matchCheat("force Moe to say")) == -1 && forcedMessageId(matchCheat("force Moe to say abc")) == -1);

  // Ticker messages (spec 27 5.2).
  static const struct { const char* typed; uint16_t msg; } tk[] = {
    {"advisor", 0x327}, {"moremoney", 0x32A}, {"MAXIS", 0x32B}, {"maxis", 0x32C}, {"1234", 0x32D}, {"sc3k", 0x32E}, {"mayor", 0x32F},
    {"hello", 0x330}, {"simcity", 0x331}, {"money", 0x332}, {"ticker", 0x333}, {"electronic arts", 0x334}, {"erts", 0x335},
    {"will wright", 0x336}, {"HELP", 0x337}, {"help", 0x338}, {"SIM", 0x339}, {"sim", 0x33A}, {"easter egg", 0x33B},
    {"llama", 0x33C}, {"scurk", 0x33D}, {"bat", 0x33E}, {"porntipsguzzardo", 0x33F}, {"broccoli", 0x340}, {"simon says go", 0x341},
    {"FUND", 0x325}, {"fund", 0x326},
  };
  for (const auto& e : tk) CHECK(tickerMessageFor(matchCheat(e.typed)) == e.msg);
  CHECK(tickerMessageFor(matchCheat("I am weak")) == 0);

  // Logging rule: a cheat is logged, once, when its active flag is set; ticker phrases never are.
  {
    CheatRegistry reg;
    for (const auto& e : tk) { CheatResult r = reg.submit(e.typed); CHECK(r.match && !r.newlyLogged); }
    CHECK(reg.log().empty());
    CheatResult r = reg.submit("power to the masses");
    CHECK(r.stateOn && r.newlyLogged && reg.active(0xC2FA8E82) && reg.active("power to the masses"));
    r = reg.submit("POWER TO THE MASSES");
    CHECK(r.stateOn && !r.newlyLogged && reg.log().size() == 1);
    r = reg.submit("I am weak");
    CHECK(r.stateOn && r.newlyLogged);
    r = reg.submit("i am weak");  // toggle off
    CHECK(!r.stateOn && !r.newlyLogged && !reg.active(0x62E8630B) && reg.log().size() == 2);
    r = reg.submit("I am weak");  // back on: already logged
    CHECK(r.stateOn && !r.newlyLogged && reg.log().size() == 2);
    reg.submit("call cousin Vinnie");  // action: no flag
    reg.submit("Terrain ten up");
    CHECK(reg.log().size() == 2);
    // [OPEN]-ID cheats keep session state but are never logged
    r = reg.submit("nerdz rool");
    CHECK(r.stateOn && !r.newlyLogged && reg.active("nerdz rool") && reg.log().size() == 2);
    // garbage in, garbage out: owner sets the flag
    CHECK(reg.setActive(0xC2FAA2F5, true) && !reg.setActive(0xC2FAA2F5, true) && reg.log().size() == 3);
    // session reset keeps the log
    reg.resetSession();
    CHECK(!reg.active(0x62E8630B) && !reg.active("nerdz rool") && reg.log().size() == 3);
    reg.clearLog();
    CHECK(reg.log().empty());
  }

  // Effect helpers.
  CHECK(terrainCheatDelta(findCheatByPhrase("Terrain one up")) == 1 && terrainCheatDelta(findCheatByPhrase("terrain one down")) == -1 &&
        terrainCheatDelta(findCheatByPhrase("Terrain ten up")) == 10 && terrainCheatDelta(findCheatByPhrase("Terrain ten down")) == -10 &&
        terrainCheatDelta(findCheatByPhrase("tuneup")) == 0 && terrainCheatDelta(nullptr) == 0);
  CHECK(shiftTerrainAltitude(250, 10) == 253 && shiftTerrainAltitude(5, -10) == 2 && shiftTerrainAltitude(100, 1) == 101);
  CHECK(soSueMeClamp(-100000) == -99999 && soSueMeClamp(-99999) == -99999 && soSueMeClamp(-5) == -5);

  // Log records.
  {
    Bytes b = serializeCheatLog({0x62E8630B, 0xE37FB7B8});
    CHECK(b.size() == 8 + 4 + 8 && rd16(&b[0]) == 1 && b[2] == 0x02 && b[3] == 0 && rd32(&b[4]) == 0xDEADBEEF && rd32(&b[8]) == 2);
    std::vector<uint32_t> out;
    CHECK(parseCheatLog(b, out) && out.size() == 2 && out[1] == 0xE37FB7B8);
    Bytes truncated(b.begin(), b.end() - 2);
    CHECK(!parseCheatLog(truncated, out) && out.empty());
    // unknown IDs are accepted on read
    CHECK(parseCheatLog(serializeCheatLog({0x12345678}), out) && out.size() == 1 && !findCheatById(out[0]));
    IxfWriter w; w.add({1, 2, 3}, Bytes{9}); w.add(kCheatCountKey, serializeCheatCount(17));
    Bytes city = w.build();
    Bytes c2 = writeCheatLog(city, {0x62E8630B});
    CHECK(readCheatLog(c2, out) && out.size() == 1);
    uint32_t cnt = 0; IxfFile f; Bytes cb;
    CHECK(f.openMemory(c2) && f.getBytes(kCheatCountKey, cb) && parseCheatCount(cb, cnt) && cnt == 17);
    CHECK(!readCheatLog(writeCheatLog(c2, {}), out));  // empty log omits the record
  }
  TEST_MAIN_END();
}
