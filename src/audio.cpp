#include "audio.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb/stb_vorbis.c"

static const float TAU = 6.2831853f;

namespace {
struct Synth {
  int rate;
  std::mt19937 rng{1234};
  std::uniform_real_distribution<float> uni{-1.0f, 1.0f};
  float noise() { return uni(rng); }
  std::vector<float> make(float seconds) { return std::vector<float>((size_t)(seconds * rate), 0.0f); }
};

void normalize(std::vector<float>& b, float peak) {
  float m = 1e-6f;
  for (float v : b) m = std::max(m, std::fabs(v));
  for (float& v : b) v *= peak / m;
}
}  // namespace

bool Audio::init(const std::string& assetDir) {
  SDL_AudioSpec want{}, have{};
  want.freq = 44100;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = &Audio::callback;
  want.userdata = this;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
    return false;
  }
  dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!dev_) {
    fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
    return false;
  }
  rate_ = have.freq;
  synthesize();

  // Real recordings override the synthesised versions where available.
  struct FileMap {
    SoundId id;
    std::vector<const char*> files;
  };
  const FileMap files[] = {
      {SND_IMPACT_HARD, {"impact", "break"}},
      {SND_IMPACT_SOFT, {"land"}},
      {SND_ZAP, {"zap"}},
      {SND_EXPLOSION, {"explosion"}},
      {SND_SPAWN, {"spawn_a", "spawn_b", "spawn_c", "spawn_d"}},
      {SND_UNDO, {"undo_a", "undo_b", "undo_c", "undo_d"}},
      {SND_CLICK, {"weapon_change"}},
      {SND_PUNT, {"punt"}},
      {SND_FREEZE, {"freeze"}},
      {SND_ENGINE, {"engine"}},
      {SND_FOOTSTEPS, {"footsteps"}},
      {SND_AMBIENCE, {"ambience"}},
      {SND_JUMP, {"jump"}},
      {SND_BREAK, {"break"}},
  };
  if (!assetDir.empty()) {
    for (const FileMap& fm : files) {
      std::vector<std::vector<float>> loaded;
      for (const char* f : fm.files) {
        std::vector<float> buf;
        if (loadFile(assetDir + "/sounds/" + f + ".ogg", buf)) loaded.push_back(std::move(buf));
      }
      if (!loaded.empty()) {
        if (fm.id == SND_EXPLOSION) layeredExplosion_ = true;
        sounds_[fm.id] = std::move(loaded);
        filesLoaded_++;
      }
    }
  }
  SDL_PauseAudioDevice(dev_, 0);
  return true;
}

bool Audio::loadFile(const std::string& path, std::vector<float>& out) {
  int channels = 0, srate = 0;
  short* data = nullptr;
  int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &srate, &data);
  if (frames <= 0 || !data || channels <= 0) {
    if (data) free(data);
    return false;
  }
  // Mix down to mono and resample (linear) to the device rate.
  std::vector<float> mono(frames);
  for (int i = 0; i < frames; i++) {
    float acc = 0;
    for (int c = 0; c < channels; c++) acc += data[i * channels + c];
    mono[i] = acc / (32768.0f * channels);
  }
  free(data);
  if (srate == rate_) {
    out = std::move(mono);
  } else {
    double ratio = (double)srate / rate_;
    size_t n = (size_t)(frames / ratio);
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
      double p = i * ratio;
      size_t a = (size_t)p;
      size_t b = std::min(a + 1, mono.size() - 1);
      float t = (float)(p - a);
      out[i] = mono[a] * (1 - t) + mono[b] * t;
    }
  }
  return !out.empty();
}

const std::vector<float>* Audio::pick(SoundId id) {
  auto& v = sounds_[id];
  if (v.empty()) return nullptr;
  rng_ = rng_ * 1103515245u + 12345u;
  return &v[(rng_ >> 16) % v.size()];
}

