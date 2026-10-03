#pragma once
#include "input.h"
#include "physics.h"
#include "renderer.h"

class Player {
 public:
  void init(Physics& phys, const glm::vec3& feet, float yaw);
  // Host-side stand-in for a network player: a kinematic capsule moved by the client.
  void initRemote(Physics& phys, const glm::vec3& feet);
  void setRemoteState(const glm::vec3& feet, const glm::vec3& eye, float yaw, float pitch, bool noclip);
  void shutdown();
  void look(float dx, float dy, float sensitivity);
  void update(const Input& in, float dt, bool controlsEnabled);  // before physics step
  void postPhysics(float dt);                                    // after physics step
  void teleport(const glm::vec3& feet, float yaw, float pitch);
  void setNoclip(bool on);

  glm::vec3 feet() const;
  glm::vec3 center() const;
  glm::vec3 velocity() const;
  glm::vec3 lookForward() const { return cam.forward(); }

  Camera cam;
  btRigidBody* body = nullptr;
  bool noclip = false, onGround = false, crouching = false;
  float yaw = 0, pitch = 0;
  float bob = 0, speedH = 0;
  bool jumped = false;  // set on the frame a jump starts
  glm::vec3 respawnPoint{0, 0.2f, 0};
  float respawnYaw = 0;

  static constexpr float kRadius = 0.35f, kHeight = 1.8f;

 private:
  Physics* phys_ = nullptr;
  btCapsuleShape* shape_ = nullptr;
  glm::vec3 flyPos_{0};
  float eyeOff_ = 0.75f;
  float jumpCooldown_ = 0;
};
