// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Original UI art ("skin"): loads UI images (type 62B9DA24) from the mounted resource segments and
// exposes the layouts and draw helpers needed to render the authentic chrome (spec/08 sections 2, 4,
// 7; spec/09 sections 1.2, 2.5, 3.1, 4). Every draw helper returns false (and draws nothing) when
// the needed art is missing, so callers can fall back to the toolkit drawing.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/gamedata.h"
#include "core/image.h"
#include "core/ui.h"

namespace m3k {

// Image groups (spec/08 2.1).
namespace skin_group {
constexpr uint32_t Sys = 0x82B9B75B, Main = 0x82B9B75C, Dlg = 0xC2BF3646;
constexpr uint32_t Ui640 = 0x00640480, Ui800 = 0x00800600, Ui1024 = 0x01024768, UiBig = 0x02F78C6D;
constexpr uint32_t Gonzo = 0x42DEC7D2, Other = 0x22DEC92D;
}  // namespace skin_group

// Cell index of a 4-cell strip (disabled, normal, pressed/on, highlighted); spec/08 3.2.
enum Cell4 { CELL_DISABLED = 0, CELL_NORMAL = 1, CELL_PRESSED = 2, CELL_HOVER = 3 };
int cell4For(bool enabled, bool pressed, bool checked, bool hover);
// 8-cell check box / radio strip, measured on the art: cell = 2*state + (checked ? 0 : 1), state 0 normal,
// 1 hover, 2 pressed, 3 disabled.
int cell8For(bool enabled, bool pressed, bool checked, bool hover);

constexpr int kNavPanelH = 164;       // navigator panel height (bottom-right), see game_nav.cpp

// Tool menu layout (spec/08 section 7), chosen by screen height.
struct ToolMenuLayout {
  struct Root { int id = 0, x = 0, y = 0, dx = 0, dy = 0; bool hasSub = false; };
  struct Piece { uint32_t group = 0, inst = 0; int y = 0; bool tileDown = false; int until = 0; };  // until: last tiled y (0 = window bottom)
  bool valid = false;
  int heightClass = 480;                // 480, 600 or 1000 (">600")
  int width = 96;                       // column width
  uint32_t artGroup = 0;                // group of sub-menu strips and panels (00640480 or 02F78C6D)
  std::array<Root, 9> root;             // index 0..8 = root button N-1 (ids 0x1001..0x1009)
  std::array<std::vector<uint32_t>, 9> subIds;  // button ids top to bottom (0x2001.. etc); empty for Meet Advisors
  std::array<std::vector<int>, 9> subY;         // y of each sub-menu button (x is 0), parallel to subIds
  std::vector<Piece> pieces;            // column background pieces, in draw order
};
// Pure geometry lookup; no art needed.
ToolMenuLayout toolMenuLayout(int screenH);

// Bottom bar layout (spec/08 4.2); rects relative to the bar image's top-left.
struct BottomBarLayout {
  uint32_t group = 0;                   // 00640480 / 00800600 / 01024768
  int variant = 2;                      // 0 = 640, 1 = 800, 2 = >=1024
  Rect fieldA, fieldB, fieldC, fieldD;  // city name, population, funds, date
  Rect textLine;                        // news ticker line
  Rect tickerStrip;                     // the ticker strip's fill as the bar art has it (top row + black rows; one more column left of x)
  int p1x = 0, p1y = 0, p2x = 0, p2y = 0, p3x = 0, p3y = 0, p4x = 0, p4y = 0;
};
BottomBarLayout bottomBarLayout(int screenW);

// Absolute placement (window coordinates) of a drawn bottom bar.
struct BottomBarPlace {
  Rect bar;                             // background image rect
  Rect fieldA, fieldB, fieldC, fieldD, textLine, tickerStrip;
  int p1x = 0, p1y = 0, p2x = 0, p2y = 0, p3x = 0, p3y = 0, p4x = 0, p4y = 0;
  int rightEnd = 0;                     // x where the bar art ends (window width)
};

// RCI indicator geometry (spec/09 4.5). Bars are R, C, I.
struct RciGeometry {
  static constexpr int bottomClip = 8;   // rows of the art below the screen edge (800 x 600)
  static constexpr int imgW = 41, imgH = 88, zeroY = 51, clipBottom = 62, unitPx = 16;
  // The art has two 16-px slots per bar: positive demand fills upward from the zero line inside y 35..50, negative
  // fills downward inside y 62..77, with the R/C/I label row between (y 51..61) [measured on the art].
  static constexpr int slotTop = 35, lowerTop = 62, lowerBottom = 78, labelTop = 51, labelH = 11;
  static constexpr int barX[3] = {5, 14, 23};
  static constexpr int barW = 5;
  // Rect relative to the indicator image for bar i (0..2) at demand value v (demand units, 1.0 = 16 px).
  // Positive grows up from the zero line, negative down; clipped to y in [0, clipBottom). h==0 -> empty.
  static Rect bar(int i, double v);
};
extern const uint32_t kRciColors[3];  // RGBA of R, C, I bars

class Skin {
 public:
  // Look up art for the given window size. Returns true if anything of the in-game or front-end skin
  // was found. `screenW` 0 = infer from height (480 -> 640, 600 -> 800, else 1024).
  bool load(ResourceDb& db, int screenH, int screenW = 0);
  bool load(GameData& gd, int screenH, int screenW = 0) { return load(gd.db(), screenH, screenW); }
  bool loaded() const { return loaded_; }
  int screenW() const { return w_; }
  int screenH() const { return h_; }

