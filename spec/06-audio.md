# 06 — Audio

Scope: the `.XA` audio codec, WAV usage, the `AUDIO.INI` sound table, ambient ("freshness")
sound, building, interface and event sounds, music, mixing/volume and localised sounds.

Related documents: `27-platform.md` (sound preferences and registry), `25-rendering.md` (zoom
levels), `09-ui-screens.md` (sound option controls), `07-video.md` (movie audio), `05-text-and-fonts.md`
(language/country selection used by localised sounds).

## 1. Files

All audio lives under `Res/Sound/` (path relative to `Apps/`). [DATA]

| Path | Contents |
|------|----------|
| `AUDIO.INI` | The sound table (plain text INI, CRLF). See §4. |
| `MUSIC/` | 19 music files (`.XA`; 18 used, §4.9) and one loop `3KLOOP.WAV` |
| `Sfx/interface/` | UI one-shots (zoom, rotate, bulldoze, query, errors, dispatch) |
| `Sfx/buildings/` | Building "query" and "sparse" sounds |
| `Sfx/disasters/` | Disaster one-shots and loops |
| `Sfx/petitioners/` | Crowd reactions (boo, cheer, polite) |
| `Sfx/VEHICLES/` | Vehicle drive-by, sirens, horns `CARHRN1..8` |
| `Sfx/MBE/` | Add-on ("Unlimited") one-shots |
| `Sfx/localized/` | Region-specific variants |
| `Sfx/ZONES/<area>/` | Ambient schedules (`*.FSC`) plus their samples, one folder per ambient area |
| `Sfx/AUTOPLAY/`, `Shared/SYS/` | Launcher/system click and start-up sounds (WAV) |

