#pragma once
#include "mathutil.h"
#include <SDL.h>
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
  SND_HUM,     // loop
  SND_THRUST,  // loop
  SND_COUNT
};

enum LoopChannel { LOOP_PHYSGUN, LOOP_THRUST, LOOP_COUNT };

// Tiny software mixer with procedurally synthesised sounds.
class Audio {
 public:
  bool init();
  void shutdown();
  void setListener(const glm::vec3& pos, const glm::vec3& right);
  void play(SoundId id, float volume = 1.0f, float pitch = 1.0f);
  void playAt(SoundId id, const glm::vec3& pos, float volume = 1.0f, float pitch = 1.0f);
  void setLoop(LoopChannel ch, SoundId id, bool on, float volume = 1.0f, float pitch = 1.0f);
  float masterVolume = 0.7f;
  bool ok() const { return dev_ != 0; }

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

  SDL_AudioDeviceID dev_ = 0;
  std::vector<float> sounds_[SND_COUNT];
  Voice voices_[32];
  Voice loops_[LOOP_COUNT];
  glm::vec3 lpos_{0}, lright_{1, 0, 0};
  int rate_ = 44100;
};
