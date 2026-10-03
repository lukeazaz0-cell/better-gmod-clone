// Tool gun modes.
#include <imgui.h>
#include <cmath>
#include <random>
#include <set>
#include <algorithm>
#include "weapons.h"

namespace {

const glm::vec4 kRed(1.0f, 0.35f, 0.3f, 1.0f);
const glm::vec4 kGreen(0.4f, 1.0f, 0.5f, 1.0f);

glm::vec3 toLocal(const Prop& p, const glm::vec3& w) { return toGlm(p.body->getWorldTransform().inverse() * toBt(w)); }
glm::vec3 toLocalDir(const Prop& p, const glm::vec3& d) {
  return toGlm(p.body->getWorldTransform().getBasis().transpose() * toBt(d));
}
glm::vec3 toWorld(const Prop& p, const glm::vec3& l) { return toGlm(p.body->getWorldTransform() * toBt(l)); }

const int kKeyChoices[] = {
    SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN,   SDL_SCANCODE_LEFT,   SDL_SCANCODE_RIGHT,  SDL_SCANCODE_KP_0,
    SDL_SCANCODE_KP_1,   SDL_SCANCODE_KP_2,   SDL_SCANCODE_KP_3,   SDL_SCANCODE_KP_4,   SDL_SCANCODE_KP_5,
    SDL_SCANCODE_KP_6,   SDL_SCANCODE_KP_7,   SDL_SCANCODE_KP_8,   SDL_SCANCODE_KP_9,   SDL_SCANCODE_KP_ENTER,
    SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_HOME,
    SDL_SCANCODE_END,    SDL_SCANCODE_INSERT, SDL_SCANCODE_DELETE, SDL_SCANCODE_B,      SDL_SCANCODE_F,
    SDL_SCANCODE_G,      SDL_SCANCODE_H,      SDL_SCANCODE_I,      SDL_SCANCODE_J,      SDL_SCANCODE_K,
    SDL_SCANCODE_L,      SDL_SCANCODE_M,      SDL_SCANCODE_N,      SDL_SCANCODE_O,      SDL_SCANCODE_P,
    SDL_SCANCODE_T,      SDL_SCANCODE_U,      SDL_SCANCODE_X,      SDL_SCANCODE_Y,      SDL_SCANCODE_RALT,
};

bool keyCombo(const char* label, int* sc) {
  bool changed = false;
  if (ImGui::BeginCombo(label, SDL_GetScancodeName((SDL_Scancode)*sc))) {
    for (int k : kKeyChoices) {
      bool sel = (k == *sc);
      if (ImGui::Selectable(SDL_GetScancodeName((SDL_Scancode)k), sel)) {
        *sc = k;
        changed = true;
      }
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

int removeJointsNotify(GameCtx& c, const ToolHit& h, JointType t, const char* what) {
  if (!h.prop) return 0;
  int n = c.world.removeJointsOf(h.prop->id, (int)t);
  if (n > 0) c.notify.push("Removed " + std::to_string(n) + " " + what);
  return n;
}

// Base for tools that connect two things with two clicks.
class PairTool : public Tool {
 public:
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.hit) return false;
    if (stage == 0) {
      if (!h.prop) {
        c.notify.push("Select a prop first", kRed);
        return false;
      }
      firstProp = h.prop->id;
      firstWorld = h.pos;
      firstNormal = h.normal;
      firstLocal = toLocal(*h.prop, h.pos);
      firstNormalLocal = toLocalDir(*h.prop, h.normal);
      stage = 1;
      return true;
    }
    Prop* A = c.world.prop(firstProp);
    if (!A) {
      reset();
      return false;
    }
    if (h.prop && h.prop->id == firstProp) {
      c.notify.push("Select a different object", kRed);
      return false;
    }
    if (!h.prop && !allowWorld()) {
      c.notify.push("Select a second prop", kRed);
      return false;
    }
    stage = 0;
    return finish(c, *A, h);
  }
  std::string help() const override {
    return stage == 0 ? std::string("Left: select first object   Reload: remove ") + plural() + " from object"
                      : std::string("Left: select second object") + (allowWorld() ? " (or the world)" : "");
  }
  bool reload(GameCtx& c, const ToolHit& h) override {
    reset();
    return removeJointsNotify(c, h, jointType(), plural()) > 0;
  }

 protected:
  virtual bool finish(GameCtx& c, Prop& A, const ToolHit& h) = 0;
  virtual bool allowWorld() const { return true; }
  virtual JointType jointType() const = 0;
  virtual const char* plural() const = 0;

  bool commit(GameCtx& c, Joint j, const char* label) {
    Joint* nj = c.world.addJoint(j);
    if (!nj) {
      c.notify.push("Could not create constraint", kRed);
      return false;
    }
    UndoEntry u;
    u.label = label;
    u.joints.push_back(nj->id);
    c.world.pushUndo(u);
    return true;
  }

  glm::vec3 firstLocal{0}, firstNormalLocal{0, 1, 0};
};

// ---------------------------------------------------------------------------
class WeldTool : public PairTool {
 public:
  const char* name() const override { return "Weld"; }
  const char* desc() const override { return "Rigidly attach two objects together (or an object to the world)."; }
  void settings() override { ImGui::Checkbox("No-collide welded objects", &nocollide_); }

 protected:
  JointType jointType() const override { return JointType::Weld; }
  const char* plural() const override { return "welds"; }
  bool finish(GameCtx& c, Prop& A, const ToolHit& h) override {
    Joint j;
    j.type = JointType::Weld;
    j.a = A.id;
    j.b = h.prop ? h.prop->id : -1;
    j.nocollide = nocollide_;
    return commit(c, j, "Weld");
  }
  bool nocollide_ = false;
};

class NoCollideTool : public PairTool {
 public:
  const char* name() const override { return "No Collide"; }
  const char* desc() const override { return "Make two objects pass through each other."; }

 protected:
  JointType jointType() const override { return JointType::NoCollide; }
  const char* plural() const override { return "no-collides"; }
  bool allowWorld() const override { return false; }
  bool finish(GameCtx& c, Prop& A, const ToolHit& h) override {
    Joint j;
    j.type = JointType::NoCollide;
    j.a = A.id;
    j.b = h.prop->id;
    return commit(c, j, "No Collide");
  }
};

class BallSocketTool : public PairTool {
 public:
  const char* name() const override { return "Ball Socket"; }
  const char* desc() const override { return "Pin two objects together at a point; they can rotate freely."; }
  void settings() override { ImGui::Checkbox("No-collide", &nocollide_); }

 protected:
  JointType jointType() const override { return JointType::BallSocket; }
  const char* plural() const override { return "ball sockets"; }
  bool finish(GameCtx& c, Prop& A, const ToolHit& h) override {
    glm::vec3 pivot = toWorld(A, firstLocal);
    Joint j;
    j.type = JointType::BallSocket;
    j.a = A.id;
    j.b = h.prop ? h.prop->id : -1;
    j.la = firstLocal;
    j.lb = h.prop ? toLocal(*h.prop, pivot) : pivot;
    j.nocollide = nocollide_;
    return commit(c, j, "Ball Socket");
  }
  bool nocollide_ = true;
};

class AxisTool : public PairTool {
 public:
  const char* name() const override { return "Axis"; }
  const char* desc() const override {
    return "Hinge two objects. The hinge axis is the surface normal where you first clicked.";
  }
  void settings() override {
    ImGui::SliderFloat("Friction", &friction_, 0, 500);
    ImGui::Checkbox("No-collide", &nocollide_);
  }

 protected:
  JointType jointType() const override { return JointType::Axis; }
  const char* plural() const override { return "axes"; }
  bool finish(GameCtx& c, Prop& A, const ToolHit& h) override {
    glm::vec3 pivot = toWorld(A, firstLocal);
    glm::vec3 axisW = toGlm(A.body->getWorldTransform().getBasis() * toBt(firstNormalLocal));
    Joint j;
    j.type = JointType::Axis;
    j.a = A.id;
    j.b = h.prop ? h.prop->id : -1;
    j.la = firstLocal;
    j.axisA = firstNormalLocal;
    j.lb = h.prop ? toLocal(*h.prop, pivot) : pivot;
    j.axisB = h.prop ? toLocalDir(*h.prop, axisW) : axisW;
    j.torque = friction_;
    j.nocollide = nocollide_;
    return commit(c, j, "Axis");
  }
  float friction_ = 0;
  bool nocollide_ = true;
};

class RopeTool : public PairTool {
 public:
  RopeTool(JointType t) : type_(t) {}
  const char* name() const override {
    return type_ == JointType::Rope ? "Rope" : type_ == JointType::Elastic ? "Elastic" : "Winch";
  }
  const char* desc() const override {
    if (type_ == JointType::Rope) return "Tie two points together with a rope.";
    if (type_ == JointType::Elastic) return "Connect two points with a stretchy spring.";
    return "A rope you can reel in and out with keys.";
  }
  void settings() override {
    if (type_ == JointType::Elastic) {
      ImGui::SliderFloat("Strength", &strength_, 10, 5000, "%.0f", ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Damping", &damping_, 0, 200);
      ImGui::SliderFloat("Rest length scale", &restScale_, 0.0f, 1.5f);
    } else {
      ImGui::SliderFloat("Extra length", &addLength_, 0, 10);
    }
    if (type_ == JointType::Winch) {
      keyCombo("Reel in", &keyIn_);
      keyCombo("Reel out", &keyOut_);
      ImGui::SliderFloat("Speed", &speed_, 0.2f, 10);
    }
    ImGui::SliderFloat("Width", &width_, 0.005f, 0.2f);
    ImGui::ColorEdit3("Colour", &color_.x);
  }

 protected:
  JointType jointType() const override { return type_; }
  const char* plural() const override {
    return type_ == JointType::Rope ? "ropes" : type_ == JointType::Elastic ? "elastics" : "winches";
  }
  bool finish(GameCtx& c, Prop& A, const ToolHit& h) override {
    glm::vec3 pa = toWorld(A, firstLocal);
    Joint j;
    j.type = type_;
    j.a = A.id;
    j.b = h.prop ? h.prop->id : -1;
    j.la = firstLocal;
    j.lb = h.prop ? toLocal(*h.prop, h.pos) : h.pos;
    float d = glm::length(h.pos - pa);
    j.length = type_ == JointType::Elastic ? d * restScale_ : d + addLength_;
    j.strength = strength_;
    j.damping = damping_;
    j.width = width_;
    j.color = color_;
    j.key1 = keyIn_;
    j.key2 = keyOut_;
    j.speed = speed_;
    return commit(c, j, name());
  }
  JointType type_;
  float addLength_ = 0.5f, width_ = 0.03f, strength_ = 600, damping_ = 30, restScale_ = 0.5f, speed_ = 2.0f;
  int keyIn_ = SDL_SCANCODE_I, keyOut_ = SDL_SCANCODE_K;
  glm::vec4 color_{0.35f, 0.27f, 0.18f, 1};
};

// ---------------------------------------------------------------------------
class ThrusterTool : public Tool {
 public:
  const char* name() const override { return "Thruster"; }
  const char* category() const override { return "Gadgets"; }
  const char* desc() const override { return "Attach a thruster that pushes the object while its key is held."; }
  std::string help() const override { return "Left: attach thruster   Reload: remove thrusters from object"; }
  void settings() override {
    ImGui::SliderFloat("Force", &force_, 50, 20000, "%.0f N", ImGuiSliderFlags_Logarithmic);
    keyCombo("Key", &key_);
    ImGui::Checkbox("Toggle", &toggle_);
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    Attachment a;
    a.kind = AttachKind::Thruster;
    a.localPos = toLocal(*h.prop, h.pos);
    a.localDir = toLocalDir(*h.prop, -h.normal);
    a.power = force_;
    a.key = key_;
    a.toggle = toggle_;
    Attachment* na = c.world.addAttachment(*h.prop, a);
    UndoEntry u;
    u.label = "Thruster";
    u.attachments.push_back({h.prop->id, na->id});
    c.world.pushUndo(u);
    return true;
  }
  bool reload(GameCtx& c, const ToolHit& h) override { return clear(c, h, AttachKind::Thruster); }
  static bool clear(GameCtx& c, const ToolHit& h, AttachKind k) {
    if (!h.prop) return false;
    auto& v = h.prop->attachments;
    size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Attachment& a) { return a.kind == k; }), v.end());
    return v.size() != before;
  }

