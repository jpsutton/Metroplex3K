#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <thread>
#include "core/audio.h"
#include "core/bytes.h"
#include "core/video.h"
#include "core/xa_codec.h"
#include "testutil.h"
using namespace m3k;

static Bytes xaHeader(uint32_t outSize, uint16_t ch) {
  Bytes b = {'X', 'A', 'I', 0};
  put32(b, outSize); put16(b, 1); put16(b, ch); put32(b, 22050); put32(b, 22050u * 2 * ch);
  put16(b, uint16_t(2 * ch)); put16(b, 16);
  return b;
}

static Bytes wavBytes(const std::vector<int16_t>& s, int ch, int rate) {
  Bytes b = {'R', 'I', 'F', 'F'};
  put32(b, uint32_t(36 + s.size() * 2));
  for (char c : std::string("WAVEfmt ")) b.push_back(uint8_t(c));
  put32(b, 16); put16(b, 1); put16(b, uint16_t(ch)); put32(b, uint32_t(rate)); put32(b, uint32_t(rate * ch * 2));
  put16(b, uint16_t(ch * 2)); put16(b, 16);
  for (char c : std::string("data")) b.push_back(uint8_t(c));
  put32(b, uint32_t(s.size() * 2));
  for (auto v : s) put16(b, uint16_t(v));
  return b;
}

static void testXa() {
  // Mono: header p=1 s=10 (0x1A); first byte codes 6,0; then zeros.
  Bytes f = xaHeader(56, 1);
  f.push_back(0x1A);
  f.push_back(0x60);
  for (int i = 0; i < 13; i++) f.push_back(0x00);
  PcmBuffer pcm;
  CHECK(decodeXa(f, pcm));
  CHECK(pcm.channels == 1 && pcm.sampleRate == 22050 && pcm.frames() == 28);
  CHECK(pcm.samples[0] == 24);
  CHECK(pcm.samples[1] == 22);  // 22.5 ties to even
  CHECK(pcm.samples[2] == 21);  // 0.9375*22 = 20.625
  CHECK(pcm.samples[3] == 20);
  PcmBuffer up;
  CHECK(decodeXa(f, up, false));
  CHECK(up.samples[1] == 23);  // ties up

  // Stereo: L p=1 s=10, R p=0 s=12. Pairs (L,R).
  Bytes g = xaHeader(112, 2);
  g.push_back(0x1A); g.push_back(0x0C);
  g.push_back(0x60); g.push_back(0x78);  // L codes 6,0 ; R codes 7,8(-8 -> d=-8)
  for (int i = 1; i < 14; i++) { g.push_back(0); g.push_back(0); }
  PcmBuffer st;
  CHECK(decodeXa(g, st));
  CHECK(st.channels == 2 && st.frames() == 28);
  CHECK(st.samples[0] == 24 && st.samples[1] == 7);
  CHECK(st.samples[2] == 22 && st.samples[3] == -8);
  CHECK(st.samples[4] == 21 && st.samples[5] == 0);  // p=0 has no memory

  // Shift 13 gives d=0; history carries across blocks.
  Bytes h = xaHeader(112, 1);
  h.push_back(0x1A); h.push_back(0x60); for (int i = 0; i < 13; i++) h.push_back(0);
  h.push_back(0x1D);  // p=1 s=13
  h.push_back(0x70);
  for (int i = 0; i < 13; i++) h.push_back(0);
  PcmBuffer hp;
  CHECK(decodeXa(h, hp));
  CHECK(hp.frames() == 56 && hp.samples[28] != 7);  // continues the filter from block 1, code 7 ignored (d=0)

  // Truncated: tail silence; bad header rejected.
  Bytes t(f.begin(), f.begin() + 30);
  PcmBuffer tp; CHECK(decodeXa(t, tp) && tp.frames() == 28 && tp.samples[0] == 0);
  Bytes bad = f; bad[8] = 2;
  CHECK(!decodeXa(bad, tp));
  bad = f; bad[0] = 'Y'; CHECK(!decodeXa(bad, tp));
  // Channels 3 => silence.
  Bytes c3 = xaHeader(8, 3); PcmBuffer cp; CHECK(decodeXa(c3, cp) && cp.samples.size() == 4);

  // WAV
  auto w = wavBytes({1, -2, 3, -4}, 2, 22050);
  PcmBuffer wp; CHECK(loadWav(w, wp) && wp.channels == 2 && wp.frames() == 2 && wp.samples[1] == -2);
  CHECK(loadSoundFile("A\\B.WAV", w, wp));
  CHECK(!loadSoundFile("x.mp3", w, wp));
}

