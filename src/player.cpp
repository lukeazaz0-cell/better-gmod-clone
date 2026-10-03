#include "player.h"
#include <algorithm>
#include <cmath>

static const float kHalf = Player::kHeight * 0.5f;

void Player::init(Physics& phys, const glm::vec3& feet, float y) {
  phys_ = &phys;
  respawnPoint = feet;
  respawnYaw = y;
  shape_ = new btCapsuleShape(kRadius, kHeight - 2 * kRadius);
  btVector3 inertia(0, 0, 0);
  auto* ms = new btDefaultMotionState(makeXf(feet + glm::vec3(0, kHalf, 0)));
  btRigidBody::btRigidBodyConstructionInfo ci(80.0f, ms, shape_, inertia);
  ci.m_friction = 0.0f;
  ci.m_restitution = 0.0f;
  body = new btRigidBody(ci);
  body->setAngularFactor(0);
  body->setActivationState(DISABLE_DEACTIVATION);
  body->setUserIndex(BODY_PLAYER);
  body->setCcdMotionThreshold(0.2f);
  body->setCcdSweptSphereRadius(0.3f);
  phys.world->addRigidBody(body, COL_PLAYER, COL_ALL);
  yaw = y;
  pitch = 0;
  postPhysics(0);
}

void Player::shutdown() {
  if (!body) return;
  if (!noclip) phys_->world->removeRigidBody(body);
  delete body->getMotionState();
  delete body;
  delete shape_;
  body = nullptr;
}

glm::vec3 Player::center() const {
  if (noclip) return flyPos_;
  btTransform t;
  body->getMotionState()->getWorldTransform(t);
  return toGlm(t.getOrigin());
}
glm::vec3 Player::feet() const { return center() - glm::vec3(0, kHalf, 0); }
glm::vec3 Player::velocity() const { return noclip ? glm::vec3(0) : toGlm(body->getLinearVelocity()); }

void Player::look(float dx, float dy, float sens) {
  yaw += dx * sens;
  pitch -= dy * sens;
  const float lim = glm::radians(89.0f);
  pitch = std::max(-lim, std::min(lim, pitch));
  yaw = std::fmod(yaw, 6.2831853f);
}

void Player::teleport(const glm::vec3& f, float y, float p) {
  yaw = y;
  pitch = p;
  glm::vec3 c = f + glm::vec3(0, kHalf, 0);
  flyPos_ = c;
  btTransform t = makeXf(c);
  body->setWorldTransform(t);
  body->getMotionState()->setWorldTransform(t);
  body->setLinearVelocity(btVector3(0, 0, 0));
  postPhysics(0);
}

void Player::setNoclip(bool on) {
  if (on == noclip) return;
  if (on) {
    flyPos_ = center();
    phys_->world->removeRigidBody(body);
    noclip = true;
  } else {
    noclip = false;
    btTransform t = makeXf(flyPos_);
    body->setWorldTransform(t);
    body->getMotionState()->setWorldTransform(t);
    body->setLinearVelocity(btVector3(0, 0, 0));
    phys_->world->addRigidBody(body, COL_PLAYER, COL_ALL);
  }
}

static glm::vec2 approach(const glm::vec2& cur, const glm::vec2& target, float maxDelta) {
  glm::vec2 d = target - cur;
  float l = glm::length(d);
  if (l <= maxDelta || l < 1e-6f) return target;
  return cur + d / l * maxDelta;
}

void Player::update(const Input& in, float dt, bool controls) {
  float fwdIn = 0, sideIn = 0, upIn = 0;
  bool sprint = false, jump = false;
  crouching = false;
  if (controls) {
    fwdIn = (in.down[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (in.down[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    sideIn = (in.down[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (in.down[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    upIn = (in.down[SDL_SCANCODE_SPACE] ? 1.0f : 0.0f) - (in.down[SDL_SCANCODE_LCTRL] ? 1.0f : 0.0f);
    sprint = in.down[SDL_SCANCODE_LSHIFT];
    jump = in.down[SDL_SCANCODE_SPACE];
    crouching = in.down[SDL_SCANCODE_LCTRL];
  }
  glm::vec3 fwd(std::sin(yaw), 0, -std::cos(yaw));
  glm::vec3 right(std::cos(yaw), 0, std::sin(yaw));

  if (noclip) {
    glm::vec3 dir = cam.forward() * fwdIn + right * sideIn + glm::vec3(0, 1, 0) * upIn;
    if (glm::length(dir) > 1e-4f) dir = glm::normalize(dir);
    float speed = sprint ? 32.0f : 10.0f;
    flyPos_ += dir * speed * dt;
    speedH = 0;
    return;
  }

  glm::vec3 c = toGlm(body->getWorldTransform().getOrigin());
  RayHit g = phys_->sweepSphere(kRadius * 0.9f, c, c - glm::vec3(0, kHalf - kRadius * 0.9f + 0.12f, 0), body);
  onGround = g.hit && g.normal.y > 0.6f;
  glm::vec3 groundVel(0);
  if (onGround && g.object) {
    const btRigidBody* gb = btRigidBody::upcast(g.object);
    if (gb && !gb->isStaticObject())
      groundVel = toGlm(gb->getVelocityInLocalPoint(toBt(g.point) - gb->getCenterOfMassPosition()));
  }

  glm::vec3 wish = fwd * fwdIn + right * sideIn;
  if (glm::length(wish) > 1e-4f) wish = glm::normalize(wish);
  float speed = crouching ? 2.2f : (sprint ? 8.0f : 4.6f);

  glm::vec3 v = toGlm(body->getLinearVelocity());
  glm::vec2 vh(v.x - groundVel.x, v.z - groundVel.z);
  glm::vec2 target(wish.x * speed, wish.z * speed);
  if (onGround) {
    vh = approach(vh, target, 55.0f * dt);
  } else if (glm::length(target) > 0) {
    // air control: steer without exceeding the target speed in the wish direction
    glm::vec2 nv = approach(vh, target, 9.0f * dt);
    if (glm::length(nv) < glm::length(vh) && glm::length(vh) > speed) nv = glm::normalize(nv) * glm::length(vh);
    vh = nv;
  }
  jumpCooldown_ -= dt;
  if (onGround && jump && jumpCooldown_ <= 0) {
    v.y = 5.3f + std::max(0.0f, groundVel.y);
    jumpCooldown_ = 0.25f;
    onGround = false;
  }
  v.x = vh.x + groundVel.x;
  v.z = vh.y + groundVel.z;
  body->setLinearVelocity(toBt(v));
  body->activate(true);
  speedH = onGround ? glm::length(vh) : 0.0f;
}

void Player::postPhysics(float dt) {
  float targetEye = crouching ? 0.1f : 0.75f;
  eyeOff_ += (targetEye - eyeOff_) * std::min(1.0f, dt * 12.0f);
  glm::vec3 c = center();
  if (!noclip && c.y < -60.0f) {
    teleport(respawnPoint, respawnYaw, 0);
    c = center();
  }
  bob += speedH * dt * 1.6f;
  cam.pos = c + glm::vec3(0, eyeOff_, 0);
  cam.yaw = yaw;
  cam.pitch = pitch;
}