 private:
  float force_ = 1500;
  int key_ = SDL_SCANCODE_UP;
  bool toggle_ = false;
};

class HoverballTool : public Tool {
 public:
  const char* name() const override { return "Hoverball"; }
  const char* category() const override { return "Gadgets"; }
  const char* desc() const override { return "Keeps the object hovering at a height you can raise and lower."; }
  std::string help() const override { return "Left: attach hoverball   Reload: remove hoverballs from object"; }
  void settings() override {
    keyCombo("Up", &up_);
    keyCombo("Down", &down_);
    ImGui::SliderFloat("Speed", &speed_, 0.5f, 20);
    ImGui::SliderFloat("Strength", &strength_, 100, 10000, "%.0f", ImGuiSliderFlags_Logarithmic);
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    Attachment a;
    a.kind = AttachKind::Hoverball;
    a.localPos = toLocal(*h.prop, h.pos);
    a.localDir = toLocalDir(*h.prop, h.normal);
    a.key = up_;
    a.key2 = down_;
    a.hoverSpeed = speed_;
    a.power = strength_;
    a.hoverHeight = h.pos.y + 0.5f;
    Attachment* na = c.world.addAttachment(*h.prop, a);
    UndoEntry u;
    u.label = "Hoverball";
    u.attachments.push_back({h.prop->id, na->id});
    c.world.pushUndo(u);
    return true;
  }
  bool reload(GameCtx& c, const ToolHit& h) override { return ThrusterTool::clear(c, h, AttachKind::Hoverball); }

