#include "core/audio.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace m3k {

// ================================================================ INI helpers
std::string normalizeSoundPath(const std::string& p) {
  std::string s = p;
  for (auto& c : s) if (c == '\\') c = '/';
  return s;
}

std::vector<std::string> splitIniFields(const std::string& v) {
  std::vector<std::string> out;
  size_t i = 0, n = v.size();
  while (i <= n) {
    while (i < n && v[i] == ' ') i++;
    std::string f;
    if (i < n && v[i] == '"') {
      i++;
      while (i < n && v[i] != '"') f += v[i++];
      if (i < n) i++;  // closing quote
      while (i < n && v[i] != ',') i++;  // skip junk after the quote
    } else {
      while (i < n && v[i] != ',') f += v[i++];
      while (!f.empty() && f.back() == ' ') f.pop_back();
    }
    out.push_back(f);
    if (i >= n) break;
    i++;  // comma
  }
  return out;
}

namespace {
bool parseNum(const std::string& s0, long& out) {
  std::string s;
  for (char c : s0) if (c != ' ' && c != '\t' && c != '\r') s += c;
  if (s.empty()) return false;
  const char* p = s.c_str();
  char* end = nullptr;
  bool neg = false;
  if (*p == '-') { neg = true; p++; }
  long v;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) v = std::strtol(p + 2, &end, 16);
  else v = std::strtol(p, &end, 10);
  if (end == p) return false;
  out = neg ? -v : v;
  return true;
}
int num(const std::vector<std::string>& f, size_t i, int d = 0) {
  long v;
  return i < f.size() && parseNum(f[i], v) ? int(v) : d;
}
std::string str(const std::vector<std::string>& f, size_t i) { return i < f.size() ? f[i] : std::string(); }
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
}  // namespace

int AudioOptions::get(const std::string& key, int dflt) const {
  auto it = values.find(lower(key));
  return it == values.end() ? dflt : it->second;
}

bool AudioIni::hasSound(int id) const {
  return oneShots.count(id) || fading.count(id) || disasters.count(id) || actors.count(id) || sparse.count(id);
}

AudioIni AudioIni::parse(const std::string& text, int country) {
  AudioIni ini;
  std::istringstream in(text);
  std::string line, section;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    size_t b = line.find_first_not_of(" ");
    if (b == std::string::npos) continue;
    line = line.substr(b);
    if (line[0] == ';') continue;
    if (line[0] == '[') {
      size_t e = line.find(']');
      section = lower(line.substr(1, e == std::string::npos ? std::string::npos : e - 1));
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), val = line.substr(eq + 1);
    while (!key.empty() && key.back() == ' ') key.pop_back();
    if (section == "options") {
      long v;
      if (parseNum(val, v)) ini.options.values[lower(key)] = int(v);
      continue;
    }
    long idl;
    if (!parseNum(key, idl) || idl < 1 || idl > 511) continue;
    int id = int(idl);
    auto f = splitIniFields(val);
    if (section == "1shotsfx") {
      if (f.size() < 8) continue;
      OneShotDef d; d.id = id; d.name = str(f, 0); d.pri = num(f, 1); d.vol = num(f, 2); d.load = num(f, 3) != 0;
      d.stream = num(f, 4) != 0; d.interrupt = num(f, 5) != 0; d.absolute = num(f, 6) != 0; d.file = str(f, 7);
      d.locale = num(f, 8);
      if (!localeMatches(d.locale, country)) continue;
      ini.oneShots[id] = d;
    } else if (section == "fading") {
      if (f.size() < 9) continue;
      FadingDef d; d.id = id; d.name = str(f, 0); d.pri = num(f, 1); d.vol = num(f, 2); d.preload = num(f, 3) != 0;
      d.autofade = num(f, 4) != 0; d.useAmbienceVol = num(f, 5) != 0; d.absolute = num(f, 6) != 0;
      d.oneShotFile = str(f, 7); d.loopFile = str(f, 8);
      ini.fading[id] = d;
    } else if (section == "disaster") {
      if (f.size() < 7) continue;
      DisasterDef d; d.id = id; d.name = str(f, 0); d.pri = num(f, 1); d.vol = num(f, 2); d.spatialScheme = num(f, 3);
      d.absolute = num(f, 4) != 0; d.file = str(f, 5); d.fadingId = num(f, 6);
      ini.disasters[id] = d;
    } else if (section == "actor") {
      if (f.size() < 9) continue;
      ActorDef d; d.id = id; d.name = str(f, 0); d.pri = num(f, 1); d.vol = num(f, 2); d.locale = num(f, 3);
      d.type = num(f, 4); d.absolute = num(f, 5) != 0; d.driveBy = str(f, 6); d.special1 = str(f, 7); d.special2 = str(f, 8);
      if (!localeMatches(d.locale, country)) continue;
      ini.actors[id] = d;
    } else if (section == "sparse") {
      if (f.size() < 6) continue;
      SparseDef d; d.id = id; d.name = str(f, 0); d.pri = num(f, 1); d.vol = num(f, 2);
      int ns = num(f, 3); d.absolute = num(f, 4) != 0; d.schedule = num(f, 5);
      for (int i = 0; i < ns; i++) {
        size_t b0 = 6 + size_t(i) * 3;
        if (b0 + 2 >= f.size()) break;
        SparseSample s; s.file = str(f, b0); s.vol = num(f, b0 + 1, 100); s.trigger = num(f, b0 + 2);
        d.samples.push_back(s);
      }
      ini.sparse[id] = d;
    } else if (section == "music") {
      if (f.size() < 8) continue;
      MusicDef d; d.id = id; d.name = str(f, 0); d.type = num(f, 1); d.file = str(f, 2); d.demoFile = str(f, 3);
      d.highPerformance = num(f, 4) != 0; d.inSongList = num(f, 5) != 0; d.absolute = num(f, 6) != 0; d.enabled = num(f, 7) != 0;
      ini.music.erase(std::remove_if(ini.music.begin(), ini.music.end(), [&](const MusicDef& m) { return m.id == id; }), ini.music.end());
      ini.music.push_back(d);
    }
  }
  return ini;
}