  // Named image or nullptr. Names: "rootpanel", "bottombar", "bottombar.cap", "rci", "speed.0".."speed.5",
  // "help", "engine" (Metroplex3K options: help chrome with a cog), "root.N.hover"/"root.N.press" (N=1..9),
  // "sub.N" (sub-menu panel), "tool.XXXX" (hex id, 4 digits, 4-cell strip), "button" (SYS GZBtn), "check",
  // "radio", "combo", "gonzo.NN" (hex instance), "menu.bg", "menu.btn.K" (K=0..8), "sys.XXXXXXXX",
  // "main.XXXXXXXX", "dlg.XXXXXXXX".
  const Image* get(const std::string& name) const;
  // Direct (group, instance) lookup; decodes lazily and caches. nullptr if missing or undecodable.
  const Image* image(uint32_t group, uint32_t inst) const { return fetch(group, inst); }

  const ToolMenuLayout& layout() const { return layout_; }
  const BottomBarLayout& bottomLayout() const { return bar_; }

  // ---- tool menu ----
  // Column top-left for a window: right-aligned, y = 0.
  Rect columnRect(int winW, int winH) const;
  Rect rootButtonRect(int n, int originX, int originY) const;        // n = 1..9, 56x32 hot rect
  Rect subButtonRect(int n, int k, int originX, int originY) const;  // 36x36
  Rect subPanelRect(int n, int originX, int originY) const;          // empty rect if no sub-menu panel
  const Image* toolStrip(uint32_t id) const { return image(layout_.artGroup, id); }
  // Draw the column background and the hover/pressed overlays (hover/selected = root N or 0).
  bool drawRootPanel(Ui& ui, int originX, int originY, int hover = 0, int selected = 0);
  // Draw the sub-menu panel for root n (1..9).
  bool drawSubPanel(Ui& ui, int originX, int originY, int n);

  // ---- bottom bar ----
  // Anchored bottom-left; art beyond the background is filled with the end cap. Fills `out`.
  bool drawBottomBar(Ui& ui, BottomBarPlace* out = nullptr);
  // Same geometry without drawing (needs the background image for height).
  bool bottomBarPlace(BottomBarPlace& out) const;

  // ---- RCI indicator ----
  // Draws background at (x,y) then the three bars. Values in demand units (e.g. valve value / scale).
  bool drawRciIndicator(Ui& ui, int x, int y, double r, double c, double i);

