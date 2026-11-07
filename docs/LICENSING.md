# Licensing notes

Metroplex3K is licensed under the GNU General Public License, version 3 or (at your option) any later version
(`GPL-3.0-or-later`). The full text is in [`LICENSE`](../LICENSE). Contributions are accepted under the same terms.

The license covers the code and documentation in this repository. It does not cover *SimCity 3000 Unlimited* or any of
its assets. No original game assets are included; the engine reads them at run time from a copy the user owns.
*SimCity* is a trademark of Electronic Arts Inc. Do not use it in the project name, icon or marketing; describe the
project as "compatible with" the game only.

Third-party-derived data and open items:

- `src/core/tqi.cpp`: the MPEG-1 intra quantiser matrix and the run/level VLC tables are standard MPEG-1 data. They were
  transcribed with reference to a public decoder's tables (ffmpeg, LGPL). The tables are facts of the standard, but
  the author also consulted ffmpeg's TQI decoder and the MultimediaWiki page when writing the decoder. LGPL code may be
  relicensed under the GPL, so this is compatible with `GPL-3.0-or-later`. Re-deriving the tables from the ISO standard
  text, or replacing the decoder, would remove the LGPL influence entirely. This is optional and tracked separately.
- The 5x7 fallback font in `src/core/font_builtin.cpp` is the classic public-domain 5x7 ASCII column font.
- SDL2 (zlib license) is linked by the desktop host only. `innoextract` is run as an external program when the user drops
  a GOG installer; it is not linked or bundled.
- `spec/` is the clean-room specification supplied by the analysis team. It contains no original code or assets. Confirm
  with that team that it may be distributed under the same license as the code.