// ================================================================ FSC
int64_t fscProbThreshold(int p) {
  static const int64_t T[] = {0, 600, 1500, 3000, 7500, 12000, 15000, 18000, 21000, 25500, 32767, 36000, 45000,
                              58000, 67500, 90000, 120000, 210000, 300000, 40000000};
  if (p <= 0) return 0;
  if (p >= 20) return INT64_MAX;
  return T[p];
}

namespace {
std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> t;
  std::string cur;
  bool hasTab = line.find('\t') != std::string::npos;
  for (char c : line) {
    bool sep = hasTab ? (c == '\t') : (c == ' ');
    if (sep) { if (hasTab || !cur.empty()) { t.push_back(cur); cur.clear(); } }
    else cur += c;
  }
  if (hasTab || !cur.empty()) t.push_back(cur);
  if (hasTab) for (auto& s : t) { size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' '); s = a == std::string::npos ? "" : s.substr(a, b - a + 1); }
  return t;
}
}  // namespace

bool FscSchedule::parse(const std::string& text, FscSchedule& out) {
  out = FscSchedule();
  std::istringstream in(text);
  std::string line;
  std::vector<std::vector<std::string>> rows;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    auto f = splitTabs(line);
    // Shipped data rows start with a tab (empty first field) before the vol column [DATA].
    if (rows.size() >= 1 && f.size() > 1 && f[0].empty()) f.erase(f.begin());
    rows.push_back(std::move(f));
  }
  if (rows.size() < 2 || rows[0].size() < 2 || lower(rows[0][0]) != "version") return false;
  long v;
  parseNum(rows[0][1], v);
  out.version = int(v);
  const auto& h = rows[1];
  if (h.size() < 14) return false;
  out.vol = num(h, 0); out.pri = num(h, 1); out.minVoices = num(h, 2); out.maxVoices = num(h, 3);
  out.rows = num(h, 4); out.cols = num(h, 5); out.tempo = num(h, 6); out.bpb = num(h, 7);
  out.selX = num(h, 8); out.selY = num(h, 9); out.quanX = num(h, 10); out.quanY = num(h, 11);
  out.difX = num(h, 12); out.difY = num(h, 13);
  if (out.rows <= 0 || out.cols <= 0 || out.rows > 1024 || out.cols > 1024) return false;
  size_t need = size_t(out.rows) * out.cols;
  if (rows.size() < 2 + need) return false;
  for (size_t i = 0; i < need; i++) {
    const auto& r = rows[2 + i];
    FscCell c;
    c.vol = num(r, 0); c.lr = num(r, 2, 512); c.fb = num(r, 3, 512); c.fin = num(r, 5); c.fout = num(r, 6);
    c.dly = num(r, 7); c.loop = num(r, 9); c.loopFlag = num(r, 10); c.quant = num(r, 11); c.prob = num(r, 12);
    c.pitchL = num(r, 13); c.pitchH = num(r, 14); c.group = num(r, 16); c.stereo = num(r, 17) != 0;
    c.file = str(r, 18);
    c.none = c.file.empty() || lower(c.file) == "none";
    for (size_t k = 19; k + 1 < r.size(); k += 2) {
      long a, b;
      if (parseNum(r[k], a) && parseNum(r[k + 1], b)) c.exclusions.push_back({int(a), int(b)});
    }
    out.cells.push_back(c);
  }
  if (out.bpb < 1) out.bpb = 1;
  if (out.maxVoices < 1) out.maxVoices = 1;
  return true;
}

void FscScheduler::init(const FscSchedule* s, uint32_t seed, int densityDivisor) {
  s_ = s;
  st_.assign(s ? s->cells.size() : 0, 0);
  last_ = -1; k_ = 0; base_ = 0; beats_ = 0;
  density_ = densityDivisor > 0 ? densityDivisor : 10;
  rng_ = AudioRng(seed);
  crt_ = AudioRng(seed * 2654435761u + 7);
}

int FscScheduler::playingCount() const {
  int n = 0;
  for (auto v : st_) n += v == 1;
  return n;
}

void FscScheduler::cellEnded(int cell) {
  if (cell >= 0 && size_t(cell) < st_.size()) st_[size_t(cell)] = 0;
}

bool FscScheduler::ready(int idx, int row, int col, bool inWindow, int preferGroup) {
  const FscCell& c = s_->cells[size_t(idx)];
  (void)row; (void)col;
  if (c.none || st_[size_t(idx)] != 0) return false;
  if (!inWindow) return false;
  if (idx == last_) return false;
  if (c.group != 0)
    for (size_t j = 0; j < s_->cells.size(); j++)
      if (int(j) != idx && s_->cells[j].group == c.group && st_[j] == 1) return false;
  for (auto& e : c.exclusions)
    if (e.first >= 0 && e.first < s_->rows && e.second >= 0 && e.second < s_->cols &&
        st_[size_t(e.first) * s_->cols + e.second] == 1) return false;
  if (preferGroup > 0 && c.group == preferGroup) return true;
  int p = std::clamp(c.prob, 0, 20);
  if (p == 0) return false;
  if (p == 20) return true;
  int64_t R = (crt_.next() >> 1) & 0x7FFF;
  return R * density_ < fscProbThreshold(p);
}

