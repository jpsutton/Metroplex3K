#include "core/resdb.h"
#include "core/skin.h"
#include "skin_fixture.h"
#include "testutil.h"
using namespace m3k;
using namespace skin_group;

int main() {
  // ---- pure geometry
  {
    auto L = toolMenuLayout(480);
    CHECK(L.heightClass == 480 && L.artGroup == Ui640);
    CHECK(L.root[0].y == 3 && L.root[1].y == 39 && L.root[1].dy == 11 && L.root[8].y == 183 && L.root[8].dy == 83);
    CHECK(L.subY[0][0] == 21 && L.subY[0][5] == 201 && L.subY[6][0] == 57 && L.subY[8][0] == 93);
    CHECK(L.subIds[5].empty() && !L.root[5].hasSub);
    CHECK(L.pieces.size() == 1);
    auto M = toolMenuLayout(600);
    CHECK(M.heightClass == 600 && M.artGroup == UiBig);
    CHECK(M.root[5].y == 291 && M.root[6].dy == 118 && M.subY[6][0] == 129 && M.subY[7][0] == 128 && M.subY[7][1] == 164 && M.subY[7][2] == 201);
    CHECK(M.subY[8][5] == 309 && M.pieces.size() == 2 && M.pieces[1].inst == 0xFFFE && M.pieces[1].y == 417);
    auto B = toolMenuLayout(768);
    CHECK(B.heightClass == 1000 && B.root[0].y == 100 && B.root[0].dy == 97 && B.subY[0][0] == 118 && B.subY[7][0] == 225 && B.subY[7][6] == 442);
    CHECK(B.pieces.size() == 3);
    CHECK(toolMenuLayout(1080).pieces.size() == 4 && toolMenuLayout(1080).pieces[3].tileDown);
    CHECK(B.subIds[2].size() == 7 && B.subIds[2][2] == 0x4004 && B.subY[2][6] == 118 + 216);
    // Spacing in screen order is 36 (480).
    CHECK(L.root[1].y - L.root[0].y == 36 && L.root[8].y - L.root[4].y == 36 && L.root[5].y - L.root[8].y == 36);
  }
  {
    CHECK(bottomBarLayout(640).group == Ui640 && bottomBarLayout(800).group == Ui800 && bottomBarLayout(1920).group == Ui1024);
    auto b = bottomBarLayout(800);
    CHECK(b.fieldA.x == 8 && b.fieldA.w == 136 && b.fieldA.h == 18 && b.fieldD.x == 396 && b.fieldD.w == 104);
    CHECK(bottomBarLayout(640).fieldA.y == 46 && bottomBarLayout(1024).p3x == 773);
  }
  CHECK(cell4For(false, true, true, true) == 0 && cell4For(true, true, false, true) == 2 &&
        cell4For(true, false, true, false) == 2 && cell4For(true, false, false, true) == 3 && cell4For(true, false, false, false) == 1);
  CHECK(cell8For(true, false, true, false) == 5 && cell8For(true, false, false, true) == 3);

  // ---- RCI geometry
  {
    Rect r = RciGeometry::bar(0, 1.0);
    CHECK(r.x == 5 && r.w == 6 && r.y == 35 && r.h == 16);
    Rect c = RciGeometry::bar(1, -0.5);
    CHECK(c.x == 14 && c.y == 51 && c.h == 8);
    Rect i = RciGeometry::bar(2, 100);  // clipped to top
    CHECK(i.x == 23 && i.y == 0 && i.h == 51);
    Rect n = RciGeometry::bar(2, -100);  // clipped at 62
    CHECK(n.y == 51 && n.h == 11);
    CHECK(RciGeometry::bar(0, 0).h == 0);
  }

  // ---- load from synthetic art
  ResourceDb db; db.mountMemory(skinfix::build(), "skin");
  Skin s;
  CHECK(s.load(db, 600));
  CHECK(s.get("rootpanel") && s.get("rootpanel")->w == 96 && s.get("rootpanel")->h == 417);
  CHECK(s.get("bottombar") && s.get("bottombar")->w == 600 && s.get("bottombar")->h == 56);
  CHECK(s.get("bottombar.cap") && s.get("rci") && s.get("rci")->h == 88);
  CHECK(s.get("tool.2001") && s.get("tool.2001")->w == 144 && s.get("tool.2001")->h == 36);
  CHECK(s.get("tool.A006") != nullptr && s.get("root.9.hover") && s.get("root.9.press") && s.get("sub.1"));
  CHECK(s.get("nonexistent") == nullptr && s.get("tool.7777") == nullptr);
  CHECK(s.get("speed.0") && s.get("speed.5") && s.get("help") && s.get("button") && s.get("check") && s.get("radio"));
  CHECK(s.get("menu.bg") && s.get("menu.bg")->w == 800 && s.get("menu.btn.8"));
  CHECK(s.image(UiBig, 0xFFFF) == s.get("rootpanel"));
  CHECK(s.image(UiBig, 0x1234) == nullptr);
  // 480 uses the 640 group for strips and bg
  Skin s4; CHECK(s4.load(db, 480));
  CHECK(s4.get("rootpanel")->h == 334 && s4.get("bottombar")->w == 440 && s4.get("menu.bg")->w == 640);
  CHECK(s4.get("sub.1")->h == 120);  // 640 panel (100+20)
  CHECK(s.get("sub.1")->h == 140);

  // keyed magenta -> transparent
  const Image* tool = s.get("tool.2001");
  CHECK(tool->px[0] == 0 && tool->px[1] != 0 && (tool->px[36 * 0 + 36] == 0));

  // ---- cell slicing
  Image cell;
  CHECK(Skin::sliceCell(*tool, 4, 3, cell) && cell.w == 36 && cell.h == 36);
  CHECK(cell.px[36 * 5 + 5] == expand16(skinfix::cellCol(3), 7));
  CHECK(Skin::sliceCell(*tool, 4, 0, cell) && cell.px[36 * 5 + 5] == expand16(skinfix::cellCol(0), 7));
  CHECK(!Skin::sliceCell(*tool, 4, 4, cell) && !Skin::sliceCell(*tool, 5, 0, cell) && !Skin::sliceCell(*tool, 0, 0, cell));

  // ---- drawing
  const int W = 800, H = 600;
  std::vector<uint32_t> fb(size_t(W) * H, 0xFF000000u);
  Ui ui; ui.begin(&fb, W, H, Input());
  Rect col = s.columnRect(W, H);
  CHECK(col.x == W - 96 && col.y == 0);
  CHECK(s.drawRootPanel(ui, col.x, col.y, 2, 4));
  CHECK(fb[size_t(5) * W + col.x + 50] != 0xFF000000u);
  // pressed overlay of root 4 at (33,111)
  Rect r4 = s.rootButtonRect(4, col.x, col.y);
  CHECK(r4.x == col.x + 33 && r4.y == 111 && r4.w == 56 && r4.h == 32);
  CHECK(fb[size_t(r4.y + 5) * W + r4.x + 5] == expand16(skinfix::c565(255, 128, 0), 7));
  Rect r2 = s.rootButtonRect(2, col.x, col.y);
  CHECK(fb[size_t(r2.y + 5) * W + r2.x + 5] == expand16(skinfix::c565(255, 255, 0), 7));
  CHECK(s.drawSubPanel(ui, col.x, col.y, 2));
  Rect sp = s.subPanelRect(2, col.x, col.y);
  CHECK(sp.y == 11 && sp.h == 160 && s.subPanelRect(6, 0, 0).w == 0);
  Rect sb = s.subButtonRect(8, 1, col.x, col.y);
  CHECK(sb.x == col.x && sb.y == 164 && sb.w == 36);
  CHECK(!s.drawSubPanel(ui, 0, 0, 6));

  BottomBarPlace pl;
  CHECK(s.drawBottomBar(ui, &pl));
  CHECK(pl.bar.y == H - 56 && pl.bar.w == 600 && pl.fieldA.x == 8 && pl.fieldA.y == H - 56 + 26 && pl.textLine.w == 473);
  CHECK(fb[size_t(H - 10) * W + 700] == expand16(skinfix::c565(20, 30, 60), 7));  // end cap beyond bar
  CHECK(fb[size_t(H - 10) * W + 100] == expand16(skinfix::c565(30, 50, 90), 7));

  CHECK(s.drawRciIndicator(ui, 300, 100, 1.0, -0.5, 0));
  CHECK(fb[size_t(100 + 40) * W + 300 + 7] == kRciColors[0]);
  CHECK(fb[size_t(100 + 34) * W + 300 + 7] != kRciColors[0]);
  CHECK(fb[size_t(100 + 55) * W + 300 + 16] == kRciColors[1]);
  CHECK(fb[size_t(100 + 60) * W + 300 + 16] != kRciColors[1]);

  // image button: hover/press selects cell, click reports
  {
    Rect br{50, 50, 36, 36};
    Input in; in.mx = 60; in.my = 60;
    ui.begin(&fb, W, H, in);
    bool clk = true;
    CHECK(s.imageButtonCells(ui, tool, 4, br, "tip", false, true, clk) && !clk && ui.tooltip == "tip");
    CHECK(fb[size_t(55) * W + 55] == expand16(skinfix::cellCol(3), 7));
    in.down = true; ui.begin(&fb, W, H, in);
    s.imageButtonCells(ui, tool, 4, br, "tip", false, true, clk);
    CHECK(fb[size_t(55) * W + 55] == expand16(skinfix::cellCol(2), 7));
    in.down = false; in.released = true; ui.begin(&fb, W, H, in);
    s.imageButtonCells(ui, tool, 4, br, "tip", false, true, clk);
    CHECK(clk);
    in.released = false; in.mx = 5; ui.begin(&fb, W, H, in);
    s.imageButtonCells(ui, tool, 4, br, "tip", true, true, clk);
    CHECK(!clk && fb[size_t(55) * W + 55] == expand16(skinfix::cellCol(2), 7));
    s.imageButtonCells(ui, tool, 4, br, "tip", false, false, clk);
    CHECK(fb[size_t(55) * W + 55] == expand16(skinfix::cellCol(0), 7));
    CHECK(!s.imageButtonCells(ui, nullptr, 4, br, "", false, true, clk));
  }
  // 3-slice: 216x33 button -> 54 cell, third 18; stretch to 200 wide keeps edges
  {
    const Image* b = s.get("button");
    ui.begin(&fb, W, H, Input());
    CHECK(s.drawButton3(ui, b, 4, 1, {10, 200, 200, 33}));
    CHECK(fb[size_t(210) * W + 10] == expand16(skinfix::cellCol(1), 7));
    CHECK(fb[size_t(210) * W + 209] == expand16(skinfix::cellCol(1), 7));
    CHECK(!s.drawButton3(ui, b, 4, 4, {10, 200, 200, 33}));
    CHECK(s.drawCheck(ui, s.get("check"), 400, 400, true) == 13);
    CHECK(fb[size_t(405) * W + 405] == expand16(skinfix::cellCol(5), 7));
    CHECK(s.drawCheck(ui, nullptr, 0, 0, true) == 0);
  }

  // ---- degradation: empty db
  {
    ResourceDb e; Skin n;
    CHECK(!n.load(e, 600) && !n.loaded());
    ui.begin(&fb, W, H, Input());
    BottomBarPlace p2; bool c;
    CHECK(!n.drawRootPanel(ui, 0, 0) && !n.drawBottomBar(ui, &p2) && !n.drawRciIndicator(ui, 0, 0, 1, 1, 1) && !n.drawSubPanel(ui, 0, 0, 1));
    CHECK(!n.imageButtonCells(ui, n.get("tool.2001"), 4, {0, 0, 36, 36}, "", false, true, c) && n.menuBackground() == nullptr);
    CHECK(n.layout().valid);
  }
  TEST_MAIN_END();
}