Shipped totals: 814 `.XA` files, 30 `.WAV` files, 25 `.FSC` files. File-name case is
inconsistent between the INI and the disk (e.g. `Sfx\Interface\zmin.xa` vs `Sfx/interface/ZMIN.XA`);
lookups must be case-insensitive and must accept `\` as a path separator. [DATA]

## 2. The XA codec ("Maxis XA", EA-XA ADPCM)

`.XA` is a 24-byte header followed by EA-XA 4-bit ADPCM blocks. Public decoders know this
format as the Maxis variant of EA-XA ADPCM. [DATA][STATIC]

### 2.1 Header

| Offset | Size | Type | Field | Meaning |
|--------|------|------|-------|---------|
| 0x00 | 2 | char[2] | magic | `XA` (0x58 0x41). Only these two bytes are checked. |
| 0x02 | 2 | u8[2] | variant | Not used by the game. Shipped values: `49 00` ("I", 513 files), `4A 00` ("J", 176), `00 00` (105), `6F 00` (20). |
| 0x04 | 4 | u32 | outSize | Size in bytes of the decoded 16-bit PCM (= frames × channels × 2). Decoding stops after this many bytes. |
| 0x08 | 2 | u16 | formatTag | Must be 1; any other value rejects the file. |
| 0x0A | 2 | u16 | channels | 1 or 2. Other values produce silence. |
| 0x0C | 4 | u32 | sampleRate | Hz. All shipped files: 22050. |
| 0x10 | 4 | u32 | avgBytesPerSec | Of the decoded PCM; informational. |
| 0x14 | 2 | u16 | blockAlign | Of the decoded PCM (2 × channels); informational. |
| 0x16 | 2 | u16 | bitsPerSample | Of the decoded PCM; informational. Output is always 16-bit signed. |
| 0x18 | … | | blocks | ADPCM data to end of file. |

The bytes from 0x08 to 0x17 are a WAVE "fmt " body describing the *decoded* stream. [STATIC][DATA]
One shipped file states 11025 Hz / 8 bits in this header; the game decodes it like every other file
(16-bit output at the stated rate). [DATA]

Shipped mix: 784 mono and 30 stereo files; music tracks are stereo, except the `*M.XA` tracks, which
are mono. [DATA]

### 2.2 Block structure

The stream is a sequence of blocks. Each block holds 28 sample frames.

* **Mono block (15 bytes):** 1 header byte, then 14 data bytes.
* **Stereo block (30 bytes):** header byte for the left channel, header byte for the right channel,
  then 14 pairs of data bytes, each pair being (left byte, right byte).

Header byte: high nibble = predictor index *p* (0–3 used), low nibble = shift *s* (0–15).

Data byte: the **high nibble is the earlier sample**, the low nibble the later one. Each data byte
therefore yields two consecutive samples of one channel.

Expected file size = 24 + ceil(frames / 28) × 15 × channels, where frames = outSize / (2 × channels).
801 of 814 files match exactly; 13 are slightly short (the decoder then stops at end of file and
the missing tail is silence). [DATA]

### 2.3 Predictor table

| p | c1 (×1) | c2 (×1) | c1 × 256 | c2 × 256 |
|---|---------|---------|----------|----------|
| 0 | 0.0 | 0.0 | 0 | 0 |
| 1 | 0.9375 | 0.0 | 240 | 0 |
| 2 | 1.796875 | −0.8125 | 460 | −208 |
| 3 | 1.53125 | −0.859375 | 392 | −220 |

[STATIC] (These are the standard EA-XA coefficients.) Predictor indices above 3 do not occur in the
shipped data. [DATA]

### 2.4 Decoding one sample

Per channel keep two history values h1 (previous output) and h2 (the one before), both starting at 0
at the start of the file and **carried across blocks** (they are never reset per block).

For each 4-bit code n (0–15):

1. Signed value v = n if n < 8, else n − 16 (range −8..7).
2. Step d = v × 2^(12 − s) if s ≤ 12; d = 0 if s is 13, 14 or 15.
3. x = c1[p] × h1 + c2[p] × h2 + d, evaluated in floating point (exact, since all coefficients are
   multiples of 1/256).
4. Round x to the nearest integer, **ties to even**. Call the result y.
5. h2 ← h1; h1 ← y. The history keeps the full integer value (no clamping).
6. The output sample is the low 16 bits of y as a signed 16-bit value (no saturation).

Equivalently in fixed point: y = round((256·d + C1·h1 + C2·h2) / 256) with C1, C2 the ×256 table
values. [STATIC]

Notes for implementers:

* Some public decoders use the same structure but round ties upward and saturate to 16 bits.
  The algorithm above with ties-up rounding matches such a decoder bit for bit on every file
  tested (21/21 files sampled across all folders, 8.4 M samples) [DATA]. With ties-to-even the two differ by a few LSBs (max 24–28 LSB through the recursive filter); the
  difference is inaudible. Either rounding is acceptable; ties-to-even is the bit-exact original
  behaviour.
* No shipped sample overflows 16 bits during decoding (overflow count 0 over all 814 files), so
  the saturate-vs-wrap difference is moot for shipped data. [DATA]
* Shift values 13–15 never occur in shipped data (observed range 0–12). [DATA]
* Streaming in chunks of any size gives the same output. [STATIC]

Validator: all 814 `.XA` files decode without error; see the coverage note in §11. [DATA]

## 3. WAV usage

`.WAV` files are standard RIFF/WAVE PCM (format tag 1). Shipped variants: 21 stereo 16-bit
22050 Hz, 8 mono 16-bit 22050 Hz, 1 mono 8-bit 22050 Hz. [DATA] They are used for **looping**
sounds (fading loops in `[Fading]`, the audio-zoom-4 ambience bed (§5.1 numbering) `Sfx\Zones\AMBZOOM4.WAV`, the underground
loop `SUBLOOP.WAV`, disaster loops such as earthquake, UFO, whirlpool, the audio-zoom-3 loop
`ZM3LOOP.WAV`, the music fallback loop `Music\3kloop.wav`) and for the shell's button/start-up clicks
(`Shared/SYS`, `Sfx/AUTOPLAY`). An item's file name decides the decoder: `.xa` → §2, `.wav` → PCM.
[DATA][INFERRED]

## 4. AUDIO.INI

### 4.1 Location and syntax

`Res/Sound/AUDIO.INI` (the program builds the path from `Res\Sound\` + `audio.ini`). INI
syntax: `[Section]` lines, `key=value` lines, `;` starts a comment line, blank lines ignored.
Within a value, fields are separated by commas; text fields are enclosed in double quotes. The
file is read by the game's own INI reader, not by the Windows profile functions [STATIC]. Spaces
around unquoted fields are not significant. Whether tabs are also trimmed is [OPEN]; follow the
`02-containers.md` §5.3 grammar (only spaces are trimmed). Tabs **inside** quotes are always kept. Some shipped lines contain stray tabs inside
quoted file names (e.g. `"Sfx\Buildings	\Defense.xa"`, `"...CheerB2.xa	"`); such files then fail to
open and the sound is silently absent. [DATA][STATIC]

Keys in the sound sections are **sound IDs**: decimal (`34`) or C-style hex (`0x100`). The sound ID
is what the game code uses to trigger the sound. Valid IDs are 1–511; entries outside this range are
ignored. If two lines in the file define the same ID, the later line **replaces** the earlier one
(subject to the locale filter in §4.3). [STATIC][DATA]

The second field of every sound entry (the quoted name) is a human-readable label only; the game
never refers to sounds by name. [STATIC]

Sections are processed in this order: `1ShotSfx`, `Sparse`, `Disaster`, `Fading`, `Actor`, `Music`.
`[Options]` is read separately; `[3D1ShotSfx]` is empty and unused; `[Events]` is a reference list
for developers and is not needed to play sounds (see §4.9). [STATIC][DATA]

### 4.2 `[Options]`

All values are integers read with base 10.

| Key | Units | Meaning |
|-----|-------|---------|
| `UseC2HCache` | flag | 1 = copy music files from CD to a hard-disk cache (`CachePath`, `CacheSize` bytes). Shipped 0; ignore on modern systems. |
| `MusicDataDir` | path | Alternative music root used only with the CD cache. Ignore. |
| `CachePath`, `CacheSize` | path, bytes | CD cache location/size. Ignore. |
| `ZoomLoopVol` | 0–1024 | Volume of the zoom-level ambience bed loop. |
| `BuildingRandMultiplier` | divisor | Scales the trigger probability of `[Sparse]` building sounds (see §6). |
| `AmbienceStartFadeAtPercent` | percent | Ambience begins to fade when the relevant ambience weight drops below this percentage. |
| `AmbienceMinimumFadedVolume` | 0–1024 | Floor volume when ambience is faded. |
| `MusicPlayerSongDurationMsec` | ms | Maximum play time of one song (0 or missing = keep default). |
| `MusicPlayerSongDelayMsec` | ms | Silence between the end of one song and the start of the next. |
| `MusicStartDelayAfterStop` | ms | Delay before music resumes after it was stopped (e.g. after a screen change). |
| `FreshnessIntroDurationMsec`, `FreshnessOutroDurationMsec` | ms | Fade-in / fade-out time when an ambient schedule (FSC) starts or stops. |
| `FreshnessMaxXSpeed` | units/s | Scroll speed above which ambient selection does not change. |
| `FreshnessSelectionAreaMinMsec` | ms | Minimum time an ambient area stays selected before switching. |
| `TrafficDensityMin`, `TrafficDensityMultiplier` | count, factor | Map traffic density on screen to the number of traffic ambience voices. |
| `VehicleHonkDivisor` | divisor | One honk per this many vehicles. |
| `VehicleMinDistance`, `VehicleMaxDistance` | screen px | Distance attenuation range for vehicle sounds. |
| `VehicleAttenuation`, `HonkAttenuation`, `BuildingsInAmbienceAttenuation` | 0–1024 | Volume reduction for those classes. |
| `VehicleMinZoom`, `HonkMinZoom`, `BuildingsInAmbienceMinZoom` | audio zoom 1–5 (§5.1) | Minimum zoom level at which the class is audible. |
| `VehicleEffectsLevel`, `VehicleEffectsPreset`, `VehicleEffectsTallBuildingCrit` | | DirectSound3D/EAX reverb settings for vehicles. Optional. |
| `ForceCrimeTempo`, `ForceAmbienceQuadrant` | | Debug overrides; 0 = off. |

[STATIC] for the key list (all keys are read from `[Options]`); the units column is [INFERRED]
from names and usage except the two music keys, which are [STATIC]. Shipped values are in the file.

### 4.3 Locale filter (localised sounds)

Several sections accept an optional *locale* field (the INI header comment documents it). Its
position is fixed per section and is given in that section's field table: in `[Actor]` it is the
4th field (§4.7); elsewhere it follows the section's fixed fields.

| Locale | Sound is used when |
|--------|--------------------|
| 0 or absent | always |
| 1 | the installation's country is in North America |
| 2 | the installation's country is not in North America |

"North America" means country ID 8 or 54 (the country IDs used by the language/country service;
54 is the default, United States; 8 is Canada [INFERRED]). The country comes from the registry value
`Country` under `HKLM\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited` (DWORD, accepted if below
57), else the default 54. [STATIC]

An entry whose locale does not match is ignored *before* it can replace an existing entry; therefore
pairs such as two lines with key `34` (locale 1 and 2) yield exactly one active definition. The
`Sfx/localized/` folder holds region variants referenced this way (e.g. a European siren). [STATIC][DATA]

### 4.4 `[1ShotSfx]` — one-shot effects

Value fields (after the key):

| # | Field | Meaning |
|---|-------|---------|
| 1 | name | Label (quoted). |
| 2 | pri | Priority (higher wins voice stealing; shipped all 32). |
| 3 | vol | Volume 0–1024 (1024 = full). |
| 4 | load | 1 = preload into memory at start-up; 0 = load on demand. |
| 5 | stream | 1 = stream from disk while playing. |
| 6 | intrpt | Duplicate rule when the same ID is triggered while still playing: 1 = restart (interrupt) the playing instance; 0 = let the playing instance continue / overlap according to the default rule. |
| 7 | PathsAreAbsolute | 0 = file name is relative to `Res\Sound\`; 1 = absolute. |
| 8 | filename | Quoted. |
| 9 | locale | Optional, see §4.3. |

The header comment of the INI lists an older order (`bIsSteamed, bIsPreloaded, DupType`); the
section comment line (`;pri,vol,load,stream,intrpt,PathsAreAbsolute,filename[,language]`) matches
the parser. [STATIC][DATA]

### 4.5 `[Fading]` — sounds with a one-shot head and a fading loop

Fields: name, pri, vol, preload, autofade, UseAmbienceVol, PathsAreAbsolute, one-shot file, loop file.
The one-shot (may be `""`) plays first, then the loop plays until the sound is stopped, at which point
it fades out. `UseAmbienceVol` = 1 puts the item in the ambience volume category instead of the
effects category. [DATA][INFERRED]

### 4.6 `[Disaster]`

Fields: name, pri, vol, SpatialScheme, PathsAreAbsolute, filename (may be `""`), fadingSfxId.

| SpatialScheme | Meaning |
|---------------|---------|
| 0 | positioned in the world; loudness depends on disaster severity |
| 1 | not positioned; audible only in the surface (above-ground) view |
| 2 | not positioned; audible in all views |

`fadingSfxId` names a `[Fading]` entry started together with the disaster sound (0 = none). Rule
from the file: a disaster's ID must be lower than its fading ID so that shutdown (in ID order) stops
the disaster sound before the loop. [DATA]

### 4.7 `[Actor]` — moving sound sources (vehicles)

Fields: name, pri, vol, locale, type, PathsAreAbsolute, drive-by file, special-1 file, special-2 file.
`type`: 0 = special-2 is the horn (honk) sound; 1 = special-2 is an altitude sound (aircraft);
2 = neither. Drive-by is the looping pass-by sound positioned at the vehicle. Locale as §4.3 (used
for region-specific sirens). Vehicles are audible from zoom `VehicleMinZoom` upward, attenuated by
screen distance between `VehicleMinDistance` and `VehicleMaxDistance`; one honk per
`VehicleHonkDivisor` vehicles. [DATA][INFERRED] See `26-automata.md` for which automata exist.

### 4.8 `[Sparse]` — building sounds

Fields: name, pri, vol, numsamples, PathsAreAbsolute, schedule-info, then `numsamples` groups of
{filename, vol, triggerval}. In the shipped file every entry has one sample, schedule-info 0, sample
volume 100 and trigger value 600 (IDs 0x100–0x110, generic building classes) or 8000 (IDs
0x180–0x19D, specific landmarks/rewards). [DATA] See §6.

### 4.9 `[Music]`

Fields: name, type, filename, demofilename, highPerformance, inSongList, PathIsAbsolute, enabled.

| Field | Meaning |
|-------|---------|
| type | 0 = sampled track (XA/WAV); 1 = interactive "freshness" music (unused in shipped data). |
| filename | Track file relative to `Res\Sound\`. |
| demofilename | Unused (`""`). |
| highPerformance | 1 = only on fast machines. Shipped 0. |
| inSongList | 1 = eligible for the automatic playlist; 0 = only played on explicit request. |
| PathIsAbsolute | 0 = relative path. |
| enabled | 1 = enabled by default (the user can toggle songs in the options screen). |

Shipped: 18 active entries with keys 0x01–0x12 [DATA]. Keys 0x01 and 0x0A are defined by the last
two lines of the section (`3kse5.xa` and `3kse4.xa`). Earlier lines for those keys are
commented out, so they define nothing. Of the 19 shipped music files, `3KP5M.XA` is referenced only
by a commented-out line and is never played. The commented-out 0x01 line names `3kg2.xa`, which is
not shipped. Track 0x0F (`Music\SC2KBug.xa`, the SimCity 2000
theme) has inSongList = 0: it never plays in the normal rotation and is played only when explicitly
triggered (an easter-egg event). [DATA][INFERRED] See §7.

### 4.10 `[Events]`

A developer reference listing event names and numeric event codes accepted by the sound engine (e.g.
1000–1008 options, 3000–3002 listener position/orientation/zoom, 10000–10017 screen/city lifecycle,
50–59 advisor/petitioner/budget windows, 200 explosion, 201 disaster warning, 0–8 generic sound
play/stop/update/kill/special/load/unload). The engine does not need to read this section. It is
useful as the list of hooks a reimplementation must provide. [DATA][INFERRED]

## 5. Ambient sound (zones, zoom levels)

### 5.1 Ambient areas and schedule files

Ambient sound is produced by "freshness" schedules: one `.FSC` text file per ambient area, each in
its own folder under `Res/Sound/Sfx/ZONES/`. The program knows these areas (schedule file, folder):
[STATIC]

| Area | Schedule | Folder |
|------|----------|--------|
| residential zones | `residential.fsc` | `sfx\zones\residential\` |
| commercial zones | `commercial.fsc` | `sfx\zones\commercial\` |
| industrial zones | `industrial.fsc` | `sfx\zones\industrial\` |
| agriculture | `Agricult.fsc` | `sfx\zones\Agri\` |
| nature (trees, empty land) | `Nature.fsc` | `sfx\zones\nature\` |
| desert / jungle variants of nature | `Desert.fsc`, `Jungle.fsc` | `sfx\zones\nature\` |
| water | `water.fsc` | `sfx\zones\Water\` |
| wind (high zoom) | `Wind.fsc` | `sfx\zones\Wind\` |
| arctic wind | `Arctic.fsc` | `sfx\zones\wind\` |
| traffic | `traffic.fsc` | `sfx\zones\Traffic\` |
| construction | `construction.fsc` | `sfx\zones\Construction\` |
| seaport | `Seaport.fsc` | `sfx\zones\Seaport\` |
| airport | `Airports.fsc` | `sfx\zones\Airport\` |
| abandoned buildings | `Abandoned.fsc` | `sfx\zones\Abandoned\` |
| garbage | `Garbage.fsc` | `sfx\zones\Garbage\` |
| crime | `Crime.fsc` | `sfx\zones\Crime\` |
| unpowered | `Unpwrd.fsc` | `sfx\zones\Unpowered\` |
| pipes view | `pipes.fsc` | `sfx\zones\pipes\` |
| subway view | `subway_v.fsc` | `sfx\zones\subway_v\` |
| audio-zoom-3 city | `zm3city.fsc` | `sfx\zones\Zoom3City\` |
| audio-zoom-3 generic | `zm3.fsc` | `sfx\zones\Zoom3\` |

Additional folders (`PANIC`, `RIOT`, `trafficjam`) hold samples triggered by disasters/events rather
than by an area schedule. They have their own schedules (`PANIC.FSC`, `RIOT.FSC`, `trafficjam\jam.fsc`).
With the 22 area schedules above, these give the 25 shipped `.FSC` files [DATA].

**Zoom numbering in audio.** The audio system numbers zooms **1–5**, with 5 the closest. Audio
zoom n is rendering zoom z = n − 1 of `25-rendering.md` §2.1 (z = 0 farthest … 4 closest). This
numbering is used by the `*MinZoom` keys (§4.2) and by the sound names (`Zoom3Loop`, `Zoom4Loop`,
`Zoom5Loop` play `ambzoom3/4/5.WAV`; `EarthquakeCarAlarmHellZoom3`–`Zoom5`) [INFERRED, high
confidence, from those names]. The farthest zooms (audio 1–2) use the wind schedules; audio zooms
3–5 add the matching `ambzoomN` bed loop. [DATA][INFERRED]

### 5.2 FSC schedule format

Plain text, CRLF, tab-separated. Lines starting with `#` are column-header comments. [DATA]

