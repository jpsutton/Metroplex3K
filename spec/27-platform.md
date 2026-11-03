# 27 — Platform: settings, registry, command line, display, cheats, networking

Status: draft. Provenance tags per `00-README.md`.

## 1. Registry, language and country

Key: `HKEY_LOCAL_MACHINE\Software\Electronic Arts\Maxis\SimCity 3000 Unlimited`. The original is a 32-bit
program, so on 64-bit Windows this key is found under
`HKEY_LOCAL_MACHINE\Software\WOW6432Node\Electronic Arts\Maxis\SimCity 3000 Unlimited`. An implementation
that reads the original installation's `Language` and `Country` must look there [INFERRED, high confidence].

| Value | Type | Written by | Read by game | Meaning |
|---|---|---|---|---|
| `InstalledPath` | string | installer | not observed | `<root>\Apps` [DATA] |
| `ScenarioCreator` | string | installer | no | path of the external scenario-creator database [DATA] |
| `Language` | DWORD or string | installer / language tool | yes | see the rules below [DATA][STATIC] |
| `Country` | DWORD or string | – | yes | see the rules below [STATIC] |
| `SKU` | – | installer | yes | product variant; semantics [OPEN] |
| `ergc` | string | registration | yes | Registration code. It is a **value** of this key, not a sub-key. It is decoded into the BAT owner string (`24-bat-buildings.md` §1.8) and is also the City Exchange serial number (on-line only, §7) [STATIC] |

The GOG language tool writes `Language` as a DWORD LCID: 1033 English, 2057 English (UK),
1036 French, 1040 Italian, 1031 German, 1043 Dutch, 1053 Swedish [DATA].

**Language selection** follows these rules in order [STATIC]:

1. Read `Language` as a DWORD. A value below 44 is an internal language index (1–43). Any other
   value is a Windows LCID, which is mapped to the matching language.
2. If the value is not a DWORD, read it as a string and match it against the language names.
3. A valid result overrides the language stored in `SC3.cfg`.
4. If no language is known yet, use the `Default Locale` value from the `[Language]` section of the
   start-up INI, matched by name or number. If that is missing, or names a language that is not
   installed, use the default language of `05-text-and-fonts.md` §2.3, provided that it is
   installed. That section also gives the Ctrl/Shift start-up quirk, which applies here.
5. A language counts as installed only if `Res\Text\<LanguageName>\SC3StringsApp.IXF` exists.
6. The language name replaces `%Language%` in resource paths (`Default.ini [DBSegments]`). See
   `05-text-and-fonts.md`.

**Country selection** [STATIC]: read `Country` as a DWORD; a value below 57 is a country index.
Otherwise read it as a string and match by name. If no country results, the default country is
used [STATIC]. Its value is 54 (United States), as in `05-text-and-fonts.md` §2.3 and
`06-audio.md` §4.3. The
list of country indices 0–56 and their names is [OPEN]. Until it is known, an implementation can
accept only numeric `Country` values and `-c` arguments.

The `-l` and `-c` command-line switches override both (§2).

## 2. Command line

