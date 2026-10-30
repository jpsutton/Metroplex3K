# Licensing notes

The repository owner has not chosen a license. Until one is added all rights are reserved. Decide before publishing.

Third-party-derived data to review:

- `src/core/tqi.cpp`: the MPEG-1 intra quantiser matrix and the run/level VLC tables are standard MPEG-1 data. They were
  transcribed with reference to a public decoder's tables (ffmpeg, LGPL). The tables are facts of the standard, but
  the author also consulted ffmpeg's TQI decoder and the MultimediaWiki page when writing the decoder. Re-derive the
  tables from the ISO standard text, or replace the decoder, if the project needs to be free of LGPL influence.
- The 5x7 fallback font in `src/core/font_builtin.cpp` is the classic public-domain 5x7 ASCII column font.
- No original game assets are included. The `spec/` folder is the clean-room specification supplied by the analysis team.
