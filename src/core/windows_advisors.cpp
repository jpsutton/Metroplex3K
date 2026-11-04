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
    Rect lr{r.x + 19, r.y + 164, 358, 183};
    ui.fill(lr, kLav); sunkenLines(ui, lr);
    auto has = [&](uint32_t t, uint32_t i) { return E.has(t, i); };
    int k = 0;
    // Ascending petition id: the original's list control orders its rows by the id of the heading string (spec 09 6.5, 4/4 on Berlin).
    auto pending = pm.presentable();
    std::stable_sort(pending.begin(), pending.end(), [](const PendingPetition* a, const PendingPetition* b) { return a->id < b->id; });
    for (const PendingPetition* p : pending) {
      if (k >= (lr.h - 3) / 14) break;
      uint32_t tbl = pm.config.tableFor(PetitionText::Heading, p->id, has);
      std::string h = tbl ? E.text(tbl, p->id, hexName("Petition ", p->id)) : hexName("Petition ", p->id);
      const int ty = lr.y + 3 + 14 * k;
      Rect row{lr.x + 1, ty - 1, lr.w - 2, 14};
      E.spot("petition." + std::to_string(p->id), row);
      const bool sel = petSel == int(p->id);
      if (sel) ui.fill({r.x + 28, ty, lr.x + lr.w - 1 - (r.x + 28), 13}, rgb(0, 0, 255));
      for (int c = 0; c < 4; c++) ui.fill({r.x + 23 + c, ty + 6 - (3 - c), 1, 2 * (3 - c) + 1}, sel ? kWhite : kInk);
      ui.text(r.x + 34, ty, ellipsize(ui, h, lr.x + lr.w - (r.x + 34) - 6), "main10", sel ? kWhite : kInk);
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
  if (frame(ui, E, r, advisorName(E, selected))) { R.close = true; R.add(Act::Close); }
  if (sysButton(ui, E, "back", 1, r.x + 328, r.y + 7)) { selected = 0; comboOpen = false; return R; }
  ui.text(r.x + 9, r.y + 46, E.win(0x75, "Discuss:"), "main9", kInk);
  const uint32_t tabIds[3] = {0x258, 0x1BF, 0x76};
  const char* tabFb[3] = {"News Topics", "Briefings", "Ordinances"};
  // Drop-down: sunken white field with a double dark top/left edge and the combo arrow (62B99D31 cell 1).
  Rect combo{r.x + 62, r.y + 43, 194, 17};
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
  // List / topic area.
  Rect lr{r.x + 7, r.y + 67, 248, 279};
  ui.fill(lr, kLav); sunkenLines(ui, lr);
  TopicList& list = am.list(selected);
  if (tab == 2) {
    if (ordinances) R.merge(ordWindow.draw(ui, E, *ordinances, money, selected, false, {lr.x + 3, lr.y + 3, lr.w - 6, lr.h - 6}));
  } else if (topicSel >= 0) {
    // Topic text (no reference screenshot): heading, message, Dismiss / back-to-list buttons at the bottom of the box.
    const TopicInstance* ti = list.find(uint32_t(topicSel));
    if (!ti) topicSel = -1;
    else {
      ui.text(lr.x + 6, lr.y + 4, ellipsize(ui, E.text(kTblAdvisorHeading, ti->topicId, hexName("Topic ", ti->topicId)), lr.w - 12), "main9", kInk);
      std::string raw = E.text(kTblAdvisorMessage, ti->topicId, "(no text)");
      paragraph(ui, {lr.x + 6, lr.y + 22, lr.w - 12, lr.h - 64}, topicText(raw, E.tc, ti->args), "main9", rgb(0, 0, 0));
      if (button(ui, E, "topics", {lr.x + 6, lr.y + lr.h - 38, 112, 33}, "<<")) topicSel = -1;
      if (button(ui, E, "dismiss", {lr.x + lr.w - 118, lr.y + lr.h - 38, 112, 33}, E.win(0x2DE, "Dismiss"))) {
        uint32_t t = ti->topicId;
        list.dismiss(t);
        R.add(Act::DismissTopic, selected, t);
        topicSel = -1;
      }
    }
  } else {
    auto topics = topicsFor(list, tab);
    const int visible = lr.h / 14;
    if (ui.hover(lr) && ui.in.wheel) scroll -= ui.in.wheel;
    scroll = std::max(0, std::min(scroll, std::max(0, int(topics.size()) - visible)));
    for (int i = 0; i < visible && scroll + i < int(topics.size()); i++) {
      uint32_t t = topics[size_t(scroll + i)];
      Rect row{lr.x + 1, lr.y + 14 * i, lr.w - 2, 14};
      E.spot("topic." + std::to_string(i), row);
      bool hot = ui.hover(row);
      if (hot) ui.fill(row, rgb(0x7B, 0x92, 0xFF));
      bullet(ui, lr.x + 4, row.y + 3);
      ui.text(lr.x + 15, row.y, ellipsize(ui, E.text(kTblAdvisorHeading, t, hexName("Topic ", t)), lr.w - 22), "main9", hot ? kWhite : kInk);
      if (ui.clicked(row)) topicSel = int(t);
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
    Rect dd{combo.x, combo.y + combo.h, combo.w, 3 * 14 + 2};
    ui.fill(dd, kWhite); ui.frame(dd, false);
    for (int t = 0; t < 3; t++) {
      Rect row{dd.x + 1, dd.y + 1 + 14 * t, dd.w - 2, 14};
      E.spot("tab." + std::to_string(t), row);
      bool hot = ui.hover(row) || (t == tab && !ui.hover(dd));
      if (hot) ui.fill(row, rgb(0, 0, 255));
      ui.text(row.x + 3, row.y, E.win(tabIds[t], tabFb[t]), "main9", hot ? kWhite : kInk);
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
    std::string raw = E.text(t, p.id, fb);
    return topicText(raw, tc ? *tc : E.tc, p.args);
  };
  v.name = get(PetitionText::Name, "Citizen");
  v.heading = get(PetitionText::Heading, hexName("Petition ", p.id));
  v.message = get(PetitionText::Message, "");
  v.button1 = get(PetitionText::Button1, "");
  v.button2 = get(PetitionText::Button2, "");
  v.advisorResponse = get(PetitionText::Advice, "");
  auto ex = cfg.extraInfo(p.id);
  if (ex.found && ex.portrait) v.portrait = E.img(0x52DB60A0, ex.portrait);
  return v;
}

// Layout measured on dialog-shots/hud--meet--petitioner-issue.png (800x600): 384x354 frame titled with the petition
// heading (back and close title-bar buttons), blue strip with a plate holding the round Dismiss button (Gonzo
// 42DEC7D2:02), the petitioner's name and portrait, and the message in black main9 in a sunken lavender box. Answer /
// impact buttons (no reference screenshot) sit at the bottom of the message box when the petition has them.
WindowResult PetitionerDialog::draw(Ui& ui, const WinEnv& E, const PetitionerView& v) {
  WindowResult R;
  Rect r = centered(ui, 384, 354);
  if (frame(ui, E, r, v.heading.empty() ? E.win(0x2DD, "Petitioner") : v.heading)) { R.close = true; R.add(Act::Close); }
  std::string t = v.tag.empty() ? "pet" : v.tag;
  if (sysButton(ui, E, t + ".back", 1, r.x + 328, r.y + 7)) { R.close = true; R.add(Act::Close); }
  auto answer = [&](bool accepted) {
    if (v.isDeal) R.add(accepted ? Act::AcceptDeal : Act::DeclineDeal, uint32_t(v.pendingIndex));
    else R.add(Act::AnswerPetition, v.petition, accepted);
    R.close = true;
  };
  blueBand(ui, {r.x + 4, r.y + 34, 8, 316});
  blueBand(ui, {r.x + 4, r.y + 310, 47, 40});
  ui.fill({r.x + 5, r.y + 311, 11, 38}, rgb(0x7B, 0x92, 0xFF));   // plate merges with the strip
  ui.text(r.x + 21, r.y + 41, v.name, "main9", kInk);
  Rect pp{r.x + 19, r.y + 58, 92, 115};
  if (v.portrait) ui.blitRect(*v.portrait, pp.x, pp.y, 0, 0, std::min(92, v.portrait->w), std::min(115, v.portrait->h));
  else portrait(ui, pp, nullptr, v.name.empty() ? "?" : v.name.substr(0, 1), rgb(0x9A, 0x84, 0x7A));
  sunkenLines(ui, pp);
  Rect mb{r.x + 121, r.y + 58, 258, 291};
  ui.fill(mb, kLav); sunkenLines(ui, mb);
  const bool hasAnswers = !v.button1.empty() || !v.button2.empty();
  const bool hasImpact = !v.advisorResponse.empty() || !v.councilResponse.empty();
  const int textH = mb.h - 8 - ((hasAnswers || hasImpact || impact) ? 40 : 0);
  if (!impact) {
    paragraph(ui, {r.x + 133, r.y + 62, 236, textH}, v.message, "main9", rgb(0, 0, 0));
    int bx = mb.x + 6, by = mb.y + mb.h - 38, bw = 80;
    if (hasAnswers) {
      if (!v.button1.empty() && button(ui, E, t + ".button1", {bx, by, bw, 33}, v.button1)) answer(true);
      if (!v.button2.empty() && button(ui, E, t + ".button2", {bx + bw + 4, by, bw, 33}, v.button2)) answer(false);
    } else if (hasImpact && button(ui, E, t + ".ok", {bx, by, bw, 33}, E.win(0x85, "OK"))) {
      if (v.isDeal) answer(false); else { R.add(Act::DismissPetitioner, v.petition); R.close = true; }
    }
    if (hasImpact && button(ui, E, t + ".impact", {mb.x + mb.w - bw - 6, by, bw, 33}, E.win(0x79, "Impact"))) impact = true;
  } else {
    int y = r.y + 62;
    ui.text(r.x + 133, y, E.win(0x80, "Advisor's response"), "main9", kInk);
    y += paragraph(ui, {r.x + 133, y + 16, 236, 100}, v.advisorResponse.empty() ? "-" : v.advisorResponse, "main9", rgb(0, 0, 0)) + 24;
    ui.text(r.x + 133, y, E.win(0x81, "City council's response"), "main9", kInk);
    paragraph(ui, {r.x + 133, y + 16, 236, 100}, v.councilResponse.empty() ? "-" : v.councilResponse, "main9", rgb(0, 0, 0));
    if (button(ui, E, t + ".return", {mb.x + 6, mb.y + mb.h - 38, 112, 33}, "<<")) impact = false;
  }
  // Round Dismiss button on the plate with its two-line label.
  {
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
