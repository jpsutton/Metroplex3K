// SPDX-FileCopyrightText: 2025-2026 Metroplex3K contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Platform-independent audio engine core: AUDIO.INI, FSC ambient schedules, mixer, music playlist,
// event sound selection. spec/06-audio.md. No SDL / OS audio: the platform layer calls
// AudioSystem::mixAudio(out, frames) from its audio callback. AudioMixer locks its own voice list, so
// the callback may run concurrently with the rest of AudioSystem; everything else is game-thread only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "core/fsutil.h"
#include "core/xa_codec.h"

namespace m3k {

// ---------------------------------------------------------------- RNG (spec 7.3)
class AudioRng {
 public:
  explicit AudioRng(uint32_t seed = 1) : s_(seed) {}
  // state' = state*1103515245+12345 (mod 2^32); output = bits 16.. of the 64-bit product-sum.
  uint32_t next() {
    uint64_t v = uint64_t(s_) * 1103515245ull + 12345ull;
    s_ = uint32_t(v);
    return uint32_t(v >> 16);
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
  uint32_t state() const { return s_; }
 private:
  uint32_t s_;
};

// ---------------------------------------------------------------- AUDIO.INI (spec 4)
constexpr int kCountryDefault = 54;
inline bool countryIsNorthAmerica(int c) { return c == 8 || c == 54; }
// Locale filter (spec 4.3): 0 = always, 1 = North America only, 2 = elsewhere.
inline bool localeMatches(int locale, int country) {
  return locale == 0 || (locale == 1 && countryIsNorthAmerica(country)) || (locale == 2 && !countryIsNorthAmerica(country));
}

struct OneShotDef {
  int id = 0; std::string name; int pri = 32, vol = 1024; bool load = false, stream = false, interrupt = false;
  bool absolute = false; std::string file; int locale = 0;
};
struct FadingDef {
  int id = 0; std::string name; int pri = 32, vol = 1024; bool preload = false, autofade = false, useAmbienceVol = false;
  bool absolute = false; std::string oneShotFile, loopFile;
};
struct DisasterDef {
  int id = 0; std::string name; int pri = 32, vol = 1024;
  int spatialScheme = 0;  // 0 positioned, 1 surface only, 2 all views
  bool absolute = false; std::string file; int fadingId = 0;
};
struct ActorDef {
  int id = 0; std::string name; int pri = 32, vol = 1024, locale = 0;
  int type = 2;  // 0 horn, 1 altitude, 2 none
  bool absolute = false; std::string driveBy, special1, special2;
};
struct SparseSample { std::string file; int vol = 100; int trigger = 0; };
struct SparseDef {
  int id = 0; std::string name; int pri = 32, vol = 1024; bool absolute = false; int schedule = 0;
  std::vector<SparseSample> samples;
};
struct MusicDef {
  int id = 0; std::string name; int type = 0; std::string file, demoFile;
  bool highPerformance = false, inSongList = false, absolute = false, enabled = true;
};

struct AudioOptions {
  std::map<std::string, int> values;  // keys lower-cased
  int get(const std::string& key, int dflt) const;
};

class AudioIni {
 public:
  // Parses AUDIO.INI text. Entries whose locale does not match are ignored before replacing.
  // IDs outside 1..511 are ignored; later definitions replace earlier ones.
  static AudioIni parse(const std::string& text, int country = kCountryDefault);

  std::map<int, OneShotDef> oneShots;
  std::map<int, FadingDef> fading;
  std::map<int, DisasterDef> disasters;
  std::map<int, ActorDef> actors;
  std::map<int, SparseDef> sparse;
  std::vector<MusicDef> music;  // INI/ID order (sorted by id; later definition of same id replaces)
  AudioOptions options;