* Switch prefix characters: `/`, `-` or `\` [STATIC].
* Tokens are separated by space, tab or newline. A double-quoted token may contain spaces, and
  surrounding quotes are removed [STATIC].
* **Letter switches** take their value glued to the letter, for example `-r1024x768`. The letter is
  case-insensitive [STATIC].
* **Named switches** use the form `-name:value`. A quoted value has its quotes stripped [STATIC].
* For on/off switches, only the first two characters of the value are compared with `of`. Any value
  that starts with `of` means "off"; anything else means "on" [STATIC].

| Switch | Effect |
|---|---|
| `-r<W>x<H>` | Display resolution, colour depth forced to 16 bpp. Ignored when `-restart:` is present. [STATIC] |
| `-d<name>` | Display device name, truncated to 31 characters. Ignored when `-restart:` is present. [STATIC] Its meaning as the DirectDraw device is [INFERRED]. |
| `-f` | Full-screen [STATIC] |
| `-w` | Windowed [STATIC] |
| `-l<language>` | Language, by name or number (§1). Applied only if that language is installed. [STATIC] |
| `-c<country>` | Country, by name or number [STATIC] |
| `-x…` | Parsed and ignored [STATIC] |
| `-p<a> <b> <c> <d>` | Overrides the measured machine profile (§3): `a` = CPU speed in MHz, clamped 100–1000 (default 200 if omitted); `b` = physical memory in MB, ≥ 16 (default 128); `c` = CPU **generation** G of §3 step 2 (not the machine class), clamped 5–10 (default 6); `d` = memory class, ≥ 4 (default 5). [STATIC] |
| `-cheats:<value>` | Enables a developer cheat flag and passes `<value>` to the command processor [STATIC]. Effect [OPEN]. |
| `-intro:off` | Skip the intro movie [STATIC] |
| `-sound:off` | Do not initialise sound [STATIC] |
| `-music:off` | Initialise sound but disable music [STATIC] |
| `-restart:on` | Relaunch after an update. Allows start-up while the single-instance mutex exists and skips `-r` and `-d`. [STATIC] |
| `-UpdaterApp:<path>` | Location of the updater settings (on-line updater only) [STATIC] |

## 3. Display

* **Resolutions.** 640×480, 800×600 and 1024×768. The UI layouts for each are in the segments
  `Res\UI\Shared\00640480_MainUI.IXF`, `00800600_MainUI.IXF` and `01024768_MainUI.IXF`; the segment
  ID is the resolution written in decimal digits [DATA]. See `08-ui-framework.md`.
* **Colour depth.** Always 16 bpp [STATIC].
* **Default mode.** 640×480 if the machine class is below 4, otherwise 800×600 [STATIC]. A mode
  saved in `SC3.cfg` takes precedence, and `-r` takes precedence over both.
* **Machine profile** [STATIC]. Computed once at start-up and overridable with `-p` (§2):
  1. *CPU speed S* in MHz (measured); if the measurement is below 100, S = 180.
  2. *CPU generation G*: 3 = 386, 4 = 486, 5 = Pentium, 6–9 = successive Pentium Pro/II/III-era
     models, and higher values for later processor families.
  3. *Machine class* (1–7; this is not the generation G that `-p` overrides):

     | Generation G | Class |
     |---|---|
     | G < 5 | 1 |
     | G = 5 | 2, or 3 if S > 189 |
     | G = 6 | 4, or 5 if S > 259 |
     | 7 ≤ G ≤ 9 | 5, or 6 if S > 299 |
     | G > 9 | 7 |

  4. *Memory*: total physical memory in MB (bytes >> 20), rounded up to a multiple of 4; the
     *memory class* is ⌊log₂(MB)⌋.

  Only the machine class is used for the default display mode and the default detail levels
  (§4). A modern implementation may simply treat the machine as class 7 (800×600, highest
  detail) [INFERRED]. On a modern CPU the original computes class 5 or 6 (CPUID family 6) or 7
  (base family 15), so it defaults to 800×600 [STATIC].
* **Full-screen or windowed.** Stored in preferences; overridden by `-f` and `-w` [STATIC].
* **DirectDraw.** The game renders into 16-bit off-screen surfaces and blits or flips them to the
  primary surface (`25-rendering.md`). The GOG build's `Apps/ddraw.dll` and `Apps/DDrawCompat.ini`
  are a third-party compatibility shim, not part of the game. GOG launches the game with the
  compatibility flags `16BITCOLOR`, `DISABLEDWM` and `HIGHDPIAWARE` [DATA].

## 4. Preferences persistence: `SC3.cfg`

Preferences are stored in `SC3.cfg`, an IXF container (`02-containers.md`). It is created on first
save and loaded at start-up [STATIC]. [INFERRED] It lives in the application directory (`Apps/`).

Every record has the key Type = record ID, Group = **0x230E18C3**, Instance = record sub-ID. The
records below are read on load and written on save, in this order [STATIC]:

| # | Type | Instance | Fields, in stream order (semantic types) |
|---|---|---|---|
| 1 | 0xA352F1B5 | 0x39EF6FF9 | **General/display/sound.** string; string; int (language index, −1 = unset); bool; uint width; uint height; bool (windowed flag) [INFERRED]; bool; 32-byte display device name; bool; bool music on; int music volume; bool sound effects on; int sound-effects volume; bool ambience on; int ambience volume; bool; bool; bool; three uints [OPEN]; three ints = detail levels 1–3; bool; uint. Byte layout and defaults below |
| 2 | 0x6314C631 | 0x6313BF98 | uint count, then `count` × (uint ID, bool flag) |
| 3 | 0x4324F379 | 0x39EF6F00 | three uints, then a 128-byte string (truncated at the first control character) |
| 4 | 0x0324F37E | 0x4324F45E | one bool |
| 5 | 0xA324F381 | 0xA324F46A | uint, bool, uint, uint |
| 6 | 0x0324F385 | 0xC324F475 | three uints |
| 7 | 0xC324F388 | 0xC324F47F | two uints |
| 8 | 0x2324F38B | 0x2324F488 | three uints |
| 9 | 0xA324F390 | 0x0324F492 | two uints |
| 10 | 0xC324F393 | 0xE324F49A | eleven uints |
| 11 | 0x633350DA | 0x233350DF | seven uints |
| 12 | 0x03357D61 | 0x43357D6C | one uint |
| 13 | 0xA334F3F4 | 0xE334F3FE | uint, uint count, then `count` × (uint ID, bool) |
| 14 | 0x1A35002A | 0x5A35004A | uint, uint, two resource keys |
| 15 | 0xBA3512E9 | 0xBA3512FD | one string |

The default detail levels in record 1 depend on the machine class [STATIC]:

| Machine class | Level 1 | Level 2 | Level 3 |
|---|---|---|---|
| < 2 | 1 | 1 | 0 |
| 2 | 2 | 1 | 0 |
| 3 | 2 | 1 | 1 |
| 4 | 2 | 2 | 1 |
| ≥ 5 | 3 | 2 | 1 |

**Stream primitives** [STATIC]. Every record payload is a plain concatenation of the following
primitives, with no padding and no per-record header or version word:

| Primitive | Encoding |
|---|---|
| bool | 1 byte; 0 = false, any non-zero value = true on read; written as 0 or 1 |
| uint / int | 4 bytes, little-endian (u32 / s32) |
| string | u32 byte count N, then N bytes of 8-bit text; no terminator |
| fixed char array (n) | exactly n raw bytes; NUL-padded text. On read, record 1's 32-byte device name is used up to its first NUL; record 3's 128-byte string is cut at the first control character (byte < 0x20) and forced to end at byte 127 |
| resource key | three u32: Type, Group, Instance |

**Record 1 (0xA352F1B5:0x230E18C3:0x39EF6FF9) byte layout** (offsets depend on the two leading
strings, so they are given relative to the end of string 2, written `S+`):

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0x00 | 4+N1 | string | string 1 | [OPEN] (registration/user name per §1 [INFERRED]) |
| … | 4+N2 | string | string 2 | [OPEN] (registration/company or key [INFERRED]) |
| S+0x00 | 4 | s32 | language | language index; −1 = unset (use the installed default) |
| S+0x04 | 1 | bool | flag a | [OPEN] |
| S+0x05 | 4 | u32 | width | display width in pixels (640, 800 or 1024) |
| S+0x09 | 4 | u32 | height | display height in pixels (480, 600 or 768) |
| S+0x0D | 1 | bool | windowed | windowed (true) / full screen (false) [INFERRED] |
| S+0x0E | 1 | bool | flag b | [OPEN] |
| S+0x0F | 32 | char[32] | device | display device name, NUL-padded |
| S+0x2F | 1 | bool | flag c | default false; [OPEN] |
| S+0x30 | 1 | bool | music on | default true |
| S+0x31 | 4 | s32 | music volume | 0–1024; default 1024 |
| S+0x35 | 1 | bool | sound effects on | default true |
| S+0x36 | 4 | s32 | sound-effects volume | 0–1024; default 1024 |
| S+0x3A | 1 | bool | ambience on | default true |
| S+0x3B | 4 | s32 | ambience volume | 0–1024; default 1024 |
| S+0x3F | 1 | bool | flag d | [OPEN] |
| S+0x40 | 1 | bool | flag e | [OPEN] |
| S+0x41 | 1 | bool | flag f | default true |
| S+0x42 | 12 | 3 × u32 | three values | [OPEN] |
| S+0x4E | 12 | 3 × s32 | detail levels 1–3 | defaults from the machine-class table above; these are the three **s32** fields, not the u32 triple [STATIC] |
| S+0x5A | 1 | bool | flag g | [OPEN] |
| S+0x5B | 4 | u32 | value | default 0; [OPEN] |

The three on/volume pairs are, in this order, **music**, **sound effects** and **ambience** (the
categories of `06-audio.md` §8) [STATIC]. **Volume defaults** [STATIC]: when no `SC3.cfg` exists, or
record 1 is missing, each volume is 1024 and each "on" flag is true. When a volume read from the file
is outside 0–1024, the sound system replaces it at start-up with a fixed repair value: music 1000,
sound effects 700, ambience 900. The repair values are not first-run defaults. Other defaults: language −1,
width and height 0 (meaning "use the default mode", §3), all bool flags false except flag f, and the 32
device-name bytes all 0xFF, meaning that no device is selected [STATIC].

**Records 2–15 byte layouts** [STATIC] (each payload in stream order):

| # | Payload |
|---|---|
| 2 | u32 count, then count × (u32 ID, bool flag): a per-ID on/off table (loaded into an ID → bool map) |
| 3 | u32, u32, u32, char[128] |
| 4 | bool |
| 5 | u32, bool, u32, u32 |
| 6 | u32, u32, u32 |
| 7 | u32, u32 |
| 8 | u32, u32, u32 |
| 9 | u32, u32 |
| 10 | 11 × u32 |
| 11 | 7 × u32 |
| 12 | u32 |
| 13 | u32, then u32 count, then count × (u32 ID, bool): a second ID → bool table |
| 14 | u32, u32, resource key, resource key |
| 15 | string |

Records 3–15 are each loaded into a separate settings block that carries a "present" flag; a block
whose record is missing or truncated is marked absent and its owner uses built-in defaults
[STATIC]. Which window or panel owns each block (records 3–12 are window/panel geometry and option
blocks [INFERRED]) is [OPEN]. A record that fails to parse does not abort loading of the others.

A reimplementation does not have to share this file with the original, so it may store the same
settings in its own format. It must honour the semantic defaults above.

## 5. Cheat codes

### 5.1 Mechanism

The city owns a **cheat-code registry**. Game subsystems register phrases with it. When the player
submits text in the in-game cheat entry box (`09-ui-screens.md`) whose beginning matches a
registered phrase, the registry raises a "cheat entered" notification carrying the phrase's identity
and the full entered text, and the owning subsystem reacts. Some cheats take an argument: the text
typed after the phrase. The registry also keeps an "active" flag per phrase, which some cheats set
or toggle and which the affected subsystems query [STATIC].

Unless stated otherwise, cheat state lasts only for the current session. It is not saved with the
city (only the cheat log of §5.1a is), and it is reset when a city is loaded or a new one started [STATIC for the flags kept by
the subsystems; INFERRED for the registry flags].

[OPEN] Whether phrase matching is case-insensitive. Some shell cheats compare the typed text with an
upper-case form to choose between two responses, so the matching itself is probably
case-insensitive.

### 5.1a Cheat IDs and the cheat log

Each phrase has a fixed 32-bit **cheat ID**. The IDs are data: a city save records the cheats used
in it in the cheat-log record `630E2EE6:730E2EE6` (`10-save-format.md` §3.6). An implementation
must write these exact IDs, and must accept any u32 in the log when it reads one.

**Which cheats are logged.** A cheat's ID is added to the log, at most once, when the cheat's
"active" flag is set (§5.1) [INFERRED, medium confidence]. The news-ticker phrases of §5.2 never set
the flag, so they are never logged. Shipped data agrees with this: the 24 shipped files that have
the record contain only the IDs of `I am weak`, `***`, `power to the masses`, `water in the desert`
and 0xC2FAA2F5 [DATA 59/59]. The record is omitted when the log is empty.

**The count record** `430E2EE6:530E2EE6` is present in 58 of 59 shipped files, with values from 2
to 30, including files that have no logged IDs [DATA]. It is therefore **not** the number of logged
cheats. Its meaning is [OPEN]. Round-trip it, and write 0 for a new city [INFERRED].

| Cheat ID | Phrase | Seen in shipped logs |
|---|---|---|
| 0x62E8630B | `I am weak` | yes (23 files) |
| 0xE37FB7B8 | `***` | yes (8) |
| 0xC2FA8E82 | `power to the masses` | yes (10) |
| 0x42FA93FE | `water in the desert` | yes (3) |
| 0xC2FAA2F5 | `garbage in, garbage out` [INFERRED, high: same owner and ID series as the two above; the owner sets this ID's active flag itself] | yes (6) |
| 0x23B4418F | `so sue me` | – |
| 0xC37A5AAA | `I love red tape` | – |
| 0xA3558FD6 | `call cousin Vinnie` | – |
| 0x6303EFA6 | `UFO Swarm` | – |
| 0xC5326321 | `Locust Swarm` | – |
| 0x0367F938 | `The Birds` | – |
| 0xA31CF2EF | `Terrain one up` | – |
| 0xC31CF2F6 | `Terrain one down` | – |
| 0xC31CF2FA | `Terrain ten up` | – |
| 0x831CF2FF | `Terrain ten down` | – |
| 0x837961EA | `traffic lights` | – |
| 0xA42483B1 | `tuneup` | – |
| 0x031DCE55 | `force petitioners to say` | – |
| 0x43110C27 | `force Moe to say` | – |
| 0xC310F7D4 | `force Gus to say` | – |
| 0x831109B4 | `stop forcing advice` | – |
| 0xC352E7AE, 0x0352E7C2, 0x0352E7CC, 0x8352E7D6, 0x4352E7E1, 0x4352E7ED, 0x4352E7F6, 0xA352E800, 0x03546E10, 0xE352E809, 0xA352E814, 0xE352E822, 0x4352E828, 0x0352E873, 0x0352E87C, 0x0352E885, 0xE352E88F, 0x4352E897, 0xC352E8A2, 0xC352E8AC, 0xC352E8B4, 0x8352E8BD, 0x2352E8C7 | the §5.2 phrases, respectively (one ID per phrase, in this list's order): `fund`, `advisor`, `moremoney`, `maxis`, `1234`, `sc3k`, `mayor`, `hello`, `simon says`, `simcity`, `money`, `ticker`, `electronic arts`, `erts`, `will wright`, `help`, `sim`, `easter egg`, `llama`, `scurk`, `bat`, `porntipsguzzardo`, `broccoli` | never (not logged) |

All rows are [STATIC] unless tagged. [OPEN] The IDs of `pay tribute to your king`, `zyxwvu`,
`let's make a deal`, `nerdz rool`, `Salt on`, `Salt off`, `Load terrain`, and the force phrases
for Mortimer, Constance, Maria, Randall and Karen. If any of these sets an active flag, it would
also be logged. An implementation that cannot reproduce such an ID should not log that cheat.