int FscScheduler::pick(int preferGroup) {
  const FscSchedule& s = *s_;
  int r0 = std::max(0, s.selY - s.difY), r1 = std::min(s.rows - 1, s.selY + s.difY);
  int c0 = std::max(0, s.selX - s.difX), c1 = std::min(s.cols - 1, s.selX + s.difX);
  std::vector<int> cand;
  for (int r = r0; r <= r1; r++)
    for (int c = c0; c <= c1; c++)
      if (ready(r * s.cols + c, r, c, true, preferGroup)) cand.push_back(r * s.cols + c);
  if (!cand.empty()) return cand[rng_.below(uint32_t(cand.size()))];
  int total = s.rows * s.cols;
  int start = int(crt_.next() >> 4) % s.rows * s.cols + int(crt_.next() >> 4) % s.cols;
  for (int i = 0; i < total; i++) {
    int idx = (start + i) % total;
    if (ready(idx, idx / s.cols, idx % s.cols, true, preferGroup)) return idx;
  }
  return -1;
}

void FscScheduler::startCell(int idx, std::vector<Action>& out) {
  st_[size_t(idx)] = 1;
  last_ = idx;
  out.push_back({true, idx});
}

void FscScheduler::onBeat(std::vector<Action>& out) {
  const FscSchedule& s = *s_;
  int pos = int(k_ % s.bpb);
  if (!(pos == s.bpb - 1 || (s.bpb > 6 && pos == s.bpb - 6))) return;
  int prefer = -1;
  int playing = playingCount();
  if (playing > 0 && playing >= s.minVoices) {
    int which = int(rng_.below(uint32_t(playing)));
    for (size_t i = 0; i < st_.size(); i++)
      if (st_[i] == 1 && which-- == 0) {
        st_[i] = 2;
        out.push_back({false, int(i)});
        prefer = s.cells[i].group > 0 ? s.cells[i].group : -1;
        break;
      }
  }
  if (playingCount() < s.maxVoices) {
    int c = pick(prefer);
    if (c < 0 && playingCount() < s.minVoices) c = pick(prefer);
    if (c >= 0) startCell(c, out);
  }
  if (playingCount() < s.maxVoices) {
    int c = pick(-1);
    if (c < 0 && playingCount() < s.minVoices) c = pick(-1);
    if (c >= 0) startCell(c, out);
  }
}

void FscScheduler::update(int64_t nowMs, std::vector<Action>& out) {
  if (!s_ || s_->tempo <= 0) return;
  for (int guard = 0; guard < 100000; guard++) {
    int64_t rel = k_ * 60000 / s_->tempo;
    if (rel >= 86400000) { base_ += 86400000; k_ = 0; continue; }
    if (base_ + rel > nowMs) break;
    onBeat(out);
    beats_++;
    k_++;
  }
}

// ================================================================ ambient areas
const std::vector<AmbientArea>& ambientAreas() {
  static const std::vector<AmbientArea> a = {
      {"residential", "residential.fsc", "sfx/zones/residential"}, {"commercial", "commercial.fsc", "sfx/zones/commercial"},
      {"industrial", "industrial.fsc", "sfx/zones/industrial"}, {"agriculture", "Agricult.fsc", "sfx/zones/Agri"},
      {"nature", "Nature.fsc", "sfx/zones/nature"}, {"desert", "Desert.fsc", "sfx/zones/nature"},
      {"jungle", "Jungle.fsc", "sfx/zones/nature"}, {"water", "water.fsc", "sfx/zones/Water"},
      {"wind", "Wind.fsc", "sfx/zones/Wind"}, {"arctic", "Arctic.fsc", "sfx/zones/wind"},
      {"traffic", "traffic.fsc", "sfx/zones/Traffic"}, {"construction", "construction.fsc", "sfx/zones/Construction"},
      {"seaport", "Seaport.fsc", "sfx/zones/Seaport"}, {"airport", "Airports.fsc", "sfx/zones/Airport"},
      {"abandoned", "Abandoned.fsc", "sfx/zones/Abandoned"}, {"garbage", "Garbage.fsc", "sfx/zones/Garbage"},
      {"crime", "Crime.fsc", "sfx/zones/Crime"}, {"unpowered", "Unpwrd.fsc", "sfx/zones/Unpowered"},
      {"pipes", "pipes.fsc", "sfx/zones/pipes"}, {"subway", "subway_v.fsc", "sfx/zones/subway_v"},
      {"zoom3city", "zm3city.fsc", "sfx/zones/Zoom3City"}, {"zoom3", "zm3.fsc", "sfx/zones/Zoom3"}};
  return a;
}

int AmbientAreaSelector::update(int64_t dtMs, const std::vector<float>& w, float scrollSpeed) {
  int best = -1;
  float bv = 0;
  for (size_t i = 0; i < w.size(); i++)
    if (w[i] > bv) { bv = w[i]; best = int(i); }
  if (best < 0 || best == cur_) { cand_ = -1; hold_ = 0; return cur_; }
  if (cur_ < 0) { cur_ = best; return cur_; }  // first selection is immediate
  if (scrollSpeed > maxSpeed_) return cur_;
  if (cand_ != best) { cand_ = best; hold_ = 0; }
  hold_ += dtMs;
  if (hold_ >= minHold_) { cur_ = best; cand_ = -1; hold_ = 0; }
  return cur_;
}