static void testIni() {
  const char* txt =
      "; comment\r\n[Options]\r\nZoomLoopVol=800\r\nBuildingRandMultiplier=10000\r\n"
      "[1ShotSfx]\r\n"
      "1=\"ZoomIn\", 32, 1024, 1, 0, 1, 0, \"Sfx\\Interface\\zmin.xa\"\r\n"
      "0x22 = \"Siren NA\", 32, 900, 0, 0, 0, 0, \"Sfx\\na.xa\", 1\r\n"
      "0x22 = \"Siren EU\", 32, 800, 0, 0, 0, 0, \"Sfx\\eu.xa\", 2\r\n"
      "600=\"too big\",32,1,0,0,0,0,\"x.xa\"\r\n"
      "5=\"Tab\",32,1024,0,0,0,0,\"Sfx\\Buildings\t\\Defense.xa\"\r\n"
      "5=\"Replaced\",32,1000,0,0,0,0,\"b.xa\"\r\n"
      "[Fading]\r\n18=\"Raise\",32,1024,1,1,0,0,\"\",\"Sfx\\raise.wav\"\r\n"
      "[Disaster]\r\n61=\"Quake\",32,1024,2,0,\"q.wav\",62\r\n"
      "[Actor]\r\n130=\"Police\",32,1024,0,0,0,\"d.xa\",\"s1.xa\",\"h.xa\"\r\n"
      "[Sparse]\r\n0x100=\"Gen\",32,1024,1,0,0,\"Sfx\\Buildings\\g.xa\",100,600\r\n"
      "[Music]\r\n"
      ";1=\"old\",0,\"a.xa\",\"\",0,1,0,1\r\n"
      "2=\"B\",0,\"b.xa\",\"\",0,1,0,1\r\n"
      "0x0F=\"SC2K\",0,\"Music\\SC2KBug.xa\",\"\",0,0,0,1\r\n"
      "1=\"New\",0,\"n.xa\",\"\",0,1,0,0\r\n";
  AudioIni ini = AudioIni::parse(txt);
  CHECK(ini.options.get("zoomloopvol", 0) == 800);
  CHECK(ini.oneShots.size() == 3);
  CHECK(ini.oneShots.at(1).file == "Sfx\\Interface\\zmin.xa" && ini.oneShots.at(1).interrupt);
  CHECK(ini.oneShots.at(0x22).vol == 900);  // NA locale wins by default country 54
  CHECK(ini.oneShots.at(5).vol == 1000);
  AudioIni eu = AudioIni::parse(txt, 12);
  CHECK(eu.oneShots.at(0x22).vol == 800);
  CHECK(!ini.oneShots.count(600));
  CHECK(ini.fading.at(18).loopFile == "Sfx\\raise.wav" && ini.fading.at(18).oneShotFile.empty());
  CHECK(ini.disasters.at(61).fadingId == 62 && ini.disasters.at(61).spatialScheme == 2);
  CHECK(ini.actors.at(130).special2 == "h.xa");
  CHECK(ini.sparse.at(0x100).samples.size() == 1 && ini.sparse.at(0x100).samples[0].trigger == 600);
  CHECK(ini.music.size() == 3 && ini.music[0].id == 2 && ini.music[2].id == 1 && ini.music[2].file == "n.xa");
  CHECK(!ini.music[1].inSongList);
  auto f = splitIniFields("\"a,b\", 3 ,\"\"");
  CHECK(f.size() == 3 && f[0] == "a,b" && f[1] == "3" && f[2].empty());
}

