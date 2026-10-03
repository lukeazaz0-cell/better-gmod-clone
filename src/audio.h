#pragma once
#include "mathutil.h"
#include <SDL.h>
#include <functional>
#include <string>
#include <vector>

enum SoundId {
  SND_IMPACT_SOFT,
  SND_IMPACT_HARD,
  SND_ZAP,
  SND_EXPLOSION,
  SND_SPAWN,
  SND_UNDO,
  SND_CLICK,
  SND_GRAB,
  SND_DROP,
  SND_FREEZE,
  SND_PUNT,
  SND_HUM,        // loop
  SND_THRUST,     // loop
  SND_ENGINE,     // loop
  SND_FOOTSTEPS,  // loop
  SND_AMBIENCE,   // loop
  SND_JUMP,
  SND_RUMBLE,
  SND_BREAK,
  SND_COUNT
};

enum LoopChannel { LOOP_PHYSGUN, LOOP_THRUST, LOOP_ENGINE, LOOP_FOOTSTEPS, LOOP_AMBIENCE, LOOP_COUNT };

// Where gameplay code sends sounds. The local Audio device implements it; in
// multiplayer the host gives remote players' weapons a sink that forwards to them.
class SoundOut {
 public:
  virtual ~SoundOut() = default;
  virtual void play(SoundId id, float volume = 1.0f, float pitch = 1.0f) = 0;
  virtual void playAt(SoundId id, const glm::vec3& pos, float volume = 1.0f, float pitch = 1.0f) = 0;
};

// Small software mixer. Sounds come from assets/sounds/*.ogg when available,
// otherwise they are synthesised procedurally.
class Audio : public SoundOut {
 public:
  bool init(const std::string& assetDir);
  void shutdown();
  void setListener(const glm::vec3& pos, const glm::vec3& right);
  void play(SoundId id, float volume = 1.0f, float pitch = 1.0f) override;
  void playAt(SoundId id, const glm::vec3& pos, float volume = 1.0f, float pitch = 1.0f) override;
  void setLoop(LoopChannel ch, SoundId id, bool on, float volume = 1.0f, float pitch = 1.0f);
  float masterVolume = 0.7f;
  bool ok() const { return dev_ != 0; }
  int filesLoaded() const { return filesLoaded_; }
  // Called for every positional sound (the multiplayer host forwards these to clients).
  std::function<void(SoundId, const glm::vec3&, float, float)> onPlayAt;

 private:
  struct Voice {
    const std::vector<float>* buf = nullptr;
    double pos = 0;
    float step = 1, volL = 0, volR = 0;
    bool loop = false, active = false;
  };
  static void callback(void* user, Uint8* stream, int len);
  void mix(float* out, int frames);
  void startVoice(SoundId id, float l, float r, float pitch);
  void synthesize();
  bool loadFile(const std::string& path, std::vector<float>& out);
  const std::vector<float>* pick(SoundId id);

  SDL_AudioDeviceID dev_ = 0;
  std::vector<std::vector<float>> sounds_[SND_COUNT];  // variants per sound
  Voice voices_[40];
  Voice loops_[LOOP_COUNT];
  glm::vec3 lpos_{0}, lright_{1, 0, 0};
  int rate_ = 44100;
  int filesLoaded_ = 0;
  bool layeredExplosion_ = false;  // recorded explosion + synthesised rumble
  unsigned rng_ = 12345;
};
