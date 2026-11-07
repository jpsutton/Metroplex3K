# Intellectual-property review

Review date: 2026-11-07. Scope: every tracked file in the repository at the time of the review, plus the git history
that is published on `origin`. This is an engineering review, not legal advice. The owner has decided not to commission a
legal review; the residual risks below are accepted by the owner (see "Open items").

## 1. What the project is

Metroplex3K is a clean-room reimplementation of a game engine that is compatible with a commercial game. It contains no
game assets and loads everything from a copy the user owns. It was built in two stages:

1. An analysis team examined the original program and wrote a specification (`spec/`).
2. An implementation team wrote the engine from that specification only.

The specification is the only channel between the two teams. Its rules are in `spec/00-README.md`: no code, no
implementation internals, no copied creative content.

## 2. Findings and actions

### 2.1 Game assets and game data

- No art, sound, music, video, text, maps or buildings from the game are tracked. Searched the tracked tree and the
  history of every branch on `origin` for image, audio, video, archive, executable and save-file extensions: none.
- Screenshots (`*.png`, `shots/`, `dialog-shots/`) are listed in `.gitignore` and are not tracked.
- Tests use synthetic fixtures built on disk (`tests/fake_install.h`, `tests/skin_fixture.h`). They need no game data.
- `diag.txt` (output of the `diag` tool on a real install: a list of shipped INI file names, resource counts and two UI
  strings) was tracked. **Removed from the tree in this change.** It remains in the published history (commit
  `229a432`). It contains file names and counts only, so it is low risk. A history rewrite is not recommended unless the
  owner wants one.
- Local build directories (`build-debug/`, about 100 MB of binaries) appeared in some local checkpoint refs created by
  the editor tooling. They are not on `origin`. Do not push any `refs/patchbay/*` ref.

### 2.2 Code and clean-room integrity

- The README and `spec/00-README.md` state the clean-room process. The code itself does not contain any original program
  code, and none was found.
- **Finding (fixed):** about 40 source comments in `src/` carried references that came from the specification:
  code addresses, module names of the original program, decompiler function names (`FUN_...`), virtual-table offsets
  and the phrases "decompile", "disassembly", "dirty side" and "clean side". These are not original code and not
  copyrightable expression, but they show that implementation-level detail crossed the wall and they contradict the
  stated process. All were removed. The behaviour descriptions were kept.
- **Finding (fixed, specification):** the specification broke its own rules in many places. The most frequent cases were
  code addresses and module names in provenance tags, vtable slots and object offsets, code-only message, interface and
  class IDs, references to the analysis team's tools and run labels, a local file path, and revision-history notes. All
  of these were removed or reworded in this change (see section 3).
- Names of the original program's data files, INI keys, resource IDs and save-record keys remain. They are interface
  facts that a compatible program must use.

### 2.3 Copied text and data

- **Verbatim game text in the specification (fixed):** a few dialogue and prompt sentences, 18 reason-code strings,
  about 60 help-page titles, about 35 ordinance names, flavour and tooltip lists, character names and a table of 24
  building names were reproduced. They were replaced with string IDs and short paraphrases.
- **Numeric data tables (kept, residual risk):** the specification and the engine contain numeric facts that came from
  the original data: record layouts, tuning values, the new-city record template (`spec/30-new-city-template.md` and
  `src/core/newcity_template.cpp`, about 13 KB of record bytes) and the default occupant values
  (`src/core/occ_defaults.cpp`). They are facts needed for byte compatibility, not creative expression, so copyright
  protection is weak or absent. Recommended mitigation if the owner wants less exposure: build the new-city template from a
  shipped starter file at run time instead of embedding it.
- **Cheat phrases:** `src/core/cheats.cpp` and `spec/27-platform.md` list the original's cheat phrases and their effects.
  They are short strings needed for behaviour compatibility. Kept.
- No embedded font, sprite or sound data was found in the code. The only embedded font is the public-domain 5x7 font.

### 2.4 Trademarks and naming

- The game's name appears in the README only to say what the engine is compatible with, plus the run-time file, folder,
  registry and cheat-phrase names the engine must match. Those literals are collected in `src/core/compat_names.h`.
  Source comments, user-visible text, tests and tools no longer use the name.
- User-visible text now says "the original game" instead of naming it. The "need data" screen points to the README.
- The specification still names the game (title, registry keys, mutex and window names, a few UI captions). It is a
  description of the compatibility target. This was left as is. Consider replacing the name in the title line.