 private:
  int up_ = SDL_SCANCODE_PAGEUP, down_ = SDL_SCANCODE_PAGEDOWN;
  float speed_ = 3.0f, strength_ = 2000;
};

class WheelTool : public Tool {
 public:
  const char* name() const override { return "Wheel"; }
  const char* category() const override { return "Gadgets"; }
  const char* desc() const override { return "Attach a motorised wheel. Forward/back keys drive it."; }
  std::string help() const override { return "Left: attach wheel   Reload: remove wheels from object"; }
  void settings() override {
    ImGui::SliderFloat("Radius", &radius_, 0.15f, 1.5f);
    ImGui::SliderFloat("Torque", &torque_, 10, 5000, "%.0f Nm", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Max speed", &speed_, 1, 80, "%.0f rad/s");
    keyCombo("Forward", &fwd_);
    keyCombo("Reverse", &back_);
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) {
      c.notify.push("Wheels must be attached to a prop", kRed);
      return false;
    }
    float halfW = 0.12f * radius_ / 0.45f;
    glm::vec3 n = h.normal;
    glm::vec3 pos = h.pos + n * (halfW + 0.03f);
    ShapeDesc sd{ShapeKind::Cylinder, {radius_, halfW, 0}, ""};
    float k = radius_ / 0.45f;
    Prop* w = c.world.createProp("wheel", sd, MAT_RUBBER, glm::vec4(0.13f, 0.13f, 0.14f, 1), 18.0f * k * k,
                                 makeXf(pos, rotationBetween(glm::vec3(0, 1, 0), n)), 1.3f, 0.2f);
    Joint j;
    j.type = JointType::Wheel;
    j.a = h.prop->id;
    j.b = w->id;
    j.la = toLocal(*h.prop, pos);
    j.axisA = toLocalDir(*h.prop, n);
    j.lb = glm::vec3(0);
    j.axisB = glm::vec3(0, 1, 0);
    j.key1 = fwd_;
    j.key2 = back_;
    j.speed = speed_;
    j.torque = torque_;
    // Pick the spin direction so that "forward" rolls the way the player was facing.
    glm::vec3 roll = glm::cross(n, glm::vec3(0, 1, 0));
    glm::vec3 f = c.fwd;
    f.y = 0;
    j.motorSign = glm::dot(roll, f) >= 0 ? -1.0f : 1.0f;
    Joint* nj = c.world.addJoint(j);
    UndoEntry u;
    u.label = "Wheel";
    u.props.push_back(w->id);
    if (nj) u.joints.push_back(nj->id);
    c.world.pushUndo(u);
    return true;
  }
  bool reload(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    std::vector<int> wheels;
    for (auto& kv : c.world.joints)
      if (kv.second.type == JointType::Wheel && kv.second.a == h.prop->id) wheels.push_back(kv.second.b);
    for (int id : wheels) c.world.removeProp(id);
    return !wheels.empty();
  }