static void testMixer() {
  AudioMixer m;
  m.setOutputRate(22050);
  auto pcm = std::make_shared<PcmBuffer>();
  pcm->channels = 1; pcm->sampleRate = 22050; pcm->samples.assign(100, 1000);
  VoiceParams p;
  auto h = m.play(pcm, p);
  CHECK(h && m.isPlaying(h));
  int16_t out[2 * 60];
  m.mixAudio(out, 60);
  CHECK(out[0] == 1000 && out[1] == 1000 && out[118] == 1000);
  m.mixAudio(out, 60);  // 40 left, then silence
  CHECK(out[78] == 1000 && out[80] == 0);
  CHECK(!m.isPlaying(h));
  // pan + category volume
  p.pan = 1.f;
  m.setCategoryVolume(AudioCategory::Sfx, 512);
  m.play(pcm, p);
  m.mixAudio(out, 2);
  CHECK(out[0] == 0 && out[1] == 500);
  m.stopAll();
  // categories off, loop, fade out
  m.setCategoryVolume(AudioCategory::Sfx, 1024);
  m.setCategoryEnabled(AudioCategory::Sfx, false);
  p = VoiceParams(); p.loop = true;
  auto lh = m.play(pcm, p);
  m.mixAudio(out, 60); CHECK(out[0] == 0 && m.isPlaying(lh));
  m.setCategoryEnabled(AudioCategory::Sfx, true);
  m.mixAudio(out, 60); CHECK(out[0] == 1000);
  m.mixAudio(out, 60); CHECK(m.isPlaying(lh) && out[2] == 1000);  // wrapped loop
  m.fadeOut(lh, 10);  // 220 frames
  int16_t big[2 * 400];
  m.mixAudio(big, 400);
  CHECK(big[0] > big[200] && big[2 * 399] == 0 && !m.isPlaying(lh));
  // Resample: 11025 Hz source at 22050 out doubles length.
  auto lo = std::make_shared<PcmBuffer>(); lo->channels = 1; lo->sampleRate = 11025; lo->samples.assign(10, 500);
  m.play(lo, VoiceParams());
  m.mixAudio(big, 40);
  CHECK(big[2 * 18] == 500 && big[2 * 21] == 0);
  // Stereo + delay + voice cap with priority.
  m.stopAll(); m.setMaxVoices(1);
  VoiceParams lowp; lowp.priority = 10; VoiceParams hip; hip.priority = 50;
  auto a = m.play(pcm, hip);
  CHECK(m.play(pcm, lowp) == 0 && m.isPlaying(a));
  auto b = m.play(pcm, hip);
  CHECK(b && !m.isPlaying(a));
}

static void testPlaylistAndRng() {
  AudioRng r(1);
  uint64_t v = 1ull * 1103515245ull + 12345ull;
  CHECK(r.next() == uint32_t(v >> 16));
  MusicPlaylist pl;
  std::vector<MusicDef> songs;
  for (int i = 1; i <= 9; i++) { MusicDef d; d.id = i; d.inSongList = i != 9; songs.push_back(d); }
  pl.setSongs(songs);
  CHECK(pl.eligibleCount() == 8);
  std::vector<int> seq;
  for (int i = 0; i < 40; i++) {
    int id = pl.pickNext(r);
    CHECK(id >= 1 && id <= 8);
    seq.push_back(id);
  }
  // No song repeats within the last floor(2N/3)=5 picks.
  for (size_t i = 8; i < seq.size(); i++)
    for (size_t j = i - 5; j < i; j++) CHECK(seq[i] != seq[j]);
  pl.setEnabled(1, false);
  CHECK(pl.eligibleCount() == 7);
  for (int i = 1; i <= 8; i++) pl.setEnabled(i, false);
  CHECK(pl.pickNext(r) == 0);
}