int ambienceFadeVolume(float share, int startPct, int minVol) {
  if (startPct <= 0 || share >= float(startPct)) return 1024;
  if (share < 0) share = 0;
  float t = share / float(startPct);
  return int(std::lround(float(minVol) + (1024.f - float(minVol)) * t));
}

// ================================================================ mixer
AudioMixer::Voice* AudioMixer::find(Handle h) {
  for (auto& v : voices_) if (v.h == h) return &v;
  return nullptr;
}
const AudioMixer::Voice* AudioMixer::find(Handle h) const {
  for (auto& v : voices_) if (v.h == h) return &v;
  return nullptr;
}

AudioMixer::Handle AudioMixer::play(std::shared_ptr<const PcmBuffer> pcm, const VoiceParams& p) {
  std::lock_guard<std::mutex> l(mu_);
  if (!pcm || pcm->frames() == 0) return 0;
  if (maxVoices_ > 0 && int(voices_.size()) >= maxVoices_) {
    size_t vi = 0;
    for (size_t i = 1; i < voices_.size(); i++)
      if (voices_[i].p.priority < voices_[vi].p.priority ||
          (voices_[i].p.priority == voices_[vi].p.priority && voices_[i].order < voices_[vi].order)) vi = i;
    if (p.priority < voices_[vi].p.priority) return 0;
    voices_.erase(voices_.begin() + long(vi));
  }
  Voice v;
  v.h = nextH_++;
  v.pcm = std::move(pcm);
  v.p = p;
  v.delay = p.startDelayFrames;
  v.vol = v.volTarget = std::clamp(p.volume, 0, 1024) / 1024.f;
  if (p.fadeInMs > 0) { v.env = 0; v.envStep = 1.f / (float(p.fadeInMs) * float(outRate_) / 1000.f); }
  v.order = order_++;
  voices_.push_back(std::move(v));
  return voices_.back().h;
}

void AudioMixer::stopLocked(Handle h) {
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.h == h; }), voices_.end());
}
void AudioMixer::stop(Handle h) { std::lock_guard<std::mutex> l(mu_); stopLocked(h); }
void AudioMixer::stopAll() { std::lock_guard<std::mutex> l(mu_); voices_.clear(); }

void AudioMixer::fadeOut(Handle h, int ms) {
  std::lock_guard<std::mutex> l(mu_);
  Voice* v = find(h);
  if (!v) return;
  float frames = float(ms) * float(outRate_) / 1000.f;
  if (frames < 1.f) { stopLocked(h); return; }
  v->fadingOut = true;
  v->envStep = -v->env / frames;
  if (v->envStep == 0) stopLocked(h);
}

void AudioMixer::setVolume(Handle h, int volume, int rampMs) {
  std::lock_guard<std::mutex> l(mu_);
  Voice* v = find(h);
  if (!v) return;
  v->volTarget = std::clamp(volume, 0, 1024) / 1024.f;
  if (rampMs <= 0) { v->vol = v->volTarget; v->volStep = 0; }
  else v->volStep = (v->volTarget - v->vol) / (float(rampMs) * float(outRate_) / 1000.f);
}
void AudioMixer::setPan(Handle h, float pan) { std::lock_guard<std::mutex> l(mu_); if (Voice* v = find(h)) v->p.pan = std::clamp(pan, -1.f, 1.f); }
bool AudioMixer::isPlaying(Handle h) const { std::lock_guard<std::mutex> l(mu_); return h != 0 && find(h) != nullptr; }

void AudioMixer::setCategoryVolume(AudioCategory c, int v) { std::lock_guard<std::mutex> l(mu_); catVol_[int(c)] = std::clamp(v, 0, 1024); }
void AudioMixer::setCategoryEnabled(AudioCategory c, bool on) { std::lock_guard<std::mutex> l(mu_); catOn_[int(c)] = on; }

void AudioMixer::mixAudio(int16_t* out, size_t frames) {
  std::lock_guard<std::mutex> l(mu_);
  acc_.assign(frames * 2, 0.f);
  for (auto& v : voices_) {
    float cat = catOn_[int(v.p.category)] && !mute_ ? float(catVol_[int(v.p.category)]) / 1024.f : 0.f;
    const PcmBuffer& pcm = *v.pcm;
    const size_t n = pcm.frames();
    const int ch = pcm.channels;
    const double step = double(pcm.sampleRate) / double(outRate_) * double(v.p.pitch);
    const float gl = v.p.pan > 0 ? 1.f - v.p.pan : 1.f, gr = v.p.pan < 0 ? 1.f + v.p.pan : 1.f;
    bool done = false;
    for (size_t f = 0; f < frames && !done; f++) {
      if (v.delay > 0) { v.delay--; continue; }
      if (v.volStep != 0) {
        v.vol += v.volStep;
        if ((v.volStep > 0 && v.vol >= v.volTarget) || (v.volStep < 0 && v.vol <= v.volTarget)) { v.vol = v.volTarget; v.volStep = 0; }
      }
      if (v.envStep != 0) {
        v.env += v.envStep;
        if (v.envStep > 0 && v.env >= 1.f) { v.env = 1.f; v.envStep = 0; }
        else if (v.envStep < 0 && v.env <= 0.f) { done = true; break; }
      }
      size_t i0 = size_t(v.pos);
      if (i0 >= n) { done = true; break; }
      size_t i1 = i0 + 1;
      if (i1 >= n) i1 = v.p.loop ? 0 : i0;
      float fr = float(v.pos - double(i0));
      float l, r;
      if (ch == 2) {
        l = float(pcm.samples[i0 * 2]) * (1 - fr) + float(pcm.samples[i1 * 2]) * fr;
        r = float(pcm.samples[i0 * 2 + 1]) * (1 - fr) + float(pcm.samples[i1 * 2 + 1]) * fr;
      } else {
        l = r = float(pcm.samples[i0]) * (1 - fr) + float(pcm.samples[i1]) * fr;
      }
      float g = v.vol * v.env * cat;
      acc_[f * 2] += l * g * gl;
      acc_[f * 2 + 1] += r * g * gr;
      v.pos += step;
      if (v.pos >= double(n)) {
        if (v.p.loop) v.pos = std::fmod(v.pos, double(n));
        else done = true;
      }
    }
    if (done) v.h = 0;
  }
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.h == 0; }), voices_.end());
  for (size_t i = 0; i < frames * 2; i++) {
    float x = std::nearbyint(acc_[i]);
    out[i] = int16_t(std::clamp(x, -32768.f, 32767.f));
  }
}

