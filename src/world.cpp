#include "world.h"
#include "audio.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <set>
#include <sstream>

static const float PI = 3.14159265358979f;
static const float PI_2 = PI * 0.5f;
static const float PI_4 = PI * 0.25f;

btTransform Prop::renderXf() const {
  btTransform t;
  if (frozen || !body->getMotionState())
    t = body->getWorldTransform();
  else
    body->getMotionState()->getWorldTransform(t);
  return t;
}

static const char* kJointNames[] = {"weld", "nocollide", "ballsocket", "axis", "rope",
                                    "elastic", "winch", "wheel", "ragdoll"};
const char* jointTypeName(JointType t) { return kJointNames[(int)t]; }
JointType jointTypeFromName(const std::string& s) {
  for (int i = 0; i < 9; i++)
    if (s == kJointNames[i]) return (JointType)i;
  return JointType::Weld;
}

// ---------------------------------------------------------------------------
// Ragdoll layout (adapted from the classic Bullet ragdoll demo, metres)
// ---------------------------------------------------------------------------
namespace {
enum { R_PELVIS, R_SPINE, R_HEAD, R_LUL, R_LLL, R_RUL, R_RLL, R_LUA, R_LLA, R_RUA, R_RLA, R_COUNT };
struct RagPart {
  float radius, height;  // capsule radius and straight-section length
  glm::vec3 pos;
  float rotZ;
  float mass;
  int colorSlot;  // 0 skin, 1 shirt, 2 pants
};
const RagPart kRagParts[R_COUNT] = {
    {0.15f, 0.20f, {0, 1.0f, 0}, 0, 10, 2},           {0.15f, 0.28f, {0, 1.2f, 0}, 0, 12, 1},
    {0.10f, 0.05f, {0, 1.6f, 0}, 0, 5, 0},            {0.07f, 0.45f, {-0.18f, 0.65f, 0}, 0, 7, 2},
    {0.05f, 0.37f, {-0.18f, 0.2f, 0}, 0, 4, 2},       {0.07f, 0.45f, {0.18f, 0.65f, 0}, 0, 7, 2},
    {0.05f, 0.37f, {0.18f, 0.2f, 0}, 0, 4, 2},        {0.05f, 0.33f, {-0.35f, 1.45f, 0}, PI_2, 3, 1},
    {0.04f, 0.25f, {-0.7f, 1.45f, 0}, PI_2, 2, 0},    {0.05f, 0.33f, {0.35f, 1.45f, 0}, -PI_2, 3, 1},
    {0.04f, 0.25f, {0.7f, 1.45f, 0}, -PI_2, 2, 0},
};
struct RagJoint {
  int a, b;
  bool hinge;
  glm::vec3 oa, ea, ob, eb;  // frame origins and setEulerZYX arguments
  float l0, l1, l2;
};
const RagJoint kRagJoints[10] = {
    {R_PELVIS, R_SPINE, true, {0, 0.15f, 0}, {0, PI_2, 0}, {0, -0.15f, 0}, {0, PI_2, 0}, -PI_4, PI_2, 0},
    {R_SPINE, R_HEAD, false, {0, 0.30f, 0}, {0, 0, PI_2}, {0, -0.14f, 0}, {0, 0, PI_2}, PI_4, PI_4, PI_2},
    {R_PELVIS, R_LUL, false, {-0.18f, -0.10f, 0}, {0, 0, -PI_4 * 5}, {0, 0.225f, 0}, {0, 0, -PI_4 * 5}, PI_4, PI_4, 0},
    {R_LUL, R_LLL, true, {0, -0.225f, 0}, {0, PI_2, 0}, {0, 0.185f, 0}, {0, PI_2, 0}, 0, PI_2, 0},
    {R_PELVIS, R_RUL, false, {0.18f, -0.10f, 0}, {0, 0, PI_4}, {0, 0.225f, 0}, {0, 0, PI_4}, PI_4, PI_4, 0},
    {R_RUL, R_RLL, true, {0, -0.225f, 0}, {0, PI_2, 0}, {0, 0.185f, 0}, {0, PI_2, 0}, 0, PI_2, 0},
    {R_SPINE, R_LUA, false, {-0.2f, 0.15f, 0}, {0, 0, PI}, {0, -0.18f, 0}, {0, 0, PI_2}, PI_2, PI_2, 0},
    {R_LUA, R_LLA, true, {0, 0.18f, 0}, {0, PI_2, 0}, {0, -0.14f, 0}, {0, PI_2, 0}, 0, PI_2, 0},
    {R_SPINE, R_RUA, false, {0.2f, 0.15f, 0}, {0, 0, 0}, {0, -0.18f, 0}, {0, 0, PI_2}, PI_2, PI_2, 0},
    {R_RUA, R_RLA, true, {0, 0.18f, 0}, {0, PI_2, 0}, {0, -0.14f, 0}, {0, PI_2, 0}, 0, PI_2, 0},
};
btTransform ragFrame(const glm::vec3& o, const glm::vec3& e) {
  btTransform t;
  t.setIdentity();
  t.getBasis().setEulerZYX(e.x, e.y, e.z);
  t.setOrigin(toBt(o));
  return t;
}
}  // namespace

// ---------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------
void World::addBrush(ShapeKind kind, const glm::vec3& c, const glm::vec3& half, int mat, const glm::vec4& color,
                     const glm::quat& rot) {
  Brush b;
  b.shape.kind = kind;
  b.shape.size = half;
  b.xf = makeXf(c, rot);
  b.material = mat;
  b.color = color;
  b.mesh = assets.mesh(b.shape);
  btCollisionShape* s = assets.shape(b.shape);
  btRigidBody::btRigidBodyConstructionInfo ci(0, nullptr, s);
  ci.m_startWorldTransform = b.xf;
  ci.m_friction = 0.9f;
  ci.m_restitution = 0.1f;
  b.body = new btRigidBody(ci);
  b.body->setUserIndex(BODY_WORLD);
  phys.world->addRigidBody(b.body, COL_WORLD, COL_ALL);
  brushes.push_back(b);
}