### 5.2 Shell cheats: news-ticker responses only

Their only effect is to post a triggered news-ticker message from string table 0x42C1ED2D
(`22-advisors-news-petitioners.md`) [STATIC]. The message IDs below are string IDs in that table.

| Phrase | Ticker message ID |
|---|---|
| `fund` | 0x325 or 0x326 (depends on whether typed in capitals) |
| `advisor` | 0x327 |
| `moremoney` | 0x32A |
| `maxis` | 0x32B if typed as `MAXIS`, else 0x32C |
| `1234` | 0x32D |
| `sc3k` | 0x32E |
| `mayor` | 0x32F |
| `hello` | 0x330 |
| `simcity` | 0x331 |
| `money` | 0x332 |
| `ticker` | 0x333 |
| `electronic arts` | 0x334 |
| `erts` | 0x335 |
| `will wright` | 0x336 |
| `help` | 0x337 or 0x338 (capitals or not) |
| `sim` | 0x339 or 0x33A (capitals or not) |
| `easter egg` | 0x33B |
| `llama` | 0x33C |
| `scurk` | 0x33D |
| `bat` | 0x33E |
| `porntipsguzzardo` | 0x33F |
| `broccoli` | 0x340 |
| `simon says` | 0x341, and the text typed after the phrase is echoed on the ticker |