 private:
  float radius_ = 0.45f, torque_ = 600, speed_ = 25;
  int fwd_ = SDL_SCANCODE_UP, back_ = SDL_SCANCODE_DOWN;
};

class BalloonTool : public Tool {
 public:
  const char* name() const override { return "Balloon"; }
  const char* category() const override { return "Gadgets"; }
  const char* desc() const override { return "Tie a balloon to something. Enough of them will lift it."; }
  std::string help() const override { return "Left: attach balloon"; }
  void settings() override {
    ImGui::SliderFloat("Lift force", &lift_, 0, 1500, "%.0f N");
    ImGui::SliderFloat("Rope length", &length_, 0.5f, 10);
    ImGui::Checkbox("Random colour", &random_);
    if (!random_) ImGui::ColorEdit3("Colour", &color_.x);
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.hit) return false;
    const PropDef* d = findPropDef("balloon");
    if (!d) return false;
    UndoEntry u;
    u.label = "Balloon";
    Prop* b = c.world.spawn(*d, makeXf(h.pos + glm::vec3(0, length_ + 0.3f, 0)), &u);
    b->lift = lift_;
    if (random_) {
      static std::mt19937 rng(42);
      std::uniform_real_distribution<float> hue(0, 1);
      float hh = hue(rng) * 6.0f;
      glm::vec3 col = glm::clamp(glm::vec3(std::fabs(hh - 3) - 1, 2 - std::fabs(hh - 2), 2 - std::fabs(hh - 4)), 0.0f, 1.0f);
      b->color = glm::vec4(col * 0.85f + 0.1f, 1);
    } else {
      b->color = color_;
    }
    Joint j;
    j.type = JointType::Rope;
    j.a = b->id;
    j.la = glm::vec3(0, -0.3f, 0);
    j.b = h.prop ? h.prop->id : -1;
    j.lb = h.prop ? toLocal(*h.prop, h.pos) : h.pos;
    j.length = length_;
    j.width = 0.012f;
    j.color = glm::vec4(0.9f, 0.9f, 0.88f, 1);
    if (Joint* nj = c.world.addJoint(j)) u.joints.push_back(nj->id);
    c.world.pushUndo(u);
    return true;
  }

 private:
  float lift_ = 150, length_ = 2.5f;
  bool random_ = true;
  glm::vec4 color_{0.9f, 0.2f, 0.3f, 1};
};