static void testEvents() {
  AudioRng r(5);
  CHECK(pickExplosionSound(0x119, 0, r) == 76);
  int e = pickExplosionSound(0x11B, 0, r); CHECK(e == 134 || e == 135);
  for (int i = 0; i < 50; i++) {
    int s = pickExplosionSound(0, 513, r); CHECK(s >= 103 && s <= 108);
    int m = pickExplosionSound(0, 514, r); CHECK(m >= 109 && m <= 112);
    int l = pickExplosionSound(0, 2501, r); CHECK(l == 134 || l == 135);
  }
  TileSoundContext c; c.kind = TileKind::Tree; c.desert = true;
  CHECK(querySoundFor(c).ids == std::vector<int>{252});
  c.desert = false; c.jungle = true; CHECK(querySoundFor(c).ids == std::vector<int>{253});
  c = TileSoundContext(); c.kind = TileKind::Commercial; CHECK(querySoundFor(c).ids == std::vector<int>{137});
  c.sparseId = 0x110; auto q = querySoundFor(c); CHECK(q.sparseId == 0x110 && q.ids.empty());
  auto pl = placementSoundFor(c); CHECK(pl.sparseId == 0x110 && pl.ids == std::vector<int>{22});
  c.sparseId = 0; pl = placementSoundFor(c); CHECK(pl.ids == std::vector<int>{137} && pl.sparseId == 0);
  c.view = SoundView::Subway; CHECK(placementSoundFor(c).ids == std::vector<int>{53});
  CHECK(querySoundFor(c).ids.empty());
  CHECK(repairCategoryVolume(AudioCategory::Music, 5000) == 1000);
  CHECK(repairCategoryVolume(AudioCategory::Sfx, -1) == 700);
  CHECK(repairCategoryVolume(AudioCategory::Ambience, 2000) == 900);
  CHECK(repairCategoryVolume(AudioCategory::Ambience, 300) == 300);
  CHECK(ambienceFadeVolume(100, 50, 200) == 1024 && ambienceFadeVolume(0, 50, 200) == 200);
  CHECK(ambienceFadeVolume(25, 50, 200) == 612);
  int hits = 0;
  AudioRng r2(9);
  for (int i = 0; i < 20000; i++) hits += sparseTriggers(600, 10000, r2);
  CHECK(hits > 800 && hits < 1600);
}

static void testArea() {
  AmbientAreaSelector s;
  s.configure(1000, 5.f);
  std::vector<float> w(ambientAreas().size(), 0.f);
  CHECK(ambientAreas().size() == 22);
  w[0] = 1.f;
  CHECK(s.update(10, w, 0) == 0);  // first selection immediate
  w[0] = 0; w[3] = 1.f;
  CHECK(s.update(500, w, 0) == 0);
  CHECK(s.update(600, w, 100.f) == 0);  // scrolling fast: hold
  CHECK(s.update(400, w, 0) == 0);      // 900 ms accumulated
  CHECK(s.update(200, w, 0) == 3);
}

