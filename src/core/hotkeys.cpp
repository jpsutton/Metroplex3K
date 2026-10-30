#include "core/hotkeys.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace m3k {

int HotkeyTable::parse(const std::string& text) {
  int n = 0;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    // Left part: 4 numbers; right part: 4 numbers.
    long L[4], R[4]; int nl = 0, nr = 0;
    { const char* p = line.c_str(); const char* e = p + eq;
      while (p < e && nl < 4) { while (p < e && (*p == ' ' || *p == '\t')) p++; if (p >= e) break; char* end; long v = std::strtol(p, &end, 10); if (end == p) { nl = -1; break; } L[nl++] = v; p = end; } }
    { const char* p = line.c_str() + eq + 1;
      while (*p && nr < 4) { while (*p == ' ' || *p == '\t') p++; if (!*p || *p == '\r') break; char* end; long v = std::strtol(p, &end, 10); if (end == p) { nr = -1; break; } R[nr++] = v; p = end; } }
    if (nl != 4 || nr != 4) continue;
    if (L[0] > 1 || L[1] > 1 || L[2] > 1 || L[3] > 255 || L[0] < 0 || L[1] < 0 || L[2] < 0 || L[3] < 0) continue;
    int mods = (L[2] ? HK_SHIFT : 0) | (L[0] ? HK_CTRL : 0) | (L[1] ? HK_ALT : 0);
    map_[{int(L[3]), mods}] = {int(R[1]), int(R[2]), int(R[3])};
    n++;
  }
  return n;
}

const HotkeyBinding* HotkeyTable::find(int vk, int mods) const {
  auto it = map_.find({vk, mods});
  return it == map_.end() ? nullptr : &it->second;
}

void HotkeyTable::loadDefaults() {
  struct D { int ctrl, alt, shift, vk, cmd; };
  static const D d[] = {
      {0,0,0,'Q',1},{0,0,0,'W',4},{0,0,0,'E',5},{0,0,0,'A',6},{0,0,0,'S',7},{0,0,0,'D',8},{0,0,0,'Z',9},{0,0,0,'X',10},{0,0,0,'C',11},
      {0,0,0,'V',12},{0,0,0,'L',13},{0,1,0,'R',14},{0,0,0,'R',15},{0,0,0,'T',16},{0,1,0,'T',17},{0,0,0,'B',18},{1,0,0,'A',19},
      {1,0,0,'B',20},{1,0,0,'O',21},{1,0,0,'M',22},{0,0,0,'P',24},{0,0,0,'F',25},{0,0,0,'H',26},{0,0,0,'K',27},{1,0,0,'L',28},
      {1,0,0,'N',29},{1,0,0,'S',30},{1,1,0,'S',31},{0,0,0,121,33},{0,0,0,191,35},{0,0,0,188,36},{0,0,0,190,37},{1,1,1,'C',40},
      {0,0,0,107,41},{0,0,0,109,42},{0,0,0,34,43},{0,0,0,33,44},{0,0,0,37,45},{0,0,0,38,46},{0,0,0,39,47},{0,0,0,40,48},{0,0,0,123,49},
      {0,0,0,32,50},{1,0,0,'E',51},{0,0,1,'R',52},{0,0,1,'T',53},{0,1,1,'T',54},{0,0,1,'P',55},{0,0,1,'K',56},{0,0,0,'J',57},
      {0,0,1,'J',58},{1,0,0,'R',59},{1,0,0,'P',60},{0,0,0,115,61},{0,0,0,114,62},{0,0,0,'G',63},{1,0,0,'T',64},{0,0,0,113,65},
      {1,0,0,'I',68},{0,0,0,'I',69},{0,0,0,'U',70},{0,0,1,'U',71},{1,0,0,'D',72},{1,0,0,'W',73},
      {0,0,0,97,75},{0,0,0,98,77},{0,0,0,99,79},{0,0,0,100,81},{0,0,0,101,83},{0,0,0,27,85},{0,0,0,116,86},{1,0,0,'F',87},
      {0,0,0,117,88},{0,0,0,118,89},{0,0,0,112,90},{1,0,0,'Q',91},{0,0,0,'Y',93},{1,0,0,'Y',95},{0,1,0,115,102}};
  map_.clear();
  for (const D& e : d) map_[{e.vk, (e.shift ? HK_SHIFT : 0) | (e.ctrl ? HK_CTRL : 0) | (e.alt ? HK_ALT : 0)}] = {e.cmd, 0, 0};
}

uint32_t hotkeyToolClass(int cmd, uint32_t* param) {
  if (param) *param = 0;
  struct T { int cmd; uint32_t cls, param; };
  static const T t[] = {
      {1,0x42E88586,0},{4,0x02E88915,0},{5,0xA2E8893B,0},{6,0x02E88949,0},{7,0xA2E8895A,0},{8,0x02E88968,0},{9,0xC2E8898F,0},
      {10,0xA2E8899D,0},{11,0x02E889AA,0},{12,0x82E88C9C,0},{13,0x42C2F8BF,0},{14,0x02FF04FE,0},{15,0xC2B5DE77,0},{16,0x42B5DE98,0},
      {17,0x22FF058F,0},{18,0xE3166801,0},{20,0x025DDC4A,0},{24,0x42FF052F,0},{25,0x22FF054C,0},{26,0xE2FF0555,0},{27,0xC2FF0563,0},
      {28,0x610438A4,0x83172AD7},{29,0xE3270FE9,0},{30,0x410438DD,0},{31,0x410438DD,0xC3172AEA},{51,0xE2ED3560,0x62E925A2},
      {52,0x42B5DE86,0},{53,0xC2B5DEA6,0},{54,0x62FF059B,0},{55,0x22FF053F,0},{56,0xC2FF0571,0},{57,0x02FF05AA,0},{58,0x82FF05B7,0},
      {59,0xE2ED3560,0x62E92553},{60,0xC3001401,0},{61,0xC3001401,0},{62,0x610438A4,0x83172AD7},{64,0x610438A4,0x23172AF2},
      {65,0x410438DD,0},{68,0xE2ED3560,0xE2E92527},{69,0x22C2F8C5,0},{70,0x4253EC3F,0},{71,0x02E763C1,0},{72,0xE2ED3560,0x22E92581},
      {73,0x630288E9,0},{86,0xE3270FE9,0},{87,0x610438A4,0x63172B74},{88,0x610438A4,0x63172B74},{89,0x610438A4,0x23172AF2},
      {90,0x410438DD,0xC3172AEA},{93,0xC2E889B6,0},{95,0xE2ED3560,0xC2F291A9},{22,0x411164F1,0}};
  for (const T& e : t) if (e.cmd == cmd) { if (param) *param = e.param; return e.cls; }
  return 0;
}

}  // namespace m3k
