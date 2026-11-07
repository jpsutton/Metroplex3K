# 08 — UI framework, layout data, input, cursors, hotkeys

Status: first pass. Sections are filled in order of implementation priority; unfinished items are
tagged `[OPEN]`. Per-screen behaviour (what each panel/dialog does) is in `09-ui-screens.md`.
Image payload decoding is in `04-graphics-formats.md`; string tables and fonts in
`05-text-and-fonts.md`; the IXF container in `02-containers.md`.

## 1. Summary of where UI "layout" lives

1. **There is no window-layout resource format.** `[DATA]` Every resource in every
   `Res/UI/Shared/*.ixf`, `Res/UI/Shared/DLG.IXF`, `MAIN.IXF` and `SYS.IXF` has Type `0x62B9DA24`
   (a 2-D image). No resource of any other type exists in these files (checked: 14/14 IXF files in `Res/UI/Shared/`,
   711/711 entries). The Windows executables carry no dialog/RCDATA resources either `[STATIC]`.
   The values `0x0000057E` and `0x00006400` seen among the resource keys of other files are
   **Group** IDs (not Type IDs, see `03-resource-types.md` §2) of occupant attribute files and
   sprite/path attribute files (`13-buildings-occupants.md`, `26-automata.md`); they are not UI
   resources.
2. Window trees, positions, sizes and which image each control uses are **built by program
   logic**, not read from data. This document specifies the generic control behaviours and the
   layout of the in-game frame (bottom bar, section 4; tool menu, section 7) as tables. The
   contents and behaviour of each dialog are in `09-ui-screens.md`; most dialog geometry is still
   `[OPEN]` there.
3. The data-driven parts of the UI are:
   * the UI image resources (Type `0x62B9DA24`), addressed by Group = "image set" and
     Instance = image ID (section 2);
   * `Sys/MenuItem.INI` (packed in `Sys/SYS.PAK`), which defines the main tool-menu button tree and
     the command each button issues (section 6);
   * `Res/Hotkeys/<LANGUAGE>.IXF`, the keyboard bindings (section 5);
   * `Res/UI/Shared/Cursors/*.cur`, standard Windows cursor files loaded by file name (section 8);
   * string tables (labels, tooltips) — `05-text-and-fonts.md`.

## 2. UI image resources

### 2.1 Key scheme `[DATA]`

All UI images use resource key **Type `0x62B9DA24`**, Group = image-set ID, Instance = image ID.
Each UI IXF file contains exactly one group. Files whose name starts with eight hex digits are
named after that group; `SYS.IXF`, `MAIN.IXF` and `DLG.IXF` are not, and their groups are given in
the table.

| File (under `Res/UI/Shared/`) | Group | Entries | Contents |
|---|---|---|---|
| `SYS.IXF` | `0x82B9B75B` | 84 | Generic framework control art (buttons, check boxes, radio buttons, scroll bars, sliders, spinners, combo arrow, message-box icons, window frame pieces) |
| `MAIN.IXF` | `0x82B9B75C` | 27 | Full-screen backgrounds (640×480 set `0x22721000..0x22721006`, 800×600 `0x01140000`, `0x22729921`) and large panels |
| `DLG.IXF` | `0xC2BF3646` | 11 | Dialog backgrounds and pieces (e.g. 464×350 dialog bodies `0x2302ADD8`, `0xC2DECCDE`) |
| `00640480_MainUI.ixf` | `0x00640480` | 64 | In-game UI at 640×480: menu button strips, sub-menu panels, bottom bar |
| `00800600_MainUI.ixf` | `0x00800600` | 1 | Bottom bar for 800×600 (`0x82790741`, 600×56) |
| `01024768_MainUI.ixf` | `0x01024768` | 4 | Bottom bar for ≥1024 wide (`0x82790741`, 824×64) and 3 extra panels `0xFFFC..0xFFFE` |
| `02F78C6D_MainUI.ixf` | `0x02F78C6D` | 112 | In-game UI for resolutions above 640×480 (menu buttons, panels, small widgets) |
| `42DEC7D2_GonzoCtrls.ixf` | `0x42DEC7D2` | 28 | Custom ("Gonzo") control art |
| `22DEC92D_OtherCtrls.ixf` | `0x22DEC92D` | 7 | Small control pieces |
| `22DEC90F_TerrEdit.ixf` | `0x22DEC90F` | 32 | Terrain-editor panel art |
| `000005E5_Maps.ixf` | `0x000005E5` | 44 | Map/data-view widgets |
| `52DB60A0_Advisors.ixf` | `0x52DB60A0` | 201 | Advisor and petitioner portraits |
| `831D112B_AdvTopics.ixf` | `0x831D112B` | 83 | Advisor topic pictures |
| `85535958_CitySchemeUI.ixf` | `0x85535958` | 13 | City-scheme picker images |

The Building Architect Tool keeps loose BMP equivalents under `Res/BA/UI/SHARED/`. Its `SYS/`
folder names files `<instance-hex><Name>.BMP`, which documents the role of several `SYS.IXF`
images (instance → role): `0x0000000A` GZRadioBtn, `0x02B99F6F` GZSliderV, `0x42E55FD4` MBIcons
(message-box icons), `0x62B19CE9` SysBtns (window title-bar buttons), `0x62B99D31`
GZComboDownArrow, `0x82B99D9D` GZSpinner, `0x82D6F678` GZBtnQuestion, `0xA2DEFD9A` GZBtnDone,
`0xC2B99D52` ListBoxToggle, `0xC2B99E09` GZSliderH, `0xC2D6E93A` GZCheckBox, `0xE2B14588` WinGen
(generic window frame), `0xE2B66DB8` GZBtn, `0xE2B99E01` GZScrollbar, `0xE2B99F01`
GZScrollbarOutlined. `[DATA]` These names are used below only as labels for the images; they
carry no meaning for an implementation.

### 2.2 Payload header (UI images) `[DATA]`

Verified for all 711 UI images in the 14 files: header fields, RefPack stream ending exactly at the
resource end, and decompressed length = width × height × bytes-per-pixel (710 × format 7, 1 × format
3). This is the general image header of `04-graphics-formats.md` §6.2, which owns the full pixel
format; the table repeats it for the UI case.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4 | u32 | kind | Encoding kind as defined in `04-graphics-formats.md` §6.2 (1 = raw, 2 = RefPack-compressed). Every UI image has kind 2 |
| 0x04 | 4 | u32 | dataLen | Number of bytes from offset 0x14 to the end of the resource (= resource size − 20) |
| 0x08 | 4 | u32 | width | Pixels |
| 0x0C | 4 | u32 | height | Pixels |
| 0x10 | 4 | u32 | format | 7 = 16-bit RGB565 (decompressed length = width×height×2); 3 = 8-bit (one DLG image) |
| 0x14 | 4 | u32 | compLen | Kind-2 length word of `04` §6.2; in every UI image it equals dataLen |
| 0x18 | dataLen − 4 | bytes | data | RefPack stream (`02-containers.md`); decompresses to width × height × 2 bytes (format 7) or width × height bytes (format 3), rows top to bottom |

In format-7 UI images the colour `0xF81F` (pure magenta) is the transparent colour key. `[INFERRED]`
(visible as the background of every control strip; confirm in `04`).

### 2.3 Control image strips `[DATA]` / `[STATIC]`

Stateful control images are **horizontal strips of equally sized cells**; the cell width is
`imageWidth / cellCount`, the cell height is the full image height (unless noted).

| Control art | Size | Cells | Cell size |
|---|---|---|---|
| Tool-menu buttons (`0x2001..0xA006` in `0x00640480`/`0x02F78C6D`) | 144×36 | 4 | 36×36 |
| GZBtn (`SYS 0xE2B66DB8`) | 216×33 | 4 | 54×33 |
| GZBtnDone family (`0xA2DEFD9A`, `0xA2DEFD9B`, `0xA2DEFE93`, `0xA2DEFE94`, …) | 168×32 | 4 | 42×32 |
| GZComboDownArrow | 64×16 | 4 | 16×16 |
| SysBtns | 96×100 | 4 columns × 4 rows | 24×25 (row = button kind, column = state) |
| GZRadioBtn | 96×12 | 8 | 12×12 (2 check values × 4 states) |
| GZCheckBox | 104×13 | 8 | 13×13 (2 check values × 4 states) |
| GZScrollbar | 192×16 | 12 | 16×16 |
| GZSliderH / GZSliderV | 80×20 / 20×80 | 1 | 60 px track + 20×20 thumb (see below) |
| GZSpinner | 48×18 | 4 | 12×18; each cell holds an up arrow (top 9 rows) over a down arrow (bottom 9 rows) |
| ListBoxToggle (`0xC2B99D52`) | 44×22 | 2 | 22×22 (checked, unchecked) |
| GZScrollbarOutlined (`0xE2B99F01`) | 216×18 | 12 | 18×18, same cell order as GZScrollbar, magenta = transparent |
| Field texture (`0x42B99D48`) | 136×22 | 1 | tiled background of text-entry/list fields `[INFERRED]` |
| `0xE2B99DF9` | 8×6 | 1 | small marker (grey with transparent magenta) `[OPEN]` use |

State order within a 4-cell strip (left → right): see section 3.2.