void World::buildMap() {
  using K = ShapeKind;
  const glm::vec4 grass(0.4f, 0.6f, 0.25f, 1), gray(0.5f, 0.5f, 0.53f, 1), dark(0.42f, 0.42f, 0.45f, 1);
  const glm::vec4 orange(0.92f, 0.52f, 0.16f, 1), concrete(0.72f, 0.71f, 0.68f, 1), white(0.85f, 0.85f, 0.85f, 1);

  // Ground and the central construction slab
  addBrush(K::Box, {0, -1, 0}, {320, 1, 320}, MAT_GRASS, grass);
  addBrush(K::Box, {0, -0.2f, 0}, {40, 0.25f, 40}, MAT_DEV, gray);

  // The building
  const float fy = 0.05f;
  addBrush(K::Box, {0, fy + 3, -31}, {8.2f, 3, 0.2f}, MAT_DEV, gray);
  addBrush(K::Box, {-8, fy + 3, -25}, {0.2f, 3, 6}, MAT_DEV, gray);
  addBrush(K::Box, {8, fy + 3, -25}, {0.2f, 3, 6}, MAT_DEV, gray);
  addBrush(K::Box, {-5.25f, fy + 3, -19}, {2.95f, 3, 0.2f}, MAT_DEV, gray);
  addBrush(K::Box, {5.25f, fy + 3, -19}, {2.95f, 3, 0.2f}, MAT_DEV, gray);
  addBrush(K::Box, {0, fy + 4.75f, -19}, {2.3f, 1.25f, 0.2f}, MAT_DEV, gray);
  addBrush(K::Box, {0, fy + 6.2f, -25}, {8.4f, 0.2f, 6.4f}, MAT_DEV, dark);
  addBrush(K::Box, {0, fy + 0.02f, -25}, {7.8f, 0.03f, 5.8f}, MAT_DEV, orange);  // interior floor
  addBrush(K::Box, {-5.5f, fy + 0.6f, -29}, {1.5f, 0.6f, 1.0f}, MAT_DEV, orange);
  addBrush(K::Box, {5.5f, fy + 1.0f, -29}, {1.5f, 1.0f, 1.0f}, MAT_DEV, orange);
  // Ramp up to the roof
  addBrush(K::Wedge, {14.4f, fy + 3.2f, -21.5f}, {6, 3.2f, 2.2f}, MAT_CONCRETE, concrete);
  // Roof railing
  addBrush(K::Box, {0, fy + 6.9f, -31.2f}, {8.4f, 0.5f, 0.1f}, MAT_DEV, white);
  addBrush(K::Box, {-8.3f, fy + 6.9f, -25}, {0.1f, 0.5f, 6.4f}, MAT_DEV, white);

  // Tower with ramp
  addBrush(K::Box, {-25, fy + 2, 10}, {4, 2, 4}, MAT_CONCRETE, concrete);
  addBrush(K::Wedge, {-16, fy + 2, 10}, {5, 2, 1.8f}, MAT_CONCRETE, concrete);
  addBrush(K::Box, {-25, fy + 4.5f, 6.1f}, {4, 0.5f, 0.1f}, MAT_DEV, white);
  addBrush(K::Box, {-28.9f, fy + 4.5f, 10}, {0.1f, 0.5f, 4}, MAT_DEV, white);

  // Car jump
  addBrush(K::Wedge, {22, fy + 1.2f, 14}, {5, 1.2f, 3}, MAT_DEV, orange, glm::angleAxis(PI, glm::vec3(0, 1, 0)));

  // Pillars and blocks
  for (int i = 0; i < 4; i++) {
    float x = -30.0f + i * 4.0f;
    addBrush(K::Box, {x, fy + 1.5f + i * 0.75f, -12}, {0.6f, 1.5f + i * 0.75f, 0.6f}, MAT_DEV, gray);
  }
  addBrush(K::Cylinder, {28, fy + 4, -20}, {2.0f, 4.0f, 0}, MAT_CONCRETE, concrete);

  // A low wall to build against
  addBrush(K::Box, {0, fy + 0.5f, 32}, {12, 0.5f, 0.25f}, MAT_BRICK, glm::vec4(0.7f, 0.35f, 0.25f, 1));

  // Far boundary walls keep stuff from flying into the void
  const float B = 160, H = 12;
  addBrush(K::Box, {0, H, -B}, {B, H, 1}, MAT_DEV, gray);
  addBrush(K::Box, {0, H, B}, {B, H, 1}, MAT_DEV, gray);
  addBrush(K::Box, {-B, H, 0}, {1, H, B}, MAT_DEV, gray);
  addBrush(K::Box, {B, H, 0}, {1, H, B}, MAT_DEV, gray);

  spawnPoint = glm::vec3(0, 0.2f, 14);
  spawnYaw = 0;
}

void World::shutdown() {
  clearProps();
  for (auto& b : brushes) {
    phys.world->removeRigidBody(b.body);
    delete b.body;
  }
  brushes.clear();
}

