#pragma once
#include "audio.h"
#include "input.h"
#include "player.h"
#include "renderer.h"
#include "world.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>

struct Notifier {
  struct Note {
    std::string text;
    float t = 0;
    glm::vec4 color;
  };
  std::deque<Note> notes;
  void push(const std::string& s, const glm::vec4& color = glm::vec4(0.35f, 0.65f, 1.0f, 1.0f));
  void update(float dt);
};

struct GameCtx {
  World& world;
  Player& player;
  Physics& physics;
  Audio& audio;
  Notifier& notify;
  Assets& assets;
  glm::vec3 eye{0}, fwd{0, 0, -1};
  glm::vec3 muzzle{0};
  RayHit trace(float dist = 4000.0f) const;
};

struct ToolHit {
  bool hit = false;
  glm::vec3 pos{0}, normal{0, 1, 0};
  Prop* prop = nullptr;
  bool world = false;
};

class Tool {
 public:
  virtual ~Tool() = default;
  virtual const char* name() const = 0;
  virtual const char* category() const { return "Construction"; }
  virtual const char* desc() const = 0;
  virtual std::string help() const = 0;
  virtual bool primary(GameCtx&, const ToolHit&) { return false; }
  virtual bool secondary(GameCtx&, const ToolHit&) { return false; }
  virtual bool reload(GameCtx&, const ToolHit&) { return false; }
  virtual void settings() {}
  virtual void reset() { stage = 0; }
  virtual int highlight() const { return stage > 0 ? firstProp : -1; }

 protected:
  int stage = 0;
  int firstProp = -1;
  glm::vec3 firstWorld{0}, firstNormal{0, 1, 0};
};
std::vector<std::unique_ptr<Tool>> makeTools();

struct VMPart {
  glm::vec3 offset, half, color;
  int material;
};

class Weapon {
 public:
  virtual ~Weapon() = default;
  virtual const char* name() const = 0;
  virtual void update(GameCtx&, const Input&, float dt) {}
  virtual void preTick(GameCtx&, float dt) {}
  virtual void render(GameCtx&, Renderer&) {}
  virtual void holster(GameCtx&) {}
  virtual bool locksView() const { return false; }
  virtual bool consumesWheel() const { return false; }
  virtual bool active() const { return false; }  // drives the glow on the viewmodel

  void updateSway(const Input& in, float dt);
  void renderViewmodel(Renderer& r, Assets& assets, const Camera& cam, float bob);
  glm::vec3 muzzleWorld(const Camera& cam, float bob) const;

 protected:
  std::vector<VMPart> parts_;
  glm::vec3 glowColor_{0.3f, 0.7f, 1.0f};
  glm::vec2 sway_{0};
  float kick_ = 0;
  glm::mat4 vmBase(const Camera& cam, float bob) const;
};

class PhysGun : public Weapon {
 public:
  PhysGun();
  const char* name() const override { return "Physics Gun"; }
  void update(GameCtx&, const Input&, float dt) override;
  void preTick(GameCtx&, float dt) override;
  void render(GameCtx&, Renderer&) override;
  void holster(GameCtx&) override;
  bool locksView() const override { return rotating_; }
  bool consumesWheel() const override { return held_ >= 0; }
  bool active() const override { return firing_; }
  int held() const { return held_; }

 private:
  void release(GameCtx&);
  int held_ = -1;
  glm::vec3 localHit_{0};
  float dist_ = 0;
  glm::quat rel_{1, 0, 0, 0}, raw_{1, 0, 0, 0};
  bool rotating_ = false, snap_ = false, firing_ = false;
  glm::vec3 beamEnd_{0};
};

class GravGun : public Weapon {
 public:
  GravGun();
  const char* name() const override { return "Gravity Gun"; }
  void update(GameCtx&, const Input&, float dt) override;
  void preTick(GameCtx&, float dt) override;
  void render(GameCtx&, Renderer&) override;
  void holster(GameCtx&) override;
  bool active() const override { return held_ >= 0 || pulling_; }

 private:
  int held_ = -1;
  float holdDist_ = 2;
  glm::quat rel_{1, 0, 0, 0};
  bool pulling_ = false;
};

class ToolGun : public Weapon {
 public:
  ToolGun();
  const char* name() const override { return "Tool Gun"; }
  void update(GameCtx&, const Input&, float dt) override;
  void render(GameCtx&, Renderer&) override;
  void holster(GameCtx&) override;
  bool active() const override { return shotTimer_ > 0; }

  std::vector<std::unique_ptr<Tool>> tools;
  int current = 0;
  void select(int i);
  Tool* tool() { return tools.empty() ? nullptr : tools[current].get(); }

 private:
  float shotTimer_ = 0;
  glm::vec3 shotStart_{0}, shotEnd_{0};
};