class DynamiteTool : public Tool {
 public:
  const char* name() const override { return "Dynamite"; }
  const char* category() const override { return "Gadgets"; }
  const char* desc() const override { return "Place dynamite. Press its key to blow it up."; }
  std::string help() const override { return "Left: place dynamite   Right: detonate everything now"; }
  void settings() override {
    keyCombo("Detonate key", &key_);
    ImGui::SliderFloat("Power", &power_, 5, 80);
    ImGui::Checkbox("Weld to prop", &weld_);
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.hit) return false;
    const PropDef* d = findPropDef("dynamite");
    if (!d) return false;
    UndoEntry u;
    u.label = "Dynamite";
    Prop* p = c.world.spawn(*d, makeXf(h.pos + h.normal * 0.16f, rotationBetween(glm::vec3(0, 1, 0), h.normal)), &u);
    p->key = key_;
    p->power = power_;
    if (weld_ && h.prop) {
      Joint j;
      j.type = JointType::Weld;
      j.a = p->id;
      j.b = h.prop->id;
      j.nocollide = true;
      if (Joint* nj = c.world.addJoint(j)) u.joints.push_back(nj->id);
    }
    c.world.pushUndo(u);
    c.notify.push(std::string("Dynamite placed - press ") + SDL_GetScancodeName((SDL_Scancode)key_) + " to detonate");
    return true;
  }
  bool secondary(GameCtx& c, const ToolHit&) override {
    std::set<int> keys;
    for (auto& kv : c.world.props)
      if (kv.second->special == Special::Dynamite) keys.insert(kv.second->key);
    for (int k : keys) c.world.keyEvent(k, true);
    return !keys.empty();
  }

 private:
  int key_ = SDL_SCANCODE_B;
  float power_ = 25;
  bool weld_ = true;
};