void World::clearProps() {
  while (!joints.empty()) removeJoint(joints.begin()->first);
  while (!props.empty()) removeProp(props.begin()->first);
  undo_.clear();
  effects.clear();
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------
Prop* World::prop(int id) {
  auto it = props.find(id);
  return it == props.end() ? nullptr : it->second.get();
}
const Prop* World::prop(int id) const {
  auto it = props.find(id);
  return it == props.end() ? nullptr : it->second.get();
}
Prop* World::propFromObject(const btCollisionObject* o) {
  if (!o) return nullptr;
  int id = o->getUserIndex();
  return id > 0 ? prop(id) : nullptr;
}

Prop* World::createProp(const std::string& type, const ShapeDesc& shape, int material, const glm::vec4& color,
                        float mass, const btTransform& xf, float friction, float restitution) {
  auto p = std::make_unique<Prop>();
  p->id = nextId_++;
  p->type = type;
  p->shape = shape;
  p->material = material;
  p->color = color;
  p->mass = std::max(0.05f, mass);
  p->friction = friction;
  p->restitution = restitution;
  p->col = assets.shape(shape);
  p->mesh = assets.mesh(shape);
  btVector3 inertia(0, 0, 0);
  p->col->calculateLocalInertia(p->mass, inertia);
  auto* ms = new btDefaultMotionState(xf);
  btRigidBody::btRigidBodyConstructionInfo ci(p->mass, ms, p->col, inertia);
  ci.m_friction = friction;
  ci.m_restitution = restitution;
  ci.m_linearDamping = 0.04f;
  ci.m_angularDamping = 0.12f;
  if (shape.kind == ShapeKind::Sphere) ci.m_rollingFriction = 0.002f;
  p->body = new btRigidBody(ci);
  p->body->setUserIndex(p->id);
  btVector3 c;
  btScalar r;
  p->col->getBoundingSphere(c, r);
  p->body->setCcdMotionThreshold(r * 0.5f);
  p->body->setCcdSweptSphereRadius(r * 0.3f);
  phys.world->addRigidBody(p->body, COL_PROP, COL_ALL);
  Prop* raw = p.get();
  props[raw->id] = std::move(p);
  return raw;
}

Prop* World::spawn(const PropDef& d, const btTransform& xf, UndoEntry* undo) {
  if (d.special == Special::Ragdoll) {
    auto ids = spawnRagdoll(xf, undo);
    return ids.empty() ? nullptr : prop(ids[0]);
  }
  Prop* p = createProp(d.id, d.shape, d.material, d.color, d.mass, xf, d.friction, d.restitution);
  p->special = d.special;
  if (d.special == Special::Balloon) {
    p->lift = 150.0f;
    p->body->setDamping(0.6f, 0.6f);
  } else if (d.special == Special::Dynamite) {
    p->key = SDL_SCANCODE_B;
    p->power = 25.0f;
  } else if (d.special == Special::Lamp) {
    p->key = SDL_SCANCODE_L;
    p->brightness = 14.0f;
  }
  if (undo) undo->props.push_back(p->id);
  return p;
}

std::vector<int> World::spawnRagdoll(const btTransform& xf, UndoEntry* undo) {
  static const glm::vec4 slots[3] = {{0.9f, 0.72f, 0.58f, 1}, {0.25f, 0.42f, 0.72f, 1}, {0.24f, 0.24f, 0.3f, 1}};
  int group = nextGroup_++;
  std::vector<int> ids;
  for (int i = 0; i < R_COUNT; i++) {
    const RagPart& rp = kRagParts[i];
    ShapeDesc sd;
    sd.kind = ShapeKind::Capsule;
    sd.size = glm::vec3(rp.radius, rp.height * 0.5f, 0);
    btTransform local;
    local.setIdentity();
    local.getBasis().setEulerZYX(0, 0, rp.rotZ);
    local.setOrigin(toBt(rp.pos + glm::vec3(0, 0.05f, 0)));
    Prop* p = createProp("ragdoll", sd, MAT_PLASTIC, slots[rp.colorSlot], rp.mass, xf * local, 0.8f, 0.0f);
    p->group = group;
    p->body->setDamping(0.05f, 0.5f);
    p->body->setSleepingThresholds(1.6f, 2.5f);
    ids.push_back(p->id);
    if (undo) undo->props.push_back(p->id);
  }
  for (int i = 0; i < 10; i++) {
    Joint j;
    j.type = JointType::Ragdoll;
    j.a = ids[kRagJoints[i].a];
    j.b = ids[kRagJoints[i].b];
    j.ragIndex = i;
    Joint* nj = addJoint(j);
    if (undo && nj) undo->joints.push_back(nj->id);
  }
  return ids;
}

void World::removeProp(int id) {
  auto it = props.find(id);
  if (it == props.end()) return;
  removeJointsOf(id);
  Prop& p = *it->second;
  phys.world->removeRigidBody(p.body);
  delete p.body->getMotionState();
  delete p.body;
  props.erase(it);
}

void World::removeEntity(int id) {
  Prop* p = prop(id);
  if (!p) return;
  if (p->group == 0) {
    removeProp(id);
    return;
  }
  int g = p->group;
  std::vector<int> ids;
  for (auto& kv : props)
    if (kv.second->group == g) ids.push_back(kv.first);
  for (int i : ids) removeProp(i);
}

// ---------------------------------------------------------------------------
// Joints
// ---------------------------------------------------------------------------
Joint* World::joint(int id) {
  auto it = joints.find(id);
  return it == joints.end() ? nullptr : &it->second;
}

bool World::buildConstraint(Joint& j) {
  Prop* A = prop(j.a);
  if (!A) return false;
  Prop* B = j.b >= 0 ? prop(j.b) : nullptr;
  if (j.b >= 0 && !B) return false;
  btRigidBody& ra = *A->body;
  btRigidBody& rb = B ? *B->body : btTypedConstraint::getFixedBody();
  bool disableCollide = j.nocollide;
  switch (j.type) {
    case JointType::Weld: {
      btTransform fa;
      fa.setIdentity();
      btTransform fb = rb.getWorldTransform().inverse() * ra.getWorldTransform();
      j.c = new btFixedConstraint(ra, rb, fa, fb);
      break;
    }
    case JointType::NoCollide: {
      if (!B) return false;
      btTransform fa, fb;
      fa.setIdentity();
      fb = rb.getWorldTransform().inverse() * ra.getWorldTransform();
      auto* g = new btGeneric6DofConstraint(ra, rb, fa, fb, true);
      g->setLinearLowerLimit(btVector3(1, 1, 1));
      g->setLinearUpperLimit(btVector3(-1, -1, -1));
      g->setAngularLowerLimit(btVector3(1, 1, 1));
      g->setAngularUpperLimit(btVector3(-1, -1, -1));
      j.c = g;
      disableCollide = true;
      break;
    }
    case JointType::BallSocket:
      j.c = B ? new btPoint2PointConstraint(ra, rb, toBt(j.la), toBt(j.lb)) : new btPoint2PointConstraint(ra, toBt(j.la));
      break;
    case JointType::Axis:
    case JointType::Wheel: {
      btHingeConstraint* h = B ? new btHingeConstraint(ra, rb, toBt(j.la), toBt(j.lb), toBt(j.axisA), toBt(j.axisB))
                               : new btHingeConstraint(ra, toBt(j.la), toBt(j.axisA));
      if (j.type == JointType::Axis && j.torque > 0) h->enableAngularMotor(true, 0, j.torque * phys.fixedDt);
      j.c = h;
      if (j.type == JointType::Wheel) disableCollide = true;
      break;
    }
    case JointType::Ragdoll: {
      if (!B || j.ragIndex < 0 || j.ragIndex >= 10) return false;
      const RagJoint& rj = kRagJoints[j.ragIndex];
      btTransform fa = ragFrame(rj.oa, rj.ea), fb = ragFrame(rj.ob, rj.eb);
      if (rj.hinge) {
        auto* h = new btHingeConstraint(ra, rb, fa, fb);
        h->setLimit(rj.l0, rj.l1);
        j.c = h;
      } else {
        auto* c = new btConeTwistConstraint(ra, rb, fa, fb);
        c->setLimit(rj.l0, rj.l1, rj.l2);
        j.c = c;
      }
      disableCollide = true;
      break;
    }
    case JointType::Rope:
    case JointType::Elastic:
    case JointType::Winch:
      j.c = nullptr;  // solved by World::solveRope
      return true;
  }
  if (j.c) phys.world->addConstraint(j.c, disableCollide);
  ra.activate(true);
  if (B) rb.activate(true);
  return j.c != nullptr;
}

Joint* World::addJoint(Joint j) {
  if (j.a == j.b) return nullptr;
  j.id = nextId_++;
  if (!buildConstraint(j)) return nullptr;
  auto res = joints.emplace(j.id, j);
  return &res.first->second;
}

void World::removeJoint(int id) {
  auto it = joints.find(id);
  if (it == joints.end()) return;
  Joint& j = it->second;
  if (j.c) {
    phys.world->removeConstraint(j.c);
    delete j.c;
  }
  if (Prop* a = prop(j.a)) a->body->activate(true);
  if (Prop* b = prop(j.b)) b->body->activate(true);
  joints.erase(it);
}

int World::removeJointsOf(int propId, int typeFilter) {
  std::vector<int> ids;
  for (auto& kv : joints) {
    const Joint& j = kv.second;
    if ((j.a == propId || j.b == propId) && (typeFilter < 0 || (int)j.type == typeFilter)) ids.push_back(kv.first);
  }
  for (int id : ids) removeJoint(id);
  return (int)ids.size();
}

// ---------------------------------------------------------------------------
// State changes
// ---------------------------------------------------------------------------
void World::setFrozen(Prop& p, bool f) {
  if (p.frozen == f) return;
  btRigidBody* b = p.body;
  if (f) {
    // Sync the render transform so a frozen prop doesn't snap.
    if (b->getMotionState()) b->getMotionState()->setWorldTransform(b->getWorldTransform());
  }
  p.frozen = f;
  phys.world->removeRigidBody(b);
  if (f) {
    b->setMassProps(0, btVector3(0, 0, 0));
    b->setLinearVelocity(btVector3(0, 0, 0));
    b->setAngularVelocity(btVector3(0, 0, 0));
  } else {
    btVector3 inertia;
    p.col->calculateLocalInertia(p.mass, inertia);
    b->setMassProps(p.mass, inertia);
  }
  b->updateInertiaTensor();
  phys.world->addRigidBody(b, COL_PROP, COL_ALL);
  b->forceActivationState(ACTIVE_TAG);
  b->setDeactivationTime(0);
  // wake neighbours so they notice
  for (auto& kv : joints) {
    if (kv.second.a == p.id || kv.second.b == p.id) {
      if (Prop* o = prop(kv.second.a == p.id ? kv.second.b : kv.second.a)) o->body->activate(true);
    }
  }
}

void World::setMass(Prop& p, float m) {
  p.mass = std::max(0.05f, m);
  if (p.frozen) return;
  phys.world->removeRigidBody(p.body);
  btVector3 inertia;
  p.col->calculateLocalInertia(p.mass, inertia);
  p.body->setMassProps(p.mass, inertia);
  p.body->updateInertiaTensor();
  phys.world->addRigidBody(p.body, COL_PROP, COL_ALL);
  p.body->activate(true);
}

std::vector<int> World::contraption(int propId) const {
  std::vector<int> out;
  if (!prop(propId)) return out;
  std::multimap<int, int> adj;
  for (auto& kv : joints) {
    if (kv.second.a >= 0 && kv.second.b >= 0) {
      adj.emplace(kv.second.a, kv.second.b);
      adj.emplace(kv.second.b, kv.second.a);
    }
  }
  std::set<int> seen{propId};
  std::deque<int> q{propId};
  while (!q.empty()) {
    int c = q.front();
    q.pop_front();
    out.push_back(c);
    auto range = adj.equal_range(c);
    for (auto it = range.first; it != range.second; ++it)
      if (seen.insert(it->second).second) q.push_back(it->second);
  }
  return out;
}

int World::unfreezeContraption(int propId) {
  int n = 0;
  for (int id : contraption(propId)) {
    Prop* p = prop(id);
    if (p && p->frozen) {
      setFrozen(*p, false);
      p->flash = 0.5f;
      n++;
    }
  }
  return n;
}

void World::explode(const glm::vec3& pos, float radius, float power) {
  for (auto& kv : props) {
    Prop& p = *kv.second;
    if (p.frozen) continue;
    glm::vec3 d = p.pos() - pos;
    float dist = glm::length(d);
    if (dist > radius) continue;
    float fall = 1.0f - dist / radius;
    glm::vec3 dir = dist > 0.01f ? d / dist : glm::vec3(0, 1, 0);
    dir = glm::normalize(dir + glm::vec3(0, 0.35f, 0));
    float m = std::min(p.mass, 150.0f);
    p.body->activate(true);
    p.body->applyCentralImpulse(toBt(dir * power * fall * m));
    p.body->applyTorqueImpulse(toBt(glm::cross(dir, glm::vec3(0.3f, 1, 0.2f)) * power * fall * m * 0.05f));
  }
  Effect e;
  e.pos = pos;
  e.size = radius * 0.45f;
  effects.push_back(e);
  if (audio) audio->playAt(SND_EXPLOSION, pos, 1.6f, 1.0f);
  if (onExplosion) onExplosion(pos, radius, power);
}

Attachment* World::addAttachment(Prop& p, Attachment a) {
  a.id = nextAttach_++;
  p.attachments.push_back(a);
  return &p.attachments.back();
}

bool World::removeAttachment(int propId, int attId) {
  Prop* p = prop(propId);
  if (!p) return false;
  auto& v = p->attachments;
  auto it = std::find_if(v.begin(), v.end(), [&](const Attachment& a) { return a.id == attId; });
  if (it == v.end()) return false;
  v.erase(it);
  return true;
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------
void World::keyEvent(int sc, bool down) {
  if (!down) return;
  std::vector<int> boom;
  for (auto& kv : props) {
    Prop& p = *kv.second;
    for (auto& a : p.attachments)
      if (a.kind == AttachKind::Thruster && a.toggle && a.key == sc) a.on = !a.on;
    if (p.special == Special::Dynamite && p.key == sc) boom.push_back(p.id);
    if (p.special == Special::Lamp && p.key == sc) p.on = !p.on;
  }
  for (int id : boom) {
    Prop* p = prop(id);
    if (!p) continue;
    glm::vec3 pos = p->pos();
    float power = p->power;
    removeProp(id);
    explode(pos, 4.0f + power * 0.25f, power);
  }
}

void World::update(float dt, const bool* keys) {
  thrusting_ = false;
  // Count hoverballs per contraption to share the load.
  std::map<int, std::pair<float, int>> hoverInfo;  // root prop id -> (mass, count)
  std::map<int, int> rootOf;
  for (auto& kv : props) {
    Prop& p = *kv.second;
    bool hasHover = false;
    for (auto& a : p.attachments) {
      if (a.kind == AttachKind::Thruster) {
        if (!a.toggle) a.on = keys[a.key];
        if (a.on) thrusting_ = true;
      } else if (a.kind == AttachKind::Hoverball) {
        hasHover = true;
        if (keys[a.key]) a.hoverHeight += a.hoverSpeed * dt;
        if (keys[a.key2]) a.hoverHeight -= a.hoverSpeed * dt;
      }
    }
    if (hasHover && !rootOf.count(p.id)) {
      auto group = contraption(p.id);
      float mass = 0;
      int count = 0;
      for (int id : group) {
        rootOf[id] = p.id;
        if (Prop* q = prop(id)) {
          mass += q->frozen ? 0 : q->mass;
          for (auto& a : q->attachments) count += a.kind == AttachKind::Hoverball;
        }
      }
      hoverInfo[p.id] = {mass, std::max(1, count)};
    }
    if (p.flash > 0) p.flash -= dt;
  }
  for (auto& kv : props) {
    for (auto& a : kv.second->attachments) {
      if (a.kind != AttachKind::Hoverball) continue;
      auto it = rootOf.find(kv.first);
      if (it == rootOf.end()) continue;
      auto& hi = hoverInfo[it->second];
      a.supportMass = hi.first / hi.second;
    }
  }

  for (auto& kv : joints) {
    Joint& j = kv.second;
    if (j.type == JointType::Wheel && j.c) {
      auto* h = static_cast<btHingeConstraint*>(j.c);
      bool f = keys[j.key1], b = keys[j.key2];
      if (f != b) {
        h->enableAngularMotor(true, (f ? 1.0f : -1.0f) * j.speed * j.motorSign, j.torque * phys.fixedDt);
        if (Prop* a = prop(j.a)) a->body->activate(true);
        if (Prop* w = prop(j.b)) w->body->activate(true);
      } else {
        h->enableAngularMotor(false, 0, 0);
      }
    } else if (j.type == JointType::Winch) {
      float old = j.length;
      if (keys[j.key1]) j.length = std::max(0.2f, j.length - j.speed * dt);
      if (keys[j.key2]) j.length = std::min(200.0f, j.length + j.speed * dt);
      if (old != j.length) {
        if (Prop* a = prop(j.a)) a->body->activate(true);
        if (Prop* b = prop(j.b)) b->body->activate(true);
      }
    }
  }

  for (auto& e : effects) e.t += dt;
  effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return e.t >= e.dur; }),
                effects.end());

  // Props that fell out of the world
  std::vector<int> lost;
  for (auto& kv : props)
    if (kv.second->pos().y < -150.0f) lost.push_back(kv.first);
  for (int id : lost) removeProp(id);
}