  bool hasSound(int id) const;
};

// Split one INI value into comma fields; quoted fields lose quotes (tabs inside quotes are kept),
// unquoted fields are space-trimmed.
std::vector<std::string> splitIniFields(const std::string& value);
// Normalise a file name: '\\' -> '/'.
std::string normalizeSoundPath(const std::string& p);

// ---------------------------------------------------------------- FSC schedule (spec 5.2)
struct FscCell {
  int vol = 0, lr = 512, fb = 512, fin = 0, fout = 0, dly = 0, loop = 0, loopFlag = 0, quant = 0, prob = 0;
  int pitchL = 0, pitchH = 0, group = 0;
  bool stereo = false, none = true;
  std::string file;
  std::vector<std::pair<int, int>> exclusions;  // (row, col)
};
struct FscSchedule {
  int version = 0, vol = 1024, pri = 32, minVoices = 0, maxVoices = 1, rows = 0, cols = 0, tempo = 60, bpb = 4;
  int selX = 0, selY = 0, quanX = 0, quanY = 0, difX = 0, difY = 0;
  std::vector<FscCell> cells;  // rows*cols, row-major
  const FscCell& cell(int r, int c) const { return cells[size_t(r) * cols + c]; }
  static bool parse(const std::string& text, FscSchedule& out);
};

// Probability table T(p) of spec 5.2 (p clamped 0..20; 20 = always).
int64_t fscProbThreshold(int p);

class FscScheduler {
 public:
  struct Action { bool start; int cell; };  // start a cell / fade a cell out
  void init(const FscSchedule* s, uint32_t seed = 1, int densityDivisor = 10);
  // Advance to `nowMs` (ms since schedule start). Appends start/fade-out actions.
  void update(int64_t nowMs, std::vector<Action>& out);
  void cellEnded(int cell);  // sample finished by itself (or fade-out completed)
  int playingCount() const;  // cells in the playing state (fading-out not counted)
  bool isPlaying(int cell) const { return st_[size_t(cell)] == 1; }
  int lastStarted() const { return last_; }
  uint32_t beatsProcessed() const { return beats_; }
 private:
  bool ready(int idx, int row, int col, bool inWindow, int preferGroup);
  int pick(int preferGroup);
  void onBeat(std::vector<Action>& out);
  void startCell(int idx, std::vector<Action>& out);
  const FscSchedule* s_ = nullptr;
  std::vector<uint8_t> st_;  // 0 idle, 1 playing, 2 fading out
  int last_ = -1, density_ = 10;
  int64_t k_ = 0;  // next beat number
  int64_t base_ = 0;  // ms offset of beat 0 (24h restarts)
  uint32_t beats_ = 0;
  AudioRng rng_;
  AudioRng crt_;  // stands in for the C runtime rand()
};

// ---------------------------------------------------------------- Ambient areas (spec 5.1, 5.3)
struct AmbientArea { const char* name; const char* schedule; const char* folder; };
const std::vector<AmbientArea>& ambientAreas();  // 22 areas of spec 5.1 in table order

class AmbientAreaSelector {
 public:
  void configure(int minHoldMs, float maxScrollSpeed) { minHold_ = minHoldMs; maxSpeed_ = maxScrollSpeed; }
  void reset(int area) { cur_ = area; cand_ = -1; hold_ = 0; }
  // weights: relative share per area (index = ambientAreas()). Returns the selected area (-1 none).
  // The dominant area must persist for minHold before the switch; no switch while scrolling fast.
  int update(int64_t dtMs, const std::vector<float>& weights, float scrollSpeed);
  int current() const { return cur_; }
 private:
  int cur_ = -1, cand_ = -1, minHold_ = 0;
  int64_t hold_ = 0;
  float maxSpeed_ = 1e30f;
};
// Ambience volume (0..1024) given the area's share (0..100): full above startFadePercent, linear
// down to minFadedVolume at 0 % (spec 5.3, [INFERRED]).
int ambienceFadeVolume(float sharePercent, int startFadePercent, int minFadedVolume);

// ---------------------------------------------------------------- Mixer (spec 8)
enum class AudioCategory { Music = 0, Sfx = 1, Ambience = 2 };

struct VoiceParams {
  AudioCategory category = AudioCategory::Sfx;
  int volume = 1024;       // item volume 0..1024
  float pan = 0.f;         // -1 left .. +1 right
  float pitch = 1.f;       // playback-rate multiplier
  bool loop = false;
  int priority = 32;
  int fadeInMs = 0;
  int64_t startDelayFrames = 0;  // output frames before the voice starts
};

// Thread-safe: every public method takes mu_, so mixAudio (audio thread) and control calls (game
// thread) may interleave.
class AudioMixer {
 public:
  using Handle = int;  // 0 = invalid
  void setOutputRate(int hz) { std::lock_guard<std::mutex> l(mu_); outRate_ = hz > 0 ? hz : 44100; }
  int outputRate() const { std::lock_guard<std::mutex> l(mu_); return outRate_; }
  void setMaxVoices(int n) { std::lock_guard<std::mutex> l(mu_); maxVoices_ = n; }  // 0 = unlimited

