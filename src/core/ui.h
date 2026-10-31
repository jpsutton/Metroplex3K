// Immediate-mode UI toolkit drawing into an RGBA framebuffer. Platform independent: the host feeds
// mouse/keyboard state each frame. Used for all menus, dialogs and the in-city HUD.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "core/font.h"
#include "core/image.h"

namespace m3k {

class Skin;

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

inline uint32_t rgb(int r, int g, int b) { return rgba(uint8_t(r), uint8_t(g), uint8_t(b)); }

struct KeyEvent { int vk = 0; int mods = 0; };  // Windows virtual-key code, HotkeyMod mask (no auto-repeat)

struct Input {
  int mx = 0, my = 0;
  bool down = false;       // left button currently held
  bool pressed = false;    // went down this frame
  bool released = false;   // went up this frame
  bool rightPressed = false;
  bool rightDown = false;  // right button currently held
  int wheel = 0;
  std::string text;        // typed characters this frame
  bool key[16] = {};
  bool held[4] = {};
  std::vector<KeyEvent> keyDowns;   // key-down events this frame       // arrows held: left, right, up, down       // see Key enum
};
enum Key { K_ENTER, K_ESC, K_BACKSPACE, K_UP, K_DOWN, K_PAGEUP, K_PAGEDOWN, K_TAB, K_LEFT, K_RIGHT };

class Ui {
 public:
  std::vector<uint32_t>* fb = nullptr;
  int W = 0, H = 0;
  const Fonts* fonts = nullptr;
  Input in;
  uint32_t textCol = rgb(0x31, 0x36, 0x7C);
  uint32_t panelCol = rgb(0xC8, 0xD0, 0xDC);
  uint32_t panelDark = rgb(0x6C, 0x74, 0x90);
  uint32_t panelLight = rgb(0xF0, 0xF4, 0xFA);
  const Skin* skin = nullptr;   // original UI art for dialogs and controls; null = flat fallback look
  std::string tooltip;
  int focusId = 0;       // text box with keyboard focus
  int activeId = 0;      // widget being dragged

  void begin(std::vector<uint32_t>* f, int w, int h, const Input& i);
  // Frames of the windows drawn this frame (windows register theirs through wui::centered), so the host can tell
  // whether the pointer is over a window instead of guessing sizes.
  mutable std::vector<Rect> windowRects;
  int nextId() { return ++idCounter_; }

  // Movable dialogs: returns `home` shifted by the distance the user dragged the window by its title bar (the top
  // `barH` pixels, 0 = the skin's title-bar height, minus the close box at the right end). Windows drawn later are on
  // top: a press only starts a drag when no later window covers the point. The offset lasts while the window is drawn
  // every frame, so a closed window reopens at its home position. `key` identifies the window (0 = its size).
  Rect movable(Rect home, uint64_t key = 0, int barH = 0);
  static uint64_t windowKey(const char* name);   // explicit key for windows whose size changes while open
  bool draggingWindow() const { return dragKey_ != 0; }

  void fill(Rect r, uint32_t c);
  void fillAlpha(Rect r, uint32_t c, float a);
  void frame(Rect r, bool raised = true);
  void panel(Rect r, const std::string& title = "");
  void text(int x, int y, const std::string& s, const char* font = "main10", uint32_t col = 0);
  void textCentered(Rect r, const std::string& s, const char* font = "main10", uint32_t col = 0);
  int textWidth(const std::string& s, const char* font = "main10") const;
  void blit(const Image& im, int x, int y, int sx = 0, int sw = -1);  // optionally a horizontal slice
  void blitRect(const Image& im, int dx, int dy, int sx, int sy, int sw, int sh);   // source sub-rectangle
  bool button(Rect r, const std::string& label, bool enabled = true, bool toggled = false, const char* font = "main10");
  // Button width that fits `label` (text plus side padding), at least `minW`.
  int buttonWidth(const std::string& label, int minW = 0) const { return std::max(minW, textWidth(label) + 28); }
  bool imageButton(Rect r, const Image& strip, int cells, const std::string& tip, bool toggled = false);
  bool checkbox(Rect r, const std::string& label, bool& v);
  bool slider(Rect r, int& v, int lo, int hi);
  // Returns true if selection changed. `scroll` is the first visible row.
  bool list(Rect r, const std::vector<std::string>& rows, int& sel, int& scroll);
  bool textBox(Rect r, std::string& s, size_t maxLen = 31);
  bool hover(Rect r) const { return r.contains(in.mx, in.my); }
  bool clicked(Rect r) const { return in.released && r.contains(in.mx, in.my); }

 private:
  int idCounter_ = 0;
  struct WinOffset { int dx = 0, dy = 0; bool seen = false; };
  std::map<uint64_t, WinOffset> winOffsets_;
  uint64_t dragKey_ = 0, pressKey_ = 0;   // window being dragged; window whose title bar got this frame's press
  int dragMx_ = 0, dragMy_ = 0, dragDx_ = 0, dragDy_ = 0;   // press position and the offset at the press
};

}  // namespace m3k
