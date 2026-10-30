// Advisor window, petitioner dialog (spec 09 sections 6.5, 6.6).
#include <algorithm>
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

std::string AdvisorWindow::advisorName(const WinEnv& E, uint32_t id) {
  int i = advisorIndex(id);
  if (!i) return "Advisor";
  return E.win(0x155 + uint32_t(i), kAdvisorFb[i - 1]);
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

WindowResult AdvisorWindow::draw(Ui& ui, const WinEnv& E, AdvisorManager& am, const PetitionerManager& pm) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 680, 460);
  if (frame(ui, E, r, E.win(0x64, "Advisors"))) { R.close = true; R.add(Act::Close); }
  // Left column.
  int lx = r.x + 10, ly = r.y + 34;
  ui.text(lx, ly, E.win(0x64, "Advisors"), "title12");
  ly += lh + 6;
  for (int i = 0; i < 7; i++) {
    uint32_t id = kAdvisorOrder[i];
    Rect row{lx, ly + i * 42, 220, 40};
    bool sel = selected == id;
    ui.fill(row, sel ? rgb(0x90, 0xA8, 0xE0) : ui.panelCol);
    ui.frame(row, !sel);
    E.spot("advisor." + std::to_string(i + 1), row);
    int mood = am.mood(id);
    portrait(ui, {row.x + 2, row.y + 2, 36, 36}, E.img(0x52DB60A0, portraitInstance(i + 1, mood)), std::string(1, advisorName(E, id)[0]), rgb(0x7A, 0x84, 0xB8));
    ui.text(row.x + 44, row.y + 6, advisorName(E, id));
    size_t n = AdvisorWindow::topicsFor(am.list(id), 0).size() + AdvisorWindow::topicsFor(am.list(id), 1).size();
    ui.text(row.x + 44, row.y + 6 + lh, std::to_string(n) + " topics", "main10", rgb(0x60, 0x68, 0x90));
    if (ui.clicked(row)) { selected = id; tab = 0; topicSel = -1; scroll = 0; R.add(Act::SelectAdvisor, id); }
  }
  int py = ly + 7 * 42 + 8;
  ui.text(lx, py, E.win(0x63, "Petitioner issues"), "title12");
  py += lh + 4;
  int shown = 0;
  auto has = [&](uint32_t t, uint32_t i) { return E.has(t, i); };
  for (const PendingPetition* p : pm.presentable()) {
    if (py + lh > r.y + r.h - 10 || shown >= 4) break;
    uint32_t tbl = pm.config.tableFor(PetitionText::Heading, p->id, has);
    std::string h = tbl ? E.text(tbl, p->id, hexName("Petition ", p->id)) : hexName("Petition ", p->id);
    Rect row{lx, py, 220, lh + 2};
    E.spot("petition." + std::to_string(p->id), row);
    ui.fill(row, ui.hover(row) ? rgb(0xDC, 0xE4, 0xF0) : ui.panelCol);
    ui.text(row.x + 4, row.y + 1, h);
    if (ui.clicked(row)) R.add(Act::OpenPetitioner, p->id);
    py += lh + 4;
    shown++;
  }
  // Right: discussion page.
  Rect pg{r.x + 244, r.y + 34, r.w - 254, r.h - 44};
  if (selected == 0) {
    paragraph(ui, pg, E.win(0x75, "Select an advisor to discuss the state of your city."));
    return R;
  }
  ui.text(pg.x, pg.y, advisorName(E, selected), "title12");
  int ty = pg.y + lh + 8;
  const uint32_t tabIds[3] = {0x258, 0x1BF, 0x76};
  const char* tabFb[3] = {"News topics", "Briefings", "Ordinances"};
  for (int t = 0; t < 3; t++)
    if (button(ui, E, "tab." + std::to_string(t), {pg.x + t * 130, ty, 126, 22}, E.win(tabIds[t], tabFb[t]), true, tab == t)) { tab = t; topicSel = -1; scroll = 0; }
  Rect body{pg.x, ty + 30, pg.w, pg.y + pg.h - (ty + 30)};
  if (tab == 2) {
    if (ordinances) R.merge(ordWindow.draw(ui, E, *ordinances, money, selected, false, body));
    else ui.text(body.x, body.y, "-");
    return R;
  }
  TopicList& list = am.list(selected);
  auto topics = topicsFor(list, tab);
  int rowH = lh + 6, visible = 5;
  scrollBar(ui, E, {body.x, body.y, 220, visible * rowH}, int(topics.size()), visible, scroll);
  for (int i = 0; i < visible && scroll + i < int(topics.size()); i++) {
    uint32_t t = topics[size_t(scroll + i)];
    Rect row{body.x, body.y + i * rowH, 204, rowH};
    bool sel = topicSel == int(t);
    ui.fill(row, sel ? rgb(0x90, 0xA8, 0xE0) : rgb(255, 255, 255));
    E.spot("topic." + std::to_string(i), row);
    ui.text(row.x + 3, row.y + 3, E.text(kTblAdvisorHeading, t, hexName("Topic ", t)));
    if (ui.clicked(row)) topicSel = int(t);
  }
  if (topics.empty()) ui.text(body.x + 4, body.y + 4, "-");
  if (topicSel >= 0) {
    const TopicInstance* ti = list.find(uint32_t(topicSel));
    if (!ti) { topicSel = -1; return R; }
    int dx = body.x + 232, dy = body.y;
    uint32_t g = 0, inst = 0;
    int px = 0;
    if (am.gfx.get(ti->topicId, g, inst)) {
      const Image* im = E.img(g, inst);
      portrait(ui, {dx, dy, 118, 110}, im, "?", rgb(0x7A, 0x84, 0xB8));
      px = 126;
    }
    ui.text(dx + px, dy, E.text(kTblAdvisorHeading, ti->topicId, hexName("Topic ", ti->topicId)), "title12");
    std::string raw = E.text(kTblAdvisorMessage, ti->topicId, "(no text)");
    paragraph(ui, {dx + px, dy + lh + 6, pg.w - 232 - px, 130}, topicText(raw, E.tc, ti->args));
    if (button(ui, E, "dismiss", {dx, body.y + visible * rowH + 8 + 40, 120, 24}, E.win(0x2DE, "Dismiss"))) {
      uint32_t t = ti->topicId;
      list.dismiss(t);
      R.add(Act::DismissTopic, selected, t);
      topicSel = -1;
    }
  }
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