class LampTool : public Tool {
 public:
  const char* name() const override { return "Lamp"; }
  const char* category() const override { return "Gadgets"; }
  const char* desc() const override { return "Place a glowing light. Its key toggles it on and off."; }
  std::string help() const override { return "Left: place lamp"; }
  void settings() override {
    ImGui::ColorEdit3("Colour", &color_.x);
    ImGui::SliderFloat("Range", &range_, 2, 40);
    keyCombo("Toggle key", &key_);
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.hit) return false;
    const PropDef* d = findPropDef("lamp");
    if (!d) return false;
    UndoEntry u;
    u.label = "Lamp";
    Prop* p = c.world.spawn(*d, makeXf(h.pos + h.normal * 0.22f), &u);
    p->color = color_;
    p->brightness = range_;
    p->key = key_;
    if (h.prop) {
      Joint j;
      j.type = JointType::Weld;
      j.a = p->id;
      j.b = h.prop->id;
      j.nocollide = true;
      if (Joint* nj = c.world.addJoint(j)) u.joints.push_back(nj->id);
    }
    c.world.pushUndo(u);
    return true;
  }

 private:
  glm::vec4 color_{1.0f, 0.85f, 0.6f, 1};
  float range_ = 14;
  int key_ = SDL_SCANCODE_L;
};

// ---------------------------------------------------------------------------
class RemoverTool : public Tool {
 public:
  const char* name() const override { return "Remover"; }
  const char* category() const override { return "Utility"; }
  const char* desc() const override { return "Remove objects."; }
  std::string help() const override {
    return "Left: remove object   Right: remove whole contraption   Reload: remove constraints";
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    c.world.removeEntity(h.prop->id);
    return true;
  }
  bool secondary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    auto ids = c.world.contraption(h.prop->id);
    for (int id : ids) c.world.removeProp(id);
    c.notify.push("Removed " + std::to_string(ids.size()) + " objects");
    return true;
  }
  bool reload(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    int n = 0;
    std::vector<int> ids;
    for (auto& kv : c.world.joints)
      if ((kv.second.a == h.prop->id || kv.second.b == h.prop->id) && kv.second.type != JointType::Ragdoll)
        ids.push_back(kv.first);
    for (int id : ids) c.world.removeJoint(id), n++;
    h.prop->attachments.clear();
    c.notify.push("Removed " + std::to_string(n) + " constraints");
    return true;
  }
};

class ColorTool : public Tool {
 public:
  const char* name() const override { return "Colour"; }
  const char* category() const override { return "Render"; }
  const char* desc() const override { return "Paint objects."; }
  std::string help() const override { return "Left: apply colour   Right: copy colour   Reload: reset"; }
  void settings() override { ImGui::ColorPicker3("##col", &color_.x, ImGuiColorEditFlags_PickerHueWheel); }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    h.prop->color = color_;
    return true;
  }
  bool secondary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    color_ = h.prop->color;
    return true;
  }
  bool reload(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    if (const PropDef* d = findPropDef(h.prop->type)) h.prop->color = d->color;
    return true;
  }

 private:
  glm::vec4 color_{0.2f, 0.6f, 1.0f, 1};
};