// ================================================================ playlist
void MusicPlaylist::setSongs(const std::vector<MusicDef>& songs) {
  songs_.clear();
  for (auto& s : songs) songs_.push_back({s.id, s.inSongList, s.enabled, 0});
  P_ = 0;
}
void MusicPlaylist::setEnabled(int id, bool on) { for (auto& s : songs_) if (s.id == id) s.on = on; }
bool MusicPlaylist::enabled(int id) const { for (auto& s : songs_) if (s.id == id) return s.on; return false; }
int MusicPlaylist::eligibleCount() const {
  int n = 0;
  for (auto& s : songs_) n += s.inList && s.on;
  return n;
}
int MusicPlaylist::pickNext(AudioRng& rng) {
  int N = eligibleCount();
  if (N == 0) return 0;
  int M = 0;
  for (auto& s : songs_) if (s.inList && s.on) M = std::max(M, P_ - s.last);
  int T = std::min(M, 2 * N / 3);
  std::vector<Song*> cand;
  for (auto& s : songs_) if (s.inList && s.on && P_ - s.last >= T) cand.push_back(&s);
  Song* pick = cand[rng.next() % cand.size()];
  P_++;
  pick->last = P_;
  return pick->id;
}

// ================================================================ event selection
TileSoundChoice querySoundFor(const TileSoundContext& c) {
  TileSoundChoice r;
  if (c.view == SoundView::Subway) { if (c.subwayPiece) r.ids = {53}; return r; }
  if (c.view == SoundView::Pipes) { if (c.pipePiece) r.ids = {50}; return r; }
  if (c.sparseId) { r.sparseId = c.sparseId; return r; }
  int id = 0;
  switch (c.kind) {
    case TileKind::None: break;
    case TileKind::Water: id = 140; break;
    case TileKind::Abandoned: id = 143; break;
    case TileKind::Tree: id = c.desert ? 252 : c.jungle ? 253 : 151; break;
    case TileKind::Road: id = 14; break;
    case TileKind::Rail: id = 51; break;
    case TileKind::PowerLine: id = 144; break;
    case TileKind::Tunnel: id = 52; break;
    case TileKind::Residential: id = 136; break;
    case TileKind::Commercial: id = 137; break;
    case TileKind::Industrial: id = 138; break;
    case TileKind::Construction: id = 139; break;
    case TileKind::WaterFacility: id = 140; break;
    case TileKind::Landmark: id = 141; break;
    case TileKind::Agricultural: id = 142; break;
    case TileKind::Airport: id = 173; break;
    case TileKind::Seaport: id = 174; break;
    case TileKind::Landfill: id = 172; break;
    case TileKind::Flora: id = 151; break;
  }
  if (id) r.ids = {id};
  return r;
}

TileSoundChoice placementSoundFor(const TileSoundContext& c) {
  TileSoundChoice r;
  if (c.view == SoundView::Subway) { r.ids = {53}; return r; }
  if (c.view == SoundView::Pipes) { r.ids = {50}; return r; }
  if (c.kind == TileKind::None) { r.sparseId = c.sparseId; return r; }
  if (c.sparseId) { r.sparseId = c.sparseId; r.ids = {22}; return r; }
  int id = 0;
  switch (c.kind) {
    case TileKind::Tree: id = 17; break;
    case TileKind::Road: id = 14; break;
    case TileKind::Rail: id = 51; break;
    case TileKind::PowerLine: id = 144; break;
    case TileKind::Tunnel: id = 52; break;
    case TileKind::Residential: id = 136; break;
    case TileKind::Commercial: id = 137; break;
    case TileKind::Industrial: id = 138; break;
    case TileKind::Construction: id = 139; break;
    case TileKind::WaterFacility: id = 0x169; break;
    case TileKind::Landmark: id = 0x110; break;
    case TileKind::Agricultural: id = 0x170; break;
    case TileKind::Abandoned: id = 143; break;
    case TileKind::Airport: id = 0x103; break;
    case TileKind::Seaport: id = 0x123; break;
    case TileKind::Landfill: id = 0x138; break;
    case TileKind::Flora: id = 151; break;
    default: break;
  }
  if (id) r.ids = {id};
  return r;
}