static void testFsc() {
  // 1x3 grid, all always-ready, one voice, 100 ms beats, 4 beats/bar.
  std::string t = "version 7\r\n# hdr\r\n1024\t32\t1\t1\t1\t3\t600\t4\t1\t0\t0\t0\t2\t0\r\n";
  for (int c = 0; c < 3; c++)
    t += "1024\t0\t512\t512\t0\t10\t20\t0\t0\t0\t0\t0\t20\t0\t0\t0\t0\t0\ts" + std::to_string(c) + ".xa\r\n";
  FscSchedule s;
  CHECK(FscSchedule::parse(t, s));
  CHECK(s.rows == 1 && s.cols == 3 && s.tempo == 600 && s.cells.size() == 3 && s.cells[1].file == "s1.xa" && s.cells[0].prob == 20);
  {  // shipped files start the header and cell rows with a tab
    std::string t2 = "version 7\r\n#\tvol\r\n\t1024\t32\t1\t1\t1\t2\t600\t4\t1\t0\t0\t0\t2\t0\r\n";
    for (int c = 0; c < 2; c++) t2 += "\t1024\t0\t512\t512\t0\t10\t20\t0\t0\t0\t0\t0\t20\t0\t0\t0\t0\t0\tt" + std::to_string(c) + ".xa\r\n";
    FscSchedule s2;
    CHECK(FscSchedule::parse(t2, s2));
    CHECK(s2.vol == 1024 && s2.cols == 2 && s2.tempo == 600 && s2.cells.size() == 2 && s2.cells[1].file == "t1.xa" && s2.cells[0].prob == 20);
  }
  CHECK(fscProbThreshold(0) == 0 && fscProbThreshold(10) == 32767 && fscProbThreshold(18) == 300000);
  FscScheduler sch;
  sch.init(&s, 3);
  std::vector<FscScheduler::Action> a;
  sch.update(299, a);
  CHECK(a.empty());
  sch.update(300, a);  // beat 3 = BPB-1
  CHECK(a.size() == 1 && a[0].start && sch.playingCount() == 1);
  int first = a[0].cell;
  a.clear();
  sch.update(700, a);  // beat 7: retire the playing cell, start another (not the last one)
  CHECK(a.size() == 2 && !a[0].start && a[0].cell == first && a[1].start && a[1].cell != first);
  CHECK(sch.playingCount() == 1);
  // never exceeds max
  a.clear();
  sch.update(100000, a);
  CHECK(sch.playingCount() <= 1);
  // prob 0 never plays
  FscSchedule z = s; for (auto& c : z.cells) c.prob = 0;
  FscScheduler s2; s2.init(&z, 1); a.clear(); s2.update(5000, a); CHECK(a.empty());
  // NONE cells are not ready
  FscSchedule n = s; for (auto& c : n.cells) c.none = true;
  FscScheduler s3; s3.init(&n, 1); a.clear(); s3.update(5000, a); CHECK(a.empty());
  // exclusions: cell 0 and 1 always excluded by cell 2 playing -> only exclusion check
  FscSchedule e = s; e.maxVoices = 3; e.minVoices = 0;
  e.cells[0].exclusions = {{0, 2}}; e.cells[1].exclusions = {{0, 2}};
  FscScheduler s4; s4.init(&e, 1); a.clear(); s4.update(100000, a);
  CHECK(s4.playingCount() <= 3);
}

