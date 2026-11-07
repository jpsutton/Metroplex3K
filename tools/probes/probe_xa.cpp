// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Decodes every shipped .xa file. With ffmpeg on PATH each decode is also compared against ffmpeg's (issue #66):
// ties-up rounding is bit-identical on almost all files; the default ties-to-even stays within ~28 LSB.
//   probe_xa [GAME_DIR]
#include <fstream>
#include <iterator>
#include "core/xa_codec.h"
#include "probe_common.h"
using namespace probe;

#ifdef _WIN32
#define NULL_SINK "NUL"
#else
#define NULL_SINK "/dev/null"
#endif

static bool readAll(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), {});
  return true;
}

int main(int argc, char** argv) {
  fs::path root;
  gameDirFromArgs(argc, argv, root);
  if (needInstall(root, "probe_xa GAME_DIR")) return 1;
  const bool haveFfmpeg = std::system("ffmpeg -version > " NULL_SINK " 2>&1") == 0;
  const fs::path tmp = fs::temp_directory_path() / "m3k_probe_xa.raw";

  int files = 0, silent = 0, parseFail = 0, ffOk = 0, identical = 0, differ = 0;
  long maxEven = 0;
  for (const auto& e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file() || lower(e.path().extension().string()) != ".xa") continue;
    files++;
    std::vector<uint8_t> data;
    PcmBuffer pcm, pcmUp;
    if (!readAll(e.path(), data) || !decodeXa(data, pcm, true)) { parseFail++; std::printf("PARSE FAIL %s\n", e.path().string().c_str()); continue; }
    decodeXa(data, pcmUp, false);
    int peak = 0;
    for (int16_t v : pcm.samples) peak = std::max(peak, std::abs(int(v)));
    if (peak < 100) { silent++; std::printf("SILENT %s (peak %d)\n", e.path().string().c_str(), peak); }
    if (!haveFfmpeg) continue;
    std::error_code ec;
    fs::remove(tmp, ec);
    std::string cmd = "ffmpeg -v error -y -i \"" + e.path().string() + "\" -f s16le \"" + tmp.string() + "\" > " NULL_SINK " 2>&1";
    std::vector<uint8_t> ref;
    if (std::system(cmd.c_str()) != 0 || !readAll(tmp, ref) || ref.empty()) continue;   // ffmpeg rejects some files (e.g. magic "XAo")
    ffOk++;
    const int16_t* r = reinterpret_cast<const int16_t*>(ref.data());
    size_t n = std::min(ref.size() / 2, pcmUp.samples.size());
    bool same = true;
    for (size_t i = 0; i < n; i++) if (r[i] != pcmUp.samples[i]) { same = false; break; }
    if (same)   // files where ffmpeg clamps differently would inflate the figure
      for (size_t i = 0; i < n; i++) maxEven = std::max(maxEven, std::labs(long(r[i]) - long(pcm.samples[i])));
    if (same) identical++; else { differ++; std::printf("differs from ffmpeg (ties-up): %s\n", e.path().string().c_str()); }
  }
  fs::remove(tmp);
  std::printf("%d files: %d parse failures, %d silent\n", files, parseFail, silent);
  if (haveFfmpeg) std::printf("ffmpeg decoded %d: %d identical with ties-up, %d differ; ties-to-even max error %ld LSB (over the identical files)\n", ffOk, identical, differ, maxEven);
  else std::printf("ffmpeg not found on PATH: decode health only\n");
  return 0;
}