int pickExplosionSound(int s, int V, AudioRng& rng) {
  if (s == 0x119 || s == 0x11C || s == 0x11D) return 76;
  if (s == 0x118 || s == 0x11A || s == 0x11B || s == 0x11E) return rng.below(2) == 0 ? 135 : 134;
  static const int tbl[3][12] = {{103, 104, 105, 106, 107, 108, 103, 104, 105, 106, 107, 108},
                                 {109, 110, 111, 112, 109, 110, 111, 112, 109, 110, 111, 112},
                                 {134, 135, 134, 135, 134, 135, 134, 135, 134, 135, 134, 135}};
  int row = V <= 513 ? 0 : (V <= 2500 ? 1 : 2);
  return tbl[row][rng.below(12)];
}

bool sparseTriggers(int trigger, int mult, AudioRng& rng) {
  if (mult <= 0) mult = 10000;
  return int(rng.below(uint32_t(mult))) < trigger;
}

int repairCategoryVolume(AudioCategory c, int stored) {
  if (stored >= 0 && stored <= 1024) return stored;
  return c == AudioCategory::Music ? 1000 : c == AudioCategory::Sfx ? 700 : 900;
}

// ================================================================ AudioSystem
SoundFileLoader makeDirLoader(const fs::path& dir) {
  return [dir](const std::string& path, bool absolute, std::vector<uint8_t>& out) {
    fs::path p = absolute ? fs::path(normalizeSoundPath(path)) : resolveCI(dir, normalizeSoundPath(path));
    if (p.empty()) return false;
    return readFile(p, out);
  };
}

void AudioSystem::init(AudioIni ini, SoundFileLoader loader, int rate, uint32_t seed) {
  ini_ = std::move(ini);
  loader_ = std::move(loader);
  mixer_.stopAll();
  mixer_.setOutputRate(rate);
  rng_ = AudioRng(seed);
  cache_.clear(); active_.clear(); fades_.clear(); musicLoads_.clear(); musicWant_.clear();
  playlist_.setSongs(ini_.music);
  musicDelay_ = -1; musicDelayRunning_ = true; musicElapsed_ = 0; musicH_ = 0; curSong_ = 0;
  ambArea_ = -1; cellH_.clear(); now_ = 0;
}

std::string AudioSystem::cacheKey(const std::string& path, bool absolute) const {
  return lower(normalizeSoundPath(path)) + (absolute ? "|a" : "");
}

std::shared_ptr<const PcmBuffer> AudioSystem::load(const std::string& path, bool absolute) {
  if (path.empty() || !loader_) return nullptr;
  std::string key = cacheKey(path, absolute);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  std::shared_ptr<const PcmBuffer> res;
  std::vector<uint8_t> data;
  if (loader_(path, absolute, data)) {
    auto b = std::make_shared<PcmBuffer>();
    if (loadSoundFile(path, data, *b)) res = b;
  }
  cache_[key] = res;
  return res;
}

AudioMixer::Handle AudioSystem::playBuf(const std::string& path, bool absolute, const VoiceParams& p, double* len) {
  auto pcm = load(path, absolute);
  if (!pcm) return 0;
  if (len) *len = double(pcm->frames()) * mixer_.outputRate() / std::max(1, pcm->sampleRate) / double(p.pitch);
  return mixer_.play(pcm, p);
}

void AudioSystem::stopId(int id) {
  auto it = active_.find(id);
  if (it == active_.end()) return;
  for (auto h : it->second) mixer_.stop(h);
  active_.erase(it);
}

bool AudioSystem::zoomIn() { if (zoom_ >= 5) return false; request(1); zoom_++; return true; }
bool AudioSystem::zoomOut() { if (zoom_ <= 1) return false; request(2); zoom_--; return true; }

static int scaleVol(int vol, float att) { return int(std::lround(float(vol) * std::clamp(att, 0.f, 1.f))); }