1. Line 1: `version 7`.
2. A header row: vol, pri, min, max, rows, cols, tempo, BPB, SelX, SelY, QuanX, QuanY, DifX, DifY.
   * vol (0–1024), pri; min/max = minimum/maximum simultaneous voices; rows × cols = the size of the
     selection grid of sound cells; tempo in beats per minute and BPB (beats per bar) define the
     scheduling clock; SelX/SelY = the current/initial selection position in the grid;
     QuanX/QuanY = quantisation of selection movement; DifX/DifY = selection jitter. [INFERRED]
3. rows × cols cell rows, each with columns: vol, rand?, LR, FB, rand?, fin, fout, dly, rand?, loop,
   loop?, quant, prob, pitchL, pitchH, fast?, groupID, stereo?, filename, exclusion cells.
   * filename `NONE` = empty cell. Files are relative to the schedule's folder.
   * vol 0–1024; LR/FB = pan left-right and front-back (512 = centre); fin/fout = fade in/out;
     dly = delay; loop = loop count; quant = beat quantisation; prob = trigger probability weight
     per beat (0–15 observed); pitchL/pitchH = random pitch range in semitones; groupID = cells of
     the same non-zero group do not play simultaneously. [INFERRED]

Cell picking (observed behaviour of the scheduler): when a voice is free, the scheduler picks a cell
at random among the *ready* cells (non-empty, not currently playing, not excluded by a playing cell)
inside a window centred on the current selection position and extending by the jitter (DifX/DifY)
in each direction, clipped to the grid (readiness is defined below); the pick uses the audio random generator of §7.3 (index =
r mod count). If no window cell is ready, the search falls back to scanning the whole grid from a
random starting cell, wrapping around. The start cell is drawn with the process-wide C-runtime
generator of `14-sim-clock.md` §8 (`rand()` mod rows, `rand()` mod columns). That generator is
shared with the simulation's shuffles, so this draw need not be reproduced exactly.
[STATIC]

