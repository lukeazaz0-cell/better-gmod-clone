#include "weapons.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
void Notifier::push(const std::string& s, const glm::vec4& color) {
  notes.push_back({s, 0.0f, color});
  while (notes.size() > 6) notes.pop_front();
}
void Notifier::update(float dt) {
  for (auto& n : notes) n.t += dt;
  while (!notes.empty() && notes.front().t > 5.0f) notes.pop_front();
}

RayHit GameCtx::trace(float dist) const { return physics.raycast(eye, eye + fwd * dist, player.body); }

// Steers a body towards a target pose by setting its velocities (used by the physgun / gravgun).
static void driveBody(btRigidBody* b, const glm::vec3& targetPos, const glm::quat& targetRot, float dt, float gain,
                      float maxLin, float maxAng) {
  btTransform t = b->getWorldTransform();
  glm::vec3 lv = (targetPos - toGlm(t.getOrigin())) / dt * gain;
  float l = glm::length(lv);
  if (l > maxLin) lv *= maxLin / l;
  glm::quat cq = toGlm(t.getRotation());
  glm::quat dq = targetRot * glm::inverse(cq);
  if (dq.w < 0) dq = -dq;
  float angle = 2.0f * std::acos(std::min(1.0f, std::max(-1.0f, dq.w)));
  glm::vec3 av(0);
  float s = std::sin(angle * 0.5f);
  if (angle > 1e-4f && s > 1e-5f) av = glm::vec3(dq.x, dq.y, dq.z) / s * (angle / dt * gain);
  float al = glm::length(av);
  if (al > maxAng) av *= maxAng / al;
  b->setLinearVelocity(toBt(lv));
  b->setAngularVelocity(toBt(av));
  b->activate(true);
}

// ---------------------------------------------------------------------------
// Weapon base: viewmodel
// ---------------------------------------------------------------------------
void Weapon::updateSway(const Input& in, float dt) {
  glm::vec2 target(-in.mdx * 0.0006f, in.mdy * 0.0006f);
  target = glm::clamp(target, glm::vec2(-0.04f), glm::vec2(0.04f));
  sway_ += (target - sway_) * std::min(1.0f, dt * 10.0f);
  kick_ = std::max(0.0f, kick_ - dt * 6.0f);
}

glm::mat4 Weapon::vmBase(const Camera& cam, float bob) const {
  glm::mat4 inv = glm::inverse(cam.view());
  glm::vec3 off(0.15f + sway_.x + std::sin(bob) * 0.008f, -0.135f + sway_.y - std::fabs(std::cos(bob)) * 0.006f,
                -0.36f + kick_ * 0.035f);
  return inv * glm::translate(glm::mat4(1), off) * glm::rotate(glm::mat4(1), 0.04f, glm::vec3(0, 1, 0)) *
         glm::rotate(glm::mat4(1), kick_ * 0.15f, glm::vec3(1, 0, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.55f));
}

glm::vec3 Weapon::muzzleWorld(const Camera& cam, float bob) const {
  return glm::vec3(vmBase(cam, bob) * glm::vec4(0, 0.01f, -0.34f, 1));
}

void Weapon::renderViewmodel(Renderer& r, Assets& assets, const Camera& cam, float bob) {
  glm::mat4 base = vmBase(cam, bob);
  for (const VMPart& p : parts_) {
    ShapeDesc sd{ShapeKind::Box, p.half, ""};
    DrawCmd c;
    c.mesh = assets.mesh(sd);
    c.model = base * glm::translate(glm::mat4(1), p.offset);
    c.material = p.material;
    c.color = glm::vec4(p.color, 1);
    if (p.material == MAT_GLOW) {
      c.color = glm::vec4(glowColor_ * (active() ? 1.0f : 0.5f), 1);
      c.emissive = 1;
    }
    c.castShadow = false;
    r.drawViewmodel(c);
  }
}

// ---------------------------------------------------------------------------
// Physics gun
// ---------------------------------------------------------------------------
PhysGun::PhysGun() {
  glowColor_ = glm::vec3(0.3f, 0.75f, 1.0f);
  parts_ = {
      {{0, 0, 0}, {0.045f, 0.05f, 0.2f}, {0.22f, 0.24f, 0.3f}, MAT_METAL},
      {{0, 0.0f, -0.17f}, {0.025f, 0.025f, 0.09f}, {1, 1, 1}, MAT_GLOW},
      {{0, 0.065f, -0.24f}, {0.012f, 0.012f, 0.08f}, {0.7f, 0.72f, 0.78f}, MAT_METAL},
      {{0.055f, -0.03f, -0.24f}, {0.012f, 0.012f, 0.08f}, {0.7f, 0.72f, 0.78f}, MAT_METAL},
      {{-0.055f, -0.03f, -0.24f}, {0.012f, 0.012f, 0.08f}, {0.7f, 0.72f, 0.78f}, MAT_METAL},
      {{0, -0.09f, 0.08f}, {0.03f, 0.07f, 0.035f}, {0.12f, 0.12f, 0.14f}, MAT_RUBBER},
      {{0, 0.055f, 0.04f}, {0.018f, 0.008f, 0.08f}, {1, 1, 1}, MAT_GLOW},
  };
}

