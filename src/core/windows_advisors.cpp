// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Advisor window, petitioner dialog (spec 09 sections 6.5, 6.6).
#include <algorithm>
#include "core/skin.h"
#include "core/windows.h"

namespace m3k {

using namespace wui;

// ================================================================ Advisor window
namespace {
const uint32_t kAdvisorOrder[7] = {kAdvUtilities, kAdvBudget, kAdvPlanner, kAdvEnvironment, kAdvSafety, kAdvTransport, kAdvDemographics};
const char* kAdvisorFb[7] = {"Utilities Advisor", "Financial Advisor", "City Planner", "Environmental Advisor",
                             "Public Safety Advisor", "Transportation Advisor", "Demographics Advisor"};
std::string hexName(const char* p, uint32_t v) { char b[40]; std::snprintf(b, sizeof b, "%s%X", p, v); return b; }
}  // namespace

int AdvisorWindow::advisorIndex(uint32_t id) {
  for (int i = 0; i < 7; i++) if (kAdvisorOrder[i] == id) return i + 1;
  return 0;
}

uint32_t AdvisorWindow::portraitInstance(int idx, int mood) {
  if (mood < 1 || mood > 5) mood = 3;
  return (uint32_t(idx & 0xFF) << 8) | uint32_t(mood);
}
uint32_t AdvisorWindow::smallPortraitInstance(int idx, int mood) { return 0x01000000u | portraitInstance(idx, mood); }

// Advisor title: table C29A6083 0x51.. (observed in the advisor window title bar).
std::string AdvisorWindow::advisorName(const WinEnv& E, uint32_t id) {
  int i = advisorIndex(id);
  if (!i) return "Advisor";
  static const uint32_t kTitle[7] = {0x51, 0x52, 0x57, 0x54, 0x55, 0x56, 0x53};   // by advisor index 1..7
  std::string s = E.text(kTblOrdinance, kTitle[i - 1], kAdvisorFb[i - 1]);
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

// Person name (029541F4:0x156.., observed order).
std::string AdvisorWindow::personName(const WinEnv& E, uint32_t id) {
  int i = advisorIndex(id);
  if (!i) return "";
  static const uint32_t kPerson[7] = {0x159, 0x157, 0x156, 0x158, 0x15A, 0x15B, 0x15C};
  return E.win(kPerson[i - 1], "");
}

std::vector<uint32_t> AdvisorWindow::topicsFor(const TopicList& l, int tab) {
  std::vector<uint32_t> out;
  for (uint32_t t : l.visibleTopics()) {
    const TopicInstance* ti = l.find(t);
    if (!ti) continue;
    if ((tab == 1) == ti->persistent()) out.push_back(t);
  }
  return out;
}

namespace {
const uint32_t kInk = rgb(0x31, 0x34, 0x7B), kLav = rgb(0xDE, 0xDF, 0xFF), kWhite = rgb(255, 255, 255);
// Right-pointing triangle bullet (rows 1,2,3,4,5,4,3,2,1 px wide), as observed in the Meet lists.
void bullet(Ui& ui, int x, int y) {
  for (int k = 0; k < 9; k++) ui.fill({x, y + k, k < 5 ? k + 1 : 9 - k, 1}, kInk);
}
std::string ellipsize(const Ui& ui, std::string s, int w) {
  if (ui.textWidth(s, "main9") <= w) return s;
  while (!s.empty() && ui.textWidth(s + "...", "main9") > w) s.pop_back();
  return s + "...";
}
// Title-bar system button (SysBtns 62B19CE9, rows: 0 close, 1 back; column = state).
bool sysButton(Ui& ui, const WinEnv& E, const std::string& name, int row, int x, int y) {
  const Image* sb = ui.skin ? ui.skin->get("sysbtns") : nullptr;
  Rect hit{x, y, 24, 25};
  E.spot(name, hit);
  bool hot = ui.hover(hit), down = hot && ui.in.down;
  if (sb && sb->w >= 96) { int cw = sb->w / 4, ch = sb->h / 4; ui.blitRect(*sb, x, y, (down ? 2 : hot ? 3 : 1) * cw, row * ch, cw, ch); }
  else ui.button(hit, row == 1 ? "<" : "x");
  return ui.clicked(hit);
}
// Help topics of the Briefings tab: each advisor appends a fixed list of help ids in order [STATIC]; the heading is the string of table
// 82B85965 with that id and the body the string of E26EC3B8 (A26EC340 is unused). The city planner's extra "Starter Town" item is not modelled.
std::vector<uint32_t> briefingIds(uint32_t adv) {
  std::vector<uint32_t> v;
  auto range = [&](uint32_t a, uint32_t b) { for (uint32_t i = a; i <= b; i++) v.push_back(i); };
  if (adv == kAdvBudget) range(0xBB8, 0xBC0);
  else if (adv == kAdvPlanner) range(0x3E9, 0x3ED);
  else if (adv == kAdvEnvironment) range(0x7D0, 0x7D7);
  else if (adv == kAdvDemographics) range(0xFA0, 0xFA5);
  else if (adv == kAdvSafety) range(0x1388, 0x1391);
  else if (adv == kAdvTransport) v = {0x1770, 0x1772, 0x1773, 0x1774, 0x177A};
  else if (adv == kAdvUtilities) range(0x1B59, 0x1B64);
  return v;
}
// Position of an advisor in the title blocks of table 029541F4 (Financial, Transportation, City Planning, Environmental, Public Safety, HEA, Utilities).
int titleBlockIndex(uint32_t adv) {
  if (adv == kAdvBudget) return 0; if (adv == kAdvTransport) return 1; if (adv == kAdvPlanner) return 2; if (adv == kAdvEnvironment) return 3;
  if (adv == kAdvSafety) return 4; if (adv == kAdvDemographics) return 5; return 6;
}
}  // namespace

// Two screens measured on the original (dialog-shots/hud--meet-*.png, 800x600):
// * the Meet list (383x354): seven small portraits (52DB60A0:0100AAMM) and the petitioner issues;
// * one advisor (384x354): "Discuss:" drop-down (news topics / briefings / ordinances), the topic list and the advisor's
//   big portrait with his or her name on a blue panel; title-bar back button returns to the list.
WindowResult AdvisorWindow::draw(Ui& ui, const WinEnv& E, AdvisorManager& am, const PetitionerManager& pm) {
  WindowResult R;
  if (selected == 0) {
    Rect r = placedAt(ui, 8, 8, 383, 354);
    if (frame(ui, E, r, " ")) { R.close = true; R.add(Act::Close); }
    blueBand(ui, {r.x + 4, r.y + 4, 8, 346});
    if (ui.skin) ui.fill({r.x + 5, r.y + 5, 8, 29}, rgb(0x7B, 0x92, 0xFF));
    blueBand(ui, {r.x + 4, r.y + 130, 375, 8});
    ui.text(r.x + 8, r.y + 8, E.win(0x62, "Meet"), "title12", ui.skin ? rgb(222, 223, 255) : kWhite);
    ui.text(r.x + 21, r.y + 40, E.win(0x64, "Advisors:"), "main9", kInk);
    // Order left to right as observed: Budget, Transport, Planner, Environment, Safety, HEA, Utilities.
    static const int kShown[7] = {2, 6, 3, 4, 5, 7, 1};
    for (int k = 0; k < 7; k++) {
      int idx = kShown[k];
      uint32_t id = kAdvisorOrder[idx - 1];
      Rect pr{r.x + 19 + 52 * k, r.y + 60, 45, 56};
      E.spot("advisor." + std::to_string(idx), pr);
      const Image* im = E.img(0x52DB60A0, smallPortraitInstance(idx, am.mood(id)));
      if (im) ui.blit(*im, pr.x, pr.y); else portrait(ui, pr, nullptr, std::string(1, advisorName(E, id)[0]), rgb(0x7A, 0x84, 0xB8));
      sunkenLines(ui, pr);
      if (ui.hover(pr)) ui.tooltip = advisorName(E, id);
      if (ui.clicked(pr)) { selected = id; tab = 0; topicSel = -1; scroll = 0; comboOpen = false; R.add(Act::SelectAdvisor, id); }
    }
    ui.text(r.x + 21, r.y + 146, E.win(0x63, "Petitioner Issues:"), "main9", kInk);
    // Petitioner Issues list (spec 09 6.5 [OBSERVED]): inset box (19,164)-(377,347), 14 px rows from text top y = 167, a drawn 4 px triangle at
    // x ~ 23 and the title in the small bold font from x ~ 34. One click selects (blue bar from x ~ 28, white text); a double click opens the page.
    frameNo++;
    Rect lr{r.x + 19, r.y + 164, 359, 184};   // light right/bottom lines at x 385 / y 355 on the original (observed)
    ui.fill(lr, kLav); sunkenLines(ui, lr);
    auto has = [&](uint32_t t, uint32_t i) { return E.has(t, i); };
    int k = 0;
    // Ascending petition id: the original's list control orders its rows by the id of the heading string (spec 09 6.5, 4/4 on Berlin).
    auto pending = pm.presentable();
    std::stable_sort(pending.begin(), pending.end(), [](const PendingPetition* a, const PendingPetition* b) { return a->id < b->id; });
    // More than 13 petitions: a scrollbar inside the list box (screen x ~370..385, y 175..355) and the rows start 1 px lower, first ink row y 176 [OBSERVED].
    const bool bar = int(pending.size()) > 13;
    const int visibleRows = 13;   // 13 rows in both cases [OBSERVED]
    if (bar) {
      const Rect br{lr.x + 343, lr.y + 1, 16, 182};   // plate rows y173..354 [OBSERVED]
      ui.fill(br, rgb(0x7B, 0x92, 0xFF));
      scenarioScrollBar(ui, E, br, lr, int(pending.size()), visibleRows, petScroll);
    } else petScroll = 0;
    petScroll = std::max(0, std::min(petScroll, std::max(0, int(pending.size()) - visibleRows)));
    for (size_t pi = size_t(petScroll); pi < pending.size(); pi++) {
      const PendingPetition* p = pending[pi];
      if (k >= visibleRows) break;
      uint32_t tbl = pm.config.tableFor(PetitionText::Heading, p->id, has);
      std::string h = hexName("Petition ", p->id);
      if (tbl) { std::string raw = E.rawText(tbl, p->id); if (!raw.empty()) h = topicText(raw, E.tc, p->args); }
      const int ty = lr.y + 3 + (bar ? 1 : 0) + 14 * k;
      Rect row{lr.x + 1, ty - 1, lr.w - 2 - (bar ? 18 : 0), 14};
      E.spot("petition." + std::to_string(p->id), row);
      const bool sel = petSel == int(p->id);
      if (sel) ui.fill({r.x + 28, ty, lr.x + lr.w - 1 - (r.x + 28) - (bar ? 18 : 0), 13}, rgb(0, 0, 255));
      const int bx = bar ? 1 : 0;   // with the scrollbar the row text and arrows sit 1 px further right [OBSERVED]
      for (int k2 = 0; k2 < 9; k2++) ui.fill({r.x + 23 + bx, ty + k2, k2 < 5 ? k2 + 1 : 9 - k2, 1}, sel ? kWhite : kInk);   // 5 x 9 triangle, rows 175..183 [observed]
      ui.text(r.x + 34 + bx, ty - 3, ellipsize(ui, h, lr.x + lr.w - (r.x + 34) - 6), "main9", sel ? kWhite : kInk);   // main9: first ink row 175 (observed)
      if (ui.clicked(row)) {
        if (sel && frameNo - petClickFrame <= 30) R.add(Act::OpenPetitioner, p->id);
        petSel = int(p->id); petClickFrame = frameNo;
      }
      k++;
    }
    if (ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
    return R;
  }

  // ---- one advisor
  Rect r = placedAt(ui, 8, 8, 384, 354);
  TopicList& list = am.list(selected);
  const uint32_t tabIds[3] = {0x258, 0x1BF, 0x76};
  const char* tabFb[3] = {"News Topics", "Briefings", "Ordinances"};
  // Rows of the current tab: news topics, briefings (persistent topics), ordinance announcements (spec 22 3.6).
  struct Row { uint32_t id; std::string title; };
  std::vector<Row> rows;
  if (tab == 2) {
    for (const auto& oc : AdvisorManager::ordinanceTopics(selected))
      if (!ordinances || ordinances->available(oc.first)) rows.push_back({oc.second, E.text(0x82B85965, oc.second, hexName("Ordinance ", oc.second))});   // help ids = the 3.6 topic numbers
  } else if (tab == 1) {
    for (uint32_t t : briefingIds(selected)) rows.push_back({t, E.text(0x82B85965, t, hexName("Help ", t))});
  } else {
    auto ids = topicsFor(list, tab);
    std::stable_sort(ids.begin(), ids.end());   // the news topics are listed by ascending topic id, not in raise order [OBSERVED: Transportation, Public Safety, Health pages]
    for (uint32_t t : ids) rows.push_back({t, E.text(kTblAdvisorHeading, t, hexName("Topic ", t))});
  }
  const bool onPage = topicSel >= 0;
  // Title: the advisor role on the news pages; "Financial Briefings" (029541F4 0x2C3..) / "Financial Ordinances" (0x2BC..) on the other tabs [OBSERVED / STATIC].
  std::string title = advisorName(E, selected);
  if (onPage && tab != 0) {
    static const char* fb[2] = {"Briefings", "Ordinances"};
    title = E.win((tab == 1 ? 0x2C3u : 0x2BCu) + uint32_t(titleBlockIndex(selected)), fb[tab == 1 ? 0 : 1]);
  }
  if (frame(ui, E, r, title)) { R.close = true; R.add(Act::Close); }
  if (sysButton(ui, E, "back", 1, r.x + 328, r.y + 7)) {
    if (onPage) { topicSel = -1; scroll = 0; return R; }   // back from a page to its list
    selected = 0; comboOpen = false; return R;
  }
  // Drop-down: sunken white field with a double dark top/left edge and the combo arrow (62B99D31 cell 1); gone on the pages.
  Rect combo{r.x + 62, r.y + 43, 194, 17};
  if (!onPage) {
    ui.text(r.x + 9, r.y + 46, E.win(0x75, "Discuss:"), "main9", kInk);
    E.spot("discuss", combo);
    ui.fill({combo.x, combo.y, combo.w, 2}, rgb(0x7B, 0x8A, 0xFF)); ui.fill({combo.x, combo.y, 2, combo.h}, rgb(0x7B, 0x8A, 0xFF));
    ui.fill({combo.x + 2, combo.y + 2, 173, 14}, kWhite);
    ui.fill({combo.x + 175, combo.y + 2, 1, 14}, rgb(0xFF, 0xF3, 0xFF));
    ui.fill({combo.x + 176, combo.y + 1, 1, 15}, rgb(0x7B, 0x92, 0xFF));
    ui.fill({combo.x + 193, combo.y, 1, 17}, rgb(0xFF, 0xF3, 0xFF)); ui.fill({combo.x, combo.y + 16, 177, 1}, rgb(0xFF, 0xF3, 0xFF));
    { bool c = false; Rect ar{r.x + 239, r.y + 44, 16, 16};
      if (!(ui.skin && ui.skin->imageButtonCells(ui, ui.skin->get("combo"), 4, ar, "", false, true, c))) c = ui.button(ar, "v");
      if (c || ui.clicked(combo)) comboOpen = !comboOpen; }
    ui.text(r.x + 66, r.y + 45, E.win(tabIds[tab], tabFb[tab]), "main9", kInk);
  } else comboOpen = false;
  if (onPage) {
    // Text panel x 15..262, y 47..353: (222,223,255) interior inside a dark top/left, light bottom/right bevel [OBSERVED].
    const Rect P{r.x + 7, r.y + 39, 248, 307};
    ui.fill(P, rgb(222, 223, 255)); insetBevel(ui, P);
    const TopicInstance* ti = list.find(uint32_t(topicSel));
    const bool help = tab >= 1;   // briefings and ordinances read the help tables 82B85965 / E26EC3B8
    const std::string head = help ? E.text(0x82B85965, uint32_t(topicSel), hexName("Help ", uint32_t(topicSel)))
                                  : E.text(kTblAdvisorHeading, uint32_t(topicSel), hexName("Topic ", uint32_t(topicSel)));
    ui.text(r.x + 13, r.y + 50, ellipsize(ui, head, 232), "main10", kInk);   // bold heading: ink x 21, rows 61..73
    const std::string raw = help ? E.text(0xE26EC3B8, uint32_t(topicSel), "(no text)") : E.text(kTblAdvisorMessage, uint32_t(topicSel), "(no text)");
    const std::string body = topicText(raw, E.tc, ti ? ti->args : std::vector<TopicArg>{});
    auto lines = wrapKeepSpaces(ui, body, 230, "main9");   // a line fits while its ink is <= 228 px [OBSERVED]
    const int bodyTop = r.y + 78, visible = std::max(1, (P.y + P.h - 4 - bodyTop) / 14);
    const bool overflow = int(lines.size()) > visible;
    if (overflow) { lines = wrapKeepSpaces(ui, body, 212, "main9"); scenarioScrollBar(ui, E, {r.x + 238, r.y + 78, 16, 265}, P, int(lines.size()), visible, scroll); }
    else scroll = 0;
    for (int i = 0; i < visible && scroll + i < int(lines.size()); i++) ui.text(r.x + 12, bodyTop + 14 * i, lines[size_t(scroll + i)], "main9", rgb(0, 0, 0));   // first line's ink at y 89, pitch 14
    if (tab == 2) {
      // "Review Ordinances" box x 277..376, y 84..149: a (173,182,255) border round (123,146,255), two centred bold lines and the Ordinances button.
      const Rect B{r.x + 269, r.y + 76, 100, 66};
      ui.fill(B, rgb(173, 182, 255)); ui.fill({B.x + 1, B.y + 1, B.w - 2, B.h - 2}, rgb(123, 146, 255));
      const std::string cap = E.win(0x1C2, "Review Ordinances");
      const size_t sp = cap.find(' ');
      ui.textCentered({B.x, B.y + 7, B.w, 14}, sp == std::string::npos ? cap : cap.substr(0, sp), "main10", kInk);
      if (sp != std::string::npos) ui.textCentered({B.x, B.y + 21, B.w, 14}, cap.substr(sp + 1), "main10", kInk);
      bool c = false; Rect ob{B.x + 33, B.y + 37, 36, 24};
      E.spot("ordinances", ob);
      if (!(ui.skin && ui.skin->imageButtonCells(ui, ui.skin->image(skin_group::Gonzo, 0x09), 4, ob, "", false, true, c))) c = ui.button(ob, "?");
      if (c) R.add(Act::OpenOrdinances);
    } else if (tab == 0) {
      // Topic picture 118 x 110 (831D112B via the topic's graphic id) at (270, 48) inside a dark top/left, light bottom/right frame x 269..388, y 47..158.
      insetBevel(ui, {r.x + 261, r.y + 39, 120, 112});
      uint32_t g = 0, gi = 0;
      if (am.gfx.get(uint32_t(topicSel), g, gi)) if (const Image* im = E.img(g, gi)) ui.blit(*im, r.x + 262, r.y + 40);
    }
  } else {
    // List: 13 px selection bars on a 14 px pitch x 16..258 from y 76; a single click selects (blue bar, white text and arrow), a double click opens [OBSERVED].
    const Rect lr{r.x + 7, r.y + 67, 248, 279};
    ui.fill(lr, kLav); sunkenLines(ui, lr);
    ++frameNo;
    const int visible = lr.h / 14;
    if (ui.hover(lr) && ui.in.wheel) scroll -= ui.in.wheel;
    scroll = std::max(0, std::min(scroll, std::max(0, int(rows.size()) - visible)));
    for (int i = 0; i < visible && scroll + i < int(rows.size()); i++) {
      const Row& row = rows[size_t(scroll + i)];
      const Rect bar{r.x + 8, r.y + 68 + 14 * i, 243, 13};
      E.spot("topic." + std::to_string(i), bar);
      const bool sel = rowSel == int(row.id);
      if (sel) ui.fill(bar, rgb(0, 0, 255));
      for (int k = 0; k < 9; k++) ui.fill({lr.x + 4, bar.y + 2 + k, k < 5 ? k + 1 : 9 - k, 1}, sel ? kWhite : kInk);
      ui.text(lr.x + 15, bar.y - 1, ellipsize(ui, row.title, lr.w - 22), "main9", sel ? kWhite : kInk);
      if (ui.clicked(bar)) {
        if (sel && frameNo - rowClickFrame <= 30) {
          topicSel = int(row.id); scroll = 0;
          if (tab == 0) list.dismiss(row.id);   // reading a topic hides it (and withdraws its ticker item) when it has a limit, is persistent or expiring [STATIC]
        }
        rowSel = int(row.id); rowClickFrame = frameNo;
      }
    }
  }
  // Portrait panel: big portrait (52DB60A0:00AAMM, 92x115 cells; cell 0) with a 1 px border, name on two lines.
  blueBand(ui, {r.x + 261, r.y + 180, 119, 166});
  int idx = advisorIndex(selected);
  if (const Image* im = E.img(0x52DB60A0, portraitInstance(idx, am.mood(selected)))) {
    int cw = im->w >= 460 ? 92 : im->w;
    ui.blitRect(*im, r.x + 274, r.y + 187, 0, 0, cw, std::min(115, im->h));
    Rect pb{r.x + 274, r.y + 187, cw, std::min(115, im->h)};
    ui.fill({pb.x, pb.y, pb.w, 1}, rgb(0x42, 0x41, 0x42)); ui.fill({pb.x, pb.y, 1, pb.h}, rgb(0x42, 0x41, 0x42));
    ui.fill({pb.x, pb.y + pb.h - 1, pb.w, 1}, kWhite); ui.fill({pb.x + pb.w - 1, pb.y, 1, pb.h}, kWhite);
  }
  {
    std::string pn = personName(E, selected);
    size_t sp = pn.find(' ');
    ui.text(r.x + 275, r.y + 310, sp == std::string::npos ? pn : pn.substr(0, sp), "main9", kInk);
    if (sp != std::string::npos) ui.text(r.x + 275, r.y + 324, pn.substr(sp + 1), "main9", kInk);
  }
  // Drop-down list on top.
  if (comboOpen) {
    Rect dd{r.x + 63, combo.y + combo.h, 192, 42};   // x71..262 y68..109 on the original
    ui.fill(dd, kWhite);
    { const uint32_t blk = rgb(0, 0, 0);
      ui.fill({dd.x, dd.y, dd.w, 1}, blk); ui.fill({dd.x, dd.y + dd.h - 1, dd.w, 1}, blk); ui.fill({dd.x, dd.y, 1, dd.h}, blk); ui.fill({dd.x + dd.w - 1, dd.y, 1, dd.h}, blk);
      ui.fill({dd.x + 1, dd.y + 1, 1, dd.h - 2}, rgb(189, 190, 189)); ui.fill({dd.x + 1, dd.y + dd.h - 2, dd.w - 2, 1}, rgb(189, 190, 189)); ui.fill({dd.x + dd.w - 2, dd.y + 1, 1, dd.h - 2}, rgb(140, 142, 140));
      }
    for (int t = 0; t < 3; t++) {
      Rect row{dd.x + 1, dd.y + 14 * t, dd.w - 2, 14};
      E.spot("tab." + std::to_string(t), row);
      bool hot = ui.hover(row) || (t == tab && !ui.hover(dd));
      if (hot) ui.fill({row.x, row.y + (t == 0 ? 1 : 0), row.w, row.h - (t == 0 ? 1 : 0) - (t == 2 ? 1 : 0)}, rgb(0, 0, 255));   // inside the frame's black rows [OBSERVED]
      ui.text(row.x + 2, row.y, E.win(tabIds[t], tabFb[t]), "main9", hot ? kWhite : kInk);
      if (ui.clicked(row)) { tab = t; topicSel = -1; scroll = 0; comboOpen = false; }
    }
    if (ui.in.released && !dd.contains(ui.in.mx, ui.in.my) && !combo.contains(ui.in.mx, ui.in.my) && !Rect{r.x + 239, r.y + 44, 16, 16}.contains(ui.in.mx, ui.in.my)) comboOpen = false;
  }
  if (ui.in.key[K_ESC]) { if (comboOpen) comboOpen = false; else { R.close = true; R.add(Act::Close); } }
  return R;
}

// ================================================================ Petitioner dialog
PetitionerView makePetitionerView(const WinEnv& E, const PetitionerConfig& cfg, const PendingPetition& p, const TextContext* tc) {
  PetitionerView v;
  v.petition = p.id;
  v.tag = "pet";
  auto has = [&](uint32_t t, uint32_t i) { return E.has(t, i); };
  auto get = [&](PetitionText c, const std::string& fb) {
    uint32_t t = cfg.tableFor(c, p.id, has);
    if (!t) return fb;
    std::string raw = E.rawText(t, p.id);
    if (raw.empty()) raw = fb;
    return topicText(raw, tc ? *tc : E.tc, p.args);
  };
  v.name = get(PetitionText::Name, "Citizen");
  v.heading = get(PetitionText::Heading, hexName("Petition ", p.id));
  v.message = get(PetitionText::Message, "");
  v.button1 = get(PetitionText::Button1, "");
  v.button2 = get(PetitionText::Button2, "");
  v.impactText = get(PetitionText::Advice, "");   // the Impact Analysis popup (spec 09 6.6.1)
  auto ex = cfg.extraInfo(p.id);
  if (ex.found && ex.second >= 1 && ex.second <= 7) {   // field b of the extra-info line = the responsible advisor 1..7, in kAdvisorOrder's order (spec 22 5.2)
    v.impactTitle = AdvisorWindow::advisorName(E, kAdvisorOrder[ex.second - 1]);
    v.impactPortrait = E.img(0x52DB60A0, AdvisorWindow::portraitInstance(int(ex.second), 3));   // mood 3 whatever the advisor's mood; the variant prefix rule is OPEN (plain 0x0AMM)
  }
  if (ex.found && ex.portrait) { const uint32_t pic = petitionerPortraitImage(ex.portrait); v.portrait = E.img(0x52DB60A0, pic ? pic : ex.portrait); }
  return v;
}

// Layout measured on dialog-shots/hud--meet--petitioner-issue.png (800x600): 384x354 frame titled with the petition
// heading (back and close title-bar buttons), blue strip with a plate holding the round Dismiss button (Gonzo
// 42DEC7D2:02), the petitioner's name and portrait, and the message in black main9 in a sunken lavender box. Answer /
// impact buttons (no reference screenshot) sit at the bottom of the message box when the petition has them.
WindowResult PetitionerDialog::draw(Ui& ui, const WinEnv& E, const PetitionerView& v) {
  if (!impact) return page(ui, E, v);
  // The Impact Analysis popup is modal: the page underneath is drawn but ignores the mouse and keys (spec 09 6.6.1).
  WindowResult R;
  {
    const Input saved = ui.in;
    ui.in = Input();
    ui.in.mx = ui.in.my = -100000;
    page(ui, E, v);
    ui.in = saved;
  }
  impactPopup(ui, E, v);   // its OK closes it only; Enter / Esc were not tested in the original (spec 09 6.6.1 [OPEN]), so no key closes it
  return R;
}

// Impact Analysis popup (spec 09 6.6.1 [STATIC; OBSERVED at 800 x 600, petition 0x64]): 337 x 254, no title bar or close box, centred over the petition
// page (client (31, 59) when the page is at (8, 8)). The page underneath is not changed and answers nothing.
void PetitionerDialog::impactPopup(Ui& ui, const WinEnv& E, const PetitionerView& v) {
  const Rect p{pageRect_.x + 23, pageRect_.y + 51, 337, 254};
  const std::string t = v.tag.empty() ? "pet" : v.tag;
  if (!(ui.skin && ui.skin->drawDialogBody(ui, p))) ui.panel(p, "");
  const uint32_t plate = rgb(123, 146, 255), ring = rgb(173, 182, 255);
  ui.fill({p.x + 1, p.y + 1, 10, 252}, plate); ui.fill({p.x + 1, p.y + 1, 335, 10}, plate);       // four 10 px plate bands: left, top,
  ui.fill({p.x + 326, p.y + 1, 10, 252}, plate); ui.fill({p.x + 1, p.y + 243, 335, 10}, plate);    // right, bottom
  ui.fill({p.x + 11, p.y + 11, 315, 1}, ring); ui.fill({p.x + 11, p.y + 242, 315, 1}, ring);       // 1 px ring on their inner side
  ui.fill({p.x + 11, p.y + 11, 1, 232}, ring); ui.fill({p.x + 325, p.y + 11, 1, 232}, ring);
  std::string title = v.impactTitle;
  while (!title.empty() && ui.textWidth(title, "main9") > 290) title.pop_back();   // long titles are cut to 290 px
  ui.text(p.x + 19, p.y + 15, title, "main9", kInk);   // ink starts at (19, 18) [OBSERVED, about]; main9 puts the first ink row 3 below the origin
  if (v.impactPortrait) ui.blitRect(*v.impactPortrait, p.x + 227, p.y + 37, 0, 0, std::min(92, v.impactPortrait->w), std::min(115, v.impactPortrait->h));
  // Text box (17, 37)-(221, 236): inset like the petition's message box, wrapped like it (box width - 41), scrollbar inside the right edge when needed.
  const Rect tb{p.x + 17, p.y + 37, 204, 199};
  ui.fill(tb, kLav); sunkenLines(ui, tb);
  const auto lines = wrap(ui, v.impactText, tb.w - 41, "main9");
  const int visible = (tb.h - 8) / 14;
  if (int(lines.size()) > visible) {
    const Rect sb{tb.x + tb.w - 16, tb.y, 16, tb.h};
    ui.fill(sb, plate);
    scenarioScrollBar(ui, E, sb, tb, int(lines.size()), visible, impactScroll);
  } else impactScroll = 0;
  for (int i = 0; i < visible && impactScroll + i < int(lines.size()); i++) ui.text(tb.x + 12, tb.y + 6 + 14 * i, lines[size_t(impactScroll + i)], "main9", rgb(0, 0, 0));   // ink from x 29, first line ink top y 46 [OBSERVED, about]
  // OK plate (227, 187)-(319, 236) with a 1 px ring on all four sides, the check-mark button (GZBtnDone art) centred in it.
  const Rect okp{p.x + 227, p.y + 187, 92, 49};
  blueBand(ui, okp);
  const Image* art = ui.skin ? ui.skin->image(skin_group::Sys, 0xA2DEFD9A) : nullptr;
  const int cw = art && art->w >= 4 ? art->w / 4 : 42, ch = art && !art->empty() ? art->h : 24;
  const Rect ob{okp.x + (okp.w - cw) / 2, okp.y + (okp.h - ch) / 2, cw, ch};
  E.spot(t + ".impact.ok", ob);
  bool c = false;
  if (!(art && ui.skin->imageButtonCells(ui, art, 4, ob, "", false, true, c))) c = ui.button(ob, "OK");
  if (c) { impact = false; impactScroll = 0; }
}

WindowResult PetitionerDialog::page(Ui& ui, const WinEnv& E, const PetitionerView& v) {
  WindowResult R;
  Rect r = inMeet ? placedAt(ui, 8, 8, 384, 354) : centered(ui, 384, 354);
  pageRect_ = r;
  if (frame(ui, E, r, v.heading.empty() ? E.win(0x2DD, "Petitioner") : v.heading)) { R.close = true; R.add(Act::Close); }
  std::string t = v.tag.empty() ? "pet" : v.tag;
  if (sysButton(ui, E, t + ".back", 1, r.x + 328, r.y + 7)) { R.close = true; R.add(Act::Close); }
  auto answer = [&](bool accepted) {
    if (v.isDeal) R.add(accepted ? Act::AcceptDeal : Act::DeclineDeal, uint32_t(v.pendingIndex));
    else R.add(Act::AnswerPetition, v.petition, accepted);
    R.close = true;
  };
  const bool offer = !v.button1.empty() || !v.button2.empty();   // offers use their own layout [OBSERVED: business, reward, neighbour deal, complaints]
  blueBand(ui, {r.x + 4, r.y + 34, 8, 316});
  if (!offer) {
    blueBand(ui, {r.x + 4, r.y + 310, 47, 40});
    ui.fill({r.x + 5, r.y + 311, 11, 38}, rgb(0x7B, 0x92, 0xFF));   // plate merges with the strip
  }
  ui.text(r.x + 21, r.y + 41, v.name, "main9", kInk);
  Rect pp{r.x + 19, r.y + 58, 92, 115};
  if (v.portrait) ui.blitRect(*v.portrait, pp.x, pp.y, 0, 0, std::min(92, v.portrait->w), std::min(115, v.portrait->h));
  else portrait(ui, pp, nullptr, v.name.empty() ? "?" : v.name.substr(0, 1), rgb(0x9A, 0x84, 0x7A));
  sunkenLines(ui, pp);
  Rect mb{r.x + 121, r.y + 58, 258, offer ? 202 : 291};
  ui.fill(mb, kLav); sunkenLines(ui, mb);
  const bool hasAnswers = !v.button1.empty() || !v.button2.empty();
  const int textH = mb.h - 8 - (hasAnswers ? 40 : 0);
  // Body text: lines of up to ~232 px on a 14 px pitch [OBSERVED]; returns the height used.
  auto body = [&](Rect br, const std::string& text) {
    int y = 0;
    for (const auto& l : wrap(ui, text, 232, "main9")) { if (br.h > 0 && y + 14 > br.h) break; ui.text(br.x, br.y + y, l, "main9", rgb(0, 0, 0)); y += 14; }
    return y;
  };
  if (offer) {
    // Text panel x129..386 y66..267 with a scrollbar inside its right edge; 13 lines of 14 px; "Take Action:" plate with two radio buttons; "Impact Analysis:"
    // plate with the round "?" button [OBSERVED at 800 x 600, window at (8, 8)].
    const auto lines = wrap(ui, v.message, 217, "main9");   // a line fits while its ink is <= 216 px [OBSERVED]
    const int visible = 13;
    const bool over = int(lines.size()) > visible;
    if (over) {
      ui.fill({r.x + 364, r.y + 58, 16, 202}, rgb(0x7B, 0x92, 0xFF));
      scenarioScrollBar(ui, E, {r.x + 364, r.y + 58, 16, 202}, mb, int(lines.size()), visible, scroll);   // y66..267 [OBSERVED]
    } else scroll = 0;
    for (int i = 0; i < visible && scroll + i < int(lines.size()); i++) ui.text(r.x + 133, r.y + 62 + 14 * i, lines[size_t(scroll + i)], "main9", rgb(0, 0, 0));
    const uint32_t plate = rgb(0x7B, 0x92, 0xFF);
    ui.fill({r.x + 5, r.y + 263, 115, 24}, plate); ui.fill({r.x + 5, r.y + 287, 45, 62}, plate);
    { const uint32_t ring = rgb(173, 182, 255);   // 1 px ring: top row, the band's right edge, the strip's right edge [OBSERVED]
      ui.fill({r.x + 12, r.y + 262, 108, 1}, ring); ui.fill({r.x + 120, r.y + 262, 1, 25}, ring); ui.fill({r.x + 50, r.y + 287, 1, 62}, ring);
      ui.fill({r.x + 51, r.y + 287, 70, 1}, ring); ui.fill({r.x + 5, r.y + 349, 46, 1}, ring); }   // ... and under the band right of the strip and under the strip
    ui.text(r.x + 21, r.y + 268, "Take Action:", "main9", kInk);
    const std::string labels[2] = {v.button1, v.button2};
    for (int k = 0; k < 2; k++) {   // the big round button art (the Dismiss button's 42DEC7D2:02) as radio, ink x28..51 y298..321 / y326..349
      Rect hit{r.x + 19, r.y + 290 + 28 * k, 26 + 4 + ui.textWidth(labels[k], "main9") + 8, 25};
      Rect rb{r.x + 19, r.y + 290 + 28 * k, 26, 25};
      E.spot(t + (k ? ".button2" : ".button1"), hit);
      bool c = false;
      const Image* art = E.img(0x42DEC7D2, 0x02);
      if (!(ui.skin && art && ui.skin->imageButtonCells(ui, art, 4, rb, "", false, true, c))) { ui.fill({rb.x + 6, rb.y + 6, 12, 12}, rgb(255, 255, 255)); ui.frame({rb.x + 6, rb.y + 6, 12, 12}, false); c = ui.clicked(rb); }
      ui.text(r.x + 58, r.y + 292 + 28 * k, labels[k], "main9", kInk);
      if (c || ui.clicked(hit)) answer(k == 0);
    }
    blueBand(ui, {r.x + 272, r.y + 293, 107, 56});   // plate x281..385 y302..355 with its 1 px ring outside (x280, x386, y301, y356)
    ui.text(r.x + 278, r.y + 307, "Impact", "main9", kInk); ui.text(r.x + 278, r.y + 321, "Analysis:", "main9", kInk);
    {
      Rect q{r.x + 342, r.y + 317, 26, 24};
      E.spot(t + ".impact", q);
      bool c = false;
      if (ui.skin && ui.skin->imageButtonCells(ui, ui.skin->image(skin_group::Gonzo, 0x19), 4, q, "", false, true, c)) {}
      else c = ui.button(q, "?");
      if (c) impact = true;
    }
  } else {
    // An information petition (no choice buttons): just the text; the round Dismiss button below closes it. It has no Impact Analysis plate (spec 09 6.6).
    body({r.x + 133, r.y + 63, 232, textH}, v.message);
  }
  // Round Dismiss button on the plate with its two-line label (info petitions only).
  if (!offer) {
    Rect db{r.x + 19, r.y + 318, 26, 25};
    E.spot(t + ".dismiss", db);
    bool c = false;
    const Image* art = E.img(0x42DEC7D2, 0x02);
    if (!(ui.skin && art && ui.skin->imageButtonCells(ui, art, 4, db, "", false, true, c))) c = ui.button(db, "x");
    std::string lbl = E.win(0x2DE, "Dismiss Petitioner");
    size_t sp = lbl.find(' ');
    ui.text(r.x + 58, r.y + 318, sp == std::string::npos ? lbl : lbl.substr(0, sp), "main9", kInk);
    if (sp != std::string::npos) ui.text(r.x + 58, r.y + 332, lbl.substr(sp + 1), "main9", kInk);
    if (c || ui.clicked({r.x + 58, r.y + 318, 70, 28})) {
      if (v.isDeal) R.add(Act::DeclineDeal, uint32_t(v.pendingIndex)); else R.add(Act::DismissPetitioner, v.petition);
      R.close = true;
    }
  }
  if (ui.in.key[K_ESC]) { R.close = true; R.add(Act::Close); }
  return R;
}

}  // namespace m3k