WindowResult PetitionerDialog::draw(Ui& ui, const WinEnv& E, const PetitionerView& v) {
  WindowResult R;
  int lh = lineHeight(ui);
  Rect r = centered(ui, 520, 330);
  if (frame(ui, E, r, v.isDeal ? v.heading : E.win(0x2DD, "Petitioner"))) { R.close = true; R.add(Act::Close); }
  std::string t = v.tag.empty() ? "pet" : v.tag;
  auto answer = [&](bool accepted) {
    if (v.isDeal) R.add(accepted ? Act::AcceptDeal : Act::DeclineDeal, uint32_t(v.pendingIndex));
    else R.add(Act::AnswerPetition, v.petition, accepted);
    R.close = true;
  };
  portrait(ui, {r.x + 14, r.y + 34, 90, 100}, v.portrait, v.name.empty() ? "?" : v.name.substr(0, 1), rgb(0x9A, 0x84, 0x7A));
  int tx = r.x + 118, tw = r.w - 132;
  if (!impact) {
    ui.text(tx, r.y + 34, v.name, "title12");
    int th = std::max(lh, ui.fonts->get("title12").height() + 2);
    ui.text(tx, r.y + 34 + th + 4, v.heading, "main10", rgb(0x60, 0x20, 0x20));
    paragraph(ui, {tx, r.y + 34 + th + lh + 10, tw, 150}, v.message);
    int by = r.y + r.h - 70;
    if (!v.button1.empty() || !v.button2.empty()) {
      if (!v.button1.empty() && button(ui, E, t + ".button1", {r.x + 14, by, 150, 26}, v.button1)) answer(true);
      if (!v.button2.empty() && button(ui, E, t + ".button2", {r.x + 174, by, 150, 26}, v.button2)) answer(false);
    } else if (button(ui, E, t + ".ok", {r.x + 14, by, 150, 26}, E.win(0x85, "OK"))) {
      if (v.isDeal) answer(false); else { R.add(Act::DismissPetitioner, v.petition); R.close = true; }
    }
    if (!v.advisorResponse.empty() || !v.councilResponse.empty())
      if (button(ui, E, t + ".impact", {r.x + 334, by, 170, 26}, E.win(0x79, "Impact analysis"))) impact = true;
  } else {
    int y = r.y + 34;
    ui.text(tx, y, E.win(0x80, "Advisor's response"), "title12");
    y += paragraph(ui, {tx, y + lh + 2, tw, 90}, v.advisorResponse.empty() ? "-" : v.advisorResponse) + lh + 10;
    ui.text(tx, y, E.win(0x81, "City council's response"), "title12");
    paragraph(ui, {tx, y + lh + 2, tw, 90}, v.councilResponse.empty() ? "-" : v.councilResponse);
    if (button(ui, E, t + ".return", {r.x + 14, r.y + r.h - 70, 200, 26}, E.win(0x82, "Return to petitioner"))) impact = false;
  }
  if (button(ui, E, t + ".dismiss", {r.x + 14, r.y + r.h - 36, 200, 24}, E.win(0x2DE, "Dismiss petitioner"))) {
    if (v.isDeal) R.add(Act::DeclineDeal, uint32_t(v.pendingIndex)); else R.add(Act::DismissPetitioner, v.petition);
    R.close = true;
  }
  return R;
}

}  // namespace m3k