static void testSystem() {
  std::map<std::string, Bytes> files;
  std::vector<int16_t> tone(2205, 8000);
  files["sfx/interface/zmin.xa"] = {};  // replaced below
  { Bytes f = xaHeader(56, 1); f.push_back(0x1A); f.push_back(0x60); for (int i = 0; i < 13; i++) f.push_back(0); files["sfx/interface/zmin.xa"] = f; }
  files["sfx/raise.wav"] = wavBytes(tone, 1, 22050);
  files["music/a.xa"] = wavBytes(std::vector<int16_t>(500, 100), 1, 22050);  // loaded by name .xa? use wav names below
  files["music/a.wav"] = wavBytes(std::vector<int16_t>(500, 100), 1, 22050);
  files["music/b.wav"] = wavBytes(std::vector<int16_t>(500, 100), 1, 22050);
  files["sfx/zones/residential/residential.fsc"] = Bytes();
  {
    std::string t = "version 7\r\n1024\t32\t1\t1\t1\t1\t600\t4\t0\t0\t0\t0\t0\t0\r\n"
                    "1024\t0\t512\t512\t0\t0\t0\t0\t0\t0\t0\t0\t20\t0\t0\t0\t0\t0\tamb.wav\r\n";
    files["sfx/zones/residential/residential.fsc"] = Bytes(t.begin(), t.end());
    files["sfx/zones/residential/amb.wav"] = wavBytes(std::vector<int16_t>(4000, 2000), 1, 22050);
  }
  auto loader = [files](const std::string& p, bool, Bytes& out) {
    std::string k = toLower(normalizeSoundPath(p));
    auto it = files.find(k);
    if (it == files.end()) return false;
    out = it->second; return true;
  };
  const char* txt =
      "[Options]\r\nMusicPlayerSongDelayMsec=100\r\n"
      "[1ShotSfx]\r\n1=\"zi\",32,1024,0,0,1,0,\"Sfx\\Interface\\ZMIN.XA\"\r\n2=\"zo\",32,1024,0,0,0,0,\"nope.xa\"\r\n"
      "[Fading]\r\n18=\"Raise\",32,1024,0,0,0,0,\"\",\"Sfx\\raise.wav\"\r\n"
      "[Music]\r\n1=\"A\",0,\"Music\\a.wav\",\"\",0,1,0,1\r\n2=\"B\",0,\"Music\\b.wav\",\"\",0,1,0,1\r\n";
  AudioSystem as;
  as.init(AudioIni::parse(txt), loader, 22050);
  as.setAudioZoom(5);
  CHECK(!as.request(1) );  // zoom in ignored at 5
  as.setAudioZoom(3);
  CHECK(as.request(1));
  CHECK(as.mixer().voiceCount() == 1);
  CHECK(as.request(1));  // interrupt: restarts, still one voice
  CHECK(as.mixer().voiceCount() == 1);
  CHECK(!as.request(999));
  CHECK(as.request(2) && as.mixer().voiceCount() == 1);  // missing file: silent
  int16_t buf[2 * 100];
  as.mixAudio(buf, 100);
  CHECK(buf[0] == 24 && buf[2] != 0);
  // fading loop
  CHECK(as.request(18, SoundAction::Play));
  int n = as.mixer().voiceCount();
  CHECK(n >= 1);
  as.request(18, SoundAction::SetAudible, 0);
  for (int i = 0; i < 5; i++) as.mixAudio(buf, 100);
  as.request(18, SoundAction::Stop);
  for (int i = 0; i < 100; i++) as.mixAudio(buf, 100);  // let the fade finish
  as.update(0);
  // music: first update starts a song
  as.update(10);
  CHECK(as.currentSong() == 1 || as.currentSong() == 2);
  int first = as.currentSong();
  for (int i = 0; i < 10; i++) as.mixAudio(buf, 100);  // song (500 samples) ends
  as.update(10);
  CHECK(as.currentSong() == 0);
  as.update(200);
  CHECK(as.currentSong() != 0 && as.currentSong() != first);
  as.stopMusic(); CHECK(as.currentSong() == 0);
  // ambient
  CHECK(as.setAmbientArea(0));
  as.update(310);
  CHECK(as.mixer().voiceCount() >= 1);
  CHECK(!as.setAmbientArea(1));  // no file
}

