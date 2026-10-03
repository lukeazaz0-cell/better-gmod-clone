#include "physics.h"
#include <algorithm>

static void preTickCallback(btDynamicsWorld* w, btScalar timeStep) {
  auto* p = static_cast<Physics*>(w->getWorldUserInfo());
  if (p && p->onPreTick) p->onPreTick((float)timeStep);
}

bool Physics::init() {
  config_ = new btDefaultCollisionConfiguration();
  dispatcher_ = new btCollisionDispatcher(config_);
  broadphase_ = new btDbvtBroadphase();
  solver_ = new btSequentialImpulseConstraintSolver();
  world = new btDiscreteDynamicsWorld(dispatcher_, broadphase_, solver_, config_);
  world->setGravity(btVector3(0, -gravity_, 0));
  world->getSolverInfo().m_numIterations = 20;
  world->getSolverInfo().m_splitImpulse = true;
  world->setInternalTickCallback(preTickCallback, this, true);
  return true;
}

void Physics::shutdown() {
  if (!world) return;
  // Remaining objects/constraints are owned by World and removed there first.
  delete world;
  delete solver_;
  delete broadphase_;
  delete dispatcher_;
  delete config_;
  world = nullptr;
}

void Physics::step(float dt) {
  if (paused) {
    lastSubSteps = 0;
    return;
  }
  float scaled = dt * timeScale;
  lastSubSteps = world->stepSimulation(scaled, maxSubSteps, fixedDt * std::min(1.0f, timeScale));
}

void Physics::setGravity(float g) {
  gravity_ = g;
  world->setGravity(btVector3(0, -g, 0));
}

namespace {
struct IgnoreRay : public btCollisionWorld::ClosestRayResultCallback {
  const btCollisionObject* ignore;
  IgnoreRay(const btVector3& a, const btVector3& b, const btCollisionObject* ig)
      : ClosestRayResultCallback(a, b), ignore(ig) {}
  bool needsCollision(btBroadphaseProxy* proxy) const override {
    if (proxy->m_clientObject == ignore) return false;
    return ClosestRayResultCallback::needsCollision(proxy);
  }
};
struct IgnoreSweep : public btCollisionWorld::ClosestConvexResultCallback {
  const btCollisionObject* ignore;
  IgnoreSweep(const btVector3& a, const btVector3& b, const btCollisionObject* ig)
      : ClosestConvexResultCallback(a, b), ignore(ig) {}
  bool needsCollision(btBroadphaseProxy* proxy) const override {
    if (proxy->m_clientObject == ignore) return false;
    return ClosestConvexResultCallback::needsCollision(proxy);
  }
};
}  // namespace

RayHit Physics::raycast(const glm::vec3& from, const glm::vec3& to, const btCollisionObject* ignore, int mask) const {
  RayHit r;
  btVector3 a = toBt(from), b = toBt(to);
  IgnoreRay cb(a, b, ignore);
  cb.m_collisionFilterMask = mask;
  world->rayTest(a, b, cb);
  if (cb.hasHit()) {
    r.hit = true;
    r.point = toGlm(cb.m_hitPointWorld);
    r.normal = glm::normalize(toGlm(cb.m_hitNormalWorld));
    r.fraction = (float)cb.m_closestHitFraction;
    r.object = cb.m_collisionObject;
    r.userIndex = cb.m_collisionObject->getUserIndex();
  }
  return r;
}

RayHit Physics::sweepSphere(float radius, const glm::vec3& from, const glm::vec3& to,
                            const btCollisionObject* ignore) const {
  RayHit r;
  btSphereShape s(radius);
  btVector3 a = toBt(from), b = toBt(to);
  IgnoreSweep cb(a, b, ignore);
  btTransform ta(btQuaternion::getIdentity(), a), tb(btQuaternion::getIdentity(), b);
  world->convexSweepTest(&s, ta, tb, cb);
  if (cb.hasHit()) {
    r.hit = true;
    r.point = toGlm(cb.m_hitPointWorld);
    r.normal = glm::normalize(toGlm(cb.m_hitNormalWorld));
    r.fraction = (float)cb.m_closestHitFraction;
    r.object = cb.m_hitCollisionObject;
    r.userIndex = cb.m_hitCollisionObject->getUserIndex();
  }
  return r;
}