**Cell readiness** [STATIC]. A cell is *ready* only if all of the following hold:

1. it has a sound (filename not `NONE`) and has not been disabled;
2. it is not currently playing;
3. it lies inside the current selection window (above);
4. it is not the cell that was started most recently;
5. if its groupID is non-zero, no other cell with the same groupID is currently playing (a cell
   that is fading out does not count);
6. none of the cells in its exclusion list (row/column pairs inside the grid) is currently playing
   (cells fading out do not count);
7. it passes its probability test. Let p = the cell's prob value clamped to 0–20.
   * p = 0: never ready.
   * p = 20: always ready.
   * Otherwise the cell is ready when R < T(p) / D, where R is a C-runtime `rand()` value
     (0–32767), D is the scheduler's density divisor (10 unless set otherwise), and T is the
     fixed table below. If the scheduler is looking for a replacement in a specific group
     (see beat processing) and the cell belongs to that group, the test is skipped and the cell
     is ready.

| p | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T(p) | 0 | 600 | 1500 | 3000 | 7500 | 12000 | 15000 | 18000 | 21000 | 25500 | 32767 |

| p | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|
| T(p) | 36000 | 45000 | 58000 | 67500 | 90000 | 120000 | 210000 | 300000 | 40000000 | (always) |

With D = 10, p ≤ 10 gives a per-attempt chance of about T(p)/327,680 (0.18 % for p = 1 up to
10 % for p = 10). p ≥ 19 always passes. [STATIC]