static void testSparse() {
  // Rectangle: zoom 4 -> half-size 6, x outer / z inner, clipped to the map; ids 0 and repeats skipped, one item per id.
  std::vector<std::pair<int, int>> visited;
  auto idAt = [&](int x, int z) { visited.push_back({x, z}); return x == 10 && z == 10 ? 7 : x == 11 && z == 3 ? 7 : x == 12 ? 9 : 0; };
  auto sites = collectSparseSites(10, 10, 4, 100, 100, idAt);
  CHECK(sites.size() == 2 && sites[0].id == 7 && sites[0].centre && sites[1].id == 9 && !sites[1].centre);
  CHECK(visited.size() == 1 + 12 * 12);   // centre probe + (cx-6 .. cx+5) x (cz-6 .. cz+5)
  CHECK(visited[1] == std::make_pair(4, 4) && visited[2] == std::make_pair(4, 5));
  visited.clear();
  collectSparseSites(0, 0, 5, 100, 100, idAt);   // zoom 5: h = 3, clipped at the origin
  CHECK(visited.size() == 1 + 3 * 3);
  CHECK(collectSparseSites(1, 1, 4, 100, 100, [](int, int) { return 0; }).empty());

  std::map<std::string, Bytes> files;
  files["sfx/buildings/g.wav"] = wavBytes(std::vector<int16_t>(20000, 3000), 1, 22050);
  auto loader = [files](const std::string& p, bool, Bytes& out) {
    auto it = files.find(toLower(normalizeSoundPath(p)));
    if (it == files.end()) return false;
    out = it->second; return true;
  };
  const char* txt =
      "[Options]\r\nBuildingRandMultiplier=10000\r\nBuildingsInAmbienceMinZoom=4\r\nBuildingsInAmbienceAttenuation=400\r\n"
      "[Sparse]\r\n0x100=\"Always\",32,1024,1,0,0,\"Sfx\\Buildings\\g.wav\",100,10000\r\n"
      "0x101=\"Never\",32,1024,1,0,0,\"Sfx\\Buildings\\g.wav\",100,0\r\n";
  AudioSystem as;
  as.init(AudioIni::parse(txt), loader, 22050);
  as.setAudioZoom(3);
  as.rebuildSparse({{0x100, true}});
  CHECK(as.sparseVoices() == 0);                 // below BuildingsInAmbienceMinZoom
  as.setAudioZoom(4);
  as.rebuildSparse({{0x101, true}});
  CHECK(as.sparseVoices() == 0);                 // trigger 0 never sounds
  as.rebuildSparse({{0x100, true}, {0x100, false}});
  CHECK(as.sparseVoices() == 2);
  as.clearSparse();
  CHECK(as.sparseVoices() == 0);
  CHECK(as.mixer().voiceCount() == 0);
}

static void testVideo() {
  Bytes d;
  auto chunk = [&](const char* tag, const Bytes& body) {
    for (int i = 0; i < 4; i++) d.push_back(uint8_t(tag[i]));
    put32(d, uint32_t(body.size() + 8));
    d.insert(d.end(), body.begin(), body.end());
  };
  Bytes sch = {'P', 'T', 0, 0, 0x85, 3, 0x00, 0x00, 28, 0x82, 1, 2, 0x83, 1, 7, 0xFF};
  chunk("SCHl", sch);
  Bytes sccl; put32(sccl, 1); chunk("SCCl", sccl);
  Bytes vf; put16(vf, 320); put16(vf, 480); vf.push_back(5); vf.push_back(6); vf.push_back(0xAA);
  chunk("pIQT", vf); chunk("pIQT", vf);
  Bytes ad; put32(ad, 28); put16(ad, 100); put16(ad, 0); put16(ad, 0); put16(ad, 0);
  ad.push_back(0x10); ad.push_back(0xCC);
  ad.push_back(0x11); for (int i = 1; i < 28; i++) ad.push_back(0);
  chunk("SCDl", ad);
  chunk("SCEl", Bytes());
  TgqMovie mv;
  CHECK(mv.parse(d));
  CHECK(mv.audio().channels == 2 && mv.audio().compression == 7 && mv.audio().sampleRate == 22050 && mv.audio().totalFrames == 28);
  CHECK(mv.frames().size() == 2 && mv.frames()[0].width == 320 && mv.frames()[0].height == 480 && mv.frames()[0].quant1 == 6);
  CHECK(mv.sawEnd() && mv.declaredAudioChunks() == 1 && mv.audioChunks().size() == 1);
  PcmBuffer pcm;
  CHECK(mv.decodeAudio(pcm));
  CHECK(pcm.channels == 2 && pcm.frames() == 28 && pcm.samples[0] == 95 && pcm.samples[1] == 1);
  CHECK(mv.frameAtMs(0) == 0 && mv.frameAtMs(100) == 1 && mv.frameAtMs(100000) == 1);
  bool called = false;
  std::vector<uint32_t> px;
  CHECK(mv.decodeFrame(1, [&](const uint8_t* p, size_t n, int w, int h, std::vector<uint32_t>& o) { called = n == 7 && w == 320 && h == 480; o.assign(1, 7); return true; }, px) && called);
  CHECK(shouldPlayIntro(false, "on", true) && !shouldPlayIntro(true, "", true) && !shouldPlayIntro(false, "OFF", true) && !shouldPlayIntro(false, "", false));
  CHECK(introVolume(128) == 0.f && introVolume(512) == 0.5f);
  auto r = introRect(1024, 768); CHECK(r.x == 192 && r.y == 144);
  auto s = stretchFrame2x({1, 2}, 2, 1); CHECK(s.size() == 4 && s[1] == 1 && s[2] == 2);
  Bytes junk = {1, 2, 3}; TgqMovie m2; CHECK(!m2.parse(junk));
}