  Handle play(std::shared_ptr<const PcmBuffer> pcm, const VoiceParams& p);
  void stop(Handle h);                      // immediate
  void fadeOut(Handle h, int ms);           // fade then remove
  void setVolume(Handle h, int volume, int rampMs = 0);  // 0..1024 item volume
  void setPan(Handle h, float pan);
  bool isPlaying(Handle h) const;
  int voiceCount() const { std::lock_guard<std::mutex> l(mu_); return int(voices_.size()); }
  void stopAll();

  // Category volume 0..1024 and on/off (off forces 0).
  void setCategoryVolume(AudioCategory c, int v);
  void setCategoryEnabled(AudioCategory c, bool on);
  int categoryVolume(AudioCategory c) const { std::lock_guard<std::mutex> l(mu_); return catVol_[int(c)]; }
  void setMasterMute(bool m) { std::lock_guard<std::mutex> l(mu_); mute_ = m; }

  // Produce `frames` interleaved s16 stereo frames at outputRate (overwrites out).
  void mixAudio(int16_t* out, size_t frames);

 private:
  struct Voice {
    Handle h; std::shared_ptr<const PcmBuffer> pcm; VoiceParams p;
    double pos = 0; int64_t delay = 0;
    float vol, volTarget, volStep = 0;   // item volume 0..1
    float env = 1, envStep = 0; bool fadingOut = false;
    uint64_t order = 0;
  };
  Voice* find(Handle h);
  const Voice* find(Handle h) const;
  void stopLocked(Handle h);
  mutable std::mutex mu_;
  std::vector<Voice> voices_;
  int outRate_ = 44100, maxVoices_ = 0;
  int catVol_[3] = {1024, 1024, 1024};
  bool catOn_[3] = {true, true, true};
  bool mute_ = false;
  Handle nextH_ = 1;
  uint64_t order_ = 0;
  std::vector<float> acc_;
};

// ---------------------------------------------------------------- Music playlist (spec 7.1)
class MusicPlaylist {
 public:
  void setSongs(const std::vector<MusicDef>& songs);
  void setEnabled(int id, bool on);
  bool enabled(int id) const;
  // Next song id per the age/threshold rule; 0 when no eligible song. Updates P and L.
  int pickNext(AudioRng& rng);
  int eligibleCount() const;
  int counter() const { return P_; }
 private:
  struct Song { int id; bool inList; bool on; int last; };
  std::vector<Song> songs_;
  int P_ = 0;
};

// ---------------------------------------------------------------- Event sound selection (spec 10)
enum class TileKind {
  None, Water, Abandoned, Tree, Road, Rail, PowerLine, Tunnel, Residential, Commercial, Industrial,
  Construction, WaterFacility, Landmark, Agricultural, Airport, Seaport, Landfill, Flora
};
enum class SoundView { Surface, Subway, Pipes };
struct TileSoundContext {
  TileKind kind = TileKind::None;
  int sparseId = 0;  // occupant attribute 0x6F (0 = none)
  bool desert = false, jungle = false;
  SoundView view = SoundView::Surface;
  bool subwayPiece = false, pipePiece = false;
};
struct TileSoundChoice { int sparseId = 0; std::vector<int> ids; };  // sparse sample plays first, then ids
TileSoundChoice querySoundFor(const TileSoundContext& c);      // spec 10.4 query click
TileSoundChoice placementSoundFor(const TileSoundContext& c);  // spec 10.4 building placed
// Spec 10.5. sizeV = building size measure.
int pickExplosionSound(int sparseId, int sizeV, AudioRng& rng);
// Spec 6: sparse sound trigger test per scheduling tick.
bool sparseTriggers(int triggerVal, int randMultiplier, AudioRng& rng);
// Spec 06 s6 steps 3-4: the sparse items for the tiles around the view centre (cx, cz). Half-size 3*(6-zoom); x outer,
// z inner; a tile is skipped when its id is 0 or equals the previous non-zero id; one item per id. `centre` is true
// for the id of the occupant on the centre tile. `idAt(x, z)` returns the [Sparse] id of the occupant on a tile.
struct SparseSite { int id = 0; bool centre = false; };
std::vector<SparseSite> collectSparseSites(int cx, int cz, int zoom, int W, int H, const std::function<int(int, int)>& idAt);
// Volume repair at start-up (spec 8): out-of-range -> music 1000, sfx 700, ambience 900.
int repairCategoryVolume(AudioCategory c, int stored);

// ---------------------------------------------------------------- AudioSystem
// Loads a file relative to Res/Sound (or absolute) into bytes. Return false if not found.
using SoundFileLoader = std::function<bool(const std::string& path, bool absolute, std::vector<uint8_t>& out)>;
SoundFileLoader makeDirLoader(const fs::path& soundDir);  // case-insensitive, '\\' or '/'

enum class SoundAction { Play, Stop, SetLevel, SetAudible };

class AudioSystem {
 public:
  void init(AudioIni ini, SoundFileLoader loader, int outputRate = 44100, uint32_t seed = 1);

