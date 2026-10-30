# 07 — Video

Scope: the intro movie `Res/UI/Shared/MOVIES/INTRO.TGQ` (25,936,684 bytes) and how it is played.
It is the only movie in the game. [DATA]

Related documents: `01-architecture.md` (start-up sequence), `27-platform.md` (command line,
settings), `06-audio.md` (EA ADPCM family, music volume setting).

## 1. When the movie plays

The intro plays once at application start, after the framework and display are initialised and before
the main menu (banner screen) appears. [STATIC]

It is **not** played when any of these holds: [STATIC]

| Condition | Effect |
|-----------|--------|
| A file to open was passed on the command line (e.g. double-clicking a city) | intro skipped, the file is opened directly |
| Command-line option `intro` has the value `off` (compared case-insensitively) | intro skipped |
| The movie file does not exist in the data directory (nor in the alternative/CD data directory) | intro skipped |
| The movie cannot be opened/decoded | intro skipped |

Command-line options use the framework's generic `-name:value` syntax (see `27-platform.md`); the same
mechanism provides `sound:off` and `music:off`. Any `intro` value other than `off`, or no `intro`
option, lets the intro play. [STATIC]

File lookup: `<DataDirectory>Res\UI\Shared\Movies\Intro.tgq` (case-insensitive on the original
platform; the shipped file is upper-case `INTRO.TGQ` in `MOVIES`). If missing, the same relative path
is tried under a second, alternative data directory that the program keeps as a setting (intended
for the installation source). Where that setting comes from was not determined [OPEN]; an
implementation may omit this fallback. `<DataDirectory>` is the data directory of
`01-architecture.md` §4 step 2 (default `Apps/`). [STATIC][DATA]

## 2. Ending and skipping

* The movie runs as a modal "intro" screen state with three states: *not started* → *playing* →
  *finished*. While playing, each application frame asks the player to present the next movie frame.
  [STATIC]
* When the stream ends, or when playback cannot start, the intro ends exactly as if the player
  had pressed **Escape**: playback stops, the state becomes *finished*, the normal cursor/display
  state is restored and the shell proceeds to the main menu. [STATIC]
* Pressing **Escape** skips the movie at any time, with the same effect. [STATIC] Whether other keys or a mouse click also
  skip is [OPEN]; a reimplementation should at least accept Escape and is encouraged to accept any
  key or click.
* No part of the movie can be replayed from the menus. [INFERRED]

## 3. Presentation

* The movie is presented at a requested size of **640 × 480** pixels, centred on the screen:
  x = (screenWidth − 640) / 2, y = (screenHeight − 480) / 2. [STATIC]
* The coded frames are **320 × 480**; they are therefore stretched horizontally by a factor of 2 to
  fill 640 × 480 (decoded frames viewed at 320 × 480 are visibly squeezed horizontally). [DATA][INFERRED]
* The rest of the screen is black. The mouse cursor is hidden during playback. [INFERRED]
* Frame rate 15 frames/s; 1051 frames (≈ 70.1 s); audio runs ≈ 72.5 s, so the movie ends when
  both streams are exhausted. [DATA]
* Audio volume: the movie's sound volume follows the user's **music volume** setting (0–1024 scale;
  see `06-audio.md`). If that setting is 128 or less, the movie plays silently; otherwise the
  setting's value (129–1024) is handed to the movie player as its volume [STATIC]. How the player
  maps that value to loudness was not determined [OPEN]; use a linear gain of value / 1024.

## 4. Container and codecs

The file is an Electronic Arts "SCxl" chunked stream, as used by many EA titles of the period
(public tools call the container `ea`). It holds EA TQI video (4:2:0, 320×480, 15 fps) and EA ADPCM
audio (22050 Hz, 2 channels). The whole file decodes without errors with a public EA decoder.
[DATA]

### 4.1 Chunk structure

Every chunk starts with an 8-byte header:

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 4 | char[4] | tag | Chunk type |
| 0x04 | 4 | u32 | size | Total chunk size **including** this header (little-endian) |
| 0x08 | size−8 | | payload | |

Chunks follow each other back-to-back to end of file. INTRO.TGQ contains, in file order: [DATA]

