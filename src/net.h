#pragma once
// Multiplayer: listen-server model over ENet (UDP).
//
// The host simulates everything. Clients send their position/aim plus button
// events; the host runs a full set of weapons for each client, so every tool
// works in multiplayer. The host replicates the world as "structure" messages
// (reliable, whenever props/constraints change) and transform snapshots
// (unreliable, 30 Hz) that clients interpolate.
#include <enet/enet.h>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "audio.h"
#include "input.h"
#include "player.h"
#include "weapons.h"

class Game;

namespace net {
constexpr uint32_t kProtocol = 1;
constexpr int kDefaultPort = 27015;
constexpr int kMaxPlayers = 16;

enum Msg : uint8_t {
  C_HELLO = 1,
  C_STATE,
  C_INPUT,
  C_SPAWN,
  C_UNDO,
  C_TOOLSET,
  C_CHAT,
  S_WELCOME = 64,
  S_REJECT,
  S_STRUCTURE,
  S_SNAPSHOT,
  S_SOUND,
  S_EXPLOSION,
  S_NOTIFY,
  S_CHAT,
  S_PLAYERS,
};

struct Writer {
  std::vector<uint8_t> b;
  template <class T>
  void put(const T& v) {
    size_t o = b.size();
    b.resize(o + sizeof(T));
    memcpy(b.data() + o, &v, sizeof(T));
  }
  void u8(uint8_t v) { put(v); }
  void f32(float v) { put(v); }
  void i32(int32_t v) { put(v); }
  void vec3(const glm::vec3& v) { put(v.x), put(v.y), put(v.z); }
  void str(const std::string& s) {
    put((uint32_t)s.size());
    b.insert(b.end(), s.begin(), s.end());
  }
};

struct Reader {
  const uint8_t* p;
  size_t n, i = 0;
  bool ok = true;
  Reader(const uint8_t* d, size_t len) : p(d), n(len) {}
  template <class T>
  T get() {
    T v{};
    if (i + sizeof(T) > n) {
      ok = false;
      return v;
    }
    memcpy(&v, p + i, sizeof(T));
    i += sizeof(T);
    return v;
  }
  uint8_t u8() { return get<uint8_t>(); }
  float f32() { return get<float>(); }
  int32_t i32() { return get<int32_t>(); }
  glm::vec3 vec3() {
    float x = f32(), y = f32(), z = f32();
    return {x, y, z};
  }
  std::string str() {
    uint32_t len = get<uint32_t>();
    if (!ok || i + len > n) {
      ok = false;
      return "";
    }
    std::string s((const char*)p + i, len);
    i += len;
    return s;
  }
};
}  // namespace net

// What other players look like (built on the host from its clients, on clients from snapshots).
struct PlayerView {
  int id = 0;
  std::string name;
  glm::vec3 feet{0}, eye{0};
  float yaw = 0, pitch = 0;
  int weapon = 0;
  int beamType = 0;  // 0 none, 1 physgun, 2 gravgun, 3 tool shot
  glm::vec3 beamEnd{0};
  int held = -1;  // local prop id
  bool noclip = false, crouch = false;
  int toolStage = 0;
};

struct ChatLine {
  std::string text;
  float t = 0;
  glm::vec4 color{1};
};

class Net {
 public:
  explicit Net(Game& g);
  ~Net();

  bool host(int port, std::string& err);
  bool connect(const std::string& address, int port, std::string& err);
  void disconnect();

  bool offline() const { return mode_ == Mode::Offline; }
  bool isHost() const { return mode_ == Mode::Host; }
  bool isClient() const { return mode_ == Mode::Client; }
  bool connected() const { return isClient() && myId_ >= 0; }

  void update(float dt);        // before physics
  void preTick(float dt);       // inside physics substeps (host)
  void afterPhysics(float dt);  // after physics

  // Client -> host requests
  void sendSpawn(const std::string& propId, const glm::vec3& eye, const glm::vec3& fwd, float yaw);
  void sendUndo();
  void sendInput(uint8_t kind, int code, bool down);  // kind 0 = key, 1 = mouse button
  void sendToolSettings(bool force = false);
  void chat(const std::string& text);
  void broadcastExplosion(const glm::vec3& pos, float radius, float power);
  void addMotion(float dx, float dy, int wheel) {  // client: mouse motion for physgun rotation / distance
    mdxAcc_ += dx;
    mdyAcc_ += dy;
    wheelAcc_ += wheel;
  }
  void combineKeys(bool* keys) const;  // host: OR every client's held keys into keys[] (thrusters, wheels...)
  int remoteCount() const { return (int)remotes_.size(); }

  std::vector<PlayerView> others;  // everyone except the local player
  PlayerView self;                 // client: our own state as seen by the host
  std::vector<std::string> playerList;
  std::deque<ChatLine> chatLines;
  std::string name = "Player";
  std::string status;
  int myId() const { return myId_; }
  int port() const { return port_; }
  float sinceLastPacket() const { return sincePacket_; }

 private:
  enum class Mode { Offline, Host, Client };

  struct RemoteSink : SoundOut {
    Net* net = nullptr;
    ENetPeer* peer = nullptr;
    void play(SoundId id, float volume, float pitch) override;
    void playAt(SoundId id, const glm::vec3& pos, float volume, float pitch) override;
  };

  struct Remote {
    int id = 0;
    std::string name;
    ENetPeer* peer = nullptr;
    Player player;
    std::vector<std::unique_ptr<Weapon>> weapons;
    int weapon = 0;
    Input input;
    Notifier notify;
    RemoteSink sink;
    glm::vec3 feet{0}, eye{0}, fwd{0, 0, -1};
    float yaw = 0, pitch = 0;
    bool noclip = false, crouch = false;
  };

  void send(ENetPeer* peer, const net::Writer& w, bool reliable, int channel);
  void broadcast(const net::Writer& w, bool reliable, int channel);
  void hostReceive(ENetPeer* peer, const uint8_t* data, size_t len);
  void clientReceive(const uint8_t* data, size_t len);
  void addRemote(ENetPeer* peer, const std::string& name);
  void removeRemote(ENetPeer* peer);
  Remote* remote(ENetPeer* peer);
  GameCtx remoteCtx(Remote& r);
  void sendStructure(ENetPeer* to);
  uint64_t structureHash() const;
  void sendSnapshot();
  void sendPlayers();
  void pushChat(const std::string& text, const glm::vec4& color);
  void buildViews();
  void leaveClientMode(const std::string& why);

  Game& g_;
  Mode mode_ = Mode::Offline;
  ENetHost* host_ = nullptr;
  ENetPeer* server_ = nullptr;
  int myId_ = -1, nextId_ = 1, port_ = 0;
  std::vector<std::unique_ptr<Remote>> remotes_;

  // host replication state
  float snapTimer_ = 0, structTimer_ = 0;
  uint64_t lastHash_ = 0;
  int snapCount_ = 0;
  struct Sent {
    glm::vec3 pos;
    glm::quat rot;
    uint8_t flags;
  };
  std::map<int, Sent> lastSent_;
  struct PendingSound {
    uint8_t id;
    glm::vec3 pos;
    float vol, pitch;
  };
  std::vector<PendingSound> pendingSounds_;

  // client state
  float stateTimer_ = 0, sincePacket_ = 0;
  float mdxAcc_ = 0, mdyAcc_ = 0;
  int wheelAcc_ = 0;
  std::map<int, int> hostToLocal_;
  std::map<int, std::string> sentToolSettings_;
};