**Scroll-bar strip cell order** (`0xE2B99E01`, 16×16 cells; `0xE2B99F01` 18×18), 0-based from the
left `[DATA]` (by inspection of the art): 0 left arrow, 1 left arrow pressed, 2 horizontal thumb,
3 horizontal track, 4 right arrow, 5 right arrow pressed, 6 up arrow, 7 up arrow pressed,
8 vertical thumb, 9 vertical track, 10 down arrow, 11 down arrow pressed. Thumb and track cells
are stretched/tiled along the bar's long axis `[INFERRED]`. **Slider art** (`0xC2B99E09` /
`0x02B99F6F`): the first 60 px along the long axis are the track (stretched to the control
length), the last 20 px are the 20×20 thumb `[DATA]`.

### 2.3a "Gonzo" custom control art (`42DEC7D2_GonzoCtrls.ixf`) `[DATA]`

The game's own custom controls ("Gonzo" is only the name of this art set) take their art from group
`0x42DEC7D2`. Users `[STATIC]`: the budget pages, the petitioner dialog, the main in-game panel, the
data-map window, the network tools, and the Building Architect Tool. Every image except the sliders
is a 4-cell horizontal strip in the standard order (disabled, normal, pressed, highlighted;
section 3.2). "Keyed" strips use magenta (255,0,255) as transparent. Roles are identified from the
pictures.

| Instance | Size | Cell | Picture / role |
|---|---|---|---|
| `0x01`, `0x15` | 216×31, 216×33 | 54×31, 54×33 | blank rounded text button (caption drawn on top) |
| `0x02` | 104×25 | 26×25 | round radio-style button |
| `0x03` | 144×35 | 36×35 | pan / move (cross arrows) |
| `0x04` | 104×29 | 26×29 | plus (zoom in), keyed |
| `0x0A` | 104×29 | 26×29 | minus (zoom out), keyed |
| `0x05` | 176×42 | 44×42 | map button |
| `0x06`, `0x07`, `0x19` | 144×24 | 36×24 | "?" advisor/help balloons (orange, red, neutral) |
| `0x08` | 144×24 | 36×24 | ordinance-style chequered icon |
| `0x09` | 144×24 | 36×24 | open book: "details" button of budget rows |
| `0x0B`, `0x0C` | 48×18 | 12×18 | spinner arrows (same layout as GZSpinner) |
| `0x0D` | 104×25 | 26×25 | "no" / cancel sign |
| `0x0E` | 144×24 | — | horizontal slider: 72 px track, then three 24×24 thumbs (normal, highlighted, pressed/disabled) `[INFERRED]` order |
| `0x0F` | 24×144 | — | vertical slider, same layout rotated |
| `0x10` | 152×33 | 38×33 | money/document |
| `0x11`, `0x12`, `0x18` | 104×29 | 26×29 | curved arrows (rotate clockwise / counter-clockwise; back), keyed |
| `0x13`, `0x16`, `0x1A` | 144×24 | 36×24 | money (budget) |
| `0x14`, `0x17`, `0x1B` | 144×24 | 36×24 | handshake (deals) |
| `0x1C` | 72×15 | 18×15 | small "%" button |

### 2.4 Default control art, fonts and colours `[STATIC]`

At start-up the application registers a "skin": the default images, fonts and colours that the
framework controls use when the screen logic does not supply its own. All default images come from
`SYS.IXF` (group `0x82B9B75B`, type `0x62B9DA24`). Images marked "keyed" are drawn with magenta
(255,0,255) as the transparent colour.

| Control | Default image instance | Art |
|---|---|---|
| Framed window (frame pieces) | `0xE2B14588` | WinGen |
| Title-bar buttons of framed windows | `0x62B19CE9` | SysBtns |
| Push button (stretchable push, section 3.2) | `0xE2B66DB8` | GZBtn |
| Check box (section 3.2) | `0xC2D6E93A` | GZCheckBox |
| Radio button (section 3.2) | `0x0000000A` | GZRadioBtn (keyed) |
| Horizontal slider | `0xC2B99E09` | GZSliderH |
| Vertical slider | `0x02B99F6F` | GZSliderV |
| Scroll bar | `0xE2B99E01` | GZScrollbar |
| Spinner | `0x82B99D9D` | GZSpinner (keyed) |
| Combo-box drop button | `0x62B99D31` | GZComboDownArrow |
| Text-entry / list background | `0x42B99D48` | field texture `[INFERRED]` role |
| `[OPEN]` unknown user | `0xE2B99DF9` | small 8×6 marker |

**Default UI font.** The framework's default text font is `title12.fbf`. For the East-Asian and
Thai languages it is replaced by a Windows bitmap font: Japanese `font16j.fon`, Simplified Chinese
`font16gh.fon`, Traditional Chinese `font16b5.fon`, Thai `font20th.fon`, Korean `font16k.fon`
(language IDs and the full font table: `05-text-and-fonts.md` §5). Button captions use a different
default (section 3.6).

**Colours** (R,G,B) registered at start-up. Roles known:

| Role | RGB |
|---|---|
| Transparent colour key of keyed art | (255,0,255) |
| Default text colour of fonts (`05-text-and-fonts.md`) | (49,54,124) |
| Combo-box field: text colour, highlight colour | (127,136,255), (255,242,255) |
| Graph/progress area (section 3.1; identity `[INFERRED]`): its three colours, in order | (127,136,255), (255,242,255), (218,221,255) |

The program registers these further colours, whose users are `[OPEN]`: (255,255,255),
(26,32,107), (49,54,124), (160,160,160), (218,221,255), (124,144,255), (168,180,255),
(125,129,181), (108,113,185), (179,182,224), (125,129,189). `[OPEN]` which controls draw with
them. Until this is known, an implementation may use them for panel frames and labels.

## 3. Controls (window classes) and generic behaviour

### 3.1 Window model `[STATIC]`

* Every UI element is a rectangular window with a parent, an ordered child list (later children
  draw on top and receive mouse input first), a 32-bit window ID, a position relative to the
  parent's top-left, a size, visibility and enabled flags. A hidden window and its children are
  neither drawn nor hit-tested. A disabled window draws its "disabled" image cell and ignores mouse
  and keyboard input.
* Controls notify a designated notification target (normally their parent dialog/panel) with
  (sender, notification kind, value). Each control carries a 32-bit window ID chosen by the
  program, and screen logic identifies the sender by it. These IDs are internal to the program
  (no data file contains them), so an implementation may choose its own.
* The framework provides these control types `[STATIC]`:

| Control type | Behaviour |
|---|---|
| Button | Push, toggle, check box and radio button are one control type (section 3.2) |
| Keyboard accelerator table | Hotkeys (section 5) |
| Container window | Panels and dialog bodies; the message box (section 9.1) is built from it |
| List box | The only control that uses double-click timing (section 3.4); the building picker uses it |
| Combo box | A closed field that shows the current item in a single-line text field drawn with the combo-box colours of section 2.4, a drop button using the combo drop-arrow image, and a drop-down list that is an embedded list box |
| Single-line text field | Draws one line in one font with two colours (text, selection/highlight). It is editable when enabled for input (keys in section 3.4a) and otherwise serves as a text read-out. Used as the combo-box field and in the query, neighbour and new-city screens |
| Multi-line text box / editor | Default capacity 511 characters; caret movement with arrows, Home/End, Page Up/Down, Insert (overwrite toggle `[INFERRED]`), Delete, Backspace. Used by the New City dialog, the petitioner and scenario texts |
| Scroll bar | Default art: GZScrollbar |
| Spinner (up/down arrows) | Default art: GZSpinner |
| Keyboard-navigable list/table | Up, Down, Page Up, Page Down, Enter; used only by the charts window |

Further control types exist whose drawing rules and notifications are not yet specified `[OPEN]`.
Their likely identities, judged from the screens that use them, are `[INFERRED]`: a slider (horizontal and vertical; default art
GZSliderH / GZSliderV); a framed window with a title bar and title-bar buttons (WinGen / SysBtns,
section 9.3); an image/bitmap window used by the map, data-map and neighbour screens and by the
Building Architect Tool; a graph/progress area used by the graphs, data-map and preferences screens
(three colours, section 2.4); and a decoration control used by the budget, system-information and
scenario dialogs. `[OPEN]` how each of these draws itself and which notifications it
sends. Until answered, an implementation should give the slider the 3.4a scroll-bar semantics with
a single thumb, and draw the others as plain image or text panels `[INFERRED]`.

### 3.2 Buttons `[STATIC]`

A button has a **button type**, an optional image strip, an optional caption (string + font), an
optional tooltip, and a state made of independent flags: *checked*, *pressed* (mouse held down on
it), *hover* (mouse over it), *enabled*.

Button types and image slicing:

| Type | Kind | Image cells (horizontal strip) | Drawing |
|---|---|---|---|
| text-only | Text-only push button | none | caption only |
| push-stretch | Push button, stretchable | 4 | 3-slice: the chosen cell is split into thirds; left third drawn at the left edge, right third at the right edge, middle third stretched (tiled) over the remainder |
| push-fixed | Push button, fixed size | 4 | cell blitted 1:1 |
| toggle-stretch | Toggle button, stretchable | 4 | 3-slice as push-stretch |
| toggle-fixed | Toggle button, fixed size | 4 | cell blitted 1:1 |
| check box | Check box | 8 | cell drawn beside the caption |
| radio button | Radio button | 8 | cell drawn beside the caption |

Cell width = strip width / 4 (push and toggle types) or / 8 (check box and radio button); cell height = strip height. When no
source rectangle is supplied the framework assumes a 128×32 strip.

**4-cell selection** (push and toggle types), cell index 0-based from the left:

| Condition (checked top-down) | Push (stretch, fixed) | Toggle (stretch, fixed) |
|---|---|---|
| disabled | 0 | 0 |
| pressed | 2 | 2 |
| checked | — | 2 |
| hover | 3 | 3 |
| otherwise | 1 | 1 |

So a 4-cell strip is ordered **disabled, normal, pressed/on, highlighted**. This matches every
4-cell strip shipped (e.g. tool-menu buttons). A secondary layout used by some application buttons
selects disabled → 0, pressed → 2, checked → 3, otherwise 1; another has only three cells
(disabled 0, normal 1, pressed 2) and no hover cell. `[STATIC]`

**8-cell selection** (check box, radio button): cell = base + (0 if checked else 1), where base is 0 normal,
2 hover, 4 pressed, 6 disabled (disabled overrides pressed, pressed overrides hover). The image is
vertically centred in the control; it sits at the left edge (caption to its right) or, with the
"image right" alignment, at the right edge. `[STATIC]`, consistent with `GZRadioBtn`/`GZCheckBox` art
`[DATA]`.

Caption: drawn centred in the control (both axes) for push/toggle buttons, offset by 1 pixel right
and down while pressed if the "shift caption when pressed" flag is set. Separate caption colours
exist for normal, highlighted and disabled; a colour left unset means "use the font's own
colour". The default button colour is grey (192,192,192) `[STATIC]` (whether this is the face or the
caption colour is `[OPEN]`). Buttons other than text-only draw only their image cell, with no separately
filled face `[INFERRED]`; a text-only button draws only its caption. `[OPEN]` whether a
text-only button fills its rectangle with the grey base colour.

**Mouse/keyboard state machine** (default "act on release" mode):

| Event | Effect |
|---|---|
| Mouse enters | set *hover*; capture the mouse so that leaving can be detected; redraw; request tooltip (3.3) |
| Mouse moves while captured and leaves the rectangle | clear *hover*, release capture, redraw |
| Left button down inside | hide any tooltip; capture mouse; set *pressed*; redraw; play the button's "press" sound (if one is assigned) |
| Left button up, pointer inside, *pressed* | flip *checked* (only visible on toggle/check/radio types); send **click** notification; clear *pressed*; redraw |
| Left button up, pointer outside | release capture; no click; clear *pressed*; redraw |
| Capture lost to another window | clear *hover* and *pressed* |
| Enter key while focused | click |
| Space key while focused | sets a key-down state and notifies; click on release `[INFERRED]` |

An "act on press" flag changes the behaviour so that the click (and *checked* flip) happens on
mouse-down and no capture is taken. `[STATIC]`

Notifications sent by a button: click; checked-state changed (with new value); pressed-state
changed (pressed / released). `[STATIC]` Radio-button grouping (unchecking siblings) is done by the
owning screen logic, not by the button. `[INFERRED]`

### 3.3 Tooltips `[STATIC]`

* A control shows a tooltip only if its tooltip flag is set and its tooltip text is non-empty.
  Tooltip text comes from a string table entry (`05-text-and-fonts.md`).
* When the pointer enters such a control, a tip is requested with: the text, the anchor (either the control's screen rectangle, or a specific point if the
  "anchor at point" option is set), two placement flags, and background colour **RGB (255,255,224)**
  (pale yellow).
* Pressing the mouse button on the control, the pointer leaving the control or the control being
  destroyed removes the tip.
* Timing `[STATIC]`: ticks of a repeating timer are counted from the moment a
  tip is requested. Placement flag 1 = "show immediately"; flag 2 = "manual" (no automatic
  timing). Without flag 2 the tip appears when the count reaches **1** tick and is removed
  automatically when the count reaches **3** ticks (counted from the request, not from the
  appearance). A new request (pointer entering another control) replaces the current tip and
  restarts the count. Each request restarts the timer, so ticks are counted
  from the request. The timer period is **750 ms** `[STATIC]`. Hence a tip appears **750 ms** after
  the pointer enters the control and is removed **2250 ms** after it entered (i.e. it is visible for
  1500 ms) unless flag 2 ("manual") is set, in which case it stays until the pointer leaves, the
  button is pressed or a hide message arrives.
* A "tooltip shown" notification is emitted when a tip becomes visible and a
  "tooltip removed" notification when it is removed, each with the tip's sequence number. Four
  further tooltip requests exist whose effect is `[OPEN]` (probably update text and move tip).

#### 3.3a Observed tooltip geometry `[OBSERVED]`

Measured in the original at 800 × 600 and 1024 × 768 (windowed under Wine) on every tool-column root
and sub-menu button, the bottom-bar and navigator buttons, and the View Data mode strip. Coordinates
are in client pixels. Box = the whole tip including its 1 px border.

**Look**

* Box: a 1 px black border; fill (255, 255, 224), seen as (255, 255, 231) on the 16-bit display.
* Text: black, one line, in the 8-point system UI font (MS Sans Serif; under Wine its substitute).
* Size: the box is 16 px tall. It is text width + 6 px wide: the text starts 2 px inside the left
  border, and 2 px are left inside the right border. The text's cap top is 3 px below the box top.
  No tip seen wraps; the longest, "Subway to Rail Connection", is 135 px wide on one line.

**Anchoring**

* The tip is anchored to the **button rectangle**, not to the pointer. Entering the Landscape root
  at its top, middle, bottom, left and right gave the identical box every time. Moving inside the
  button does not move the tip.
* Each button uses one of three placements. Gap = 2 px in every case.
  * **Left**, vertically centred:
    * box right edge (exclusive) = button left − 2;
    * box top = button top + ⌊(button height − 16) / 2⌋.
    * Used by: all tool-column root buttons (56 × 32 hot rectangles: top = button top + 8), all
      sub-menu buttons (36 × 36: top = button top + 10), the navigator Layer Views button and
      the Hide Toolbars (minimise) button. The Terrain Grid Toggle in View City Layers also
      opens to the left.
  * **Above**, horizontally centred:
    * box bottom (exclusive) = button top − 2;
    * box left = button centre x − ⌊box width / 2⌋ (±1 px).
    * Used by: Query "?", Pause, Run, the four speed cells, Visit SimCity Exchange (the map
      button above the navigator), Zoom In/Out, Rotate Clockwise/Counterclockwise.
  * **Below**, horizontally centred:
    * box top = button bottom + 2.
    * Used by: the twelve View Data mode-strip icons, e.g. Aura button (48, 72, 26 × 26) → tip
      (22, 100, 78 × 16).
* **Clamping:** a box that would cross the right edge of the client area is shifted left so that
  its right edge is client width − 2. Examples: Rotate Counterclockwise at 1024 → box ends at
  1022; at 800 → box ends at 798. There is no flip to the other side. Clamping at the left, top
  and bottom edges is `[OPEN]`.

**Behaviour**

* The tip appears at once (within 0.1 s), also from cold.
* It stays as long as the pointer stays on the button (checked to 6 s); see the timer of §3.3 for
  other controls.
* It is removed on mouse-down on the button and on leaving the button. After a click it does not
  come back while the pointer rests on the button.
* Moving straight onto an adjacent button replaces the tip at once (within 0.05 s).

**Controls without tips** (observed): the RCI indicator, the compass, the minimap, the ticker, the
bottom-bar text fields; and in dialogs the close boxes, OK buttons, check boxes, radio buttons and
text buttons of View City Layers and the Budget, and the View Data "Use As Map" toggle.

**Examples** at 1024 × 768:
* Landscape root (961, 100, 56 × 32) → (901, 108, 58 × 16).
* Road sub (928, 118, 36 × 36) → (896, 128, 30 × 16).
* Query (773, 713, 40 × 39) → (776, 695, 33 × 16).
* Zoom In (878, 704, 26 × 29) → (869, 686, 45 × 16).
* Layer Views (978, 610, 36 × 35) → (911, 619, 65 × 16).

At 800 × 600 the same rules hold exactly; the column origin is (704, 0) instead of (928, 0).

### 3.4 List boxes and double-click `[STATIC]`

A list item is selected on mouse-down. A **double-click** is recognised when the same item index is
clicked again and the time since the previous click on it is less than the operating system's
double-click time (Windows `GetDoubleClickTime`, default 500 ms). The timer is shared by all list
boxes.

### 3.4a Keyboard handling of list, combo and text controls `[STATIC]`

**List box**, when it has focus:

| Key | Effect |
|---|---|
| Up / Down | move the selection one item up / down |
| Page Up / Page Down | move the selection one visible page up / down |
| Enter | re-select the current item; a normal list then sends its "activate" notification (same as a double-click), a list in pop-up mode (combo drop-down `[INFERRED]`) reports the current item as the result |
| Esc | a normal list passes the key on; a list in pop-up mode reports "no item" (−1) as the result |
| any other key | a normal list passes the key on to its parent (so accelerators still work); a pop-up list consumes it |

Each selection change sends the list's selection-changed notification with the new item index
(−1 = no selection). A mouse-down on an item selects it, and a second click on the same item within
the double-click time sends the double-click notification (section 3.4).

**Single-line text field**, when editable and focused: Left / Right move the caret
one character; Home / End move it to the start / end of the line; Backspace deletes the character
before the caret; printable characters (delivered as character events, section 3.7) are inserted at
the caret; Enter commits the entry and Esc cancels it; Tab is handled (focus change `[INFERRED]`). A read-only field passes all keys on. Exact notification codes are
internal; the application only distinguishes commit, cancel and text-changed. `[STATIC]`
(behaviour) / `[INFERRED]` (notification naming)

