#include "net.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "game.h"

using namespace net;

static int s_enetRefs = 0;
static const glm::vec4 kJoinColor(1.0f, 0.85f, 0.35f, 1.0f);
static const glm::vec4 kErrColor(1.0f, 0.35f, 0.3f, 1.0f);

static glm::vec3 forwardFrom(float yaw, float pitch) {
  return glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
}

Net::Net(Game& g) : g_(g) {
  if (s_enetRefs++ == 0 && enet_initialize() != 0) fprintf(stderr, "ENet initialisation failed\n");
}

Net::~Net() {
  disconnect();
  if (--s_enetRefs == 0) enet_deinitialize();
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------
bool Net::host(int port, std::string& err) {
  disconnect();
  ENetAddress a;
  a.host = ENET_HOST_ANY;
  a.port = (enet_uint16)port;
  host_ = enet_host_create(&a, kMaxPlayers, 3, 0, 0);
  if (!host_) {
    err = "Could not open UDP port " + std::to_string(port);
    return false;
  }
  mode_ = Mode::Host;
  myId_ = 0;
  nextId_ = 1;
  port_ = port;
  lastHash_ = 0;
  lastSent_.clear();
  g_.audio.onPlayAt = [this](SoundId id, const glm::vec3& p, float v, float pitch) {
    if (!remotes_.empty() && pendingSounds_.size() < 64) pendingSounds_.push_back({(uint8_t)id, p, v, pitch});
  };
  status = "Hosting on UDP port " + std::to_string(port);
  playerList = {name};
  pushChat(status, kJoinColor);
  printf("[net] %s\n", status.c_str());
  return true;
}

bool Net::connect(const std::string& address, int port, std::string& err) {
  disconnect();
  host_ = enet_host_create(nullptr, 1, 3, 0, 0);
  if (!host_) {
    err = "Could not create a network socket";
    return false;
  }
  ENetAddress a;
  if (enet_address_set_host(&a, address.c_str()) != 0) {
    err = "Unknown host: " + address;
    enet_host_destroy(host_);
    host_ = nullptr;
    return false;
  }
  a.port = (enet_uint16)port;
  server_ = enet_host_connect(host_, &a, 3, kProtocol);
  if (!server_) {
    err = "Could not start connecting";
    enet_host_destroy(host_);
    host_ = nullptr;
    return false;
  }
  mode_ = Mode::Client;
  myId_ = -1;
  port_ = port;
  sincePacket_ = 0;
  status = "Connecting to " + address + ":" + std::to_string(port) + "...";
  printf("[net] %s\n", status.c_str());
  return true;
}

void Net::disconnect() {
  if (mode_ == Mode::Host) {
    for (auto& r : remotes_) enet_peer_disconnect_now(r->peer, 0);
    while (!remotes_.empty()) removeRemote(remotes_.back()->peer);
    g_.audio.onPlayAt = nullptr;
    pendingSounds_.clear();
    if (host_) enet_host_destroy(host_);
    host_ = nullptr;
    mode_ = Mode::Offline;
    status = "Stopped hosting";
    others.clear();
    playerList.clear();
  } else if (mode_ == Mode::Client) {
    if (server_) enet_peer_disconnect_now(server_, 0);
    leaveClientMode("Disconnected");
  }
  myId_ = -1;
}

void Net::leaveClientMode(const std::string& why) {
  bool wasConnected = myId_ >= 0;
  g_.world.clientMode = false;
  g_.world.clearProps();
  hostToLocal_.clear();
  others.clear();
  self = PlayerView();
  playerList.clear();
  sentToolSettings_.clear();
  myId_ = -1;
  if (host_) enet_host_destroy(host_);
  host_ = nullptr;
  server_ = nullptr;
  mode_ = Mode::Offline;
  status = why;
  if (wasConnected || why != "Disconnected") {
    g_.notify.push(why, kErrColor);
    pushChat(why, kErrColor);
  }
  printf("[net] %s\n", why.c_str());
}

void Net::send(ENetPeer* peer, const Writer& w, bool reliable, int channel) {
  if (!peer) return;
  ENetPacket* p = enet_packet_create(w.b.data(), w.b.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  enet_peer_send(peer, (enet_uint8)channel, p);
}

void Net::broadcast(const Writer& w, bool reliable, int channel) {
  if (!host_ || remotes_.empty()) return;
  ENetPacket* p = enet_packet_create(w.b.data(), w.b.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
  enet_host_broadcast(host_, (enet_uint8)channel, p);
}

void Net::pushChat(const std::string& text, const glm::vec4& color) {
  chatLines.push_back({text, 0.0f, color});
  while (chatLines.size() > 50) chatLines.pop_front();
}

// ---------------------------------------------------------------------------
// Host side
// ---------------------------------------------------------------------------
Net::Remote* Net::remote(ENetPeer* peer) {
  for (auto& r : remotes_)
    if (r->peer == peer) return r.get();
  return nullptr;
}

GameCtx Net::remoteCtx(Remote& r) {
  GameCtx c{g_.world, r.player, g_.physics, r.sink, r.notify, g_.assets};
  c.eye = r.eye;
  c.fwd = r.fwd;
  c.owner = r.id;
  glm::vec3 right = glm::cross(r.fwd, glm::vec3(0, 1, 0));
  right = glm::length(right) > 1e-4f ? glm::normalize(right) : glm::vec3(1, 0, 0);
  c.muzzle = r.eye + right * 0.2f - glm::vec3(0, 0.15f, 0) + r.fwd * 0.5f;
  return c;
}

void Net::addRemote(ENetPeer* peer, const std::string& requested) {
  auto r = std::make_unique<Remote>();
  r->id = nextId_++;
  r->name = requested.substr(0, 24);
  if (r->name.empty()) r->name = "Player" + std::to_string(r->id);
  r->peer = peer;
  r->feet = g_.world.spawnPoint;
  r->eye = r->feet + glm::vec3(0, 1.65f, 0);
  r->player.initRemote(g_.physics, r->feet);
  r->weapons.emplace_back(new PhysGun());
  r->weapons.emplace_back(new GravGun());
  r->weapons.emplace_back(new ToolGun());
  r->notify.recordOutbox = true;
  r->sink.net = this;
  r->sink.peer = peer;
  Writer w;
  w.u8(S_WELCOME);
  w.i32(r->id);
  w.str(r->name);
  send(peer, w, true, 0);
  std::string joined = r->name + " joined the game";
  remotes_.push_back(std::move(r));
  sendStructure(peer);
  lastSent_.clear();  // next snapshot carries everything
  sendPlayers();
  Writer c;
  c.u8(S_CHAT);
  c.str(joined);
  c.put(kJoinColor);
  broadcast(c, true, 0);
  pushChat(joined, kJoinColor);
  g_.notify.push(joined, kJoinColor);
}

void Net::removeRemote(ENetPeer* peer) {
  auto it = std::find_if(remotes_.begin(), remotes_.end(), [&](auto& r) { return r->peer == peer; });
  if (it == remotes_.end()) return;
  Remote& r = **it;
  GameCtx c = remoteCtx(r);
  for (auto& w : r.weapons) w->holster(c);
  r.player.shutdown();
  std::string left = r.name + " left the game";
  remotes_.erase(it);
  if (mode_ != Mode::Host) return;
  sendPlayers();
  Writer w;
  w.u8(S_CHAT);
  w.str(left);
  w.put(kJoinColor);
  broadcast(w, true, 0);
  pushChat(left, kJoinColor);
  g_.notify.push(left, kJoinColor);
}

void Net::sendPlayers() {
  playerList = {name};
  for (auto& r : remotes_) playerList.push_back(r->name);
  Writer w;
  w.u8(S_PLAYERS);
  w.u8((uint8_t)playerList.size());
  for (auto& n : playerList) w.str(n);
  broadcast(w, true, 0);
}

void Net::combineKeys(bool* keys) const {
  for (auto& r : remotes_)
    for (int i = 0; i < SDL_NUM_SCANCODES; i++) keys[i] = keys[i] || r->input.down[i];
}

void Net::hostReceive(ENetPeer* peer, const uint8_t* data, size_t len) {
  Reader rd(data, len);
  uint8_t type = rd.u8();
  Remote* r = remote(peer);
  if (type == C_HELLO) {
    uint32_t proto = rd.get<uint32_t>();
    std::string nm = rd.str();
    if (!rd.ok || proto != kProtocol) {
      Writer w;
      w.u8(S_REJECT);
      w.str("Protocol mismatch - update your game");
      send(peer, w, true, 0);
      enet_peer_disconnect_later(peer, 0);
      return;
    }
    if (!r) addRemote(peer, nm);
    return;
  }
  if (!r) return;
  switch (type) {
    case C_STATE: {
      glm::vec3 feet = rd.vec3(), eye = rd.vec3();
      float yaw = rd.f32(), pitch = rd.f32();
      int weapon = rd.u8();
      int tool = rd.i32();
      uint8_t flags = rd.u8();
      float mdx = rd.f32(), mdy = rd.f32();
      int wheel = rd.i32();
      if (!rd.ok) return;
      r->feet = feet;
      r->eye = eye;
      r->yaw = yaw;
      r->pitch = pitch;
      r->fwd = forwardFrom(yaw, pitch);
      r->noclip = flags & 1;
      r->crouch = flags & 2;
      r->input.mdx += mdx;
      r->input.mdy += mdy;
      r->input.wheel += wheel;
      if (weapon >= 0 && weapon < (int)r->weapons.size() && weapon != r->weapon) {
        GameCtx c = remoteCtx(*r);
        r->weapons[r->weapon]->holster(c);
        r->weapon = weapon;
      }
      auto* tg = static_cast<ToolGun*>(r->weapons[2].get());
      if (tool != tg->current) tg->select(tool);
      break;
    }
    case C_INPUT: {
      int kind = rd.u8();
      int code = rd.i32();
      bool down = rd.u8() != 0;
      if (!rd.ok) return;
      if (kind == 0 && code > 0 && code < SDL_NUM_SCANCODES) {
        if (down && !r->input.down[code]) {
          r->input.pressed[code] = true;
          g_.world.keyEvent(code, true);
        }
        if (!down && r->input.down[code]) r->input.released[code] = true;
        r->input.down[code] = down;
      } else if (kind == 1 && code > 0 && code < 8) {
        if (down && !r->input.mouseDown[code]) r->input.mousePressed[code] = true;
        if (!down && r->input.mouseDown[code]) r->input.mouseReleased[code] = true;
        r->input.mouseDown[code] = down;
      }
      break;
    }
    case C_SPAWN: {
      std::string id = rd.str();
      glm::vec3 eye = rd.vec3(), fwd = rd.vec3();
      float yaw = rd.f32();
      const PropDef* d = findPropDef(id);
      if (rd.ok && d && propAvailable(*d) && glm::length(fwd) > 0.1f)
        g_.spawnPropFor(*d, eye, glm::normalize(fwd), yaw, r->id, r->sink);
      break;
    }
    case C_UNDO: {
      std::string label = g_.world.undo(r->id);
      if (!label.empty()) {
        r->notify.push("Undone " + label);
        r->sink.play(SND_UNDO, 0.6f, 1.0f);
      } else {
        r->notify.push("Nothing to undo", glm::vec4(0.6f, 0.6f, 0.6f, 1));
      }
      break;
    }
    case C_TOOLSET: {
      int idx = rd.i32();
      std::string s = rd.str();
      auto* tg = static_cast<ToolGun*>(r->weapons[2].get());
      if (rd.ok && idx >= 0 && idx < (int)tg->tools.size()) tg->tools[idx]->loadSettings(s);
      break;
    }
    case C_CHAT: {
      std::string text = rd.str().substr(0, 200);
      if (!rd.ok || text.empty()) return;
      std::string line = r->name + ": " + text;
      Writer w;
      w.u8(S_CHAT);
      w.str(line);
      w.put(glm::vec4(1));
      broadcast(w, true, 0);
      pushChat(line, glm::vec4(1));
      break;
    }
    default: break;
  }
}

void Net::RemoteSink::play(SoundId id, float volume, float pitch) {
  Writer w;
  w.u8(S_SOUND);
  w.u8(1);
  w.u8((uint8_t)id);
  w.vec3(glm::vec3(0));
  w.f32(volume);
  w.f32(pitch);
  w.u8(0);  // not positional
  net->send(peer, w, false, 2);
}

void Net::RemoteSink::playAt(SoundId id, const glm::vec3& pos, float volume, float pitch) {
  net->g_.audio.playAt(id, pos, volume, pitch);  // forwarded to everyone by the audio hook
}

void Net::sendStructure(ENetPeer* to) {
  std::vector<int> ids;
  for (auto& kv : g_.world.props) ids.push_back(kv.first);
  btTransform ident;
  ident.setIdentity();
  std::string text = g_.world.serialize(ids, ident, true);
  Writer w;
  w.u8(S_STRUCTURE);
  w.put((uint32_t)ids.size());
  for (int id : ids) w.i32(id);
  w.str(text);
  if (to)
    send(to, w, true, 0);
  else
    broadcast(w, true, 0);
}

uint64_t Net::structureHash() const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
  };
  auto mixs = [&](const std::string& s) { mix(s.data(), s.size()); };
  for (auto& kv : g_.world.props) {
    const Prop& p = *kv.second;
    mix(&p.id, sizeof(p.id));
    mixs(p.type);
    mixs(p.shape.key());
    mix(&p.material, sizeof(int));
    mix(&p.color, sizeof(p.color));
    mix(&p.mass, sizeof(float));
    mix(&p.frozen, sizeof(bool));
    mix(&p.group, sizeof(int));
    mix(&p.special, sizeof(p.special));
    mix(&p.key, sizeof(int));
    mix(&p.brightness, sizeof(float));
    for (auto& a : p.attachments) {
      mix(&a.id, sizeof(int));
      mix(&a.kind, sizeof(a.kind));
      mix(&a.localPos, sizeof(a.localPos));
    }
  }
  for (auto& kv : g_.world.joints) {
    const Joint& j = kv.second;
    mix(&j.id, sizeof(int));
    mix(&j.type, sizeof(j.type));
    mix(&j.a, sizeof(int));
    mix(&j.b, sizeof(int));
    mix(&j.width, sizeof(float));
    mix(&j.color, sizeof(j.color));
  }
  return h;
}

static void writeView(Writer& w, const PlayerView& v) {
  w.u8((uint8_t)v.id);
  w.str(v.name);
  w.vec3(v.feet);
  w.vec3(v.eye);
  w.f32(v.yaw);
  w.f32(v.pitch);
  w.u8((uint8_t)v.weapon);
  w.u8((uint8_t)((v.noclip ? 1 : 0) | (v.crouch ? 2 : 0)));
  w.u8((uint8_t)v.beamType);
  w.vec3(v.beamEnd);
  w.i32(v.held);
  w.u8((uint8_t)v.toolStage);
}

static PlayerView readView(Reader& rd) {
  PlayerView v;
  v.id = rd.u8();
  v.name = rd.str();
  v.feet = rd.vec3();
  v.eye = rd.vec3();
  v.yaw = rd.f32();
  v.pitch = rd.f32();
  v.weapon = rd.u8();
  uint8_t f = rd.u8();
  v.noclip = f & 1;
  v.crouch = f & 2;
  v.beamType = rd.u8();
  v.beamEnd = rd.vec3();
  v.held = rd.i32();
  v.toolStage = rd.u8();
  return v;
}

void Net::buildViews() {
  others.clear();
  for (auto& r : remotes_) {
    PlayerView v;
    v.id = r->id;
    v.name = r->name;
    v.feet = r->feet;
    v.eye = r->eye;
    v.yaw = r->yaw;
    v.pitch = r->pitch;
    v.weapon = r->weapon;
    v.noclip = r->noclip;
    v.crouch = r->crouch;
    Weapon* w = r->weapons[r->weapon].get();
    v.beamType = w->beamInfo(v.beamEnd);
    v.held = w->heldProp();
    if (Tool* t = static_cast<ToolGun*>(r->weapons[2].get())->tool()) v.toolStage = t->getStage();
    others.push_back(v);
  }
}

void Net::sendSnapshot() {
  Writer w;
  w.u8(S_SNAPSHOT);
  // players: the host first, then every client
  PlayerView hv;
  hv.id = 0;
  hv.name = name;
  hv.feet = g_.player.feet();
  hv.eye = g_.player.cam.pos;
  hv.yaw = g_.player.yaw;
  hv.pitch = g_.player.pitch;
  hv.weapon = g_.curWeapon;
  hv.noclip = g_.player.noclip;
  hv.crouch = g_.player.crouching;
  hv.beamType = g_.weapon()->beamInfo(hv.beamEnd);
  hv.held = g_.weapon()->heldProp();
  w.u8((uint8_t)(1 + others.size()));
  writeView(w, hv);
  for (auto& v : others) writeView(w, v);

  bool full = (snapCount_++ % 30) == 0;
  std::vector<const Prop*> changed;
  for (auto& kv : g_.world.props) {
    const Prop& p = *kv.second;
    btTransform t = p.body->getWorldTransform();
    Sent s{toGlm(t.getOrigin()), toGlm(t.getRotation()), 0};
    for (auto& a : p.attachments)
      if (a.kind == AttachKind::Thruster && a.on) s.flags |= 1;
    if (p.special == Special::Lamp && p.on) s.flags |= 2;
    auto it = lastSent_.find(p.id);
    bool moved = it == lastSent_.end() || glm::length(it->second.pos - s.pos) > 0.002f ||
                 std::fabs(glm::dot(it->second.rot, s.rot)) < 0.99999f || it->second.flags != s.flags;
    if (full || moved) {
      changed.push_back(&p);
      lastSent_[p.id] = s;
    }
  }
  for (auto it = lastSent_.begin(); it != lastSent_.end();)
    it = g_.world.props.count(it->first) ? std::next(it) : lastSent_.erase(it);
  w.put((uint16_t)std::min<size_t>(changed.size(), 65535));
  for (size_t i = 0; i < changed.size() && i < 65535; i++) {
    const Prop& p = *changed[i];
    const Sent& s = lastSent_[p.id];
    w.i32(p.id);
    w.vec3(s.pos);
    w.f32(s.rot.x), w.f32(s.rot.y), w.f32(s.rot.z), w.f32(s.rot.w);
    w.u8(s.flags);
  }
  broadcast(w, false, 1);
}

void Net::broadcastExplosion(const glm::vec3& pos, float radius, float power) {
  if (!isHost()) return;
  Writer w;
  w.u8(S_EXPLOSION);
  w.vec3(pos);
  w.f32(radius);
  w.f32(power);
  broadcast(w, true, 0);
}

// ---------------------------------------------------------------------------
// Client side
// ---------------------------------------------------------------------------
void Net::clientReceive(const uint8_t* data, size_t len) {
  Reader rd(data, len);
  uint8_t type = rd.u8();
  switch (type) {
    case S_WELCOME: {
      myId_ = rd.i32();
      name = rd.str();
      GameCtx c = g_.makeCtx();
      for (auto& w : g_.weapons) w->holster(c);
      g_.world.clearProps();
      g_.world.clientMode = true;
      hostToLocal_.clear();
      sentToolSettings_.clear();
      status = "Connected as " + name;
      g_.notify.push("Connected to server", kJoinColor);
      printf("[net] connected, id %d\n", myId_);
      break;
    }
    case S_REJECT: leaveClientMode("Server rejected us: " + rd.str()); break;
    case S_STRUCTURE: {
      uint32_t n = rd.get<uint32_t>();
      std::vector<int> ids;
      for (uint32_t i = 0; i < n && rd.ok; i++) ids.push_back(rd.i32());
      std::string text = rd.str();
      if (!rd.ok) return;
      g_.world.clearProps();
      std::map<int, int> idx;
      btTransform ident;
      ident.setIdentity();
      g_.world.deserialize(text, ident, nullptr, &idx);
      hostToLocal_.clear();
      for (uint32_t k = 0; k < ids.size(); k++)
        if (idx.count((int)k)) hostToLocal_[ids[k]] = idx[(int)k];
      break;
    }
    case S_SNAPSHOT: {
      int np = rd.u8();
      std::vector<PlayerView> views;
      for (int i = 0; i < np && rd.ok; i++) views.push_back(readView(rd));
      if (!rd.ok) return;
      others.clear();
      for (auto& v : views) {
        auto it = hostToLocal_.find(v.held);
        v.held = it == hostToLocal_.end() ? -1 : it->second;
        if (v.id == myId_)
          self = v;
        else
          others.push_back(v);
      }
      int nprops = rd.get<uint16_t>();
      for (int i = 0; i < nprops && rd.ok; i++) {
        int id = rd.i32();
        glm::vec3 pos = rd.vec3();
        float qx = rd.f32(), qy = rd.f32(), qz = rd.f32(), qw = rd.f32();
        uint8_t flags = rd.u8();
        if (!rd.ok) break;
        auto it = hostToLocal_.find(id);
        if (it == hostToLocal_.end()) continue;
        if (Prop* p = g_.world.prop(it->second))
          g_.world.setNetTransform(*p, btTransform(btQuaternion(qx, qy, qz, qw), toBt(pos)), flags);
      }
      break;
    }
    case S_SOUND: {
      int n = rd.u8();
      for (int i = 0; i < n && rd.ok; i++) {
        int id = rd.u8();
        glm::vec3 pos = rd.vec3();
        float vol = rd.f32(), pitch = rd.f32();
        bool positional = rd.u8() != 0;
        if (!rd.ok || id >= SND_COUNT) break;
        if (positional)
          g_.audio.playAt((SoundId)id, pos, vol, pitch);
        else
          g_.audio.play((SoundId)id, vol, pitch);
      }
      break;
    }
    case S_EXPLOSION: {
      glm::vec3 pos = rd.vec3();
      float radius = rd.f32(), power = rd.f32();
      if (!rd.ok) return;
      g_.world.explodeVisual(pos, radius);
      if (g_.world.onExplosion) g_.world.onExplosion(pos, radius, power);
      break;
    }
    case S_NOTIFY: {
      std::string text = rd.str();
      glm::vec4 color = rd.get<glm::vec4>();
      if (rd.ok) g_.notify.push(text, color);
      break;
    }
    case S_CHAT: {
      std::string text = rd.str();
      glm::vec4 color = rd.get<glm::vec4>();
      if (rd.ok) pushChat(text, color);
      break;
    }
    case S_PLAYERS: {
      int n = rd.u8();
      std::vector<std::string> names;
      for (int i = 0; i < n && rd.ok; i++) names.push_back(rd.str());
      if (rd.ok) playerList = names;
      break;
    }
    default: break;
  }
}

void Net::sendSpawn(const std::string& propId, const glm::vec3& eye, const glm::vec3& fwd, float yaw) {
  if (!connected()) return;
  Writer w;
  w.u8(C_SPAWN);
  w.str(propId);
  w.vec3(eye);
  w.vec3(fwd);
  w.f32(yaw);
  send(server_, w, true, 0);
}

void Net::sendUndo() {
  if (!connected()) return;
  Writer w;
  w.u8(C_UNDO);
  send(server_, w, true, 0);
}

void Net::sendInput(uint8_t kind, int code, bool down) {
  if (!connected()) return;
  Writer w;
  w.u8(C_INPUT);
  w.u8(kind);
  w.i32(code);
  w.u8(down ? 1 : 0);
  send(server_, w, true, 0);
}

void Net::sendToolSettings(bool force) {
  if (!connected()) return;
  ToolGun* tg = g_.toolgun();
  Tool* t = tg->tool();
  if (!t) return;
  std::string s = t->saveSettings();
  auto it = sentToolSettings_.find(tg->current);
  if (!force && it != sentToolSettings_.end() && it->second == s) return;
  sentToolSettings_[tg->current] = s;
  Writer w;
  w.u8(C_TOOLSET);
  w.i32(tg->current);
  w.str(s);
  send(server_, w, true, 0);
}

void Net::chat(const std::string& text) {
  if (text.empty()) return;
  if (isClient()) {
    if (!connected()) return;
    Writer w;
    w.u8(C_CHAT);
    w.str(text);
    send(server_, w, true, 0);
    return;
  }
  std::string line = name + ": " + text;
  pushChat(line, glm::vec4(1));
  if (isHost()) {
    Writer w;
    w.u8(S_CHAT);
    w.str(line);
    w.put(glm::vec4(1));
    broadcast(w, true, 0);
  }
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------
void Net::update(float dt) {
  for (auto& c : chatLines) c.t += dt;
  if (mode_ == Mode::Offline || !host_) return;
  sincePacket_ += dt;
  ENetEvent ev;
  while (host_ && enet_host_service(host_, &ev, 0) > 0) {
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT:
        if (isClient()) {
          Writer w;
          w.u8(C_HELLO);
          w.put(kProtocol);
          w.str(name);
          send(ev.peer, w, true, 0);
          status = "Joining...";
        } else {
          enet_peer_timeout(ev.peer, 0, 5000, 15000);
        }
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        if (isHost()) {
          hostReceive(ev.peer, ev.packet->data, ev.packet->dataLength);
        } else {
          sincePacket_ = 0;
          clientReceive(ev.packet->data, ev.packet->dataLength);
        }
        enet_packet_destroy(ev.packet);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        if (isHost())
          removeRemote(ev.peer);
        else
          leaveClientMode(myId_ >= 0 ? "Lost connection to the server" : "Could not connect to the server");
        break;
      default: break;
    }
  }

  if (isHost()) {
    for (auto& rp : remotes_) {
      Remote& r = *rp;
      r.player.setRemoteState(r.feet, r.eye, r.yaw, r.pitch, r.noclip);
      GameCtx c = remoteCtx(r);
      r.weapons[r.weapon]->update(c, r.input, dt);
      r.input.beginFrame();
      for (auto& note : r.notify.outbox) {
        Writer w;
        w.u8(S_NOTIFY);
        w.str(note.first);
        w.put(note.second);
        send(r.peer, w, true, 0);
      }
      r.notify.outbox.clear();
      r.notify.notes.clear();
    }
    buildViews();
  } else if (isClient()) {
    if (connected()) {
      stateTimer_ += dt;
      if (stateTimer_ >= 1.0f / 60.0f) {
        stateTimer_ = 0;
        Writer w;
        w.u8(C_STATE);
        w.vec3(g_.player.feet());
        w.vec3(g_.player.cam.pos);
        w.f32(g_.player.yaw);
        w.f32(g_.player.pitch);
        w.u8((uint8_t)g_.curWeapon);
        w.i32(g_.toolgun()->current);
        w.u8((uint8_t)((g_.player.noclip ? 1 : 0) | (g_.player.crouching ? 2 : 0)));
        w.f32(mdxAcc_);
        w.f32(mdyAcc_);
        w.i32(wheelAcc_);
        send(server_, w, false, 1);
        mdxAcc_ = mdyAcc_ = 0;
        wheelAcc_ = 0;
      }
      sendToolSettings(false);
    } else if (sincePacket_ > 8.0f) {
      leaveClientMode("Could not connect to the server (timed out)");
    }
  }
}

void Net::preTick(float dt) {
  if (!isHost()) return;
  for (auto& rp : remotes_) {
    GameCtx c = remoteCtx(*rp);
    rp->weapons[rp->weapon]->preTick(c, dt);
  }
}

void Net::afterPhysics(float dt) {
  if (isHost() && !remotes_.empty()) {
    for (auto& rp : remotes_) {
      int held = rp->weapons[rp->weapon]->heldProp();
      if (held >= 0) g_.world.highlights[held] = glm::vec4(0.3f, 0.7f, 1.0f, 0.9f);
    }
    structTimer_ += dt;
    if (structTimer_ >= 0.2f) {
      structTimer_ = 0;
      uint64_t h = structureHash();
      if (h != lastHash_) {
        lastHash_ = h;
        sendStructure(nullptr);
      }
    }
    snapTimer_ += dt;
    if (snapTimer_ >= 1.0f / 30.0f) {
      snapTimer_ = 0;
      sendSnapshot();
    }
    if (!pendingSounds_.empty()) {
      Writer w;
      w.u8(S_SOUND);
      size_t n = std::min<size_t>(pendingSounds_.size(), 32);
      w.u8((uint8_t)n);
      for (size_t i = 0; i < n; i++) {
        w.u8(pendingSounds_[i].id);
        w.vec3(pendingSounds_[i].pos);
        w.f32(pendingSounds_[i].vol);
        w.f32(pendingSounds_[i].pitch);
        w.u8(1);
      }
      broadcast(w, false, 2);
    }
  }
  pendingSounds_.clear();
  if (host_) enet_host_flush(host_);
}