The shell also registers the phrase `***`, whose effect is in the user-interface layer [OPEN]. None
of the money-themed phrases in this table change city funds [STATIC].

### 5.3 Gameplay and developer cheats

"Toggle" means each entry flips the state; "sets" means it switches the state on for the rest of
the session.

| Phrase | Kind | Effect |
|---|---|---|
| `power to the masses` | sets | The power-plant build menu lists every plant type, whether or not it is otherwise available. [STATIC] |
| `water in the desert` | sets | Same, for the water-structure build menu. [STATIC] |
| `garbage in, garbage out` | sets | Same, for the garbage-structure build menu. [STATIC] |
| `call cousin Vinnie` | action | Triggers cousin Vinnie's petitioner offer (`22-advisors-news-petitioners.md`). Accepting it adds §100,000 to the treasury. Refusing it sets a session "refused Vinnie" flag used by `zyxwvu`. [STATIC] |
| `zyxwvu` | action | Only if the player refused cousin Vinnie's offer earlier in the session: unlocks reward 19999 (Quigley Castle, see `CitySim.ini`) as if earned. Otherwise does nothing. [STATIC; the unlock semantics INFERRED, high confidence] |
| `let's make a deal` | sets | Each evaluation of the business-deal petitioner offers **every** currently eligible business deal (deal occupants 0x4A39–0x4A3C), instead of the normal rule that offers one randomly chosen deal only when the treasury is below its threshold and the trigger counter has been reached. Eligibility (population requirement met, deal enabled, not already accepted or pending, and for 0x4A3B its extra availability condition) is unchanged. All eligible offers are pending **at the same time**, with no limit on the number of pending petitions; each is re-requested every cycle, so a declined deal returns at the next cycle and an accepted one stops. The flag is never cleared during the session. See `19-sim-economy.md`. [STATIC] |
| `pay tribute to your king` | sets | The Rewards build menu lists every reward that has not yet been built and is not a business-deal building, whether or not it has been earned. Normally only earned rewards that are not yet built are listed. [STATIC] |
| `I am weak` | toggle | While on, the building-placement cost (`[BuildingPlacementCosts]`, `19-sim-economy.md` §13) is 0 for every agent type, so placing zones, networks and buildings is free. [STATIC] |
| `so sue me` | toggle | While on, after each monthly budget settlement the treasury is clamped so it never falls below −99,999: if funds < −99,999 they are set to −99,999. [STATIC] |
| `I love red tape` | action | Makes every ordinance available at once. Each ordinance that is not yet available becomes available exactly as if its normal availability condition had just been met, including the "ordinance available" notification (`19-sim-economy.md`). The availability then persists like normally earned availability. [STATIC] |
| `nerdz rool` | sets | When the industrial developer picks the industry class of a new building, the agriculture check still comes first. Otherwise the class is always clean/high-tech, and the normal rule is skipped (high-tech share = clamp((EQ − 85)·100/65, min, max), adjusted by ordinances and compared with a 0–99 random draw). See `15-sim-growth.md`. [STATIC; class naming INFERRED, high confidence] |
| `UFO Swarm` | toggle | While on, a UFO attack launches `MaxUFOsPerInstanceCheatSwarm` saucers instead of the computed count (`21-disasters.md` §6.3). It does not start an attack by itself. [STATIC] |
| `Locust Swarm` | toggle | While on, a locust attack uses `MaxLocustsPerInstanceCheatSwarm` swarms instead of the count derived from crop tiles (`21-disasters.md` §6.4). It does not start an attack by itself. [STATIC] |
| `Salt on` | action | Re-runs the terrain's salt-water classification, which marks sea-connected water as salt (`11-world-terrain.md`). [STATIC that it re-runs the classification; the rule itself is in `11-…`] |
| `Salt off` | action | Clears the salt-water flag on every cell (x = 0 … width−2, y = 0 … height−2), which makes all water fresh. [STATIC] |
| `Terrain one up`, `Terrain one down`, `Terrain ten up`, `Terrain ten down` | action | Shift the whole map vertically by +1, −1, +10 or −10 altitude units. For every cell, the ground altitude and the water-surface altitude each become clamp(value + delta, 2, 253), and the map's sea level becomes clamp(sea level + delta, 2, 253). The map is then re-derived (slopes, water, redraw) as after a terrain edit. [STATIC] |
| `Load terrain <arg>` | action | Developer cheat that loads a terrain named by the argument. [STATIC; argument handling INFERRED] |
| `traffic lights <arg>` | action | The argument is the text after the 15-character prefix `traffic lights `. If it is `on` or `ON`, a traffic-simulator option is switched on; otherwise it is switched off. [STATIC] The traffic simulator stores the option in a flag that nothing reads [STATIC]. Toggling it on or off in a running city had **no visible effect** [OBSERVED]. Treat it as an accepted no-op. |
| `tuneup` | action | Developer cheat. It asks the sim-variables service's tuning object (class 0x2424854F) to run its action. That object works on the developer tuning file `\Sys\SimTune.INI`, which is not shipped [STATIC]. Entering it in a running city had **no visible effect** [OBSERVED]. Treat it as an accepted no-op. |
| `The Birds` | toggle | Toggles a flag in the sprite/animation layer; visual effect only. [INFERRED] |
| `force petitioners to say <n>`, `force <advisor> to say <n>`, `stop forcing advice` | debug | Advisor names: Mortimer = budget, Moe = transportation, Constance = city planning, Maria = public safety, Randall = health & education, Gus = utilities, Karen = environment. `<n>` is parsed as a **decimal** integer (C `atoi`) and is a topic ID of that advisor. It is appended to the advisor's forced-topic list. At the end of every evaluation of that advisor (weekly, `22-advisors-news-petitioners.md` §3.6, after the rules, the topic-list step and the post-update rules), every topic in the list is raised as a plain topic (no key, L = 0). So a forced topic stays listed until `stop forcing advice`. That cheat empties the forced lists of all seven advisors; the topics then lapse at the next evaluation by the normal life cycle (§3.4). Forcing the same n twice adds it twice. The lists are not saved. [STATIC] |