**Beat clock** [STATIC]. The schedule's tempo (beats per minute) and BPB (beats per bar) drive a
clock. Beat number k (counted from the start of the schedule) falls at time
t(k) = k × 60000 / tempo milliseconds after the schedule started. On every update, all beats with
t(k) ≤ now are processed in order. If the clock passes 24 hours (86,400,000 ms) it is restarted
at beat 0. A beat processed more than 7 ms late is marked "late" (it may be used to skip work).

**Beat processing** [STATIC]. Picks happen only on two beats of each bar: the beat whose
position in the bar is BPB − 1, and the beat whose position is BPB − 6 (when BPB > 6). On such a
beat:

1. *Retire.* If voices are playing, at least `min` of them are playing, and the schedule allows
   rotation, one playing cell is chosen and faded out with its fade-out time. Its groupID becomes the
   *preferred group* for this beat.
2. *Replace.* If fewer than `max` voices are playing, a ready cell is picked (window first, then
   the whole-grid fallback above). The preferred group is passed in, so a cell of the same group
   is accepted without its probability test. If no cell is found and the voice count is below
   `min`, the pick is retried once. The chosen cell starts with its fade-in, volume, pan, pitch
   and loop settings, and becomes the "most recently started" cell.
3. *Fill.* If still fewer than `max` voices are playing, one more pick is made without a preferred
   group. If that fails and the count is below `min`, it is retried once.

So `min` is the number of voices the bed tries to keep, and `max` is a hard cap per schedule.
The schedule never has more than `max` cells playing. [STATIC; the "rotation allowed" condition
and the exact bar-position counter are INFERRED from structure, medium confidence]

The selection position (SelX/SelY) and window size (DifX/DifY) are set from the schedule header
when the schedule is loaded. [OPEN] Whether the game moves the selection position at run time in
response to city state; no such caller was found, so the window is treated as fixed per schedule.
Variety comes instead from switching between areas (§5.3) and from the probability test.

### 5.3 Area selection

The ambient area is chosen from what is visible: the dominant zone/terrain type under the view at the
current zoom level, with special areas taking over in the matching views (pipes view → `pipes`,
subway view → `subway_v`) or conditions (unpowered, abandoned, crime, garbage, construction). A
newly selected area must persist for `FreshnessSelectionAreaMinMsec` before the switch, switches fade
with the intro/outro durations, and no switching happens while scrolling faster than
`FreshnessMaxXSpeed`. Ambience volume fades from full to `AmbienceMinimumFadedVolume` when the area's
share of the view drops below `AmbienceStartFadeAtPercent`. [INFERRED][OPEN — exact weighting]

## 6. Building sounds (query and sparse)

* **Query sounds.** When the user queries a building with the query tool, the matching
  `UiQuery…` one-shot plays (generic per class, e.g. `UiQueryGenericCommercial`,
  `UiQueryGenericWater`, `UiQueryGenericAbandoned`, `UiQueryTreeGeneric`, `UiQueryGenericLandfill`).
  The query sound ID per occupant is an occupant property (see `13-buildings-occupants.md`).
  [DATA][INFERRED]
* **Sparse sounds.** Buildings with a `[Sparse]` ID periodically emit their sample while visible at
  zoom ≥ `BuildingsInAmbienceMinZoom`, attenuated by `BuildingsInAmbienceAttenuation`. On each
  scheduling tick a visible building plays with probability triggerval / `BuildingRandMultiplier`
  (shipped 600/10000 for generic classes, 8000/10000 for landmarks). [INFERRED][OPEN]

## 7. Music

### 7.1 Playlist

The music player cycles songs automatically while music is enabled: [STATIC]