static glm::quat snapRotation(const glm::quat& q) {
  glm::vec3 e = glm::eulerAngles(q);
  const float step = glm::radians(45.0f);
  e = glm::round(e / step) * step;
  return glm::quat(e);
}

void PhysGun::release(GameCtx& c) {
  if (held_ >= 0) c.audio.play(SND_DROP, 0.6f);
  held_ = -1;
  rotating_ = false;
}

void PhysGun::holster(GameCtx& c) {
  release(c);
  firing_ = false;
}

void PhysGun::update(GameCtx& c, const Input& in, float dt) {
  updateSway(in, dt);
  if (held_ >= 0 && !c.world.prop(held_)) held_ = -1;

  if (in.mousePressed[SDL_BUTTON_LEFT]) {
    firing_ = true;
    RayHit h = c.trace();
    Prop* p = h.hit ? c.world.propFromObject(h.object) : nullptr;
    if (p) {
      if (p->frozen) c.world.setFrozen(*p, false);
      held_ = p->id;
      btTransform t = p->body->getWorldTransform();
      localHit_ = toGlm(t.inverse() * toBt(h.point));
      dist_ = glm::length(h.point - c.eye);
      rel_ = raw_ = glm::inverse(yawQuat(c.player.yaw)) * toGlm(t.getRotation());
      c.audio.play(SND_GRAB, 0.6f);
    }
  }
  if (in.mouseReleased[SDL_BUTTON_LEFT] || !in.mouseDown[SDL_BUTTON_LEFT]) {
    if (held_ >= 0) release(c);
    firing_ = false;
  }

  if (held_ >= 0) {
    Prop* p = c.world.prop(held_);
    if (in.wheel != 0) dist_ = std::max(1.0f, std::min(3000.0f, dist_ + in.wheel * std::max(0.4f, dist_ * 0.1f)));
    rotating_ = in.down[SDL_SCANCODE_E];
    bool shift = in.down[SDL_SCANCODE_LSHIFT];
    if (snap_ && !(rotating_ && shift)) raw_ = rel_;  // keep the snapped angle once shift/E is let go
    snap_ = rotating_ && shift;
    if (rotating_) {
      glm::quat r = glm::angleAxis(in.mdx * 0.006f, glm::vec3(0, 1, 0)) * glm::angleAxis(in.mdy * 0.006f, glm::vec3(1, 0, 0));
      raw_ = glm::normalize(r * raw_);
      if (shift) {
        glm::quat yq = yawQuat(c.player.yaw);
        rel_ = glm::inverse(yq) * snapRotation(yq * raw_);
      } else {
        rel_ = raw_;
      }
    }
    if (in.mousePressed[SDL_BUTTON_RIGHT]) {
      c.world.setFrozen(*p, true);
      p->flash = 0.6f;
      c.audio.play(SND_FREEZE, 0.7f);
      held_ = -1;
      rotating_ = false;
    }
  } else {
    rotating_ = false;
  }

  if (in.pressed[SDL_SCANCODE_R]) {
    RayHit h = c.trace();
    Prop* p = h.hit ? c.world.propFromObject(h.object) : nullptr;
    if (p) {
      int n = c.world.unfreezeContraption(p->id);
      if (n > 0) {
        c.notify.push("Unfroze " + std::to_string(n) + (n == 1 ? " object" : " objects"));
        c.audio.play(SND_FREEZE, 0.6f, 0.7f);
      }
    }
  }

  if (firing_) {
    if (Prop* p = c.world.prop(held_)) {
      beamEnd_ = toGlm(p->renderXf() * toBt(localHit_));
    } else {
      RayHit h = c.trace();
      beamEnd_ = h.hit ? h.point : c.eye + c.fwd * 60.0f;
    }
  }
}

void PhysGun::preTick(GameCtx& c, float dt) {
  Prop* p = c.world.prop(held_);
  if (!p) return;
  glm::vec3 target = c.eye + c.fwd * dist_;
  glm::quat rot = yawQuat(c.player.yaw) * rel_;
  glm::vec3 origin = target - rot * localHit_;
  driveBody(p->body, origin, rot, dt, 0.5f, 250.0f, 40.0f);
}