void World::solveRope(Joint& j, float dt) {
  Prop* A = prop(j.a);
  Prop* B = j.b >= 0 ? prop(j.b) : nullptr;
  if (!A || (j.b >= 0 && !B)) return;
  btRigidBody* ra = A->body;
  btRigidBody* rb = B ? B->body : nullptr;
  btVector3 pa = ra->getWorldTransform() * toBt(j.la);
  btVector3 pb = rb ? rb->getWorldTransform() * toBt(j.lb) : toBt(j.lb);
  btVector3 d = pb - pa;
  float len = (float)d.length();
  if (len < 1e-4f) return;
  btVector3 n = d / len;
  float C = len - j.length;
  if (C <= 0) return;  // slack
  btVector3 relA = pa - ra->getCenterOfMassPosition();
  btVector3 relB = rb ? pb - rb->getCenterOfMassPosition() : btVector3(0, 0, 0);
  btVector3 va = ra->getVelocityInLocalPoint(relA);
  btVector3 vb = rb ? rb->getVelocityInLocalPoint(relB) : btVector3(0, 0, 0);
  float vrel = (float)(vb - va).dot(n);  // > 0 means separating

  if (j.type == JointType::Elastic) {
    float f = j.strength * C + j.damping * vrel;
    if (f <= 0) return;
    btVector3 imp = n * (f * dt);
    ra->applyImpulse(imp, relA);
    if (rb) rb->applyImpulse(-imp, relB);
    ra->activate();
    if (rb) rb->activate();
    return;
  }
  float k = (float)(ra->getInvMass() + (ra->getInvInertiaTensorWorld() * relA.cross(n)).cross(relA).dot(n));
  if (rb) k += (float)(rb->getInvMass() + (rb->getInvInertiaTensorWorld() * relB.cross(n)).cross(relB).dot(n));
  if (k < 1e-8f) return;
  float bias = std::min(0.25f * C / dt, 15.0f);
  float lambda = (vrel + bias) / k;
  if (lambda <= 0) return;
  btVector3 P = n * lambda;
  ra->applyImpulse(P, relA);
  if (rb) rb->applyImpulse(-P, relB);
  ra->activate();
  if (rb) rb->activate();
}