  const AudioIni& ini() const { return ini_; }
  AudioMixer& mixer() { return mixer_; }
  AudioRng& rng() { return rng_; }

  // Spec 10.1 request model. Unknown IDs do nothing (returns false). `pan` -1..1 and `attenuation`
  // 0..1 apply to positioned sounds.
  bool request(int soundId, SoundAction a = SoundAction::Play, int param = 0, float pan = 0.f, float attenuation = 1.f);
  // Zoom sounds 1/2 are ignored at audio zoom 5 / 1. Updates the zoom otherwise.
  void setAudioZoom(int z) { zoom_ = z < 1 ? 1 : (z > 5 ? 5 : z); }
  int audioZoom() const { return zoom_; }
  bool zoomIn();
  bool zoomOut();

  // Query / placement / explosion / sparse notifications (spec 6, 10.4, 10.5).
  void notifyQuery(const TileSoundContext& c, float pan = 0.f);
  void notifyPlaced(const TileSoundContext& c, float pan = 0.f);
  void notifyExplosion(int sparseId, int sizeV, bool onScreen, float pan = 0.f);
  bool playSparse(int sparseId, bool bypassRandom, float pan = 0.f, float attenuation = 1.f, bool centre = false);
  // Spec 06 s6 rebuild (every 4th audio tick): each site sounds with probability trigger / BuildingRandMultiplier,
  // at the ambience volume less BuildingsInAmbienceAttenuation unless it is the centre building. A one-shot already
  // sounding is left to finish; clearSparse() cuts them (view left the surface, zoom out, overlay on).
  void rebuildSparse(const std::vector<SparseSite>& sites);
  void clearSparse();
  size_t sparseVoices();

  // Sounds attached to a moving object (a vehicle, train, boat, aircraft; spec 26 3.2: MotionSound / SirenSound start when the object appears,
  // follow it and stop when it is removed). One voice per (object key, sound id): attaching again restarts it instead of stacking a second
  // one; moveSound re-pans / re-attenuates it; detachSound ends it with a short fade; detachAllSounds ends every one (new city, view change).
  static uint64_t soundKey(int objectId, int soundId) { return uint64_t(uint32_t(objectId)) << 32 | uint32_t(soundId); }
  void attachSound(uint64_t key, int soundId, float pan = 0.f, float attenuation = 1.f);
  void moveSound(uint64_t key, float pan, float attenuation);
  void detachSound(uint64_t key);
  void detachAllSounds();
  size_t attachedSounds() const { return attached_.size(); }
  bool hasSound(uint64_t key) const;