void PhysGun::render(GameCtx& c, Renderer& r) {
  if (!firing_) return;
  std::vector<glm::vec3> pts;
  glm::vec3 a = c.muzzle, e = beamEnd_;
  float d = glm::length(e - c.eye);
  glm::vec3 ctrl = c.eye + c.fwd * (d * 0.5f);
  for (int i = 0; i <= 24; i++) {
    float t = i / 24.0f;
    pts.push_back((1 - t) * (1 - t) * a + 2 * (1 - t) * t * ctrl + t * t * e);
  }
  r.beam(pts, glm::vec4(0.25f, 0.65f, 1.0f, 1.0f), 0.05f, true);
  r.beam(pts, glm::vec4(0.25f, 0.65f, 1.0f, 0.35f), 0.18f, true);
  r.light({e, glm::vec3(0.3f, 0.6f, 1.5f), 4.0f});
  if (held_ >= 0) c.world.highlights[held_] = glm::vec4(0.3f, 0.7f, 1.0f, 0.9f);
}

// ---------------------------------------------------------------------------
// Gravity gun
// ---------------------------------------------------------------------------
GravGun::GravGun() {
  glowColor_ = glm::vec3(1.0f, 0.6f, 0.2f);
  parts_ = {
      {{0, 0, 0}, {0.06f, 0.06f, 0.18f}, {0.85f, 0.45f, 0.1f}, MAT_METAL},
      {{0, 0, -0.16f}, {0.03f, 0.03f, 0.05f}, {1, 1, 1}, MAT_GLOW},
      {{0, 0.075f, -0.24f}, {0.012f, 0.02f, 0.09f}, {0.2f, 0.2f, 0.22f}, MAT_METAL},
      {{0.065f, -0.04f, -0.24f}, {0.02f, 0.012f, 0.09f}, {0.2f, 0.2f, 0.22f}, MAT_METAL},
      {{-0.065f, -0.04f, -0.24f}, {0.02f, 0.012f, 0.09f}, {0.2f, 0.2f, 0.22f}, MAT_METAL},
      {{0, -0.1f, 0.08f}, {0.03f, 0.07f, 0.035f}, {0.15f, 0.15f, 0.15f}, MAT_RUBBER},
  };
}

void GravGun::holster(GameCtx&) {
  held_ = -1;
  pulling_ = false;
}

void GravGun::update(GameCtx& c, const Input& in, float dt) {
  updateSway(in, dt);
  Prop* hp = c.world.prop(held_);
  if (held_ >= 0 && (!hp || hp->frozen)) held_ = -1, hp = nullptr;

  if (in.mousePressed[SDL_BUTTON_RIGHT]) {
    if (hp) {
      held_ = -1;
      hp = nullptr;
      c.audio.play(SND_DROP, 0.5f);
    } else {
      pulling_ = true;
    }
  }
  if (!in.mouseDown[SDL_BUTTON_RIGHT]) pulling_ = false;

  if (pulling_ && !hp) {
    RayHit h = c.trace(40.0f);
    Prop* p = h.hit ? c.world.propFromObject(h.object) : nullptr;
    if (p && !p->frozen && p->mass <= 300.0f) {
      glm::vec3 pp = p->pos();
      float d = glm::length(pp - c.eye);
      btVector3 cc;
      btScalar rad;
      p->col->getBoundingSphere(cc, rad);
      if (d < 3.0f + rad) {
        held_ = p->id;
        holdDist_ = 1.4f + (float)rad;
        rel_ = glm::inverse(yawQuat(c.player.yaw)) * toGlm(p->body->getWorldTransform().getRotation());
        pulling_ = false;
        c.audio.play(SND_GRAB, 0.6f, 0.8f);
      } else {
        glm::vec3 v = toGlm(p->body->getLinearVelocity());
        glm::vec3 want = glm::normalize(c.eye - pp) * 12.0f;
        p->body->setLinearVelocity(toBt(glm::mix(v, want, std::min(1.0f, dt * 6.0f))));
        p->body->activate(true);
      }
    }
  }

  if (in.mousePressed[SDL_BUTTON_LEFT]) {
    if (hp) {
      held_ = -1;
      float m = std::min(hp->mass, 250.0f);
      hp->body->setLinearVelocity(btVector3(0, 0, 0));
      hp->body->applyCentralImpulse(toBt(c.fwd * 28.0f * m));
      c.audio.play(SND_PUNT, 0.9f);
      kick_ = 1;
    } else {
      RayHit h = c.trace(8.0f);
      Prop* p = h.hit ? c.world.propFromObject(h.object) : nullptr;
      if (p && !p->frozen) {
        float m = std::min(p->mass, 250.0f);
        p->body->activate(true);
        p->body->applyImpulse(toBt(c.fwd * 14.0f * m), toBt(h.point - p->pos()));
        c.audio.play(SND_PUNT, 0.9f);
        kick_ = 1;
      } else {
        c.audio.play(SND_CLICK, 0.4f, 0.6f);
      }
    }
  }
}

