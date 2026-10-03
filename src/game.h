#pragma once
#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include "assets.h"
#include "audio.h"
#include "input.h"
#include "physics.h"
#include "player.h"
#include "renderer.h"
#include "weapons.h"
#include "world.h"

struct ImFont;

class Game {
 public:
  bool init(int argc, char** argv);
  void run();
  void shutdown();

  // --- used by the UI ---
  void spawnProp(const PropDef& def);
  void selectWeapon(int i);
  void doUndo();
  bool saveGame(const std::string& name);
  bool loadGame(const std::string& name);
  std::vector<std::string> listSaves() const;
  void deleteSave(const std::string& name);
  void freezeAll(bool freeze);
  void respawn();
  ToolGun* toolgun() { return static_cast<ToolGun*>(weapons[2].get()); }
  PhysGun* physgun() { return static_cast<PhysGun*>(weapons[0].get()); }
  Weapon* weapon() { return weapons[curWeapon].get(); }
  GameCtx makeCtx();

  Renderer renderer;
  Physics physics;
  Assets assets;
  World world{physics, assets};
  Player player;
  Audio audio;
  Notifier notify;
  Input input;
  std::vector<std::unique_ptr<Weapon>> weapons;
  int curWeapon = 0;

  bool spawnMenu = false, paused = false, showHelp = false, running = true;
  float mouseSens = 0.0022f;
  float fov = 80.0f;
  float time = 0, fps = 0;
  float hintTimer = 12.0f;
  std::string saveDir, saveName = "my_save";
  ImFont* fontBig = nullptr;
  bool confirmClear = false;

 private:
  void handleEvent(const SDL_Event& e);
  void frame(float dt);
  void updateMouseMode();
  void playImpactSounds();
  void autotestStep(float dt);
  void screenshot(const std::string& path);

  SDL_Window* window_ = nullptr;
  SDL_GLContext gl_ = nullptr;
  float qOpenTime_ = 0;
  glm::vec3 frameEye_{0}, frameFwd_{0, 0, -1}, frameMuzzle_{0};
  float shake_ = 0, impactCooldown_ = 0;
  bool autotest_ = false;
  int autoFrame_ = 0;
  std::string shotDir_ = ".";
  bool wantScreenshot_ = false;
  std::string pendingShot_;
};
