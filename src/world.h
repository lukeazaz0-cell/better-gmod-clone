#pragma once
#include "assets.h"
#include "physics.h"
#include "renderer.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Audio;

enum class AttachKind { Thruster = 0, Hoverball = 1 };

// Non-physical gadgets bolted directly onto a prop (they push the prop itself).
struct Attachment {
  int id = 0;
  AttachKind kind = AttachKind::Thruster;
  glm::vec3 localPos{0};
  glm::vec3 localDir{0, 1, 0};  // thruster: force direction; hoverball: surface normal
  float power = 1500.0f;
  int key = 0, key2 = 0;  // SDL scancodes
  bool toggle = false, on = false;
  float hoverHeight = 0.0f, hoverSpeed = 3.0f;
  float supportMass = 0.0f;  // hoverball: share of the contraption's mass (recomputed every frame)
};

struct Prop {
  int id = 0;
  std::string type;  // catalog id
  ShapeDesc shape;
  int material = MAT_PLASTIC;
  glm::vec4 color{1};
  float mass = 10, friction = 0.7f, restitution = 0.1f;
  bool frozen = false;
  int group = 0;  // >0 for multi-body entities (ragdolls)

  btRigidBody* body = nullptr;
  btCollisionShape* col = nullptr;
  const Mesh* mesh = nullptr;

  Special special = Special::None;
  float lift = 0;          // balloon upward force (N)
  int key = 0;             // dynamite / lamp key
  float power = 0;         // dynamite strength
  float brightness = 12;   // lamp radius
  bool on = true, toggle = true;

  std::vector<Attachment> attachments;
  float flash = 0;  // visual highlight timer

  btTransform renderXf() const;
  glm::vec3 pos() const { return toGlm(body->getWorldTransform().getOrigin()); }
};

enum class JointType { Weld, NoCollide, BallSocket, Axis, Rope, Elastic, Winch, Wheel, Ragdoll };
const char* jointTypeName(JointType t);
JointType jointTypeFromName(const std::string& s);

struct Joint {
  int id = 0;
  JointType type = JointType::Weld;
  int a = -1, b = -1;  // prop ids; b == -1 means the world
  glm::vec3 la{0}, lb{0};                 // pivots, local to each body (lb is world-space when b == -1)
  glm::vec3 axisA{0, 1, 0}, axisB{0, 1, 0};  // hinge axes (local)
  float length = 1, strength = 400, damping = 20, width = 0.03f;
  glm::vec4 color{0.25f, 0.2f, 0.15f, 1};
  int key1 = 0, key2 = 0;
  float speed = 2, torque = 0, motorSign = 1;
  bool nocollide = false;
  int ragIndex = -1;
  btTypedConstraint* c = nullptr;
};

struct UndoEntry {
  std::string label;
  std::vector<int> props, joints;
  std::vector<std::pair<int, int>> attachments;  // (prop id, attachment id)
  bool empty() const { return props.empty() && joints.empty() && attachments.empty(); }
};

struct Brush {
  ShapeDesc shape;
  btTransform xf;
  int material = MAT_DEV;
  glm::vec4 color{1};
  btRigidBody* body = nullptr;
  const Mesh* mesh = nullptr;
};

struct Effect {
  glm::vec3 pos{0};
  float t = 0, dur = 0.7f, size = 4;
};

class World {
 public:
  World(Physics& p, Assets& a) : phys(p), assets(a) {}

  void buildMap();
  void shutdown();
  void clearProps();

  // --- props ---
  Prop* createProp(const std::string& type, const ShapeDesc& shape, int material, const glm::vec4& color,
                   float mass, const btTransform& xf, float friction = 0.7f, float restitution = 0.1f);
  Prop* spawn(const PropDef& def, const btTransform& xf, UndoEntry* undo);
  std::vector<int> spawnRagdoll(const btTransform& xf, UndoEntry* undo);
  void removeProp(int id);
  void removeEntity(int id);  // removes the whole group for ragdolls
  Prop* prop(int id);
  const Prop* prop(int id) const;
  Prop* propFromObject(const btCollisionObject* o);

  // --- joints ---
  Joint* addJoint(Joint j);
  void removeJoint(int id);
  int removeJointsOf(int propId, int typeFilter = -1);
  Joint* joint(int id);

  // --- state ---
  void setFrozen(Prop& p, bool frozen);
  void setMass(Prop& p, float mass);
  std::vector<int> contraption(int propId) const;
  int unfreezeContraption(int propId);
  void explode(const glm::vec3& pos, float radius, float power);

  Attachment* addAttachment(Prop& p, Attachment a);
  bool removeAttachment(int propId, int attId);

  // --- per frame ---
  void keyEvent(int scancode, bool down);
  void update(float dt, const bool* keys);
  void preTick(float dt);
  void render(Renderer& r, const Camera& cam);
  bool anyThrusterOn() const { return thrusting_; }

  // --- undo ---
  void pushUndo(const UndoEntry& e);
  std::string undo();  // returns label of what was undone ("" if nothing)
  size_t undoCount() const { return undo_.size(); }

  // --- save / load / duplicate ---
  std::string serialize(const std::vector<int>& ids, const btTransform& ref, bool includeWorldJoints) const;
  std::vector<int> deserialize(const std::string& data, const btTransform& ref, UndoEntry* undo);
  bool saveFile(const std::string& path, const glm::vec3& playerPos, float yaw, float pitch) const;
  bool loadFile(const std::string& path, glm::vec3* playerPos, float* yaw, float* pitch);

  std::map<int, std::unique_ptr<Prop>> props;
  std::map<int, Joint> joints;
  std::vector<Brush> brushes;
  std::vector<Effect> effects;
  std::map<int, glm::vec4> highlights;  // set by weapons each frame, cleared after render
  Audio* audio = nullptr;
  std::function<void(const glm::vec3&, float, float)> onExplosion;
  glm::vec3 spawnPoint{0, 0.2f, 14};
  float spawnYaw = 0;

 private:
  bool buildConstraint(Joint& j);
  void solveRope(Joint& j, float dt);
  void addBrush(ShapeKind kind, const glm::vec3& center, const glm::vec3& half, int mat, const glm::vec4& color,
                const glm::quat& rot = glm::quat(1, 0, 0, 0));

  Physics& phys;
  Assets& assets;
  std::vector<UndoEntry> undo_;
  int nextId_ = 1, nextGroup_ = 1, nextAttach_ = 1;
  bool thrusting_ = false;
};