**Spinner**: clicking the upper half of the 12×18 cell increments the value and
clicking the lower half decrements it `[INFERRED]` from the art. Cells: 0 disabled, 1 normal,
2 pressed, 3 highlighted (standard 4-cell order).

**Scroll bar**: arrows move by one line, the track by one page, and the thumb can be
dragged. The thumb length is proportional to visible/total `[INFERRED]`.

### 3.5 Keyboard focus and modality `[INFERRED]`

* Keyboard input goes first to the focused control (edit fields, buttons for Enter/Space), then to
  the accelerator table (section 5) of the active top-level context.
* A modal dialog captures all mouse and keyboard input until it closes; windows behind it neither
  receive input nor show hover states. Accelerators of the game screen are not processed while a
  modal dialog is open. `[OPEN]` whether the simulation pauses (owned by `09`).

### 3.6 Fonts in controls `[STATIC]`

* Controls draw text with the FBF fonts registered by font ID (`05-text-and-fonts.md` §5).
* A button whose caption font was not set explicitly uses `main10.fbf`; if that font is not
  available it uses `system9.fbf`. The caption is measured with that font
  to centre it (single line) or, in multi-line mode, wrapped to the button width minus its left and
  right text margins; the caption block height is then line height × line count.
* Text colour defaults to the font's colour ((49,54,124), `05-text-and-fonts.md`) unless the control
  overrides it (e.g. message-box captions, section 9.1).

### 3.7 Input events delivered to controls `[STATIC]`

The platform layer converts OS input into these event kinds (numbering is internal but the set
and payloads are behaviourally relevant):

| Event | Payload |
|---|---|
| key down | virtual-key code, modifier mask (Shift 1, Ctrl 2, Alt 4, plus an "auto-repeat" flag) |
| key up | virtual-key code, modifier mask |
| character | translated character (used by edit fields only) |
| left / right button down, left / right button up, mouse move | pointer position in window pixels, clamped to `0…width−1`, `0…height−1`; modifier/button state |

Double-clicks are not delivered as separate OS events; controls that need them (list boxes) detect
them by timing (section 3.4).

## 4. Resolution handling

### 4.1 Selection rule `[STATIC]`

The in-game UI picks its bottom-bar layout from the **current screen width**:

| Screen width | Layout set / image group for bottom bar |
|---|---|
| exactly 640 | `0x00640480` |
| exactly 800 | `0x00800600` |
| greater than 800 | `0x01024768`. Also used for widths above 1024: the game runs at 2048×1152 in the GOG build `[OBSERVED]` |
| other (< 800, not 640) | layout not created (bar fails to build) `[STATIC]` |

The bottom-bar background is image Instance `0x82790741` in the chosen group (640: 440×64,
800: 600×56, 1024: 824×64). A second piece, `0x02F78C6D:0x82790742` (16×64), is loaded in all
resolutions.

Tool-menu button art comes from group `0x00640480` at 640×480 and from `0x02F78C6D` otherwise
(see section 7).

### 4.2 Bottom-bar geometry `[STATIC]`

Coordinates are pixels relative to the top-left of the bottom-bar background image. Rects are
(left, top, right, bottom), right/bottom exclusive.

| Element | 640 | 800 | ≥1024 |
|---|---|---|---|
| Field A rect | (8,46,340,64) | (8,26,144,44) | (8,26,366,44) |
| Field B rect | (8,25,116,43) | (146,26,258,44) | (372,26,480,44) |
| Field C rect | (120,25,270,43) | (260,26,408,44) | (486,26,630,44) |
| Field D rect | (264,25,350,43) | (396,26,500,44) | (630,26,712,44) |
| Text line rect | (8,3,340,23) | (8,5,481,22) | (8,6,704,23) |
| Point P1 | (371,14) | (511,9) | (735,10) |
| Point P2 | (348,14) | (488,9) | (712,10) |
| Point P3 (top-left of a child panel) | (399,14) | (547,11) | (773,9) |
| Point P4 | (349,34) | (489,29) | (713,29) |

The text line window is as wide as its rect and is vertically centred in it:
`y = top + ((bottom − top) − windowHeight) / 2` (integer division).

Field roles `[STATIC]`: **A = city name**, **B = population**, **C = funds**, **D = date**. Each
field is its own child surface the size of its rect, and the text is centred in the rect
`[INFERRED]` from the centring arithmetic. (C and D overlap by a few pixels at 640 and 800; this is
in the original table.) Points P1–P4 are the **top-left corners** of the bar's buttons
`[OBSERVED 800×600 and 1024×768; images located by template match]`:

| Point | Widget | Image | Size |
|---|---|---|---|
| P2 | Pause | `02F78C6D:52FB06E0`/`E1` | 22 × 22 |
| P1 | Run | `52FB06E2`/`E3` (two 13-px halves) | 26 × 22 |
| P4 | Speed bar, four 10 × 11 cells at (4 + 10·i, 5) | `52FB06E4`–`E7` (running, speeds 1–4), `E8` hover cell, `E9` paused cell (`09-ui-screens.md` §4.2) | 48 × 23 |
| P3 | Query "?" | `52FB06EA` (four 40 × 39 cells) | 40 × 39 |

The bar is anchored at the window's bottom-left. Its top is at window height − bar height
(640: 64, 800: 56, 1024: 64). Absolute positions: 800 × 600 pause (488,553), run (511,553),
speed (489,573), query (547,555); 1024 × 768 pause (712,714), run (735,714), speed (713,733),
query (773,713).

**Right-hand cluster** `[OBSERVED, identical offsets at 800 and 1024]`. Offsets are from the
window's bottom-right corner (W, H) unless noted:

| Widget | Image | Top-left | Size |
|---|---|---|---|
| Layer Views | `22F96E35` (4 cells) | (W−46, H−158) | 36 × 35 |
| Zoom In | `22F96E2D` | (W−146, H−64) | 26 × 29 |
| Zoom Out | `22F96E2E` | (W−40, H−64) | 26 × 29 |
| Rotate Clockwise | `22F96E2F` | (W−126, H−44) | 26 × 29 |
| Rotate Counterclockwise | `22F96E30` | (W−60, H−44) | 26 × 29 |
| Hide Toolbars | `53244586` | (W−26, H−26) | 26 × 26 |
| RCI background | `42FB77F6` | (W−201, bar top − 24) | 41 × 88 |
| Visit SimCity Exchange | `52FB06F0` | (W−194, bar top − 24) | 33 × 30 |

At 640 × 480 [OBSERVED]:
* Bar buttons: pause (348,430), run (371,430), speed (349,450), query (399,430). These are the
  P2/P1/P4/P3 values on a bar whose top is 416.
* Right cluster: RCI, Exchange and Hide Toolbars use the same offsets as above.
* The navigator buttons sit 12 px lower: Layer Views (W−46, H−146), Zoom In (W−146, H−52), Zoom
  Out (W−40, H−52), Rotate Clockwise (W−126, H−32), Rotate Counterclockwise (W−60, H−32).

**Hide / Show Toolbars** [OBSERVED][DATA]:
* Hide Toolbars is image `02F78C6D:53244586`: 104 × 26, four 26 × 26 cells (disabled, normal,
  pressed, highlighted), each a small framed square on a magenta key. It is drawn on top of the
  navigator art. Tooltip `225872FE:A6`, placed to the left.
* While the toolbars are hidden, only a round button remains, in the same rect (W−26, H−26,
  26 × 26). It is `53244588` (four 26 × 26 cells), with `53244589` as its opacity plane (white
  discs) [INFERRED], so it is blended over the city. Tooltip `A9` "Show Toolbars", placed to the
  left: box (697,579,75 × 16) at 800 × 600.
Update rules are in `09-ui-screens.md` §4.1.

**Observed at 2048×1152** `[OBSERVED]`:
* The bar is anchored at the bottom-left of the window.
* The news ticker runs along its upper line, starting at the left edge.
* Below the ticker, the fields appear left to right as city name, population, funds and date.
* The pause/run buttons and the speed indicator sit immediately right of the ticker, followed by
  the help button.
* The bar background continues across the window to the RCI indicator, which sits just left of
  the navigator panel (minimap, zoom and rotate buttons) in the bottom-right corner.

**Observed at 800 × 600 and 640 × 480** `[OBSERVED]`:
* **800 × 600.** The bottom bar is about 600 px wide at the bottom-left, with the ticker on the
  top line and city name, population, funds and date on the line below. The tool column is at the
  right edge, and the navigator panel sits at the bottom-right, overlapping the column's lower
  part.
* **640 × 480.** The bottom bar is about 440 px wide, with **three** text lines:
  * the ticker;
  * population, funds and date;
  * the city name, centred under them.

  The nine root buttons of the tool column are stacked without the extra gaps used at larger
  sizes (Emergency directly below Civic). The navigator is at the bottom-right.
* At both sizes the main menu shows its own background image, sized to fill the window. At
  1024 × 768 and above, the 800 × 600 background is centred on black.

## 5. Hotkeys

### 5.1 Files and resource keys `[DATA]`

`Res/Hotkeys/<LANGUAGE>.IXF` (DUTCH, ENGLISH, English-UK, FRENCH, GERMAN, ITALIAN, SWEDISH; the
file matching the installed language is used, see `05-text-and-fonts.md`). Each file holds two
resources, Type `0xA2E3D533`, Group `0xA2E3D56D`:

| Instance | Used when |
|---|---|
| 1 | Normal city play (full table) |
| 2 | Restricted mode (terrain-editing / pre-city stage; only view, landscaping and Esc keys) `[INFERRED]` |

The city view switches to instance 1 when normal city play begins and to instance 2 when it
enters the restricted mode, replacing its current accelerator table. `[STATIC]`

### 5.2 Payload grammar `[DATA]` `[STATIC]`

The payload is plain ASCII text, one binding per line (lines separated by CR LF). Each line is
8 unsigned decimal integers separated by whitespace, with a literal `=` after the fourth:

`ctrl alt shift vk = msgType cmd arg1 arg2`

| Field | Meaning |
|---|---|
| ctrl, alt, shift | 0 or 1: modifier must be held |
| vk | Windows virtual-key code, 0–255 (e.g. 65–90 letters, 48–57 digits, 96–105 numeric keypad 0–9, 107 keypad +, 109 keypad −, 112–123 F1–F12, 33 PageUp, 34 PageDown, 37–40 arrows, 27 Esc, 32 Space, 188 `,`, 190 `.`, 191 `/`) |
| msgType | Message to post. Every shipped line uses `583838163` = `0x22CCA9D3` ("view command") |
| cmd | Command number (table 5.4) |
| arg1, arg2 | Extra arguments, always 0 in shipped files |

Lines that do not yield 8 numbers, or where a modifier field is > 1 or vk > 255, are ignored.
All shipped data follows this grammar: 7/7 files, 14/14 tables, 881/881 lines. `[DATA]`

### 5.3 Matching rule `[STATIC]`

1. The current modifier state is a bit mask: Shift = 1, Ctrl = 2, Alt = 4 (from the live key
   state at the time of the key event).
2. Bindings are keyed by the pair (vk, modifier mask); each pair holds at most one binding (a
   later line with the same pair replaces the earlier one).
3. On a key-down event whose key is not Shift, Ctrl or Alt themselves, the engine looks up
   (vk, current mask). If found it posts `{msgType, cmd, arg1, arg2}`.
4. On the matching key-up event it posts the same command and arguments marked as a "key
   released" message instead of `msgType`, so the receiver can tell press from release.
5. Auto-repeat key-downs carry a "repeat" flag inside the mask, so they **never** match: holding a
   key does not repeat its command. Character (WM_CHAR) events are never matched.
6. Lines with vk = 0 (Ctrl, Alt or Shift alone: commands 66, 67, 92) can never match a key event;
   they exist in the data but have no effect through this mechanism `[INFERRED]`.

### 5.4 Command numbers (ENGLISH.IXF, table 1) `[DATA]` + command meaning `[STATIC]`

The key assigned to each command is in the shipped file (`Res/Hotkeys/ENGLISH.IXF`, instance 1) and
is not reproduced here; an implementation reads it from the file (section 5.2). This section gives
what each command number does.

"Tool class" means the command starts the same action as the tool-menu item whose `MenuItem.INI`
key (section 6) is given; parameters of the command object come from that item.

**Tool commands.**

| cmd | Action (MenuItem key) |
|---|---|
| 1 | residential low density (`0x10003101`) |
| 4 | residential medium (`0x10003102`) |
| 5 | residential high (`0x10003103`) |
| 6 | commercial low (`0x10003201`) |
| 7 | commercial medium (`0x10003202`) |
| 8 | commercial high (`0x10003203`) |
| 9 | industrial low (`0x10003301`) |
| 10 | industrial medium (`0x10003302`) |
| 11 | industrial high (`0x10003303`) |
| 12 | de-zone (`0x10003007`) |
| 13 | power lines (`0x10005001`) |
| 14 | bus stop (`0x10004004`); only if available |
| 15 | road (`0x10004001`) |
| 16 | rail (`0x10004101`) |
| 17 | train station (`0x10004102`) |
| 18 | demolish (`0x10002006` etc.) |
| 19 | meet advisors (`0x10001006`) |
| 20 | budget (`0x10008001`) |
| 21 | ordinances (`0x10008002`) |
| 22 | map view (`0x10008004`) |
| 24 | police station (`0x10006201`) |
| 25 | fire station (`0x10006003`) |
| 26 | hospital (`0x10006401`) |
| 27 | school (`0x10006402`) |
| 28 | load saved city (`0x10009301`) |
| 29 | new city (`0x10009004`) |
| 30 | save (`0x10009002`) |
| 31 | save as (`0x10009006`) |
| 51 | power-plant picker (`0x10005002`) |
| 52 | highway (`0x10004301`); only if available |
| 53 | subway (`0x10004201`); only if available |
| 54 | subway station (`0x10004202`); only if available |
| 55 | jail (`0x10006202`) |
| 56 | college (`0x10006403`) |
| 57 | small park (`0x10006601`) |
| 58 | big park (`0x10006602`) |
| 59 | rewards picker (`0x10006009`) |
| 60 | preferences (`0x10009001`) |
| 64 | load terrain (`0x10009303`) |
| 68 | water-building picker (`0x10005009`) |
| 69 | water pipes (`0x10005003`) |
| 70 | place trees (`0x10002001`) |
| 71 | place water (`0x10002002`) |
| 72 | landmark picker (`0x10006008`) |
| 73 | neighbours (`0x10008005`) |
| 87 | load starter town (`0x10009302`) |
| 93 | landfill zone (`0x10003004`) |
| 95 | garbage-building picker (`0x1000500A`) |

Aliases (a second key for the same action): 61 = 60; 62 = 28; 65 = 30; 86 = 29; 88 = 87; 89 = 64;
90 = 31.

**Engine commands.**

| cmd | Key | Action |
|---|---|---|
| 33 | F10 | Application exit request (same as 91, 102) |
| 35 | `/` | Toggle pause (paused → resume, running → pause) |
| 36 | `,` | Simulation speed one step slower (speeds 2→1, 3→2, 4→3; no change at 1) |
| 37 | `.` | Simulation speed one step faster (1→2, 2→3, 3→4; no change at 4) |
| 40 | Ctrl+Alt+Shift+C | Open the cheat-code entry dialog (`09-ui-screens.md`) |
| 41 | keypad + | Zoom in `[INFERRED]` |
| 42 | keypad − | Zoom out `[INFERRED]` |
| 43 | PageDown | Rotate the view one 90° step **clockwise as seen on screen**. The map content turns clockwise: the tile at the screen's bottom corner moves to the left corner, and the navigator's compass N marker moves from the top corner to the right corner. `[OBSERVED]` |
| 44 | PageUp | Rotate the view one 90° step counter-clockwise as seen (it undoes 43). `[OBSERVED]` |
| 45–48 | Left, Up, Right, Down | Scroll the view in that direction `[INFERRED]` |
| 49 | F12 | Toggle the game UI panels (the "hide/show toolbars" toggle of `09-ui-screens.md` §4). Hidden: the whole tool-menu column with any open fly-out, the whole bottom bar (news ticker, city name, population, funds, date, pause/speed controls, query button, RCI indicator, messages button), and the navigator (minimap, zoom/rotate buttons, layers button). Visible: only a small round "show UI" button in the window's bottom-right corner (about 20 × 20 px). The map does **not** move or resize: it already fills the client area under the panels, so hiding just uncovers it. A second F12 restores everything. `[STATIC]` toggle / `[OBSERVED]` panels |
| 50 | Space | Centre the view on the tile under the mouse pointer `[INFERRED]` |
| 63 | G | Toggle a view display option (grid `[INFERRED]`) |
| 66 / 67 / 92 | Ctrl / Alt / Shift alone (vk 0) | no effect (see 5.3 step 6) |
| 75, 76 | keypad 1, 1 | Set zoom level index 0 |
| 77, 78 | keypad 2, 2 | Set zoom level index 1 |
| 79, 80 | keypad 3, 3 | Set zoom level index 2 |
| 81, 82 | keypad 4, 4 | Set zoom level index 3 |
| 83, 84 | keypad 5, 5 | Set zoom level index 4. Keypad 1 = zoom 0 = **farthest**; keypad 5 = zoom 4 = **nearest** `[OBSERVED]` |
| 85 | Esc | Cancel the current tool (select no tool) |
| 91 | Ctrl+Q | Application exit request |
| 94 | Ctrl+G | Same as the navigator's **layers** button (the stacked-layers icon above the minimap): opens the **View City Layers** dialog (above-ground / underground layer check boxes, City Layer Views radio list, Apply, Default View, OK). It is not a `MenuItem.INI` command. `[STATIC]` + `[OBSERVED]` |
| 96–100 | Ctrl+Shift+F5, F1, F2, F3, F4 | No effect in this build `[STATIC]` (see note below) |
| 102 | Alt+F4 | Application exit request |
| 108 | Ctrl+Shift+P | No effect in this build `[STATIC]` |
| 112 | Ctrl+C | No effect in this build `[STATIC]` |
| 113 | Ctrl+Shift+A | Take snapshot, default size (same as the `0x2435FF70` menu message with argument 0) |
| 114 | Ctrl+Shift+S | Take snapshot with the size given in arg1 (the menu message `0x2435FF70` with argument 1–3 maps to this command) |
| 115 | (unbound) | No visible effect in this build `[STATIC]`; an implementation may ignore it |

Table 2 (restricted mode) contains only commands 18, 41–48, 50, 67, 70, 71, 75–85 and 96–100 with
the same keys as table 1. `[DATA]`