  // ---- generic controls ----
  // Image button from an n-cell strip, centred in r (or exact when sizes match). Handles hover/press/
  // toggle via ui.in, sets ui.tooltip, and reports click. Returns false if strip null (nothing drawn).
  bool imageButtonCells(Ui& ui, const Image* strip, int cells, Rect r, const std::string& tip, bool toggled,
                        bool enabled, bool& clicked, int tipPlace = 0) const;   // tipPlace: Ui::kTip* (tooltip anchored to the button)
  // Draw one cell of a strip 1:1 at (x,y).
  static bool drawCell(Ui& ui, const Image* strip, int cells, int cell, int x, int y);
  // Copy one cell out of a strip. false on bad args.
  static bool sliceCell(const Image& strip, int cells, int cell, Image& out);
  // 3-slice stretchable button: cell split in thirds, middle stretched. Caption centred if non-empty.
  bool drawButton3(Ui& ui, const Image* strip, int cells, int cell, Rect r, const std::string& label = "",
                   uint32_t textCol = 0, const char* font = "main10") const;
  // Whole stretchable button with hover/press handling; returns false if no art, else sets clicked.
  bool button3(Ui& ui, const Image* strip, Rect r, const std::string& label, bool enabled, bool toggled,
               bool& clicked, uint32_t textCol = 0, const char* font = "main10") const;
  // Check box / radio (8-cell strip) at (x,y); returns cell width (0 if art missing).
  int drawCheck(Ui& ui, const Image* strip, int x, int y, bool checked, bool enabled = true, bool hover = false,
                bool pressed = false) const;

  // ---- dialogs ----
  // Original dialog frame: cloud-texture body, white top/left and black bottom/right edge lines, blue title bar with
  // the title in it (measured on the original). Returns false when the art is missing. The close box is drawn by
  // dialogCloseBox().
  bool drawDialogFrame(Ui& ui, Rect r, const std::string& title) const;
  // Close box (system-button sheet, row 0) at the title bar's right end; returns its hit rect via `hit`.
  bool dialogCloseBox(Ui& ui, Rect dialog, Rect& hit, bool& clicked) const;

  // ---- front end ----
  // Main menu background chosen for the window size (800x600 set when it fits and exists, else 640x480).
  const Image* menuBackground() const { return get("menu.bg"); }
  const Image* menuButton(int k) const { return get("menu.btn." + std::to_string(k)); }

  // Number of images found at load (diagnostics).
  size_t imageCount() const { return names_.size(); }

 private:
  mutable Image buttonRaw_;   // the GZBtn strip before keyButtonPlate (see skinBlitScaled)
  void bind(const std::string& name, uint32_t group, uint32_t inst);
  void buildShowButton();
  void buildCogButton();      // synthesise "cog" from the exchange-button strip (slimmer cog in its frame)
  void buildEngineButton();   // synthesise "engine" from the help strip (cog instead of "?")
  bool havePanel() const { return get("rootpanel") != nullptr; }
  ResourceDb* db_ = nullptr;
  bool loaded_ = false;
  int w_ = 0, h_ = 0;
  ToolMenuLayout layout_;
  BottomBarLayout bar_;
  std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> names_;
  mutable std::unordered_map<uint64_t, Image> cache_;
  mutable std::unordered_map<uint64_t, bool> missing_;
  std::unordered_map<std::string, Image> extras_;   // synthesised art (not from the resource DB)
  const Image* fetch(uint32_t group, uint32_t inst) const;
};

// Stretch-blit a source rect to a destination rect (nearest), skipping transparent pixels.
// `raw`: optional unkeyed twin of `im` (same size); where a partial-alpha pixel lands on exactly the baked plate colour the original's pixel is drawn as is.
void skinBlitScaled(Ui& ui, const Image& im, Rect src, Rect dst, const Image* raw = nullptr);

}  // namespace m3k