bool AudioSystem::request(int id, SoundAction a, int param, float pan, float att) {
  if (!ini_.hasSound(id)) return false;
  if (id == 1 && zoom_ >= 5) return false;
  if (id == 2 && zoom_ <= 1) return false;
  const int outro = ini_.options.get("FreshnessOutroDurationMsec", 500);
  if (auto it = ini_.oneShots.find(id); it != ini_.oneShots.end()) {
    const auto& d = it->second;
    if (a == SoundAction::Play) {
      if (d.interrupt) stopId(id);
      VoiceParams p; p.volume = scaleVol(d.vol, att); p.pan = pan; p.priority = d.pri;
      auto h = playBuf(d.file, d.absolute, p);
      if (h) active_[id].push_back(h);
    } else if (a == SoundAction::Stop) stopId(id);
    return true;
  }
  if (auto it = ini_.fading.find(id); it != ini_.fading.end()) {
    const auto& d = it->second;
    Fade& f = fades_[id];
    auto applyVol = [&] {
      f.vol = f.audible ? d.vol * f.level / 1024 : 0;
      if (f.loop) mixer_.setVolume(f.loop, f.vol, 100);
    };
    if (a == SoundAction::Play) {
      if (!mixer_.isPlaying(f.loop)) {
        VoiceParams p; p.category = d.useAmbienceVol ? AudioCategory::Ambience : AudioCategory::Sfx;
        p.priority = d.pri; p.volume = d.vol; p.pan = pan;
        double len = 0;
        f.head = d.oneShotFile.empty() ? 0 : playBuf(d.oneShotFile, d.absolute, p, &len);
        p.loop = true; p.startDelayFrames = f.head ? int64_t(len) : 0;
        f.loop = playBuf(d.loopFile, d.absolute, p);
        f.level = 1024; f.audible = true;
      } else { f.audible = true; applyVol(); }
    } else if (a == SoundAction::Stop) {
      if (f.head) mixer_.stop(f.head);
      if (f.loop) mixer_.fadeOut(f.loop, outro);
      f.head = f.loop = 0;
    } else if (a == SoundAction::SetLevel) { f.level = std::clamp(param, 0, 1024); applyVol(); }
    else { f.audible = param != 0; applyVol(); }
    return true;
  }
  if (auto it = ini_.disasters.find(id); it != ini_.disasters.end()) {
    const auto& d = it->second;
    if (a == SoundAction::Play) {
      VoiceParams p; p.volume = scaleVol(d.vol, att); p.pan = d.spatialScheme == 0 ? pan : 0.f; p.priority = d.pri;
      auto h = playBuf(d.file, d.absolute, p);
      if (h) active_[id].push_back(h);
      if (d.fadingId) request(d.fadingId, SoundAction::Play);
    } else if (a == SoundAction::Stop) {
      stopId(id);
      if (d.fadingId) request(d.fadingId, SoundAction::Stop);
    } else if (a == SoundAction::SetAudible || a == SoundAction::SetLevel) {
      int pct = param == 0 ? 0 : (param > 1 && param <= 100 ? param : 100);
      if (a == SoundAction::SetLevel) pct = std::clamp(param, 0, 100);
      for (auto h : active_[id]) mixer_.setVolume(h, d.vol * pct / 100, 100);
      if (d.fadingId) request(d.fadingId, SoundAction::SetAudible, param);
    }
    return true;
  }
  if (auto it = ini_.actors.find(id); it != ini_.actors.end()) {
    const auto& d = it->second;
    if (a == SoundAction::Play) {
      VoiceParams p; p.volume = scaleVol(d.vol, att); p.pan = pan; p.priority = d.pri; p.loop = true;
      auto h = playBuf(d.driveBy, d.absolute, p);
      if (h) active_[id].push_back(h);
    } else if (a == SoundAction::Stop) stopId(id);
    return true;
  }
  if (ini_.sparse.count(id)) {
    if (a == SoundAction::Play) playSparse(id, true, pan, att);
    return true;
  }
  return false;
}

bool AudioSystem::playSparse(int id, bool bypass, float pan, float att) {
  auto it = ini_.sparse.find(id);
  if (it == ini_.sparse.end() || it->second.samples.empty()) return false;
  const auto& d = it->second;
  const SparseSample& s = d.samples[rng_.below(uint32_t(d.samples.size()))];
  if (!bypass) {
    if (zoom_ < ini_.options.get("BuildingsInAmbienceMinZoom", 1)) return false;
    if (!sparseTriggers(s.trigger, ini_.options.get("BuildingRandMultiplier", 10000), rng_)) return false;
    int red = std::clamp(ini_.options.get("BuildingsInAmbienceAttenuation", 0), 0, 1024);
    att *= float(1024 - red) / 1024.f;
  }
  VoiceParams p;
  p.category = bypass ? AudioCategory::Sfx : AudioCategory::Ambience;
  p.volume = scaleVol(std::min(1024, d.vol * s.vol / 100), att); p.pan = pan; p.priority = d.pri;
  return playBuf(s.file, d.absolute, p) != 0;
}

void AudioSystem::notifyQuery(const TileSoundContext& c, float pan) {
  auto ch = querySoundFor(c);
  if (ch.sparseId) playSparse(ch.sparseId, true, pan);
  for (int id : ch.ids) request(id, SoundAction::Play, 0, pan);
}
void AudioSystem::notifyPlaced(const TileSoundContext& c, float pan) {
  auto ch = placementSoundFor(c);
  if (ch.sparseId) playSparse(ch.sparseId, true, pan);
  for (int id : ch.ids) request(id, SoundAction::Play, 0, pan);
}

void AudioSystem::notifyExplosion(int sparseId, int sizeV, bool onScreen, float pan) {
  int id = pickExplosionSound(sparseId, sizeV, rng_);
  auto it = ini_.oneShots.find(id);
  if (it == ini_.oneShots.end()) return;
  if (expl_[explNext_]) mixer_.stop(expl_[explNext_]);  // round-robin: stop the oldest
  VoiceParams p;
  p.volume = std::max(0, it->second.vol - (onScreen ? 0 : 300)); p.pan = pan; p.priority = it->second.pri;
  expl_[explNext_] = playBuf(it->second.file, it->second.absolute, p);
  explNext_ = (explNext_ + 1) % 4;
}

void AudioSystem::setMusicEnabled(bool on) {
  musicOn_ = on;
  if (!on) stopMusic();
}

void AudioSystem::setMusicHeld(bool held) {
  musicHeld_ = held;
  if (held) stopMusic();
}

bool AudioSystem::playSong(int id) {
  for (auto& m : ini_.music) {
    if (m.id != id) continue;
    if (musicH_) mixer_.stop(musicH_);
    musicH_ = 0; musicWant_.clear();
    const std::string key = cacheKey(m.file, m.absolute);
    if (asyncMusic_ && loader_ && !m.file.empty() && !cache_.count(key)) {
      // Decode off the game thread; collectMusicLoads() starts the voice when it is ready.
      if (!musicLoads_.count(key))
        musicLoads_[key] = std::async(std::launch::async, [loader = loader_, file = m.file, abs = m.absolute]() -> std::shared_ptr<const PcmBuffer> {
          std::vector<uint8_t> data;
          if (!loader(file, abs, data)) return nullptr;
          auto b = std::make_shared<PcmBuffer>();
          return loadSoundFile(file, data, *b) ? b : nullptr;
        });
      musicWant_ = key;
      curSong_ = id; musicElapsed_ = 0; musicDelayRunning_ = false;
      return true;
    }
    VoiceParams p; p.category = AudioCategory::Music;
    musicH_ = playBuf(m.file, m.absolute, p);
    if (!musicH_) { curSong_ = 0; musicDelay_ = ini_.options.get("MusicPlayerSongDelayMsec", 0); musicDelayRunning_ = true; musicElapsed_ = 0; return false; }
    curSong_ = id; musicElapsed_ = 0; musicDelayRunning_ = false;
    return true;
  }
  return false;
}