void GravGun::preTick(GameCtx& c, float dt) {
  Prop* p = c.world.prop(held_);
  if (!p) return;
  glm::vec3 target = c.eye + c.fwd * holdDist_;
  driveBody(p->body, target, yawQuat(c.player.yaw) * rel_, dt, 0.4f, 60.0f, 30.0f);
}

void GravGun::render(GameCtx& c, Renderer& r) {
  if (Prop* p = c.world.prop(held_)) {
    glm::vec3 e = toGlm(p->renderXf().getOrigin());
    r.beam({c.muzzle, e}, glm::vec4(1.0f, 0.6f, 0.2f, 0.5f), 0.12f, true);
    r.light({e, glm::vec3(1.5f, 0.8f, 0.3f), 4.0f});
    c.world.highlights[held_] = glm::vec4(1.0f, 0.6f, 0.2f, 0.7f);
  }
}

// ---------------------------------------------------------------------------
// Tool gun
// ---------------------------------------------------------------------------
ToolGun::ToolGun() {
  glowColor_ = glm::vec3(0.55f, 0.9f, 1.0f);
  parts_ = {
      {{0, 0, 0}, {0.04f, 0.06f, 0.19f}, {0.62f, 0.63f, 0.66f}, MAT_METAL},
      {{-0.042f, 0.015f, 0.02f}, {0.003f, 0.035f, 0.07f}, {1, 1, 1}, MAT_GLOW},
      {{0, 0.02f, -0.24f}, {0.016f, 0.016f, 0.06f}, {0.3f, 0.3f, 0.32f}, MAT_METAL},
      {{0, 0.02f, -0.3f}, {0.01f, 0.01f, 0.006f}, {1, 1, 1}, MAT_GLOW},
      {{0, -0.1f, 0.07f}, {0.028f, 0.07f, 0.035f}, {0.15f, 0.15f, 0.16f}, MAT_RUBBER},
  };
  tools = makeTools();
}

void ToolGun::select(int i) {
  if (i < 0 || i >= (int)tools.size()) return;
  if (tool()) tool()->reset();
  current = i;
}

void ToolGun::holster(GameCtx&) {
  if (tool()) tool()->reset();
}

void ToolGun::update(GameCtx& c, const Input& in, float dt) {
  updateSway(in, dt);
  shotTimer_ = std::max(0.0f, shotTimer_ - dt);
  Tool* t = tool();
  if (!t) return;
  auto doTrace = [&]() {
    RayHit r = c.trace();
    ToolHit h;
    h.hit = r.hit;
    h.pos = r.point;
    h.normal = r.normal;
    h.prop = r.hit ? c.world.propFromObject(r.object) : nullptr;
    h.world = r.hit && r.userIndex == BODY_WORLD;
    return h;
  };
  auto fire = [&](const ToolHit& h) {
    shotTimer_ = 0.15f;
    shotStart_ = c.muzzle;
    shotEnd_ = h.hit ? h.pos : c.eye + c.fwd * 100.0f;
    c.audio.play(SND_ZAP, 0.6f);
    kick_ = 1;
  };
  if (in.mousePressed[SDL_BUTTON_LEFT]) {
    ToolHit h = doTrace();
    if (t->primary(c, h)) fire(h);
  }
  if (in.mousePressed[SDL_BUTTON_RIGHT]) {
    ToolHit h = doTrace();
    if (t->secondary(c, h)) fire(h);
  }
  if (in.pressed[SDL_SCANCODE_R]) {
    ToolHit h = doTrace();
    if (t->reload(c, h)) fire(h);
  }
}

void ToolGun::render(GameCtx& c, Renderer& r) {
  if (shotTimer_ > 0) {
    float a = shotTimer_ / 0.15f;
    r.beam({shotStart_, shotEnd_}, glm::vec4(0.6f, 0.85f, 1.0f, a), 0.04f, true);
    r.light({shotEnd_, glm::vec3(0.6f, 0.9f, 1.5f) * a, 3.0f});
  }
  if (Tool* t = tool()) {
    int h = t->highlight();
    if (h >= 0) c.world.highlights[h] = glm::vec4(0.3f, 1.0f, 0.4f, 0.8f);
  }
}