class MaterialTool : public Tool {
 public:
  const char* name() const override { return "Material"; }
  const char* category() const override { return "Render"; }
  const char* desc() const override { return "Change what objects look like they are made of."; }
  std::string help() const override { return "Left: apply material   Right: copy material"; }
  void settings() override {
    for (int i = 0; i < MAT_COUNT; i++) ImGui::RadioButton(materialName(i), &mat_, i);
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    h.prop->material = mat_;
    return true;
  }
  bool secondary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    mat_ = h.prop->material;
    return true;
  }

 private:
  int mat_ = MAT_CHROME;
};

class WeightTool : public Tool {
 public:
  const char* name() const override { return "Weight"; }
  const char* category() const override { return "Utility"; }
  const char* desc() const override { return "Change how heavy an object is."; }
  std::string help() const override { return "Left: apply weight   Right: copy weight"; }
  void settings() override { ImGui::SliderFloat("Mass", &mass_, 0.1f, 5000, "%.1f kg", ImGuiSliderFlags_Logarithmic); }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    c.world.setMass(*h.prop, mass_);
    c.notify.push(std::string("Mass set to ") + std::to_string((int)std::round(mass_)) + " kg");
    return true;
  }
  bool secondary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    mass_ = h.prop->mass;
    return true;
  }

 private:
  float mass_ = 50;
};

class DuplicatorTool : public Tool {
 public:
  const char* name() const override { return "Duplicator"; }
  const char* category() const override { return "Utility"; }
  const char* desc() const override { return "Copy a whole contraption and paste copies of it."; }
  std::string help() const override { return "Right: copy contraption   Left: paste   Reload: clear clipboard"; }
  void settings() override {
    if (data_.empty())
      ImGui::TextDisabled("Clipboard empty - right click a contraption.");
    else
      ImGui::Text("Clipboard: %d objects", count_);
  }
  bool secondary(GameCtx& c, const ToolHit& h) override {
    if (!h.prop) return false;
    auto ids = c.world.contraption(h.prop->id);
    float minY = 1e9f;
    glm::vec3 centre(0);
    for (int id : ids) {
      Prop* p = c.world.prop(id);
      btVector3 mn, mx;
      p->body->getAabb(mn, mx);
      minY = std::min(minY, (float)mn.y());
      centre += p->pos();
    }
    centre /= (float)ids.size();
    btTransform ref = makeXf(glm::vec3(centre.x, minY, centre.z), yawQuat(c.player.yaw));
    data_ = c.world.serialize(ids, ref, false);
    count_ = (int)ids.size();
    c.notify.push("Copied " + std::to_string(count_) + " objects", kGreen);
    return true;
  }
  bool primary(GameCtx& c, const ToolHit& h) override {
    if (!h.hit || data_.empty()) return false;
    btTransform ref = makeXf(h.pos + glm::vec3(0, 0.02f, 0), yawQuat(c.player.yaw));
    UndoEntry u;
    u.label = "Duplication";
    c.world.deserialize(data_, ref, &u);
    c.world.pushUndo(u);
    return true;
  }
  bool reload(GameCtx& c, const ToolHit&) override {
    data_.clear();
    count_ = 0;
    return false;
  }

 private:
  std::string data_;
  int count_ = 0;
};

}  // namespace

std::vector<std::unique_ptr<Tool>> makeTools() {
  std::vector<std::unique_ptr<Tool>> t;
  t.emplace_back(new WeldTool());
  t.emplace_back(new NoCollideTool());
  t.emplace_back(new BallSocketTool());
  t.emplace_back(new AxisTool());
  t.emplace_back(new RopeTool(JointType::Rope));
  t.emplace_back(new RopeTool(JointType::Elastic));
  t.emplace_back(new RopeTool(JointType::Winch));
  t.emplace_back(new ThrusterTool());
  t.emplace_back(new WheelTool());
  t.emplace_back(new HoverballTool());
  t.emplace_back(new BalloonTool());
  t.emplace_back(new DynamiteTool());
  t.emplace_back(new LampTool());
  t.emplace_back(new RemoverTool());
  t.emplace_back(new WeightTool());
  t.emplace_back(new DuplicatorTool());
  t.emplace_back(new ColorTool());
  t.emplace_back(new MaterialTool());
  return t;
}