void World::preTick(float dt) {
  float g = phys.gravity();
  for (auto& kv : props) {
    Prop& p = *kv.second;
    if (p.frozen) continue;
    btRigidBody* b = p.body;
    if (p.special == Special::Balloon && p.lift != 0) {
      b->applyCentralImpulse(btVector3(0, p.lift * dt, 0));
      b->activate();
    }
    if (p.attachments.empty()) continue;
    const btMatrix3x3& basis = b->getWorldTransform().getBasis();
    for (auto& a : p.attachments) {
      btVector3 rel = basis * toBt(a.localPos);
      if (a.kind == AttachKind::Thruster && a.on) {
        btVector3 dir = basis * toBt(a.localDir);
        b->applyImpulse(dir * (a.power * dt), rel);
        b->activate();
      } else if (a.kind == AttachKind::Hoverball) {
        btVector3 wp = b->getWorldTransform().getOrigin() + rel;
        btVector3 v = b->getVelocityInLocalPoint(rel);
        float err = a.hoverHeight - (float)wp.y();
        float accel = g + err * 14.0f - (float)v.y() * 7.0f;
        accel = std::max(-g * 2.0f, std::min(accel, g * 4.0f));
        float m = a.supportMass;
        btVector3 imp(-(float)v.x() * 0.8f * m, accel * m, -(float)v.z() * 0.8f * m);
        float maxF = a.power * 10.0f;
        if (imp.length() > maxF) imp = imp.normalized() * maxF;
        b->applyImpulse(imp * dt, rel);
        b->activate();
      }
    }
  }
  for (auto& kv : joints) {
    Joint& j = kv.second;
    if (j.type == JointType::Rope || j.type == JointType::Elastic || j.type == JointType::Winch) solveRope(j, dt);
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
static std::vector<glm::vec3> ropePoints(const glm::vec3& a, const glm::vec3& b, float length) {
  std::vector<glm::vec3> pts;
  float d = glm::length(b - a);
  float sag = length > d ? std::sqrt(length * length - d * d) * 0.5f : 0.0f;
  const int n = 16;
  for (int i = 0; i < n; i++) {
    float t = (float)i / (n - 1);
    glm::vec3 p = glm::mix(a, b, t);
    p.y -= sag * 4.0f * t * (1.0f - t);
    pts.push_back(p);
  }
  return pts;
}

void World::render(Renderer& r, const Camera& cam) {
  for (const Brush& b : brushes) {
    DrawCmd c;
    c.mesh = b.mesh;
    c.model = toGlm(b.xf);
    c.color = b.color;
    c.material = b.material;
    c.worldTex = true;
    r.draw(c);
  }

  ShapeDesc thrusterShape{ShapeKind::Cylinder, {0.1f, 0.12f, 0}, ""};
  ShapeDesc flameShape{ShapeKind::Cone, {0.09f, 0.5f, 0}, ""};
  ShapeDesc hoverShape{ShapeKind::Sphere, {0.14f, 0, 0}, ""};
  ShapeDesc unitSphere{ShapeKind::Sphere, {1.0f, 0, 0}, ""};
  const Mesh* thrMesh = assets.mesh(thrusterShape);
  const Mesh* flameMesh = assets.mesh(flameShape);
  const Mesh* hovMesh = assets.mesh(hoverShape);

  for (auto& kv : props) {
    Prop& p = *kv.second;
    glm::mat4 m = toGlm(p.renderXf());
    DrawCmd c;
    c.mesh = p.mesh;
    c.model = m;
    c.color = p.color;
    c.material = p.material;
    auto hl = highlights.find(p.id);
    if (hl != highlights.end())
      c.highlight = hl->second;
    else if (p.flash > 0)
      c.highlight = glm::vec4(0.4f, 0.7f, 1.0f, std::min(1.0f, p.flash * 2.0f));
    if (p.special == Special::Lamp) {
      c.material = p.on ? MAT_GLOW : MAT_PLASTIC;
      if (p.on) r.light({glm::vec3(m[3]), glm::vec3(p.color) * 2.5f, p.brightness});
    }
    r.draw(c);

    for (auto& a : p.attachments) {
      glm::vec3 n = a.kind == AttachKind::Thruster ? -a.localDir : a.localDir;
      glm::quat q = rotationBetween(glm::vec3(0, 1, 0), glm::normalize(n));
      if (a.kind == AttachKind::Thruster) {
        glm::mat4 lm = glm::translate(glm::mat4(1), a.localPos + n * 0.12f) * glm::mat4_cast(q);
        DrawCmd t;
        t.mesh = thrMesh;
        t.model = m * lm;
        t.color = glm::vec4(0.35f, 0.35f, 0.38f, 1);
        t.material = MAT_METAL;
        r.draw(t);
        if (a.on) {
          // flame cone points away from the surface (apex outward)
          glm::mat4 fm = glm::translate(glm::mat4(1), a.localPos + n * 0.45f) * glm::mat4_cast(q);
          DrawCmd f;
          f.mesh = flameMesh;
          f.model = m * fm;
          f.color = glm::vec4(1.0f, 0.55f, 0.15f, 0.9f);
          f.material = MAT_GLOW;
          f.emissive = 1.0f;
          r.drawGlow(f);
          r.light({glm::vec3(m * glm::vec4(a.localPos + n * 0.6f, 1)), glm::vec3(2.0f, 1.0f, 0.3f), 5.0f});
        }
      } else {
        glm::mat4 lm = glm::translate(glm::mat4(1), a.localPos + n * 0.1f);
        DrawCmd h;
        h.mesh = hovMesh;
        h.model = m * lm;
        h.color = glm::vec4(0.4f, 0.8f, 1.0f, 1);
        h.material = MAT_GLOW;
        r.draw(h);
      }
    }
  }

  for (auto& kv : joints) {
    Joint& j = kv.second;
    if (j.type != JointType::Rope && j.type != JointType::Elastic && j.type != JointType::Winch) continue;
    Prop* A = prop(j.a);
    Prop* B = j.b >= 0 ? prop(j.b) : nullptr;
    if (!A || (j.b >= 0 && !B)) continue;
    glm::vec3 pa = glm::vec3(toGlm(A->renderXf()) * glm::vec4(j.la, 1));
    glm::vec3 pb = B ? glm::vec3(toGlm(B->renderXf()) * glm::vec4(j.lb, 1)) : j.lb;
    if (j.type == JointType::Elastic)
      r.beam({pa, pb}, j.color, j.width, false);
    else
      r.beam(ropePoints(pa, pb, j.length), j.color, j.width, false);
  }

  const Mesh* sph = assets.mesh(unitSphere);
  for (const Effect& e : effects) {
    float k = e.t / e.dur;
    float s = e.size * (0.3f + 0.7f * std::sqrt(k));
    DrawCmd c;
    c.mesh = sph;
    c.model = glm::scale(glm::translate(glm::mat4(1), e.pos), glm::vec3(s));
    c.color = glm::vec4(1.0f, 0.35f + 0.3f * (1 - k), 0.08f, (1.0f - k) * 0.75f);
    c.material = MAT_GLOW;
    c.emissive = 1.0f;
    r.drawGlow(c);
    r.light({e.pos + glm::vec3(0, 1, 0), glm::vec3(4.0f, 2.2f, 0.8f) * (1.0f - k) * 3.0f, e.size * 6.0f});
  }
  highlights.clear();
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------
void World::pushUndo(const UndoEntry& e) {
  if (e.empty()) return;
  undo_.push_back(e);
  if (undo_.size() > 256) undo_.erase(undo_.begin());
}

std::string World::undo() {
  while (!undo_.empty()) {
    UndoEntry e = undo_.back();
    undo_.pop_back();
    bool did = false;
    for (int id : e.joints)
      if (joints.count(id)) {
        removeJoint(id);
        did = true;
      }
    for (auto& pa : e.attachments) did |= removeAttachment(pa.first, pa.second);
    for (int id : e.props)
      if (props.count(id)) {
        removeProp(id);
        did = true;
      }
    if (did) return e.label;
  }
  return "";
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------
static std::ostream& operator<<(std::ostream& o, const glm::vec3& v) { return o << v.x << ' ' << v.y << ' ' << v.z; }
static std::ostream& operator<<(std::ostream& o, const glm::vec4& v) {
  return o << v.x << ' ' << v.y << ' ' << v.z << ' ' << v.w;
}
static std::istream& operator>>(std::istream& i, glm::vec3& v) { return i >> v.x >> v.y >> v.z; }
static std::istream& operator>>(std::istream& i, glm::vec4& v) { return i >> v.x >> v.y >> v.z >> v.w; }

std::string World::serialize(const std::vector<int>& ids, const btTransform& ref, bool includeWorldJoints) const {
  std::ostringstream o;
  o.precision(9);
  o << "GMODCLONE 1\n";
  std::map<int, int> idx;
  for (int id : ids)
    if (prop(id) && !idx.count(id)) {
      int k = (int)idx.size();
      idx[id] = k;
    }
  btTransform inv = ref.inverse();
  float refY = (float)ref.getOrigin().y();
  for (auto& kv : idx) {
    const Prop& p = *prop(kv.first);
    btTransform t = inv * p.body->getWorldTransform();
    btQuaternion q = t.getRotation();
    o << "P " << kv.second << ' ' << p.type << ' ' << shapeKindName(p.shape.kind) << ' ' << p.shape.size << ' '
      << (p.shape.compound.empty() ? "-" : p.shape.compound) << ' ' << p.material << ' ' << p.color << ' ' << p.mass
      << ' ' << p.friction << ' ' << p.restitution << ' ' << p.frozen << ' ' << p.group << ' '
      << toGlm(t.getOrigin()) << ' ' << q.x() << ' ' << q.y() << ' ' << q.z() << ' ' << q.w() << ' '
      << (int)p.special << ' ' << p.lift << ' ' << p.key << ' ' << p.power << ' ' << p.brightness << ' ' << p.on
      << ' ' << p.toggle << '\n';
    for (auto& a : p.attachments) {
      o << "A " << kv.second << ' ' << (int)a.kind << ' ' << a.localPos << ' ' << a.localDir << ' ' << a.power << ' '
        << a.key << ' ' << a.key2 << ' ' << a.toggle << ' ' << (a.hoverHeight - refY) << ' ' << a.hoverSpeed << '\n';
    }
  }
  for (auto& kv : joints) {
    const Joint& j = kv.second;
    auto ia = idx.find(j.a);
    if (ia == idx.end()) continue;
    int bi = -1;
    glm::vec3 lb = j.lb, axisB = j.axisB;
    if (j.b >= 0) {
      auto ib = idx.find(j.b);
      if (ib == idx.end()) continue;
      bi = ib->second;
    } else {
      if (!includeWorldJoints) continue;
      lb = toGlm(inv * toBt(j.lb));
      axisB = toGlm(inv.getBasis() * toBt(j.axisB));
    }
    o << "J " << jointTypeName(j.type) << ' ' << ia->second << ' ' << bi << ' ' << j.la << ' ' << lb << ' ' << j.axisA
      << ' ' << axisB << ' ' << j.length << ' ' << j.strength << ' ' << j.damping << ' ' << j.width << ' ' << j.color
      << ' ' << j.key1 << ' ' << j.key2 << ' ' << j.speed << ' ' << j.torque << ' ' << j.motorSign << ' '
      << j.nocollide << ' ' << j.ragIndex << '\n';
  }
  return o.str();
}

std::vector<int> World::deserialize(const std::string& data, const btTransform& ref, UndoEntry* undo) {
  std::istringstream in(data);
  std::string line;
  std::map<int, int> idmap, groupMap;
  std::vector<int> created;
  float refY = (float)ref.getOrigin().y();
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string tag;
    ls >> tag;
    if (tag == "P") {
      int idx, material, frozen, group, special, key, on, toggle;
      std::string type, kind, compound;
      glm::vec3 size, pos;
      glm::vec4 color;
      float mass, friction, restitution, qx, qy, qz, qw, lift, power, brightness;
      ls >> idx >> type >> kind >> size >> compound >> material >> color >> mass >> friction >> restitution >> frozen >>
          group >> pos >> qx >> qy >> qz >> qw >> special >> lift >> key >> power >> brightness >> on >> toggle;
      if (ls.fail()) continue;
      ShapeDesc sd;
      sd.kind = shapeKindFromName(kind);
      sd.size = size;
      sd.compound = compound == "-" ? "" : compound;
      if (sd.kind == ShapeKind::Compound && !compoundParts(sd.compound)) continue;
      btTransform t = ref * btTransform(btQuaternion(qx, qy, qz, qw), toBt(pos));
      Prop* p = createProp(type, sd, material, color, mass, t, friction, restitution);
      if (group != 0) {
        if (!groupMap.count(group)) groupMap[group] = nextGroup_++;
        p->group = groupMap[group];
      }
      p->special = (Special)special;
      p->lift = lift;
      p->key = key;
      p->power = power;
      p->brightness = brightness;
      p->on = on != 0;
      p->toggle = toggle != 0;
      if (p->special == Special::Balloon) p->body->setDamping(0.6f, 0.6f);
      if (p->group) {
        p->body->setDamping(0.05f, 0.5f);
        p->body->setSleepingThresholds(1.6f, 2.5f);
      }
      if (frozen) setFrozen(*p, true);
      idmap[idx] = p->id;
      created.push_back(p->id);
      if (undo) undo->props.push_back(p->id);
    } else if (tag == "A") {
      int idx, kind, key, key2, toggle;
      Attachment a;
      ls >> idx >> kind >> a.localPos >> a.localDir >> a.power >> key >> key2 >> toggle >> a.hoverHeight >> a.hoverSpeed;
      if (ls.fail() || !idmap.count(idx)) continue;
      a.kind = (AttachKind)kind;
      a.key = key;
      a.key2 = key2;
      a.toggle = toggle != 0;
      a.hoverHeight += refY;
      Prop* p = prop(idmap[idx]);
      Attachment* na = addAttachment(*p, a);
      if (undo) undo->attachments.push_back({p->id, na->id});
    } else if (tag == "J") {
      std::string type;
      int a, b, nocollide;
      Joint j;
      ls >> type >> a >> b >> j.la >> j.lb >> j.axisA >> j.axisB >> j.length >> j.strength >> j.damping >> j.width >>
          j.color >> j.key1 >> j.key2 >> j.speed >> j.torque >> j.motorSign >> nocollide >> j.ragIndex;
      if (ls.fail() || !idmap.count(a) || (b >= 0 && !idmap.count(b))) continue;
      j.type = jointTypeFromName(type);
      j.a = idmap[a];
      j.b = b >= 0 ? idmap[b] : -1;
      j.nocollide = nocollide != 0;
      if (b < 0) {
        j.lb = toGlm(ref * toBt(j.lb));
        j.axisB = toGlm(ref.getBasis() * toBt(j.axisB));
      }
      Joint* nj = addJoint(j);
      if (nj && undo) undo->joints.push_back(nj->id);
    }
  }
  return created;
}

bool World::saveFile(const std::string& path, const glm::vec3& playerPos, float yaw, float pitch) const {
  std::ofstream f(path);
  if (!f) return false;
  std::vector<int> ids;
  for (auto& kv : props) ids.push_back(kv.first);
  btTransform ident;
  ident.setIdentity();
  f << serialize(ids, ident, true);
  f << "PLAYER " << playerPos.x << ' ' << playerPos.y << ' ' << playerPos.z << ' ' << yaw << ' ' << pitch << '\n';
  return (bool)f;
}

bool World::loadFile(const std::string& path, glm::vec3* playerPos, float* yaw, float* pitch) {
  std::ifstream f(path);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  std::string data = ss.str();
  if (data.rfind("GMODCLONE", 0) != 0) return false;
  clearProps();
  btTransform ident;
  ident.setIdentity();
  deserialize(data, ident, nullptr);
  std::istringstream in(data);
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("PLAYER ", 0) == 0) {
      std::istringstream ls(line.substr(7));
      glm::vec3 p;
      float y, pt;
      if (ls >> p.x >> p.y >> p.z >> y >> pt) {
        if (playerPos) *playerPos = p;
        if (yaw) *yaw = y;
        if (pitch) *pitch = pt;
      }
    }
  }
  return true;
}