  // Categories (spec 8).
  void setCategoryVolume(AudioCategory c, int v) { mixer_.setCategoryVolume(c, v); }
  void setCategoryEnabled(AudioCategory c, bool on) { mixer_.setCategoryEnabled(c, on); }

  // Music.
  void setMusicEnabled(bool on);
  void setSongEnabled(int id, bool on) { playlist_.setEnabled(id, on); }
  bool playSong(int id);  // explicit request (also non-list songs)
  void stopMusic();
  // While held (e.g. intro movie plays its own audio) the playlist neither plays nor advances.
  void setMusicHeld(bool held);
  // Decode songs on a worker thread; the song starts from update() once decoded. Off by default so
  // playSong starts synchronously (tests); the game turns it on so a song change never stalls a frame.
  void setAsyncMusic(bool on) { asyncMusic_ = on; }
  int currentSong() const { return curSong_; }
  MusicPlaylist& playlist() { return playlist_; }

  // Ambient area (FSC schedule). index into ambientAreas(); -1 = none.
  bool setAmbientArea(int index);
  int ambientArea() const { return ambArea_; }
  // Spec 06 s5.3: the bed's volume scale (0..1024) once the area's share of the view drops below AmbienceStartFadeAtPercent; applies to the voices
  // playing now and to the ones started later.
  void setAmbientLevel(int scale1024);
  int ambientLevel() const { return ambScale_; }

  // Advance logic by dtMs (music timers, ambient scheduler, fading loop cleanup).
  void update(int64_t dtMs);
  int64_t nowMs() const { return now_; }
  void mixAudio(int16_t* out, size_t frames) { mixer_.mixAudio(out, frames); }

 private:
  std::shared_ptr<const PcmBuffer> load(const std::string& path, bool absolute);
  AudioMixer::Handle playBuf(const std::string& path, bool absolute, const VoiceParams& p, double* lenOutFrames = nullptr);
  void stopId(int id);
  void trackVoice(int id, AudioMixer::Handle h, size_t cap);
  std::string cacheKey(const std::string& path, bool absolute) const;
  void startMusicVoice(std::shared_ptr<const PcmBuffer> pcm);
  void collectMusicLoads();
  struct Fade { AudioMixer::Handle head = 0, loop = 0; int level = 1024; bool audible = true; int vol = 1024; };
  AudioIni ini_;
  SoundFileLoader loader_;
  AudioMixer mixer_;
  AudioRng rng_;
  std::map<std::string, std::shared_ptr<const PcmBuffer>> cache_;
  std::map<int, std::vector<AudioMixer::Handle>> active_;  // 1-shot / disaster / actor instances
  std::map<int, Fade> fades_;
  int zoom_ = 3;
  int64_t now_ = 0;
  std::map<int, AudioMixer::Handle> sparseH_;  // one voice per [Sparse] id started by rebuildSparse
  struct Attached { int soundId = 0, baseVol = 0, cur = 0; AudioMixer::Handle h = 0; };   // cur = the volume the ramp has reached (0..1024)
  std::map<uint64_t, Attached> attached_;
  std::map<int, int> attachedFadeRefs_;      // [Fading] ids: how many attached objects hold the loop
  // explosions: 4 round-robin slots
  AudioMixer::Handle expl_[4] = {0, 0, 0, 0};
  int explNext_ = 0;
  // music
  MusicPlaylist playlist_;
  bool musicOn_ = true;
  bool musicHeld_ = false;
  AudioMixer::Handle musicH_ = 0;
  int curSong_ = 0;
  int64_t musicElapsed_ = 0, musicDelay_ = 0;
  bool musicDelayRunning_ = true;
  bool asyncMusic_ = false;
  std::string musicWant_;  // cache key of the song waiting for its decode ("" = none)
  std::map<std::string, std::future<std::shared_ptr<const PcmBuffer>>> musicLoads_;  // in-flight decodes
  // ambient
  int ambArea_ = -1;
  int ambScale_ = 1024;
  FscSchedule sched_;
  FscScheduler sch_;
  int64_t ambStart_ = 0;
  std::vector<AudioMixer::Handle> cellH_;
};

}  // namespace m3k