| Tag | Count | Size range | Payload |
|-----|-------|-----------|---------|
| `SCHl` | 1 | 40 | Stream header: `PT` + 2-byte platform (0 = PC) + tagged audio parameters (below) |
| `SCCl` | 1 | 12 | u32 = number of `SCDl` audio chunks (0x440 = 1088) |
| `pIQT` | 1051 | 2124–24000 | One TQI video frame each |
| `SCDl` | 1088 | 1580–1732 | One block of audio each |
| `SCEl` | 1 | 8 | End of stream (empty) |

Video and audio chunks are interleaved in presentation order. [DATA]

### 4.2 Audio header (`SCHl` "PT" patches)

After `PT` and the 2-byte platform the header is a list of tagged values: a tag byte, a length byte
L, then an L-byte **big-endian** value. Tag 0xFD opens the audio sub-header and tag 0xFF ends the
header. Tags present in INTRO.TGQ (public knowledge of the EA format): [DATA][INFERRED]

| Tag | Meaning | Value in INTRO.TGQ |
|-----|---------|--------------------|
| 0x85 | Total number of sample frames | 1,599,500 (= 72.54 s at 22050 Hz) |
| 0x82 | Channels | 2 |
| 0x83 | Compression | 7 = EA ADPCM (EA-XA family) |
| 0x84 | Sample rate | absent → default 22050 Hz |
| 0x8A | End of sub-header | 4 bytes, 0 |

Every tag, known or not, is followed by its length byte L and L value bytes, so unknown tags
(0x06 etc.) are skipped by length. Tags may follow 0x8A before the final 0xFF; they are skipped the
same way. Parsing stops at 0xFF. [INFERRED from public format knowledge; INTRO.TGQ has none after
0x8A]

### 4.3 Audio data (`SCDl`)

Each audio chunk payload is: u32 number of sample frames N in this chunk; then for each channel the two
starting history samples (s16 current, s16 previous — left pair then right pair); then ⌈N/28⌉ groups.
Each group has one byte of predictor indices (high nibble left, low nibble right), one byte of shift
values (high nibble left, low nibble right) and 28 bytes, each holding one left (high nibble) and one
right (low nibble) 4-bit code. Decoding uses exactly the predictor table and per-sample prediction of
`06-audio.md` §2.3 with shift s taken from the shift byte; the history is re-seeded from the chunk
header at the start of every chunk. [STATIC][DATA: all 1088 chunks decode]

The movie player's arithmetic differs from the XA rules of `06-audio.md` §2.4 in two ways
[STATIC]:

* Rounding: with d the scaled 4-bit code as in `06-audio.md` §2.4, the sample is
  y = ⌊(256·d + C1·h1 + C2·h2 + 128) / 256⌋ in integer arithmetic, i.e. ties round **up**
  (towards +∞), not to even.
* Saturation: y is clamped to −32768…32767 before it is output and before it becomes history.

A bit-exact clone uses these rules for the movie audio; the difference from the XA rules is
inaudible.

### 4.4 Video data (`pIQT`, EA TQI)

Each `pIQT` payload starts with u16 width (320) and u16 height (480), followed by a quantiser/scale
byte pair and the compressed frame. TQI is an intra-only, MPEG-1-like codec: the frame is coded as
16×16 macroblocks of six 8×8 DCT blocks (4 luma, 2 chroma, 4:2:0) using EA's variable-length coding
(shared with EA's "TGQ"/"TGV" family), with dequantisation by a fixed intra matrix scaled by the
per-frame quantiser, inverse DCT and YCbCr→RGB conversion. Every frame is a key frame; there is no
inter prediction. Frame order equals file order. [INFERRED from public format knowledge; DATA:
1051/1051 frames decode with a public EA TQI decoder]

This specification does not define the TQI bitstream (VLC tables, intra matrix, quantiser bytes,
colour conversion) [OPEN]. An implementation must use an existing EA TQI decoder; the open-source
ones are LGPL-licensed, which is a licensing decision for the project to record. Because the
movie is optional (§1), an implementation may also skip it.

### 4.5 Timing

Video frames are displayed at a fixed 15 fps from the start of playback; audio plays continuously
from the first `SCDl`. Playback ends after the last chunk (`SCEl`) once the audio has finished.
[INFERRED]

## 5. Persisted state

None. [STATIC]

## Open questions

* Whether keys other than Escape, or a mouse click, end the intro.
* Source of the alternative data directory (§1) and the player's volume curve (§3).
* The TQI bitstream itself (§4.4).
* Exact horizontal scaling method used by the original player (pixel doubling vs. filtered) — any
  method is visually acceptable.
* Whether the original player synchronises video to the audio clock or to wall time.