## 6. Single instance

A named system mutex `SimCity 3000 Mutex` enforces a single running instance. If the mutex already
exists, the new process finds the existing top-level window (class `Gonzo`, title `SimCity 3000`),
restores it, brings it to the foreground, and exits [STATIC].

## 7. Networking and on-line features

The original has on-line features [STATIC]:

* **News-ticker and "major" advertisements** fetched from
  `www.simcity.com/<LANGUAGE>/TickerAdvertisements.html` and `MajorAdvertisements.html`, configured in
  `SC3NetMap.ini` (overridable with `SC3NetMapOverload.ini`) and `SC3Net.cfg`.
* **City Exchange**: links to download scenarios, BAT buildings and cities, and to upload cities.
  This uses the registration credentials (§1).
* **Internet chat**: IRC rooms with screenshot, city-view and file transfer, and a photo album.
* A cache directory `Maxis Network Cache\`, plus `CookieData.ini` and `FileCache.ini`.

All of these services were shut down long ago. **An implementation should not perform any network
access.** It should treat every on-line feature as unavailable, hide or disable the related UI, and
may open plain web links in the system browser if desired. The game must run with no network
present. The ticker works without advertisements [INFERRED from the design].

## 8. Version and copy-protection checks

* The executable's file version is `2.0.949` [DATA].
* A pre-release expiry message is present but is not active in the release build [INFERRED].
* The GOG build has no CD check [INFERRED]. An implementation must not implement copy protection.
  Copy-protection driver files that may be present in `Apps/` (`DRVMGT.DLL`, `DPLAYERX.DLL`,
  `CLCD32.DLL`, `CLCD16.DLL`) are not game components and must be ignored [STATIC].

## 9. Modernisation notes

* **Scaling.** Render at one of the three original UI resolutions (layouts exist only for those) and
  scale the frame to the window or display, preferably by integer factors with nearest-neighbour or
  sharp-bilinear filtering. Map mouse coordinates back to UI space. Declare the process DPI-aware.
* **Windowed mode.** This should be the default. Full-screen should be borderless.
* **Colour.** Emulate the 16-bit (RGB565) pipeline internally if exact colour reproduction matters;
  see `04-graphics-formats.md`.
* **Writable data.** Store preferences and caches in a per-user writable location instead of
  `Apps/`. Keep city saves in `Cities/`, or in a user directory mirrored to it.
* **Ignore** `Apps/ddraw.dll`, `Apps/DDrawCompat.ini` and `Apps/SimUtil2.dat`. The last is
  100,000 random bytes that no module references [STATIC].

## Open questions

* The effect of `***` (§5).
* The owners/meanings of `SC3.cfg` records 2–15 and of the [OPEN] fields of record 1 (byte layouts are complete).
* The meaning of `-cheats:`.