**Unhandled commands** `[STATIC]`. Commands
**74, 96, 97, 98, 99, 100, 104, 108 and 112** have no effect in this build, so those bindings
(including English-UK F9 / Shift+F9 for 74 / 104, Ctrl+Shift+F1–F5 and Ctrl+C) do nothing. They look like leftovers of debug or cut features. A compatible engine must accept
them in the file and ignore them.

Other languages remap keys (e.g. FRENCH swaps Q/A and W/Z for an AZERTY layout; DUTCH and SWEDISH
remap several letters; ITALIAN uses Alt+Right for command 47; English-UK adds F9 and Shift+F9 for
commands 74 and 104). Implementations must read the bindings from the file rather
than hard-code them. `[DATA]`

Commands carrying a tool class are executed exactly as if the corresponding tool-menu item were
clicked (section 6.1).

**Rotation direction** `[OPEN]`. Commands 43 and 44 rotate the view in opposite directions.
`[OPEN]` whether PageDown (43) rotates clockwise or counter-clockwise; test against the original by
pressing PageDown once from the default view and noting which compass edge comes to the top.
Command 41 zooms in and 42 zooms out `[INFERRED]` from the keys.

## 6. `MenuItem.INI`

Packed in `Apps/Sys/SYS.PAK` as `MenuItem.INI` (plain text in the user's data; see
`02-containers.md` for the PAK format). It defines the tool-menu tree. All numbers are hexadecimal
with `0x` prefix unless stated; fields are comma-separated; every record ends with the literal
token `END`. Shipped record lines carry trailing text after `END` (a `;` followed by a label, e.g.
`; Place Trees`). The packed-INI reader does not treat `;` as a comment character inside a key line
(`02-containers.md` §5.3), so the value string still contains that text; the menu reader stops at
`END` and ignores everything after it. `[DATA]` (one file, 4 data sections + `[Admin]`, all 251 lines parsed).

### 6.1 `[SC3MenuItemInfo]` — what a button does

`<itemID> = enabled, nameTable, nameID, altTable, altID, kind, target, arg1, arg2, arg3, END`

| Field | Meaning |
|---|---|
| itemID | Menu item ID (`0x1000xxxx`), referenced from `[SC3MenuBtnDefs]` |
| enabled | 1 = item available (all shipped items are 1) `[INFERRED]` |
| nameTable, nameID | String (table `0x225872FE` "GUI" or `0x041F2625` "SE-UI", instance) used as the button's tooltip/name `[STATIC]` |
| altTable, altID | Second string reference; usually identical to the first; differs for Map View and Lay Subway Rail. Used as the tool's short name (e.g. in status/help text) `[INFERRED]` |
| kind | `COMM` = run a command; `PMSG` = post a message. (No other kind is used by a shipped item.) `[STATIC]` |
| target | For `COMM`: command class ID. For `PMSG`: message ID. `0x00000000` = the item does nothing itself (used by "Take Snapshot", whose sub-menu items do the work) |
| arg1, arg2, arg3 | `COMM`: three integer arguments handed to the command object in order (e.g. building-picker category ID in arg1; `1` in arg2 for Map View and Neighbors). `PMSG`: arg1 and arg2 become the message's first two parameters (third is 0) |

Execution `[STATIC]`:

* `COMM`: run the command identified by `target` with the three integer
  arguments (arg1, arg2, arg3 in that order). Examples from the shipped file:
  `0xE2ED3560` = building picker; arg1 selects the category:
  `0x62E925A2` power plants, `0xE2E92527` water buildings, `0xC2F291A9` garbage, `0x22E92581`
  landmarks, `0x62E92553` rewards). `0x610438A4` = load dialog (arg1 `0x83172AD7` city,
  `0x63172B74` starter town, `0x23172AF2` terrain, `0xA42C7725` scenario); `0x410438DD` = save
  (arg1 `0xC3172AEA` = save-as). The behaviour of each command belongs to `09-ui-screens.md` and the
  simulation documents.
* `PMSG`: post asynchronously the message `{target, arg1, arg2, 0}`.
  Shipped messages: `0x724A82D0` meet advisors, `0x724A82D7` ordinances, `0xC30177CA` go-to
  (emergency), `0x2435FF70` snapshot (arg1 = 0 view snapshots / 1 small / 2 medium / 3 large).

### 6.2 `[SC3MenuBtnDefs]` — buttons

`<buttonID> = itemID, subMenuID, END`

* buttonID is also the **image instance** of the button's 4-cell strip (144×36) in group
  `0x00640480` (640×480) or `0x02F78C6D` (larger screens). Example: button `0x00002001` uses image
  `0x02F78C6D:0x00002001`. `[DATA]` (every buttonID 0x2001–0xA006 that appears in a root
  sub-menu has a matching image in both groups; flyout items `0x3101..` etc. have none and are
  drawn as text/list entries `[INFERRED]`).
* itemID selects the `[SC3MenuItemInfo]` record executed on click.
* subMenuID ≠ 0 means clicking opens that sub-menu (`[SC3MenuDescs]` record) instead of / in
  addition to running the item `[INFERRED]`: root buttons (`0x1001..0x1009`) open the nine
  sub-menus; zone/highway/rail/subway/police/health/parks/load/disaster/snapshot buttons open
  flyouts.

### 6.3 `[SC3MenuDescs]` — menus

`<menuID> = titleTable, titleID, colour, style, count, button1, …, buttonN, END`

| Field | Meaning |
|---|---|
| menuID | `0xD1xxxxxx` / `0xD0xxxxxx` |
| titleTable, titleID | String for the menu's title (0,0 for the root) |
| colour | 0 for root and first-level sub-menus. For flyouts a 24-bit colour written as `0x00RRGGBB` (shipped: zoning RGB 151,175,95; transport 178,122,164; special 73,103,149; load 175,185,107; emergency 218,81,133; snapshot 102,163,120) used for the flyout panel `[INFERRED]` |
| style | 0 for root/sub-menus; `0x000018FF` for every flyout `[OPEN]` meaning (flags) |
| count | number of button IDs that follow; must equal the number listed |
| buttons | button IDs in display order (top to bottom) |

### 6.4 `[SC3MenuSets]` and `[SC3MSET_<id>]` — menu sets

* `[SC3MenuSets]`: `SC3MSET_0x<setID> = a, b, <path>` — declares a menu set whose mapping section is
  `[SC3MSET_0x<setID>]` in the file `<path>` (relative to `Apps/`, the executable's working
  directory, so the shipped value `Sys\MenuItem.INI` names `Apps/Sys/MenuItem.INI`, which is served
  from `Apps/Sys/SYS.PAK` like the other packed INIs, `02-containers.md`; set `0x00001000`, a = b = 0 `[OPEN]` meaning of a, b).
* `[SC3MSET_0x<setID>]`: `<menuID> = <menuID>` lines; maps each menu slot to the `[SC3MenuDescs]`
  record that fills it (identity in the shipped data). Lines marked "Sys" are the fixed root and
  first-level menus; "Def" ones are defaults that a set may override `[INFERRED]`.
* On start the city UI loads the four sections and activates set `0x00001000`. `[STATIC]`

## 7. Main tool menu layout `[STATIC]`

The tool menu is a vertical panel. The **root panel** background is image instance `0x0000FFFF`
(640×480: group `0x00640480`, 96×334; larger: group `0x02F78C6D`, 96×417) and already shows the
nine root icons. Each root button is an invisible 56×32 hot rectangle over its icon with two
overlay images, `0x100N0001` and `0x100N0005` (56×32, group `0x02F78C6D`; highlighted / pressed
states `[INFERRED]` which is which), where N = 1…9 is the root button index (`0x1001`…`0x1009`).
When root button N is selected its sub-menu panel `0x100N0003` (96 px wide; group `0x00640480` at
480-line screens, `0x02F78C6D` otherwise) is shown: it contains the highlighted root icon at the top
and the sub-menu buttons (36×36 cells of the `0x2001..` strips) down its left side.

The layout is chosen by **screen height** (480, 600, or > 600). Root button top-left (x, y) and
the sub-menu panel's offset (dx, dy), in pixels relative to the tool-menu panel origin:

| Root button | ID | 480: (x,y) / (dx,dy) | 600: (x,y) / (dx,dy) | >600: (x,y) / (dx,dy) |
|---|---|---|---|---|
| Landscape | `0x1001` | (33,3) / (0,0) | (33,3) / (0,0) | (33,100) / (0,97) |
| Zoning | `0x1002` | (33,39) / (0,11) | (33,39) / (0,11) | (33,136) / (0,108) |
| Transportation | `0x1003` | (33,75) / (0,11) | (33,75) / (0,11) | (33,172) / (0,108) |
| Utilities | `0x1004` | (33,111) / (0,11) | (33,111) / (0,11) | (33,208) / (0,108) |
| Special | `0x1005` | (33,147) / (0,11) | (33,147) / (0,11) | (33,244) / (0,108) |
| Meet Advisors | `0x1006` | (33,219) / (0,0) `[INFERRED]` | (33,291) / (0,0) | (33,388) / (0,97) |
| Review | `0x1007` | (33,255) / (0,45) | (33,327) / (0,118) | (33,424) / (0,215) |
| Options | `0x1008` | (33,291) / (0,0) | (33,363) / (0,118) | (33,460) / (0,215) |
| Emergency | `0x1009` | (33,183) / (0,83) | (33,219) / (0,119) | (33,316) / (0,216) |