1. A song is **eligible** when it is in the song list (`inSongList` = 1) and currently enabled (the
   user's per-song checkbox, default from the `enabled` field).
2. The player keeps a counter P of songs started so far, and each song remembers the value of P when
   it was last started (L, initially 0).
3. To pick the next song: let N = number of eligible songs (if N = 0, nothing plays). For each
   eligible song compute its age A = P − L. Let M = the largest age among eligible songs. Let the
   threshold T = min(M, ⌊2N/3⌋). The candidates are the eligible songs with A ≥ T, in INI order.
   Pick candidate index r mod C, where C is the number of candidates and r is the next output of the
   audio random generator (§7.3).
4. P ← P + 1; the chosen song's L ← P; start it at the music volume.
5. When the song ends (or fails to start), a delay timer starts. When more than
   `MusicPlayerSongDelayMsec` milliseconds have elapsed, step 3 runs again.

Effect: recently played songs are excluded until roughly two-thirds of the list has played, giving a
shuffle without near repeats.

### 7.2 The SC2KBUG track

`Music\SC2KBug.xa` (ID 0x0F) is excluded from the rotation (`inSongList` = 0) and is only played when
triggered directly through the song-audition event (options screen) or a scripted easter egg.
[DATA][INFERRED]

### 7.3 Audio random generator

A 32-bit linear congruential generator: state ← state × 1103515245 + 12345 (mod 2^32). The output
is bits 16–47 of the full 64-bit value state_old × 1103515245 + 12345 (i.e. that 64-bit value shifted
right by 16, truncated to 32 bits). [STATIC] Its seed is not persisted; it is not part of the
simulation and need not match the original.

### 7.4 Fallback loop

`Music\3kloop.wav` is a short loop used by the shell when no song can be played (e.g. on the front
end). [STATIC][INFERRED]

## 8. Mixing, categories and volume

* Volumes are integers 0–1024 everywhere (1024 = unity gain). An item's effective volume is
  item volume × category volume / 1024 (and × FSC cell volume / 1024 for ambience). [INFERRED]
* Categories with user settings: **music**, **sound effects** and **ambience**, stored in this order
  in `SC3.cfg` (`27-platform.md` §4). First-run default: 1024 each, all switched on. Each also has an
  on/off switch; "off" forces the category volume to 0. A stored volume outside 0–1024 is replaced at
  start-up by a repair value: music 1000, sound effects 700, ambience 900. These repair values are not
  the first-run defaults. [STATIC] The options events
  also define separate wind and vehicle volumes and a global mute (event codes 1004–1007). [DATA]
* Voice limits. Ambience is capped per schedule: an FSC schedule never has more than its `max`
  cells playing, and it tries to keep at least `min` playing (§5.2) [STATIC]. The sound layer has
  no shipped INI key for a global voice limit, and no fixed global voice pool was found
  [STATIC, negative result]. The streaming service processes at most 10 streaming sounds per
  service pass [STATIC]. [OPEN] Whether one-shot effects ever steal voices. An implementation
  should mix all requested voices (a modern mixer has no hardware limit). If it must cap them,
  it should drop the lowest-priority voice and, on a tie, the oldest. Every item has a priority
  (shipped value 32 for nearly all) [DATA], so this rarely matters. [INFERRED]
* Panning: positioned sounds (vehicles, spatialised disasters, sparse buildings) are panned by their
  horizontal screen position relative to the view centre and attenuated by distance; the listener
  position/orientation/zoom are updated by events 3000–3002 whenever the view moves. [INFERRED]

## 9. Localised sounds

Localisation of audio is by **country**, not by language: the locale field (§4.3) selects between
North-American and rest-of-world variants (e.g. police/fire dispatch and emergency sirens). There are
no per-language voice folders. [STATIC][DATA]

## 10. Event → sound table

### 10.1 Request model

Game code asks the audio system for sounds by **`AUDIO.INI` sound ID** (§4.1) together with an
**action**. The audio system also receives a small set of *notifications* (window closed, query at a
tile, building placed at a tile, explosion at a tile, disaster warning, …) and chooses the sound
itself. An implementation needs one entry point per row of the tables below; the numeric action
and notification codes of the original are internal and are not needed. [STATIC]

| Action | Effect on the item | Used with |
|--------|--------------------|-----------|
| play / start | `[1ShotSfx]`: play once. `[Fading]`: play the head, then loop. `[Disaster]`: start the disaster sound and its fading companion. | everything |
| stop | stop the item (a fading loop fades out). | loops, disasters |
| set level | set the loop/disaster intensity; the parameter is a level 0–1024 for fading tool loops, a percentage 0–100 for fire (`21-disasters.md` §5). | terrain loops, fire |
| set audible | parameter 0 = silent (fade out but keep running), non-zero = audible; for some disasters the non-zero value is a volume percentage (75). | terrain loops, disasters |

Rules common to every request [STATIC]:

* A request for an ID that has no active definition in `AUDIO.INI` (absent, or removed by the
  locale filter of §4.3) does nothing. Several shipped requests hit such IDs (10, 102, see below);
  they are silent in the original.
* Zoom-in (ID 1) is ignored when the audio zoom (§5.1) is already 5; zoom-out (ID 2) is ignored when
  it is 1.
* "Positional" below means the request carries a tile (x, z) and the sound is placed in the
  world (panned/attenuated as in §8). All other rows are plain screen sounds.

### 10.2 Interface and tools

| Event | Section / ID | Action | Positional | Tag |
|-------|--------------|--------|------------|-----|
| Zoom in / zoom out (buttons and keyboard/scroll zoom) | `[1ShotSfx]` 1 / 2 | one-shot | no | [STATIC] |
| Rotate view (either direction) | `[1ShotSfx]` 4 | one-shot | no | [STATIC] |
| Generic button click (control panels, menus, front end, many dialogs) | `[1ShotSfx]` 12 `UiButtonClick` | one-shot | no | [STATIC] |
| Check box / toggle click in dialogs (budget, ordinances, options, graphs, …) | `[1ShotSfx]` 13 `UiCheckboxClick` | one-shot | no | [STATIC] ids; [INFERRED] which controls |
| Small button click (scenario dialogs, some panels) | `[1ShotSfx]` 146 `UiButtonClickSmall` | one-shot | no | [STATIC] |
| Close button of a window, panel or dialog clicked (also the query window's close) | `[1ShotSfx]` 56 `CloseWindow` | one-shot | no | [STATIC] |
| Other command buttons and list selections inside those windows | 12 or 146 (fixed per window and button) | one-shot | no | [STATIC] ids; per-button choice [OPEN] — recommended: 12 for main buttons, 146 for list items and small buttons |
| Some window notifications (e.g. query window opened) | none | — | — | [STATIC] |
| One UI click request asks for ID 10 (undefined → silent) | `[1ShotSfx]` 10 | one-shot | no | [STATIC] |
| Run / pause simulation | 23 `UiPlaySim` / 24 `UiPauseSim` | one-shot | no | [STATIC] (`09-ui-screens.md` §4.2) |
| Speed 1–4 selected | 25–28 | one-shot | no | [STATIC] |
| Click on a located news-ticker item | 47 `UiClickOnNewsticker` | one-shot | no | [STATIC] |
| Urgent ticker item | 54 `NewsTickerAlarm` | one-shot | no | [INFERRED] (`09` §4.4) |
| Ambience volume slider moved | 67 | one-shot | no | [STATIC] |
| Tool refused (illegal area/path/plan) — every map tool | 30 `UiError` | one-shot | no | [STATIC] |
| Tool unaffordable — every map tool | 32 `UiErrorNoMoney` | one-shot | no | [STATIC] |
| Zone tool: area accepted (all 12 zone types) | 38 `UiZoneResidential` | one-shot | no | [STATIC] |
| De-zone accepted | 44 `UiDezone` | one-shot | no | [STATIC] |
| Bulldoze accepted | 5 `UiBulldoze` | one-shot | no | [STATIC] |
| Road / highway / on-ramp built | 14 `UiLayRoad` | one-shot | no | [STATIC] |
| Rail built | 51 `LayRail` | one-shot | no | [STATIC] |
| Subway built | 53 `LaySubway` | one-shot | no | [STATIC] |
| Power line built | 16 `UiLayPowerLine` | one-shot | no | [STATIC] |
| Water pipe built | 50 `LayPipe` | one-shot | no | [STATIC] |
| Neighbour connection accepted: road/highway, rail/subway, pipe, power | 36, 37, 45, 46 | one-shot | no | [STATIC] (`12-networks.md`) |
| Level terrain accepted | 15 `UiLevelTerrain` | one-shot | no | [STATIC] |
| Raise / lower terrain: button pressed and plan accepted | `[Fading]` 18 / 19 | start loop | no | [STATIC] |
| Raise / lower: each accepted drag tick | `[Fading]` 18 / 19 | set level 1024, set audible 1024 | no | [STATIC] |
| Raise / lower: refused or unaffordable | 18 / 19 then 30 / 32 | set audible 0, then one-shot | no | [STATIC] |
| Raise / lower: button released | 18 / 19 | set audible 0 (loop fades) | no | [STATIC] |
| Create water accepted | `[Fading]` 20 `UiLayWater` | start loop | no | [STATIC] |
| Plant trees accepted | `[Fading]` 17 `UiLayTrees` | start loop | no | [STATIC] |
| Building placed ("plop", all placement tools) | chosen by §10.4 | notification at the tile | yes | [STATIC] |
| Query click on a tile | chosen by §10.4 | notification at the tile | yes (tile carried) | [STATIC] |
| Dispatch police / fire to a tile | 34 / 35 (locale variants, §4.3) | one-shot | no | [STATIC] |
| Dispatch crop duster / water bomber | 246 `UiCropDispatch` | one-shot | no | [STATIC] |
| Go to disaster | 55 `GoToDisaster` | one-shot | no | [STATIC] |
| Petition / neighbour deal answered (voice) | 155–166 (deal accept 155, decline 164) | one-shot | no | [STATIC] ids; type→voice table `22-advisors-news-petitioners.md` |
| Petition outcome crowd reaction | 70–73 | one-shot | no | [STATIC] (`09` §6.6) |
| Lightning strike | 170 `Thunder` | one-shot | no | [INFERRED] event |

### 10.3 Disasters

Unless stated, each disaster's sound is started when the disaster becomes active, made audible or
silent as the disaster enters or leaves the view, and stopped when the disaster ends. The `[Disaster]`
entry starts its `fadingSfxId` loop itself (§4.6). [STATIC]

| Event | Section / ID | Action | Positional | Tag |
|-------|--------------|--------|------------|-----|
| Disaster warning declared (siren) | 48 `UiEarlyWarningSiren` | one-shot | no | [STATIC] |
| Premonition phase 1 of a tornado | — | music is stopped | no | [STATIC] |
| Premonition phase 2 (any disaster), only at audio zoom ≥ 3 | 58 nature, 59 residential, 64 commercial | one-shot; choose 59 when the dominant ambient area (§5.3) is the residential one, 64 when it is a commercial one, else 58 | no | [STATIC] rule; [INFERRED] area naming |
| Premonition phase 3 of an earthquake | 57 `DisasterPremonitionTremor` | one-shot | no | [STATIC] |
| Disaster begins (tornado, earthquake, fire, riot, UFO) | 102 | one-shot; 102 is undefined in shipped data → silent | no | [STATIC] |
| Fire: first building starts burning / last stops | `[FireDisaster]` sound (62) | start / stop | positioned per §4.6 | [STATIC] |
| Fire: burning count changes | 62 | set level = min(100, count × 100 / MaxFires) | | [STATIC] |
| Fire: visible-fire count becomes ≥ 1 / 0 | 62 | set audible on / off | | [STATIC] |
| Tornado, riot active | ID from `[TornadoDisaster]` (shipped 60) / `[RiotDisaster]` (shipped 63), `SC3DisasterLayer.ini` | start (with the disaster as position source), audible on/off as it enters/leaves the view, stop | per §4.6 | [STATIC] |
| Parade active | ID loaded with `[ParadeEvent]` (expected 228 `DisasterParade` [INFERRED]) | start, audible on/off, stop | per §4.6 | [STATIC] |
| Earthquake main shock (≤ 2 sounding at once) | 61 | start; stop when damage is applied | no (SpatialScheme 2) | [INFERRED] from `21` §7 |
| UFO active | 74 | start, audible on/off, stop | | [STATIC] |
| Toxic cloud / toxic rain active | 175 / 231 | start, audible on/off, stop | | [STATIC] |
| Locust swarm / locusts eating | 233 / 235 | start, audible with a level, stop | | [STATIC] |
| Whirlpool active | 229 | start, audible at level 75 | | [STATIC] |
| Space junk falling | 237 | start, audible at level 75, stop | | [STATIC] |
| Space junk piece hits land / water | 243 / 250 | one-shot at the tile | yes | [STATIC] (`21` §11) |
| Space junk heavy hit (screen shake) | 244 | one-shot | no | [STATIC] |
| Whirlpool sucks up a boat | 251 | one-shot at the tile | yes | [STATIC] |
| Building destroyed by a disaster / explosion | explosion notification (§10.5) at the tile | | yes | [STATIC] |
| Random scream | 88–92 (female, uniform of 5) or 85–86 (male, uniform of 2) | one-shot | no | [STATIC] selection; sender [OPEN] |

### 10.4 Query and placement sounds per occupant

There is no `[Buildings]` section: building sounds are the `[Sparse]` entries (files under
`Sfx\Buildings\`) and the `UiQuery…` one-shots. Each occupant may name a `[Sparse]` ID in its
attribute 0x6F (key with Type 0, Group 0, Instance = sound ID; 287 of 1108 shipped occupants,
IDs 0x100–0x192, e.g. power plants 0x118–0x120, landmarks mostly 0x110) [DATA]. Attribute 0x70
(one occupant) is a second sound of the same kind [DATA]. Selection: [STATIC] unless tagged.

**Query click at tile (x, z)** — in the surface view:

1. If the occupant at the tile has a `[Sparse]` ID, that sample plays once immediately (the random
   trigger of §6 is bypassed) at the entry's volume.
2. Otherwise the first matching rule plays a `[1ShotSfx]` item:

| Tile content | ID |
|--------------|----|
| no building and the tile is water | 140 `UiQueryGenericWater` |
| no building, dry land | nothing |
| abandoned building | 143 `UiQueryGenericAbandoned` |
| tree / flora | 151 `UiQueryTreeGeneric`; in desert terrain 252, in jungle terrain 253 |
| road or highway | 14 |
| rail | 51 |
| power line | 144 |
| road tunnel | 52 |
| building of class residential / commercial / industrial / construction | 136 / 137 / 138 / 139 |
| water facility / landmark / agricultural / abandoned | 140 / 141 / 142 / 143 |
| airport / seaport / landfill | 173 / 174 / 172 |
| flora class | 151 |

In the subway view a subway piece answers 53; in the pipe view a pipe answers 50; nothing else
sounds in those views. Classification of the tile content is [INFERRED] from the sound names; the
order of tests is [STATIC]. Which occupant data gives the building class is [OPEN]; recommended:
the occupant's family (`13-buildings-occupants.md` §4) mapped as in the table.

**Building placed at tile (x, z)** — in the surface view, if a building now stands on the tile:

1. If it has a `[Sparse]` ID, that sample plays once and then 22 `UiPlopBuilding` plays.
2. Otherwise one fallback plays (no 22): tree 17, road/highway 14, rail 51, power line 144,
   tunnel 52, residential 136, commercial 137, industrial 138, construction 139, water facility
   0x169, landmark 0x110, agricultural 0x170, abandoned 143, airport 0x103, seaport 0x123,
   landfill 0x138, flora 151 (0x… are `[Sparse]` IDs).

If no building stands on the tile, the occupant's `[Sparse]` ID (if any) plays. In the subway view
placement plays 53, in the pipe view 50. One further view mode plays 16 [OPEN which view].

### 10.5 Explosion (building destroyed)

A destroyed building sends an explosion notification with its tile (x, z). Nothing plays unless the
view is the surface view. The audio system then picks one item [STATIC]:

1. Let S be the `[Sparse]` ID of the occupant at the tile (attribute 0x6F, §10.4).
2. If S is 0x119, 0x11C or 0x11D (fusion, microwave, nuclear plant): play 76
   `DisasterOneShotNuclearExplosion`.
3. Else if S is 0x118, 0x11A, 0x11B or 0x11E (coal, gas, generic, oil plant): play 135 if
   Rand(2) = 0, else 134 (`GDEBig2` / `GDEBig1`).
4. Otherwise let V be the building's size measure supplied by the city for that tile (which
   occupant quantity this is: [OPEN]; recommended: lot tile count × height class, see
   `13-buildings-occupants.md`). Row = 0 if V ≤ 513, 1 if 514 ≤ V ≤ 2500, 2 if V ≥ 2501. Draw
   r = Rand(12) and play entry r of that row:

| Row | Entries 0–11 |
|-----|--------------|
| 0 (small) | 103 104 105 106 107 108 103 104 105 106 107 108 |
| 1 (medium) | 109 110 111 112 109 110 111 112 109 110 111 112 |
| 2 (large) | 134 135 134 135 134 135 134 135 134 135 134 135 |

Rand is the audio generator of §7.3 (Rand(n) = output mod n [INFERRED]). The sound is positioned
at the tile; its volume is the effects volume, reduced by 300 (of 1024, floor 0) when the tile
is not on screen. At most 4 explosion voices exist; a new explosion reuses the slots round-robin,
stopping the oldest. [STATIC]

Note: this notification is not `AUDIO.INI` item 200 (`UiNewFEMSCM6`). Where `21-disasters.md`
says "destruction sound 200", it means this explosion notification.

## 11. Persisted state

None of the audio state is stored in city files. Volumes, on/off switches and enabled songs are user
preferences (see `27-platform.md`). [INFERRED]

## 12. Verification coverage

* XA: all 814/814 files (effects and music) decode with §2; the output matches a public EA-XA
  decoder bit for bit in ties-up mode (21 files sampled across all folders) and within 28 LSB with
  ties-to-even over all 795 effect files. Every shipped `.XA` is 22050 Hz.
  [DATA]
* WAV: 30/30 files are PCM RIFF. [DATA]

## Open questions

* Whether the FSC selection position moves at run time; the density divisor D if it is ever not 10; the exact "rotation allowed" condition.
* Exact ambient area weighting and how city state maps to the FSC selection position.
* Whether a global voice limit or voice stealing exists for one-shot effects (§8); none found.
* Exact use of `MusicPlayerSongDurationMsec` (assumed: cap on song play time).
* §10.4: which occupant data yields the building class used for fallback query/placement sounds; which view plays 16 on placement.
* §10.5: which occupant quantity is the explosion size measure V.
* §10.2: which windows/buttons use 12 vs 146. §10.3: sender of the random-scream request.