void AudioSystem::startMusicVoice(std::shared_ptr<const PcmBuffer> pcm) {
  VoiceParams p; p.category = AudioCategory::Music;
  musicH_ = mixer_.play(std::move(pcm), p);
  if (!musicH_) { curSong_ = 0; musicDelay_ = ini_.options.get("MusicPlayerSongDelayMsec", 0); musicDelayRunning_ = true; }
  musicElapsed_ = 0;
}

// Moves finished background decodes into the cache and starts the song that is waiting for one.
void AudioSystem::collectMusicLoads() {
  for (auto it = musicLoads_.begin(); it != musicLoads_.end();) {
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
    auto pcm = it->second.get();
    cache_[it->first] = pcm;
    if (it->first == musicWant_) { musicWant_.clear(); startMusicVoice(pcm); }
    it = musicLoads_.erase(it);
  }
}

void AudioSystem::stopMusic() {
  if (musicH_) mixer_.stop(musicH_);
  musicH_ = 0; curSong_ = 0; musicElapsed_ = 0; musicWant_.clear();
  musicDelayRunning_ = true;
  musicDelay_ = ini_.options.get("MusicStartDelayAfterStop", 0);
}

bool AudioSystem::setAmbientArea(int index) {
  for (size_t i = 0; i < cellH_.size(); i++) if (cellH_[i]) mixer_.fadeOut(cellH_[i], ini_.options.get("FreshnessOutroDurationMsec", 500));
  cellH_.clear();
  ambArea_ = -1;
  const auto& areas = ambientAreas();
  if (index < 0 || size_t(index) >= areas.size() || !loader_) return false;
  std::vector<uint8_t> data;
  std::string path = std::string(areas[size_t(index)].folder) + "/" + areas[size_t(index)].schedule;
  if (!loader_(path, false, data)) return false;
  if (!FscSchedule::parse(std::string(data.begin(), data.end()), sched_)) return false;
  sch_.init(&sched_, rng_.next());
  cellH_.assign(sched_.cells.size(), 0);
  ambStart_ = now_;
  ambArea_ = index;
  return true;
}

void AudioSystem::update(int64_t dt) {
  now_ += dt;
  if (!musicLoads_.empty()) collectMusicLoads();
  // music
  if (musicOn_ && !musicHeld_) {
    if (musicH_ && !mixer_.isPlaying(musicH_)) {
      musicH_ = 0; curSong_ = 0; musicDelayRunning_ = true; musicElapsed_ = 0;
      musicDelay_ = ini_.options.get("MusicPlayerSongDelayMsec", 0);
    }
    if (musicH_) {
      musicElapsed_ += dt;
      int cap = ini_.options.get("MusicPlayerSongDurationMsec", 0);
      if (cap > 0 && musicElapsed_ > cap) mixer_.fadeOut(musicH_, 1000);
    } else if (musicDelayRunning_) {
      musicElapsed_ += dt;
      if (musicElapsed_ > musicDelay_) {
        int id = playlist_.pickNext(rng_);
        if (id) playSong(id);
        else musicElapsed_ = 0;
      }
    }
  }
  // fading loops whose voice died
  for (auto& kv : fades_) if (kv.second.loop && !mixer_.isPlaying(kv.second.loop)) kv.second.loop = 0;
  // ambient
  if (ambArea_ >= 0) {
    for (size_t i = 0; i < cellH_.size(); i++)
      if (cellH_[i] && !mixer_.isPlaying(cellH_[i])) { sch_.cellEnded(int(i)); cellH_[i] = 0; }
    std::vector<FscScheduler::Action> acts;
    sch_.update(now_ - ambStart_, acts);
    for (auto& a : acts) {
      const FscCell& c = sched_.cells[size_t(a.cell)];
      if (!a.start) { if (cellH_[size_t(a.cell)]) mixer_.fadeOut(cellH_[size_t(a.cell)], c.fout); continue; }
      VoiceParams p;
      p.category = AudioCategory::Ambience;
      p.volume = std::min(1024, sched_.vol * c.vol / 1024);
      p.pan = std::clamp((float(c.lr) - 512.f) / 512.f, -1.f, 1.f);
      p.priority = sched_.pri;
      p.loop = c.loop != 0;
      p.fadeInMs = c.fin;
      p.startDelayFrames = int64_t(c.dly) * mixer_.outputRate() / 1000;
      if (c.pitchH > c.pitchL) {
        int st = c.pitchL + int(rng_.below(uint32_t(c.pitchH - c.pitchL + 1)));
        p.pitch = std::pow(2.f, float(st) / 12.f);
      } else p.pitch = std::pow(2.f, float(c.pitchL) / 12.f);
      std::string path = std::string(ambientAreas()[size_t(ambArea_)].folder) + "/" + c.file;
      cellH_[size_t(a.cell)] = playBuf(path, false, p);
      if (!cellH_[size_t(a.cell)]) sch_.cellEnded(a.cell);
    }
  }
}

}  // namespace m3k