In screen order (top to bottom: Landscape, Zoning, Transportation, Utilities, Special, Emergency,
Meet Advisors, Review, Options) consecutive root buttons are 36 px apart at 480 lines. At 600 and
> 600 lines the gaps above Emergency and above Meet Advisors are 72 px (one empty 36-px slot each);
all other gaps are 36 px. The table values are authoritative. Meet Advisors has no sub-menu (its subMenuID is 0) and
therefore no `0x10060003` panel.

**Sub-menu buttons** `[STATIC]` (positions for every button of `[SC3MenuBtnDefs]` that sits in a
second-level menu). Every second-level button (`0x2001`…`0xA006`) is a fixed-size
**36×36** button using the 4-cell strip of the same instance (section 2.3) from group
`0x00640480` at 480 lines and `0x02F78C6D` otherwise. Its left edge is at **x = 0** and its top
edge y is given below. The coordinates use the same frame as the root-button table above (the
tool-menu column origin; Landscape root is at y = 3 or 100), so the sub-menu column overlaps the
left part of the root column, and the panel image `0x100N0003` placed at (dx,dy) supplies the
background `[INFERRED]` from the overlap.

| Sub-menu | Buttons, top to bottom | y at 480 | y at 600 | y at >600 |
|---|---|---|---|---|
| Landscape (`0x1001`) | `0x2001`–`0x2006` | 21, 57, 93, 129, 165, 201 | same as 480 | 118, 154, 190, 226, 262, 298 |
| Zoning (`0x1002`) | `0x3001`–`0x3008` | 21 + 36·k, k = 0…7 (21…273) | same as 480 | 118 + 36·k (118…370) |
| Transportation (`0x1003`) | `0x4001`, `0x4002`, `0x4004`, `0x4005`, `0x4007`, `0x4009`, `0x400A` | 21 + 36·k, k = 0…6 | same as 480 | 118 + 36·k |
| Utilities (`0x1004`) | `0x5001`, `0x5002`, `0x5003`, `0x5009`, `0x500A`, `0x500B` | 21 + 36·k, k = 0…5 | same as 480 | 118 + 36·k |
| Special (`0x1005`) | `0x6002`, `0x6003`, `0x6004`, `0x6006`, `0x6008`, `0x6009`, `0x600A` | 21 + 36·k, k = 0…6 | same as 480 | 118 + 36·k |
| Review (`0x1007`) | `0x8010`, `0x8011`, `0x8001`, `0x8005`, `0x8002`, `0x8003`, `0x8004` | 57, 93, 129, 165, 201, 237, 273 | 129, 165, 201, 237, 273, 309, 345 | 226, 262, 298, 334, 370, 406, 442 |
| Options (`0x1008`) | `0x9001`, `0x900A`, `0x9002`, `0x9006`, `0x9003`, `0x9004`, `0x9005` | 57, 93, 129, 165, 201, 237, 273 | **128**, **164**, 201, 237, 273, 309, 345 | **225**, **261**, 298, 334, 370, 406, 442 |
| Emergency (`0x1009`) | `0xA001`, `0xA002`, `0xA003`, `0xA004`, `0xA006`, `0xA005` | 93, 129, 165, 201, 237, 273 | 129, 165, 201, 237, 273, 309 | 226, 262, 298, 334, 370, 406 |

The Review and Options orders above are by position, which differs from the numeric order (for
example `0x8010` and `0x8011` are the top two Review buttons). The first two Options buttons are
1 px higher than the 36-px grid at 600 and >600 lines; this is in the original table and should be
kept. Third-level fly-out buttons (`0x3101`…`0x6604`, the zone-density and building-group
fly-outs with style `0x000018FF`) are not in this table; the fly-out window lays them out
itself.

`[OBSERVED]` (1024 × 768, Health & Education fly-out):
* **Opening.** A plain click on a second-level button that owns a fly-out immediately selects the
  fly-out's currently checked entry and activates its tool. Moving onto an entry and releasing
  the mouse there selects it.
* `[OBSERVED 640 × 480, Residential, 50 fps capture]`: there is **no hold delay**. The
  fly-out appears on mouse-**down**, within one 20 ms frame, and stays only while the button is
  held. A quick click shows it for the duration of the press only (80 ms press → 4 frames).
  Release:
  * over the sub-button itself, after any hold time (tested 80 ms and 1.5 s): the fly-out closes
    (the checked entry is used, as for a plain click);
  * over an entry: that entry becomes the checked one and the fly-out closes;
  * anywhere else (for example the map): the fly-out closes and the checked entry does not change.

  The sub-menu column stays open in all three cases, and also after a later map click and Escape.
  The checked entry is **not** saved in `SC3.cfg`: the file was rewritten byte-identical on exit
  after Dense was checked, and after a restart Light Residential was checked again. `[OPEN]` whether a city
  save carries it.
* **Placement.** The fly-out opens to the **left** of the button, because the column sits at the
  right edge of the window. Its right side touches the column, and its first entry sits about
  7 px above the button's centre.
* **Look.** A single dark-blue rectangle in the menu's strip colour, about 70 px wide. One text
  row per entry, 16 px tall, in white. The current entry carries a check mark at the left.
* **Example.** Hospital (checked by default), School, College, Library and Museum.

`[OBSERVED 800 × 600]` (Residential, Commercial, Health & Education):
* The fly-out's right edge touches the sub-menu column: right = column x (704 at 800), exclusive.
* Its top equals the pressed sub-button's top.
* Rows are 16 px, plus 1 px padding at the top and the bottom: 3 entries → 50 px, 5 entries →
  82 px.
* Its width fits the longest label: Residential 112, Commercial 115, Health & Education 66.
* The background is the sub-menu's strip colour: zones (148,174,90), civic (74,101,148). Text is
  white; the current entry has a check mark.
* `[OPEN]` clamping at the screen top and bottom.

**Column background pieces** `[STATIC]` (image sizes `[DATA]`). The tool-menu column is 96 px wide
and is assembled from these instances, chosen by screen height:

| Height | Pieces (instance, group, size) |
|---|---|
| 480 | `0xFFFF` from `0x00640480` (96×334) only |
| 600 | `0xFFFF` from `0x02F78C6D` (96×417) and `0xFFFE` from `0x02F78C6D` (96×25) |
| > 600 | `0xFFFF` from `0x02F78C6D` (96×417), `0xFFFD` from `0x01024768` (96×97), `0xFFFE` from `0x01024768` (96×96) |
| > 768 | additionally `0xFFFC` from `0x01024768` (96×32) |

If the column window has no explicit size, it takes the size of the `0xFFFF` piece. Stacking
`[INFERRED]` from the 97-px shift of every button at > 600 lines: at > 600 lines `0xFFFD` is a top
cap at y = 0, `0xFFFF` follows at y = 97, `0xFFFE` sits directly below `0xFFFF`, and `0xFFFC` is a
filler tiled below that down to the bottom bar. At 600 lines `0xFFFF` is at y = 0 and the 25-px
`0xFFFE` is a bottom cap directly below it. So `0xFFFE` is always the piece below `0xFFFF`, never a
top cap. The column is placed against the **right edge** of the game window: its right side touches the
window's right edge and its top is at y = 0 `[OBSERVED at 2048×1152]`. At that size the first root
button starts about 105 px from the top, which matches the 97-px top cap above. The column's
background runs down to the navigator panel at the bottom-right.

Other in-game art in `0x02F78C6D` identified by inspection `[DATA]`: `0x0000FFFE` (96×25) bottom
cap of the menu column at 600 lines `[INFERRED]` role; `0x52FB06EA` (160×39) 4-cell help ("?") button; `0x82790742` (16×64) end cap of
the bottom bar.

## 8. Cursors

### 8.1 Files `[DATA]` `[STATIC]`

Cursors are standard Windows cursor files (`.cur`, loaded by file name from
`Res/UI/Shared/Cursors/`; matching is case-insensitive). 96 `.cur` files ship in that folder
(113 in the whole installation, `04-graphics-formats.md` §8.3). 95 of them are type-2 (cursor)
files holding one 32×32 monochrome image, 326 bytes each. The exception, `dispatch cropduster.cur`
(2238 bytes), is an icon-type file (type 1 in its header) holding one 32×32 8-bpp image, so it
carries no hotspot. `[DATA]` The `.bmp` files
beside them are design previews and are not loaded, except that the city view references
`anchor point cursor.BMP` `[OPEN]` purpose. The Building Architect Tool has its own set in
`Res/BA/UI/SHARED/CURSORS/` (`24-bat-buildings.md`).

Application-level cursors (shell): `arrow.cur` (default pointer over UI), `text edit cursor.cur`
(over editable text fields), `help arrow cursor.cur` (help mode), `hand cursor.cur` (over
clickable hot spots / dragging), `wait cursor.cur` (busy: loading/saving), `blank.cur` (pointer
hidden). `[STATIC]`

### 8.2 City-view cursor registry `[STATIC]`

The city view loads the following cursors when it is created; each tool selects one of them
while it is active. (The original refers to them by internal numbers that appear in no data file;
an implementation can key them by file name.)