- **Name check:** "Metroplex" is also the name of a character in a toy and media franchise owned by another company. The
  goods differ, so the risk is low, but a quick trademark search is advisable before promoting the project.
- Credits in the app still say "Original game by Maxis". This is attribution and not use as a brand.
- The README carries a non-affiliation notice.

### 2.5 Third-party code and licenses

- Project license: GPL-3.0-or-later (`LICENSE`). Every `.cpp` and `.h` file has an SPDX header.
- `src/core/tqi.cpp`: MPEG-1 tables and decoder written with reference to FFmpeg's TQI decoder (LGPL-2.1-or-later) and the
  MultimediaWiki page. LGPL code may be relicensed under the GPL. The credit is kept in the file. Decision: licensed
  under the project license. Optional later step: re-derive the tables from ISO/IEC 11172-2.
- Fallback font: public domain. SDL2: zlib license, linked by the desktop host only. `innoextract`: run as an external
  program, not bundled.

### 2.6 Legal context of the analysis stage

The analysis team ran and examined the original program. Such analysis for interoperability is permitted in many
jurisdictions under limits, for example 17 U.S.C. 1201(f) and Article 6 of the EU Software Directive, and those limits
depend on the end-user license agreement and on the country. This review did not assess the analysis team's own
activities, the license terms of the game, or the jurisdiction. These remain unassessed and are accepted as a risk by the
owner.

## 3. Specification review

All 31 files in `spec/` were read in full by independent reviewers against the rules in `spec/00-README.md`, then
corrected. The review looked for code, addresses and internal names, code-only IDs, references to the analysis team,
verbatim text longer than six words, lists of ten or more original strings, and byte dumps over 32 bytes.

Result before correction: no file was fully clean. Clean-room rule violations were common but small (a phrase or a
tag), and copied text was limited to the items in section 2.3. Result after correction: 30 spec files edited (about
1,700 lines added and 2,200 removed, mostly deletions). A second grep pass (section 4) finds no code addresses, module
names, decompiler names, analysis-team references or revision-history notes in the specification. The hex-looking
values that remain are resource keys, record keys, INI keys and menu item IDs that appear in data files. Remaining
known items, not fixed:

- `spec/30-new-city-template.md` still carries the record hex dumps (see 2.3).
- Several tables copy numeric data that the engine reads from the install at run time: the ordinance catalogue, the
  placement-cost table, the keyboard table. They were reduced where they carried names or text. Further trimming is
  optional.
- Wording that describes how the original was structured internally (which component calls which) was removed where a
  local rewrite was possible. Some sentences remain.
- The analysis team keeps writing the specification. The rules were not followed consistently. Before each future
  spec merge, run the checks in section 4.
- `tools/probes/probe_verify.cpp` expects the owner's reference saves to be named `W5_<City>_sNN.sc3` (an analysis-run
  prefix). It is a file-name convention for local files that are not distributed. Harmless; rename later if wanted.
- The behaviour tables in the specification still include in-memory flag values where routing rules depend on them
  (for example the network tile flag bits in `spec/18-sim-transport.md` section 3.1 and `spec/12-networks.md` section
  5.2). Marked as labels where possible.

## 4. Checks to repeat before each release

```
git grep -nIE '0x1[0-9A-Fa-f]{7}\b|SIM[A-Z]{3,}|SimTransit|FUN_|disassembl|decompil' -- spec src tests tools   # review hits: data IDs are fine
git grep -nIiE 'dirty[ -]side|clean[ -]side|analysis team|W5_|R2_' -- spec src tests tools   # only the probe_verify file-name prefix and the rule text may match
git grep -nIiE 'sim ?city' -- src tests tools docs        # only src/core/compat_names.h may match
git ls-files | grep -iE '\.(png|bmp|ixf|dat|xa|wav|tgq|exe|dll|cur)$'   # must be empty
```

## 5. Open items for the owner

1. Done: the owner decided the specification falls under the project license (it was written for this project).
2. Decide on the new-city template: embed (current) or derive from shipped files at run time.
3. Trademark search for the project name.
4. Legal review of the analysis stage and the game's EULA: declined by the owner. Risk accepted.
5. Keep `refs/patchbay/*` and `build-*` directories out of any push.