// Async music: playSong returns at once, the voice starts from update() after the worker decodes it.
static void testAsyncMusic() {
  std::promise<void> release;
  std::shared_future<void> gate = release.get_future().share();
  Bytes song = wavBytes(std::vector<int16_t>(500, 100), 1, 22050);
  auto loader = [gate, song](const std::string& p, bool, Bytes& out) {
    if (toLower(normalizeSoundPath(p)) != "music/a.wav") return false;
    gate.wait();
    out = song; return true;
  };
  AudioSystem as;
  as.init(AudioIni::parse("[Music]\r\n1=\"A\",0,\"Music\\a.wav\",\"\",0,1,0,1\r\n"), loader, 22050);
  as.setAsyncMusic(true);
  CHECK(as.playSong(1));
  CHECK(as.currentSong() == 1);
  as.update(10);
  CHECK(as.mixer().voiceCount() == 0);  // still decoding
  release.set_value();
  for (int i = 0; i < 500 && as.mixer().voiceCount() == 0; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    as.update(10);
  }
  CHECK(as.mixer().voiceCount() == 1 && as.currentSong() == 1);
  // A cached song starts synchronously.
  as.stopMusic();
  CHECK(as.playSong(1) && as.mixer().voiceCount() == 1);
  // stopMusic while a decode is pending: the decode lands in the cache but no voice starts.
  AudioSystem bs;
  std::promise<void> r2;
  std::shared_future<void> g2 = r2.get_future().share();
  bs.init(AudioIni::parse("[Music]\r\n1=\"A\",0,\"Music\\a.wav\",\"\",0,1,0,1\r\n"),
          [g2, song](const std::string&, bool, Bytes& out) { g2.wait(); out = song; return true; }, 22050);
  bs.setAsyncMusic(true);
  bs.setMusicEnabled(false);
  CHECK(bs.playSong(1));
  bs.stopMusic();
  r2.set_value();
  for (int i = 0; i < 50; i++) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); bs.update(10); }
  CHECK(bs.mixer().voiceCount() == 0 && bs.currentSong() == 0);
}

// The mixer is driven from the audio thread while the game thread changes voices.
static void testMixerThreads() {
  AudioMixer m;
  auto pcm = std::make_shared<PcmBuffer>();
  pcm->channels = 1; pcm->sampleRate = 44100; pcm->samples.assign(4410, 1000);
  std::atomic<bool> stop{false};
  std::thread audio([&] { int16_t buf[2 * 256]; while (!stop) m.mixAudio(buf, 256); });
  for (int i = 0; i < 2000; i++) {
    VoiceParams p; p.loop = (i % 3) == 0;
    auto h = m.play(pcm, p);
    m.setVolume(h, 512, 10); m.setPan(h, 0.5f);
    if (i % 2) m.fadeOut(h, 5); else m.stop(h);
  }
  stop = true; audio.join();
  m.stopAll();
  CHECK(m.voiceCount() == 0);
}

int main() {
  testXa();
  testIni();
  testMixer();
  testPlaylistAndRng();
  testEvents();
  testArea();
  testFsc();
  testSystem();
  testSparse();
  testAsyncMusic();
  testMixerThreads();
  testVideo();
  TEST_MAIN_END();
}