void Audio::synthesize() {
  Synth s{rate_};
  float R = (float)rate_;

  {  // soft impact: filtered noise + low thump
    auto b = s.make(0.18f);
    float lp = 0;
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R;
      lp += (s.noise() - lp) * 0.12f;
      b[i] = lp * std::exp(-t * 30.0f) * 1.5f + std::sin(TAU * 70 * t) * std::exp(-t * 35.0f) * 0.8f;
    }
    normalize(b, 0.8f);
    sounds_[SND_IMPACT_SOFT] = {b};
  }
  {  // hard impact
    auto b = s.make(0.3f);
    float lp = 0;
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R;
      lp += (s.noise() - lp) * 0.35f;
      b[i] = lp * std::exp(-t * 22.0f) + std::sin(TAU * 95 * t) * std::exp(-t * 25.0f) * 0.6f +
             std::sin(TAU * 410 * t) * std::exp(-t * 40.0f) * 0.2f;
    }
    normalize(b, 0.9f);
    sounds_[SND_IMPACT_HARD] = {b};
  }
  {  // toolgun zap
    auto b = s.make(0.2f);
    float ph = 0;
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R;
      float f = 2200.0f * std::exp(-t * 12.0f) + 300.0f;
      ph += TAU * f / R;
      float sq = std::sin(ph) > 0 ? 1.0f : -1.0f;
      b[i] = (std::sin(ph) * 0.6f + sq * 0.25f + s.noise() * 0.15f) * std::exp(-t * 14.0f);
    }
    normalize(b, 0.6f);
    sounds_[SND_ZAP] = {b};
  }
  {  // explosion: brown noise rumble
    auto b = s.make(2.0f);
    float br = 0, lp = 0;
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R;
      br = br * 0.995f + s.noise() * 0.1f;
      lp += (s.noise() - lp) * 0.5f;
      b[i] = br * std::exp(-t * 2.2f) * 2.0f + lp * std::exp(-t * 18.0f) * 0.8f +
             std::sin(TAU * 38 * t) * std::exp(-t * 3.0f) * 0.7f;
    }
    normalize(b, 1.0f);
    sounds_[SND_EXPLOSION] = {b};
    sounds_[SND_RUMBLE] = {b};
  }
  auto sweep = [&](float dur, float f0, float f1, float decay, float amp) {
    auto b = s.make(dur);
    float ph = 0;
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R, k = t / dur;
      ph += TAU * (f0 + (f1 - f0) * k) / R;
      b[i] = std::sin(ph) * std::exp(-t * decay) * std::min(1.0f, t * 400.0f);
    }
    normalize(b, amp);
    return b;
  };
  sounds_[SND_SPAWN] = {sweep(0.09f, 500, 950, 20, 0.5f)};
  sounds_[SND_UNDO] = {sweep(0.12f, 900, 450, 15, 0.45f)};
  sounds_[SND_CLICK] = {sweep(0.03f, 1500, 1400, 60, 0.3f)};
  sounds_[SND_GRAB] = {sweep(0.12f, 250, 700, 12, 0.45f)};
  sounds_[SND_DROP] = {sweep(0.12f, 700, 250, 12, 0.4f)};
  {  // freeze chime
    auto b = s.make(0.35f);
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R;
      b[i] = (std::sin(TAU * 1250 * t) + 0.6f * std::sin(TAU * 1875 * t)) * std::exp(-t * 12.0f);
    }
    normalize(b, 0.4f);
    sounds_[SND_FREEZE] = {b};
  }
  {  // gravity gun punt
    auto b = s.make(0.25f);
    float lp = 0;
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R;
      lp += (s.noise() - lp) * 0.25f;
      b[i] = lp * std::exp(-t * 18.0f) + std::sin(TAU * (160 - t * 300) * t) * std::exp(-t * 12.0f);
    }
    normalize(b, 0.8f);
    sounds_[SND_PUNT] = {b};
  }
  {  // physgun hum: integer cycles per second so it loops seamlessly
    auto b = s.make(1.0f);
    for (size_t i = 0; i < b.size(); i++) {
      float t = i / R;
      b[i] = std::sin(TAU * 110 * t) * 0.5f + std::sin(TAU * 220 * t + std::sin(TAU * 3 * t)) * 0.3f +
             std::sin(TAU * 331 * t) * 0.12f;
    }
    normalize(b, 0.35f);
    sounds_[SND_HUM] = {b};
  }
  {  // thruster roar
    auto b = s.make(1.0f);
    float lp = 0;
    for (size_t i = 0; i < b.size(); i++) {
      lp += (s.noise() - lp) * 0.08f;
      b[i] = lp;
    }
    // crossfade the ends so the loop is seamless
    size_t fade = rate_ / 20;
    for (size_t i = 0; i < fade; i++) {
      float k = (float)i / fade;
      b[i] = b[i] * k + b[b.size() - fade + i] * (1 - k);
    }
    b.resize(b.size() - fade);
    normalize(b, 0.5f);
    sounds_[SND_THRUST] = {b};
    sounds_[SND_ENGINE] = {b};
  }
  {  // footsteps fallback: two soft thumps per loop
    auto b = s.make(0.6f);
    float lp = 0;
    for (size_t i = 0; i < b.size(); i++) {
      float t = std::fmod(i / R, 0.3f);
      lp += (s.noise() - lp) * 0.1f;
      b[i] = lp * std::exp(-t * 40.0f);
    }
    normalize(b, 0.3f);
    sounds_[SND_FOOTSTEPS] = {b};
  }
  sounds_[SND_JUMP] = {sweep(0.12f, 300, 500, 20, 0.3f)};
  sounds_[SND_BREAK] = sounds_[SND_IMPACT_HARD];
  sounds_[SND_AMBIENCE] = {std::vector<float>(rate_, 0.0f)};
}

