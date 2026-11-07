# Licensing notes

Metroplex3K is licensed under the GNU General Public License, version 3 or (at your option) any later version
(`GPL-3.0-or-later`). The full text is in [`LICENSE`](../LICENSE). Contributions are accepted under the same terms.
Every `.cpp` and `.h` file carries an SPDX header (`SPDX-License-Identifier: GPL-3.0-or-later`); new files must too.

The license covers the code and documentation in this repository, including the `spec/` folder and the MPEG-1 decoder
in `src/core/tqi.cpp`. It does not cover the commercial game the engine is compatible with, or any of its assets. No
original game assets are included; the engine reads them at run time from a copy the user owns. The game's name is a
trademark of its owner. It is named only in the README, to say what the engine is compatible with, and in the
run-time file and folder names the engine must look for (`src/core/compat_names.h`). Do not use it in the project
name, icon or marketing. See [IP-REVIEW.md](IP-REVIEW.md) for the intellectual-property review.

Third-party-derived data:

- `src/core/tqi.cpp`: the MPEG-1 intra quantiser matrix and the run/level VLC tables are standard MPEG-1 data
  (ISO/IEC 11172-2). They were transcribed with reference to the FFmpeg project's TQI decoder (LGPL-2.1-or-later) and
  the MultimediaWiki format page. The code is distributed under `GPL-3.0-or-later`; LGPL code may be relicensed under the
  GPL. The credit to the FFmpeg developers is kept in the file. Re-deriving the tables from the ISO text, or replacing
  the decoder, would remove the LGPL influence entirely; this is optional and not planned for 1.0.
- `src/core/font_builtin.cpp`: the classic public-domain 5x7 ASCII column font.
- SDL2 (zlib license) is linked by the desktop host only. `innoextract` is run as an external program when the user
  drops a GOG installer; it is not linked or bundled.
- `spec/`: the clean-room specification written by the analysis team for this project. It is distributed under the
  same license as the code. See [IP-REVIEW.md](IP-REVIEW.md) for the review that checked it for original game material.
