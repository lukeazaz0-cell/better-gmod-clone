#pragma once
#include "mathutil.h"
#include <functional>

enum ColGroup : int {
  COL_WORLD = 1,
  COL_PROP = 2,
  COL_PLAYER = 4,
  COL_ALL = -1,
};

// Body user index conventions.
enum : int { BODY_WORLD = -1, BODY_PLAYER = -2 };

struct RayHit {
  bool hit = false;
  glm::vec3 point{0}, normal{0, 1, 0};
  float fraction = 1.0f;
  const btCollisionObject* object = nullptr;
  int userIndex = BODY_WORLD;  // prop id, BODY_WORLD or BODY_PLAYER
};

class Physics {
 public:
  bool init();
  void shutdown();

  // Advances the simulation by dt seconds (scaled by timeScale) using fixed substeps.
  void step(float dt);

  RayHit raycast(const glm::vec3& from, const glm::vec3& to, const btCollisionObject* ignore = nullptr,
                 int mask = COL_ALL) const;
  RayHit sweepSphere(float radius, const glm::vec3& from, const glm::vec3& to,
                     const btCollisionObject* ignore = nullptr) const;

  void setGravity(float g);
  float gravity() const { return gravity_; }

  btDiscreteDynamicsWorld* world = nullptr;
  std::function<void(float)> onPreTick;
  float fixedDt = 1.0f / 120.0f;
  int maxSubSteps = 10;
  float timeScale = 1.0f;
  int lastSubSteps = 0;
  bool paused = false;

 private:
  btDefaultCollisionConfiguration* config_ = nullptr;
  btCollisionDispatcher* dispatcher_ = nullptr;
  btBroadphaseInterface* broadphase_ = nullptr;
  btSequentialImpulseConstraintSolver* solver_ = nullptr;
  float gravity_ = 9.81f;
};