| File | Used for |
|---|---|
| BLANK.cur | hidden pointer |
| CENTER CURSOR.cur | default city-view cursor (no tool; click centres the view) |
| QUERY CURSOR.cur | query tool |
| REWARDS CURSOR.cur | rewards (generic) |
| DEMOLISH CLEAR CURSOR.cur | demolish / clear |
| WAND OF DESTRUCTION CURSOR.cur | demolish variant `[INFERRED]` |
| snapshot cursor.cur | snapshot mode |
| PAN `<direction>` ARROW.cur (8 directions: up, up left, left, down left, down, down right, right, up right) | drag-scroll direction (8.3) |
| EARLY MOVER CURSOR.cur | drag-scroll inside the dead zone |
| one file per landscaping / terrain-editor tool (level, lower, raise, raise-lower terrain; trees; paint water; river) | landscaping / terrain-editor tools |
| de-zone cursor.cur | de-zone |
| `ZONE RES|COM|IND LOW|MED|HIGH` (9 files), plus air-port, sea-port and landfill zone files | zoning tools |
| one file per network tool (power lines, water pipes, road, avenue, road tunnel, rail, subway tube, on-ramp) | utility / transport networks |
| DISPATCH `<service>` files and EMERGENCY | emergency tools |
| SIGNPOST CURSOR.cur | loaded but never shown: the game has no sign tool (`09-ui-screens.md` §6.17) |
| one file per placeable building tool, named after the tool (recreation, civic, landmark / reward, transit, utility buildings) | building placement |

File names are given in upper case where the program spells them so; the shipped files differ in
case only. The zone tools' cursor choice is `[STATIC]`; the other tool→cursor pairings follow the
file names `[INFERRED]`. Several shipped files (`big pointer`, `left right`, `up down`,
`diagonal 1/2`, `four way 1/2`, `dispatch cropduster`, `reg pointer`) are not in this registry and
are presumably used by other screens `[OPEN]`. Coverage: the registry names 80 files; 79 of them
are shipped (matched case-insensitively). The one remaining name, `SMITE CURSOR.cur`, has no shipped
file and is never loaded. The other 17 shipped `.cur` files are outside this
registry. `[DATA]`

### 8.3 Drag-scroll cursor and speed `[STATIC]`

While the city view is in drag-scroll mode (see `09-ui-screens.md`/`25-rendering.md` for how it is
entered), with the anchor at the point where the drag started and the pointer at P:

1. Per axis, `d = (P − anchor) / 2` (integer division, truncated toward zero).
2. Scroll velocity per axis: `80 × sign(d)` if `|d| > 80`; `d` if `12 < |d| ≤ 80`; otherwise 0.
   Units: view pixels per city-view update `[INFERRED]`. `[OPEN]` How often does an update run (once
   per rendered frame, or on a fixed timer)? Until resolved, use one update per rendered frame,
   scaled to 30 updates per second.
3. Cursor: classify each axis as negative (`d ≤ −12`), neutral (`−12 < d < 12`) or positive
   (`d ≥ 12`); x negative = left, y negative = up. Both neutral → EARLY MOVER cursor; otherwise the
   PAN arrow for the combined direction (e.g. x neutral, y negative → PAN UP ARROW; x negative,
   y negative → PAN UP LEFT ARROW).
4. The two thresholds differ on purpose and must be kept: at exactly `|d| = 12` the cursor already
   shows a PAN arrow (rule 3 uses `≥ 12`) but the velocity is still 0 (rule 2 needs `> 12`).

Edge scrolling (pointer inside one of four border rectangles while not dragging) sets per-edge
scroll flags and shows the matching PAN cursor `[STATIC]`. `[OPEN]` How wide are the four border
rectangles, and do they exclude the tool menu and bottom bar? Until resolved, use 8-px strips along
the edges of the city view `[INFERRED]`.

## 9. Common dialogs

### 9.1 Message box `[STATIC]`

The framework provides a modal message box with an optional title line, a word-wrapped message
and a fixed button set. It is used for confirmations and errors throughout the game (texts come
from string tables, `05-text-and-fonts.md`).

Button sets (the box's style code, low 16 bits):

| Code | Buttons, left to right |
|---|---|
| 0 | OK |
| 1 | OK, Cancel |
| 2 | Retry, Cancel |
| 3 | Yes, No |
| 4 | Yes, No, Cancel |
| 5 | Abort, Retry, Ignore |

The box reports which button was pressed (Abort, Cancel, Ignore, No, OK, Retry or Yes) to the
caller as its result.

| Button | Fallback caption | Caption colour |
|---|---|---|
| Abort | `Abort` | red (192,0,0) |
| Cancel | `Cancel` | red (192,0,0) |
| Ignore | `Ignore` | grey (192,192,192) |
| No | `No` | red (192,0,0) |
| OK | `OK` | green (0,192,0) |
| Retry | `Retry` | green (0,192,0) |
| Yes | `Yes` | green (0,192,0) |

The caption is the localised string for that button (string IDs in `09-ui-screens.md` §6.16); the
English fallback word is used only if no localised string exists. Colours `[INFERRED]` to be the caption
(text) colour. The buttons are framework push buttons (section 3.2).

Layout algorithm (all values in pixels):

1. Base width `B` from the button count: 1 button → 100, 2 → 208, 3 → 316. Start with
   `W = max(200, B)`.
2. If there is a title, `W = max(W, titleWidth + 16)`.
3. Word-wrap the message at width `W` and measure its height `H`. While `2·H ≥ W`, increase `W` by
   16 and re-wrap (the box grows wider until the text block is less than half as tall as wide).
4. Button row top: `y = H + 24`, plus the title height if there is a title.
5. First button left: `x₀ = (W − B) / 2 + 8`. Each following button starts 8 px after the previous
   button's right edge.
6. The box size is then the content extent plus margins `[OPEN]` exact frame margins; the box is
   centred on the screen `[INFERRED]`.
7. Default button (Enter) = the first button; for the single-button box it is also the Esc
   button. For multi-button boxes Esc selects the last ("Cancel"/"No"/"Ignore") button `[INFERRED]`.

The box is modal (section 3.5): it blocks input to other windows until a button is pressed, then
closes and returns the pressed button.

Message-box icon art: `SYS.IXF` `0x42E55FD4` (MBIcons, 128×29 — four 32×29 icons, presumably
information / question / warning / error `[INFERRED]` order) and `0x42E55FD5` (29×29) `[OPEN]`.

### 9.2 File load / save dialogs

The game does **not** use the operating-system file dialogs. Loading and saving are the game's own
screens, started by the command classes `0x610438A4` (load; argument selects city
`0x83172AD7`, starter town `0x63172B74`, terrain `0x23172AF2`, scenario `0xA42C7725`) and
`0x410438DD` (save; argument `0xC3172AEA` = save as). Their layout and behaviour are specified in
`09-ui-screens.md`. Dialog background art lives in `DLG.IXF` (e.g. 464×350 bodies `0x2302ADD8`,
`0xC2DECCDE`) `[INFERRED]`.

### 9.3 Generic dialog frame `[INFERRED]`

Framed windows use the `WinGen` 256×256 frame-piece sheet (`SYS.IXF` `0xE2B14588`) and the 4×4
`SysBtns` sheet (`0x62B19CE9`: rows = close, back/previous, minimise, maximise/restore; columns = the
4 button states of section 3.2). Exact frame-piece rectangles are `[OPEN]`.

## 10. IDs that `09-ui-screens.md` and other documents rely on

| Kind | IDs | Section |
|---|---|---|
| Hotkey resource | Type `0xA2E3D533`, Group `0xA2E3D56D`, Instances 1, 2 | 5 |
| Hotkey message type in the data | `0x22CCA9D3` (decimal 583838163) | 5.2 |
| Tool-menu image sets | `0x00640480` (480-line), `0x02F78C6D` (others) | 7 |
| Tool-menu images | `0x0000FFFF` root panel, `0x100N0001/3/5` per root button N, `0x2001..0xA006` button strips | 6.2, 7 |
| Bottom bar images | `0x82790741` per resolution group, `0x02F78C6D:0x82790742` | 4 |
| Menu messages | `0x724A82D0`, `0x724A82D7`, `0xC30177CA`, `0x2435FF70` | 6.1 |
| Cursor files | section 8.2 | 8 |

## Open questions

* Tooltip placement rules relative to the anchor rectangle (delay: 750 ms tick, section 3.3).
* Drawing rules and notifications of the slider, framed window, image window, graph/progress
  area and decoration controls (section 3.1); spinner and scroll-bar step logic.
* Exact meaning of the two 56×32 overlays `0x100N0001` / `0x100N0005`; the screen anchor of the
  tool-menu column (inferred top-left) and the stacking of its background pieces; geometry of
  third-level fly-outs. (Second-level button positions are tabulated in section 7.)
* Selection between root-menu layouts uses screen *height* (480 / 600 / >600) while the bottom bar
  uses screen *width* (640 / 800 / >800); behaviour for non-4:3 modes (e.g. 1280×1024) follows these
  rules but is untested.
* `[SC3MenuDescs]` style word `0x000018FF` and `[SC3MenuSets]` fields a, b.
* Hotkey command 94: which panel button it duplicates and which screen it opens. (Commands 74,
  96–100, 104, 108, 112 and 115 are resolved as no-ops, §5.4.)
* Rotation direction of hotkey commands 43 (PageDown) and 44 (PageUp), §5.4.
* Users of the colours registered at start-up whose role is unknown (§2.4); whether a text-only
  button fills its face (§3.2); effect of four further tooltip requests (§3.3).
* Edge-scroll border rectangle sizes and drag-scroll update rate (§8.3); tool→cursor mapping for
  non-zone tools (inferred from names).
* Message-box frame margins, icon selection and Esc mapping.
