#pragma once
#include <SDL.h>
#include <map>
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
class Net;

class Game {
 public:
  Game();
  ~Game();
  bool init(int argc, char** argv);
  void run();
  void shutdown();

  // --- used by the UI ---
  void spawnProp(const PropDef& def);
  void spawnPropFor(const PropDef& def, const glm::vec3& eye, const glm::vec3& fwd, float yaw, int owner,
                    SoundOut& sound);
  int localHeld();
  bool hostGame(int port);
  bool joinGame(const std::string& address);
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
  std::map<std::string, unsigned int> icons;  // prop id -> GL texture
  void buildIcons();
  bool confirmClear = false;
  std::unique_ptr<Net> net;
  bool chatOpen = false;
  char chatBuf[200] = {};
  std::string playerName = "Player";
  char joinAddr[128] = "127.0.0.1";
  int hostPort = 27015;
  struct NameTag {
    glm::vec3 pos;
    std::string name;
  };
  std::vector<NameTag> nameTags;

 private:
  void handleEvent(const SDL_Event& e);
  void frame(float dt);
  void updateMouseMode();
  void playImpactSounds();
  void autotestStep(float dt);
  void screenshot(const std::string& path);
  void renderPlayers(const Camera& cam);
  void releaseAllInput();
  bool suppressText_ = false;
  void netAutotestStep(float dt);

  SDL_Window* window_ = nullptr;
  SDL_GLContext gl_ = nullptr;
  float qOpenTime_ = 0;
  glm::vec3 frameEye_{0}, frameFwd_{0, 0, -1}, frameMuzzle_{0};
  float shake_ = 0, impactCooldown_ = 0;
  bool autotest_ = false;
  int autoFrame_ = 0;
  int modelProps_ = 0;
  int netTest_ = 0;  // 1 = host, 2 = client
  std::string startHost_, startJoin_;
  std::string shotDir_ = ".";
  bool wantScreenshot_ = false;
  std::string pendingShot_;
};