void Audio::shutdown() {
  if (dev_) SDL_CloseAudioDevice(dev_);
  dev_ = 0;
}

void Audio::setListener(const glm::vec3& pos, const glm::vec3& right) {
  lpos_ = pos;
  lright_ = right;
}

void Audio::startVoice(SoundId id, float l, float r, float pitch) {
  if (!dev_) return;
  SDL_LockAudioDevice(dev_);
  Voice* best = nullptr;
  for (auto& v : voices_)
    if (!v.active) {
      best = &v;
      break;
    }
  if (!best) best = &voices_[0];
  best->buf = pick(id);
  if (!best->buf) {
    SDL_UnlockAudioDevice(dev_);
    return;
  }
  best->pos = 0;
  best->step = pitch;  // buffers are synthesised at the device rate
  best->volL = l;
  best->volR = r;
  best->loop = false;
  best->active = true;
  SDL_UnlockAudioDevice(dev_);
}

void Audio::play(SoundId id, float volume, float pitch) {
  startVoice(id, volume, volume, pitch);
  if (id == SND_EXPLOSION && layeredExplosion_) startVoice(SND_RUMBLE, volume * 0.8f, volume * 0.8f, 1.0f);
}

void Audio::playAt(SoundId id, const glm::vec3& pos, float volume, float pitch) {
  if (onPlayAt) onPlayAt(id, pos, volume, pitch);
  glm::vec3 d = pos - lpos_;
  float dist = glm::length(d);
  float att = 1.0f / (1.0f + dist * 0.08f + dist * dist * 0.002f);
  float pan = dist > 0.01f ? glm::dot(d / dist, lright_) : 0.0f;
  float l = volume * att * (0.6f - 0.4f * pan), r = volume * att * (0.6f + 0.4f * pan);
  if (l + r < 0.01f) return;
  startVoice(id, l, r, pitch);
  if (id == SND_EXPLOSION && layeredExplosion_) startVoice(SND_RUMBLE, l * 0.8f, r * 0.8f, 1.0f);
}

void Audio::setLoop(LoopChannel ch, SoundId id, bool on, float volume, float pitch) {
  if (!dev_) return;
  SDL_LockAudioDevice(dev_);
  Voice& v = loops_[ch];
  const std::vector<float>* buf = sounds_[id].empty() ? nullptr : &sounds_[id][0];
  if (on && buf) {
    if (!v.active || v.buf != buf) {
      v.buf = buf;
      v.pos = 0;
    }
    v.active = true;
    v.loop = true;
    v.volL = v.volR = volume;
    v.step = pitch;
  } else {
    v.active = false;
  }
  SDL_UnlockAudioDevice(dev_);
}

void Audio::callback(void* user, Uint8* stream, int len) {
  static_cast<Audio*>(user)->mix(reinterpret_cast<float*>(stream), len / (int)(sizeof(float) * 2));
}

void Audio::mix(float* out, int frames) {
  for (int i = 0; i < frames * 2; i++) out[i] = 0;
  auto run = [&](Voice& v) {
    if (!v.active || !v.buf || v.buf->empty()) return;
    const std::vector<float>& b = *v.buf;
    for (int i = 0; i < frames; i++) {
      size_t p = (size_t)v.pos;
      if (p >= b.size()) {
        if (v.loop) {
          v.pos -= (double)b.size();
          p = (size_t)v.pos;
        } else {
          v.active = false;
          return;
        }
      }
      float s = b[p];
      out[i * 2] += s * v.volL;
      out[i * 2 + 1] += s * v.volR;
      v.pos += v.step;
    }
  };
  for (auto& v : voices_) run(v);
  for (auto& v : loops_) run(v);
  for (int i = 0; i < frames * 2; i++) {
    float x = out[i] * masterVolume;
    out[i] = x / (1.0f + std::fabs(x));  // soft clip
  }
}
