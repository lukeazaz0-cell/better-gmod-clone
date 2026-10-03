#include "game.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include "gl.h"
#include "net.h"
#include "ui.h"

namespace fs = std::filesystem;

static std::string defaultSaveDir() {
  std::string base;
  if (const char* x = getenv("XDG_DATA_HOME"); x && *x)
    base = x;
  else if (const char* h = getenv("HOME"); h && *h)
    base = std::string(h) + "/.local/share";
  else
    base = ".";
  return base + "/better-gmod-clone/saves";
}

Game::Game() = default;
Game::~Game() = default;

bool Game::init(int argc, char** argv) {
  int w = 1600, h = 900;
  bool fullscreen = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--width" && i + 1 < argc) w = atoi(argv[++i]);
    else if (a == "--height" && i + 1 < argc) h = atoi(argv[++i]);
    else if (a == "--fullscreen") fullscreen = true;
    else if (a == "--autotest") autotest_ = true;
    else if (a == "--shots" && i + 1 < argc) shotDir_ = argv[++i];
    else if (a == "--name" && i + 1 < argc) playerName = argv[++i];
    else if (a == "--host") startHost_ = (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) ? argv[++i] : "27015";
    else if (a == "--connect" && i + 1 < argc) startJoin_ = argv[++i];
    else if (a == "--autotest-host") netTest_ = 1, autotest_ = true;
    else if (a == "--autotest-client") netTest_ = 2, autotest_ = true;
    else if (a == "--help" || a == "-h") {
      printf(
          "Usage: gmodclone [options]\n"
          "  --width W --height H   window size\n"
          "  --fullscreen           borderless fullscreen\n"
          "  --name NAME            multiplayer name\n"
          "  --host [PORT]          host a multiplayer game (default port 27015)\n"
          "  --connect HOST[:PORT]  join a multiplayer game\n"
          "  --autotest             run the scripted single-player self test\n"
          "  --shots DIR            where autotest screenshots go\n");
      return false;
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
  Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  window_ = SDL_CreateWindow("Better GMod Clone", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
  if (!window_) {
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    window_ = SDL_CreateWindow("Better GMod Clone", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
  }
  if (!window_) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return false;
  }
  gl_ = SDL_GL_CreateContext(window_);
  if (!gl_) {
    fprintf(stderr, "Could not create an OpenGL 3.3 core context: %s\n", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(window_, gl_);
  SDL_GL_SetSwapInterval(autotest_ ? 0 : 1);
  printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
  glEnable(GL_MULTISAMPLE);

  setAssetDir(findAssetDir(argv[0]));
  if (assetDir().empty())
    fprintf(stderr, "Asset folder not found - imported models and sound files are disabled\n");
  else
    printf("Assets: %s\n", assetDir().c_str());

  if (!renderer.init()) {
    fprintf(stderr, "Renderer init failed\n");
    return false;
  }
  int dw, dh;
  SDL_GL_GetDrawableSize(window_, &dw, &dh);
  renderer.resize(dw, dh);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ui::setupStyle(*this);
  ImGui_ImplSDL2_InitForOpenGL(window_, gl_);
  ImGui_ImplOpenGL3_Init("#version 330");

  audio.init(assetDir());
  physics.init();
  world.audio = &audio;
  world.buildMap();
  player.init(physics, world.spawnPoint, world.spawnYaw);

  weapons.emplace_back(new PhysGun());
  weapons.emplace_back(new GravGun());
  weapons.emplace_back(new ToolGun());

  if (playerName == "Player")
    if (const char* u = getenv("USER"); u && *u) playerName = u;
  net = std::make_unique<Net>(*this);
  net->name = playerName;

  physics.onPreTick = [this](float dt) {
    world.preTick(dt);
    if (!net->isClient()) {
      GameCtx c = makeCtx();
      weapon()->preTick(c, dt);
    }
    net->preTick(dt);
  };
  world.onExplosion = [this](const glm::vec3& pos, float radius, float power) {
    net->broadcastExplosion(pos, radius, power);
    glm::vec3 d = player.center() - pos;
    float dist = glm::length(d);
    if (dist < radius * 2.0f) shake_ = std::max(shake_, 1.0f - dist / (radius * 2.0f));
    if (dist < radius && !player.noclip) {
      glm::vec3 dir = glm::normalize(d + glm::vec3(0, 0.5f, 0));
      player.body->setLinearVelocity(player.body->getLinearVelocity() + toBt(dir * power * 0.5f * (1 - dist / radius)));
    }
  };

  buildIcons();
  saveDir = defaultSaveDir();
  std::error_code ec;
  fs::create_directories(saveDir, ec);
  updateMouseMode();
  notify.push("Welcome! Hold Q to spawn things.");
  if (!startHost_.empty()) hostGame(atoi(startHost_.c_str()));
  if (!startJoin_.empty()) joinGame(startJoin_);
  return true;
}

void Game::buildIcons() {
  Uint64 t0 = SDL_GetPerformanceCounter();
  for (const PropDef& d : propCatalog()) {
    if (!propAvailable(d)) continue;
    std::vector<DrawCmd> cmds;
    if (d.special == Special::Ragdoll) {
      world.ragdollPreview(cmds);
    } else {
      DrawCmd c;
      c.mesh = assets.mesh(d.shape);
      c.color = d.color;
      c.material = d.material;
      c.model = glm::mat4(1);
      // Long thin things look better standing at an angle.
      if (d.shape.kind == ShapeKind::Cylinder && d.shape.size.y < d.shape.size.x * 0.5f)
        c.model = glm::rotate(glm::mat4(1), 1.2f, glm::vec3(1, 0, 0));
      cmds.push_back(c);
    }
    icons[d.id] = renderer.renderIcon(cmds, 128);
  }
  printf("Rendered %zu spawn icons in %.0f ms\n", icons.size(),
         (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());
}

bool Game::hostGame(int port) {
  std::string err;
  net->name = playerName;
  if (!net->host(port, err)) {
    notify.push(err, glm::vec4(1, 0.3f, 0.3f, 1));
    return false;
  }
  notify.push("Hosting on port " + std::to_string(port), glm::vec4(0.4f, 1, 0.5f, 1));
  return true;
}

bool Game::joinGame(const std::string& address) {
  std::string host = address;
  int port = net::kDefaultPort;
  size_t colon = address.rfind(':');
  if (colon != std::string::npos) {
    host = address.substr(0, colon);
    port = atoi(address.c_str() + colon + 1);
  }
  std::string err;
  net->name = playerName;
  if (!net->connect(host, port, err)) {
    notify.push(err, glm::vec4(1, 0.3f, 0.3f, 1));
    return false;
  }
  notify.push("Connecting to " + host + ":" + std::to_string(port) + "...");
  return true;
}

int Game::localHeld() { return net->isClient() ? net->self.held : weapon()->heldProp(); }

void Game::shutdown() {
  net.reset();
  for (auto& kv : icons) glDeleteTextures(1, &kv.second);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  world.shutdown();
  player.shutdown();
  physics.shutdown();
  assets.shutdown();
  renderer.shutdown();
  audio.shutdown();
  if (gl_) SDL_GL_DeleteContext(gl_);
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

GameCtx Game::makeCtx() {
  GameCtx c{world, player, physics, audio, notify, assets};
  c.eye = frameEye_;
  c.fwd = frameFwd_;
  c.muzzle = frameMuzzle_;
  return c;
}

void Game::updateMouseMode() {
  bool rel = !spawnMenu && !paused && !autotest_;
  SDL_SetRelativeMouseMode(rel ? SDL_TRUE : SDL_FALSE);
  ImGuiIO& io = ImGui::GetIO();
  if (rel)
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
  else
    io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange);
}

void Game::selectWeapon(int i) {
  if (i < 0 || i >= (int)weapons.size() || i == curWeapon) return;
  GameCtx c = makeCtx();
  weapon()->holster(c);
  curWeapon = i;
  audio.play(SND_CLICK, 0.5f);
}

void Game::doUndo() {
  if (net->isClient()) {
    net->sendUndo();
    return;
  }
  std::string label = world.undo(0);
  if (!label.empty()) {
    notify.push("Undone " + label);
    audio.play(SND_UNDO, 0.6f);
  } else {
    notify.push("Nothing to undo", glm::vec4(0.6f, 0.6f, 0.6f, 1));
  }
}

void Game::spawnProp(const PropDef& def) {
  if (net->isClient()) {
    net->sendSpawn(def.id, player.cam.pos, player.cam.forward(), player.yaw);
    return;
  }
  spawnPropFor(def, player.cam.pos, player.cam.forward(), player.yaw, 0, audio);
}

void Game::spawnPropFor(const PropDef& def, const glm::vec3& eye, const glm::vec3& fwd, float yaw, int owner,
                        SoundOut& sound) {
  RayHit h = physics.raycast(eye, eye + fwd * 80.0f, nullptr, COL_WORLD | COL_PROP);
  glm::vec3 pos = h.hit ? h.point : eye + fwd * 8.0f;
  glm::vec3 n = h.hit ? h.normal : glm::vec3(0, 1, 0);
  glm::quat rot = yawQuat(yaw + 3.14159265f);
  float d = 0.0f;
  if (def.special != Special::Ragdoll) {
    btCollisionShape* s = assets.shape(def.shape);
    btVector3 mn, mx;
    s->getAabb(btTransform(toBt(rot)), mn, mx);
    for (int i = 0; i < 8; i++) {
      glm::vec3 corner((i & 1) ? mx.x() : mn.x(), (i & 2) ? mx.y() : mn.y(), (i & 4) ? mx.z() : mn.z());
      d = std::max(d, -glm::dot(corner, n));
    }
  }
  pos += n * (d + 0.02f);
  UndoEntry u;
  u.label = def.special == Special::Ragdoll ? "Ragdoll" : def.name;
  u.owner = owner;
  world.spawn(def, makeXf(pos, rot), &u);
  world.pushUndo(u);
  sound.play(SND_SPAWN, 0.5f);
}

static std::string sanitize(const std::string& s) {
  std::string o;
  for (char c : s)
    if (isalnum((unsigned char)c) || c == '_' || c == '-') o += c;
  return o.empty() ? "unnamed" : o;
}

bool Game::saveGame(const std::string& name) {
  if (net->isClient()) {
    notify.push("Only the host can save", glm::vec4(1, 0.6f, 0.3f, 1));
    return false;
  }
  std::string path = saveDir + "/" + sanitize(name) + ".gsave";
  bool ok = world.saveFile(path, player.feet(), player.yaw, player.pitch);
  if (!ok) notify.push("Could not save to " + path, glm::vec4(1, 0.3f, 0.3f, 1));
  return ok;
}

bool Game::loadGame(const std::string& name) {
  if (net->isClient()) {
    notify.push("Only the host can load saves", glm::vec4(1, 0.6f, 0.3f, 1));
    return false;
  }
  std::string path = saveDir + "/" + sanitize(name) + ".gsave";
  GameCtx c = makeCtx();
  for (auto& w : weapons) w->holster(c);
  glm::vec3 pos = player.feet();
  float yaw = player.yaw, pitch = player.pitch;
  if (!world.loadFile(path, &pos, &yaw, &pitch)) {
    notify.push("Could not load " + name, glm::vec4(1, 0.3f, 0.3f, 1));
    return false;
  }
  player.setNoclip(false);
  player.teleport(pos, yaw, pitch);
  return true;
}

std::vector<std::string> Game::listSaves() const {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(saveDir, ec))
    if (e.path().extension() == ".gsave") out.push_back(e.path().stem().string());
  std::sort(out.begin(), out.end());
  return out;
}

void Game::deleteSave(const std::string& name) {
  std::error_code ec;
  fs::remove(saveDir + "/" + sanitize(name) + ".gsave", ec);
}

void Game::freezeAll(bool f) {
  for (auto& kv : world.props) world.setFrozen(*kv.second, f);
  notify.push(f ? "Froze everything" : "Unfroze everything");
}

void Game::respawn() {
  player.setNoclip(false);
  player.teleport(world.spawnPoint, world.spawnYaw, 0);
}

void Game::screenshot(const std::string& path) {
  pendingShot_ = path;
  wantScreenshot_ = true;
}

// ---------------------------------------------------------------------------
void Game::releaseAllInput() {
  for (int i = 0; i < SDL_NUM_SCANCODES; i++)
    if (input.down[i]) net->sendInput(0, i, false);
  for (int i = 0; i < 8; i++)
    if (input.mouseDown[i]) net->sendInput(1, i, false);
  input.releaseAll();
}

void Game::handleEvent(const SDL_Event& e) {
  if (e.type == SDL_TEXTINPUT && suppressText_) {  // the 'T' that opened the chat box
    suppressText_ = false;
    return;
  }
  ImGui_ImplSDL2_ProcessEvent(&e);
  ImGuiIO& io = ImGui::GetIO();
  bool playing = !spawnMenu && !paused;
  switch (e.type) {
    case SDL_QUIT: running = false; break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) {
        int dw, dh;
        SDL_GL_GetDrawableSize(window_, &dw, &dh);
        renderer.resize(dw, dh);
      } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !autotest_) {
        releaseAllInput();
        if (!spawnMenu) paused = true;
      }
      break;
    case SDL_KEYDOWN: {
      SDL_Scancode sc = e.key.keysym.scancode;
      if (chatOpen) {
        if (sc == SDL_SCANCODE_ESCAPE) chatOpen = false;
        break;
      }
      if (io.WantTextInput && sc != SDL_SCANCODE_ESCAPE) break;
      if (e.key.repeat) {
        if (sc == SDL_SCANCODE_Z && !paused) doUndo();
        break;
      }
      input.down[sc] = true;
      input.pressed[sc] = true;
      if (!paused) net->sendInput(0, sc, true);
      switch (sc) {
        case SDL_SCANCODE_T:
        case SDL_SCANCODE_RETURN:
          if (!paused && !spawnMenu) {
            chatOpen = true;
            chatBuf[0] = 0;
            suppressText_ = sc == SDL_SCANCODE_T;
            releaseAllInput();
          }
          break;
        case SDL_SCANCODE_ESCAPE:
          if (showHelp) showHelp = false;
          else if (spawnMenu) spawnMenu = false;
          else paused = !paused;
          break;
        case SDL_SCANCODE_Q:
          if (paused) break;
          if (!spawnMenu) {
            spawnMenu = true;
            qOpenTime_ = time;
            hintTimer = 0;
          } else {
            spawnMenu = false;
          }
          break;
        case SDL_SCANCODE_Z: if (!paused) doUndo(); break;
        case SDL_SCANCODE_V:
          if (!paused) {
            player.setNoclip(!player.noclip);
          }
          break;
        case SDL_SCANCODE_1: selectWeapon(0); break;
        case SDL_SCANCODE_2: selectWeapon(1); break;
        case SDL_SCANCODE_3: selectWeapon(2); break;
        case SDL_SCANCODE_F1: showHelp = !showHelp; break;
        case SDL_SCANCODE_F5:
          if (saveGame("quicksave")) notify.push("Quick saved", glm::vec4(0.4f, 1, 0.5f, 1));
          break;
        case SDL_SCANCODE_F9:
          if (loadGame("quicksave")) notify.push("Quick loaded", glm::vec4(0.4f, 1, 0.5f, 1));
          break;
        case SDL_SCANCODE_F12: {
          std::string dir = saveDir + "/../screenshots";
          std::error_code ec;
          fs::create_directories(dir, ec);
          char name[64];
          snprintf(name, sizeof(name), "/shot_%u.bmp", SDL_GetTicks());
          screenshot(dir + name);
          break;
        }
        default: break;
      }
      if (!paused) world.keyEvent(sc, true);
      break;
    }
    case SDL_KEYUP: {
      SDL_Scancode sc = e.key.keysym.scancode;
      if (input.down[sc]) net->sendInput(0, sc, false);
      input.down[sc] = false;
      input.released[sc] = true;
      if (sc == SDL_SCANCODE_Q && spawnMenu && time - qOpenTime_ > 0.3f && !io.WantTextInput) spawnMenu = false;
      break;
    }
    case SDL_MOUSEMOTION:
      if (playing) {
        input.mdx += (float)e.motion.xrel;
        input.mdy += (float)e.motion.yrel;
        net->addMotion((float)e.motion.xrel, (float)e.motion.yrel, 0);
      }
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (playing && e.button.button < 8) {
        input.mouseDown[e.button.button] = true;
        input.mousePressed[e.button.button] = true;
        net->sendToolSettings();
        net->sendInput(1, e.button.button, true);
      }
      break;
    case SDL_MOUSEBUTTONUP:
      if (e.button.button < 8) {
        if (input.mouseDown[e.button.button]) net->sendInput(1, e.button.button, false);
        input.mouseDown[e.button.button] = false;
        input.mouseReleased[e.button.button] = true;
      }
      break;
    case SDL_MOUSEWHEEL:
      if (playing) {
        int dy = e.wheel.y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
        bool consumes = net->isClient() ? (curWeapon == 0 && net->self.held >= 0) : weapon()->consumesWheel();
        if (consumes) {
          input.wheel += dy;
          net->addMotion(0, 0, dy);
        } else if (dy != 0) {
          int n = (int)weapons.size();
          selectWeapon(((curWeapon - (dy > 0 ? 1 : -1)) % n + n) % n);
        }
      }
      break;
  }
}

void Game::renderPlayers(const Camera& cam) {
  nameTags.clear();
  static const glm::vec3 kColors[] = {{1, 1, 1},       {0.6f, 0.8f, 1},  {1, 0.7f, 0.6f}, {0.7f, 1, 0.6f},
                                      {1, 0.95f, 0.5f}, {0.9f, 0.6f, 1}, {0.5f, 1, 1},    {1, 0.6f, 0.85f}};
  bool haveModel = modelExists("soldier");
  auto beamFor = [&](const PlayerView& v, const glm::vec3& muzzle) {
    if (v.beamType == 1) {
      std::vector<glm::vec3> pts;
      glm::vec3 fwd(std::cos(v.pitch) * std::sin(v.yaw), std::sin(v.pitch), -std::cos(v.pitch) * std::cos(v.yaw));
      glm::vec3 ctrl = v.eye + fwd * (glm::length(v.beamEnd - v.eye) * 0.5f);
      for (int i = 0; i <= 20; i++) {
        float t = i / 20.0f;
        pts.push_back((1 - t) * (1 - t) * muzzle + 2 * (1 - t) * t * ctrl + t * t * v.beamEnd);
      }
      renderer.beam(pts, glm::vec4(0.25f, 0.65f, 1.0f, 1.0f), 0.05f, true);
      renderer.beam(pts, glm::vec4(0.25f, 0.65f, 1.0f, 0.35f), 0.18f, true);
      renderer.light({v.beamEnd, glm::vec3(0.3f, 0.6f, 1.5f), 4.0f});
    } else if (v.beamType == 2) {
      renderer.beam({muzzle, v.beamEnd}, glm::vec4(1.0f, 0.6f, 0.2f, 0.5f), 0.12f, true);
    } else if (v.beamType == 3) {
      renderer.beam({muzzle, v.beamEnd}, glm::vec4(0.6f, 0.85f, 1.0f, 0.8f), 0.04f, true);
    }
    if (v.held >= 0) world.highlights[v.held] = glm::vec4(0.3f, 0.7f, 1.0f, 0.9f);
  };
  for (const PlayerView& v : net->others) {
    glm::vec3 col = kColors[v.id % 8];
    glm::mat4 base = glm::translate(glm::mat4(1), v.feet) *
                     glm::rotate(glm::mat4(1), 3.14159265f - v.yaw, glm::vec3(0, 1, 0)) *
                     glm::scale(glm::mat4(1), glm::vec3(1, v.crouch ? 0.65f : 1.0f, 1));
    if (haveModel) {
      DrawCmd c;
      c.mesh = assets.mesh(ShapeDesc{ShapeKind::Model, glm::vec3(1), "soldier"});
      c.model = base * glm::translate(glm::mat4(1), glm::vec3(0, 0.9f, 0));
      c.color = glm::vec4(col, 1);
      c.material = MAT_PAINTED;
      renderer.draw(c);
    } else {
      DrawCmd b;
      b.mesh = assets.mesh(ShapeDesc{ShapeKind::Capsule, glm::vec3(0.3f, 0.45f, 0), ""});
      b.model = base * glm::translate(glm::mat4(1), glm::vec3(0, 0.75f, 0));
      b.color = glm::vec4(col * 0.8f, 1);
      renderer.draw(b);
      DrawCmd hd;
      hd.mesh = assets.mesh(ShapeDesc{ShapeKind::Sphere, glm::vec3(0.18f, 0, 0), ""});
      hd.model = base * glm::translate(glm::mat4(1), glm::vec3(0, 1.62f, 0));
      hd.color = glm::vec4(0.9f, 0.75f, 0.6f, 1);
      renderer.draw(hd);
    }
    glm::vec3 fwd(std::cos(v.pitch) * std::sin(v.yaw), std::sin(v.pitch), -std::cos(v.pitch) * std::cos(v.yaw));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)) + glm::vec3(1e-4f, 0, 0));
    glm::vec3 muzzle = v.eye + right * 0.25f - glm::vec3(0, 0.25f, 0) + fwd * 0.5f;
    // held weapon
    DrawCmd w;
    w.mesh = assets.mesh(ShapeDesc{ShapeKind::Box, glm::vec3(0.05f, 0.06f, 0.25f), ""});
    w.model = glm::translate(glm::mat4(1), muzzle - fwd * 0.25f) *
              glm::mat4_cast(glm::quatLookAt(fwd, glm::vec3(0, 1, 0)));
    w.color = v.weapon == 0 ? glm::vec4(0.3f, 0.5f, 0.9f, 1)
                            : v.weapon == 1 ? glm::vec4(0.9f, 0.5f, 0.1f, 1) : glm::vec4(0.7f, 0.7f, 0.75f, 1);
    w.material = MAT_METAL;
    renderer.draw(w);
    beamFor(v, muzzle);
    if (glm::length(v.feet - cam.pos) < 60.0f) nameTags.push_back({v.feet + glm::vec3(0, 2.05f, 0), v.name});
  }
  if (net->isClient()) beamFor(net->self, frameMuzzle_);
}

void Game::playImpactSounds() {
  btDispatcher* disp = physics.world->getDispatcher();
  int played = 0;
  int maxLife = std::max(1, physics.lastSubSteps);
  for (int i = 0; i < disp->getNumManifolds() && played < 3; i++) {
    btPersistentManifold* m = disp->getManifoldByIndexInternal(i);
    const btCollisionObject* a = m->getBody0();
    const btCollisionObject* b = m->getBody1();
    if (a->getUserIndex() == BODY_PLAYER || b->getUserIndex() == BODY_PLAYER) continue;
    const btRigidBody* ra = btRigidBody::upcast(a);
    const btRigidBody* rb = btRigidBody::upcast(b);
    float ma = ra && ra->getInvMass() > 0 ? 1.0f / ra->getInvMass() : 1e9f;
    float mb = rb && rb->getInvMass() > 0 ? 1.0f / rb->getInvMass() : 1e9f;
    float mass = std::min(ma, mb);
    if (mass > 1e8f) continue;
    for (int j = 0; j < m->getNumContacts(); j++) {
      const btManifoldPoint& pt = m->getContactPoint(j);
      if (pt.getLifeTime() > maxLife) continue;
      float dv = (float)pt.getAppliedImpulse() / mass;  // velocity change of the lighter body
      if (dv < 1.2f) continue;
      float vol = std::min(1.0f, dv / 8.0f);
      static std::mt19937 rng(7);
      std::uniform_real_distribution<float> pr(0.85f, 1.15f);
      float pitch = pr(rng) * (mass < 5 ? 1.3f : mass > 80 ? 0.75f : 1.0f);
      audio.playAt(dv > 5.0f ? SND_IMPACT_HARD : SND_IMPACT_SOFT, toGlm(pt.getPositionWorldOnA()), vol, pitch);
      played++;
      break;
    }
  }
}

void Game::frame(float dt) {
  time += dt;
  fps = fps * 0.95f + (1.0f / std::max(dt, 1e-4f)) * 0.05f;
  notify.update(dt);
  if (hintTimer > 0) hintTimer -= dt;
  if (autotest_) {
    if (netTest_)
      netAutotestStep(dt);
    else
      autotestStep(dt);
  }
  updateMouseMode();

  ImGuiIO& io = ImGui::GetIO();
  bool client = net->isClient();
  bool playing = !spawnMenu && !paused && !chatOpen;
  bool controls = !paused && !io.WantTextInput && !chatOpen;
  static const Input kEmpty{};

  bool lockView = client ? (curWeapon == 0 && input.down[SDL_SCANCODE_E] && net->self.held >= 0) : weapon()->locksView();
  if (playing && !lockView) player.look(input.mdx, input.mdy, mouseSens);
  player.cam.fov = fov;
  player.update(controls ? input : kEmpty, dt, controls);

  Camera aim = player.cam;
  aim.yaw = player.yaw;
  aim.pitch = player.pitch;
  frameEye_ = aim.pos;
  frameFwd_ = aim.forward();
  frameMuzzle_ = weapon()->muzzleWorld(aim, player.bob);
  if (!client) {
    GameCtx c = makeCtx();
    weapon()->update(c, playing ? input : kEmpty, dt);
  } else {
    weapon()->updateSway(playing ? input : kEmpty, dt);
    if (Tool* t = toolgun()->tool()) t->setStage(net->self.toolStage);
  }
  net->update(dt);
  client = net->isClient();
  static bool keys[SDL_NUM_SCANCODES];
  memcpy(keys, controls ? input.down : kEmpty.down, sizeof(keys));
  if (net->isHost()) net->combineKeys(keys);
  world.update(dt, keys);
  if (!paused || !net->offline()) physics.step(dt);
  player.postPhysics(dt);
  if (!client) playImpactSounds();
  net->afterPhysics(dt);

  // Audio state
  audio.setListener(player.cam.pos, player.cam.right());
  bool physgunOn = client ? net->self.beamType == 1 : weapon()->active();
  audio.setLoop(LOOP_PHYSGUN, SND_HUM, curWeapon == 0 && physgunOn && !paused, 0.5f);
  audio.setLoop(LOOP_THRUST, SND_THRUST, world.anyThrusterOn() && !paused, 0.5f);
  audio.setLoop(LOOP_ENGINE, SND_ENGINE, world.anyMotorOn() && !paused, 0.45f);
  audio.setLoop(LOOP_FOOTSTEPS, SND_FOOTSTEPS, player.onGround && player.speedH > 1.0f && !paused, 0.35f,
                player.speedH > 6.0f ? 1.35f : 1.0f);
  audio.setLoop(LOOP_AMBIENCE, SND_AMBIENCE, !paused, 0.25f);
  if (player.jumped) audio.play(SND_JUMP, 0.35f);

  // Camera shake
  Camera cam = player.cam;
  if (shake_ > 0) {
    static std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-1, 1);
    cam.pos += glm::vec3(u(rng), u(rng), u(rng)) * shake_ * 0.12f;
    shake_ = std::max(0.0f, shake_ - dt * 1.5f);
  }

  // Render
  renderer.begin(cam, time);
  frameMuzzle_ = weapon()->muzzleWorld(cam, player.bob);
  frameEye_ = cam.pos;
  if (!client) {
    GameCtx c = makeCtx();
    weapon()->render(c, renderer);
  }
  renderPlayers(cam);
  world.render(renderer, cam);
  weapon()->renderViewmodel(renderer, assets, cam, player.bob);
  renderer.render();

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  ui::drawHUD(*this);
  if (spawnMenu) ui::drawSpawnMenu(*this);
  if (paused) ui::drawPauseMenu(*this);
  if (showHelp) ui::drawHelp(*this);
  if (chatOpen) ui::drawChat(*this);
  ImGui::Render();
  glViewport(0, 0, renderer.width, renderer.height);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  if (wantScreenshot_) {
    wantScreenshot_ = false;
    if (renderer.saveScreenshot(pendingShot_)) {
      printf("Saved screenshot %s\n", pendingShot_.c_str());
      if (!autotest_) notify.push("Screenshot saved");
    }
  }
  SDL_GL_SwapWindow(window_);
}

void Game::run() {
  Uint64 last = SDL_GetPerformanceCounter();
  double freq = (double)SDL_GetPerformanceFrequency();
  while (running) {
    input.beginFrame();
    SDL_Event e;
    while (SDL_PollEvent(&e)) handleEvent(e);
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = (float)((now - last) / freq);
    last = now;
    if (autotest_) dt = 1.0f / 60.0f;
    dt = std::min(dt, 0.1f);
    frame(dt);
  }
}

// ---------------------------------------------------------------------------
// Scripted self-test: builds a scene with the real tools, drives a car, blows
// things up, saves/loads, duplicates and takes screenshots. Run with --autotest.
// ---------------------------------------------------------------------------
int g_autotestFailures = 0;

void Game::autotestStep(float dt) {
  int f = autoFrame_++;
  auto check = [&](bool ok, const std::string& what) {
    printf("[autotest] %-48s %s\n", what.c_str(), ok ? "OK" : "FAIL");
    if (!ok) g_autotestFailures++;
  };
  auto findTool = [&](const char* name) {
    ToolGun* tg = toolgun();
    for (size_t i = 0; i < tg->tools.size(); i++)
      if (std::string(tg->tools[i]->name()) == name) {
        tg->select((int)i);
        return tg->tools[i].get();
      }
    return (Tool*)nullptr;
  };
  auto aim = [&](const glm::vec3& from, const glm::vec3& to) {
    RayHit r = physics.raycast(from, from + glm::normalize(to - from) * 100.0f, player.body);
    ToolHit h;
    h.hit = r.hit;
    h.pos = r.point;
    h.normal = r.normal;
    h.prop = r.hit ? world.propFromObject(r.object) : nullptr;
    h.world = r.hit && r.userIndex == BODY_WORLD;
    return h;
  };
  auto ctxFacing = [&](const glm::vec3& fwd) {
    GameCtx c = makeCtx();
    c.fwd = fwd;
    return c;
  };
  static int cartId = -1, topCrate = -1, ballId = -1;
  static glm::vec3 cartStart;
  static size_t savedProps = 0, savedJoints = 0;
  static size_t undoStartProps = 0;

  if (f == 0) {
    player.teleport(glm::vec3(0, 0.1f, 12), 0, -0.1f);
    UndoEntry u;
    u.label = "Autotest";
    for (int i = 0; i < 4; i++) {
      Prop* p = world.spawn(*findPropDef("crate_small"), makeXf({-4, 0.36f + 0.61f * i, 4}), &u);
      topCrate = p->id;
    }
    world.spawn(*findPropDef("barrel"), makeXf({-1.5f, 0.55f, 3}), &u);
    world.spawn(*findPropDef("oildrum"), makeXf({-2.2f, 0.55f, 3.4f}), &u);
    ballId = world.spawn(*findPropDef("ball_big"), makeXf({5, 1.0f, 3}), &u)->id;
    world.spawn(*findPropDef("ragdoll"), makeXf({2.5f, 0.05f, 6.5f}, yawQuat(3.14159f)), &u);
    world.spawn(*findPropDef("table"), makeXf({-6, 0.5f, 8}), &u);
    world.spawn(*findPropDef("chair"), makeXf({-4.8f, 0.6f, 8}), &u);
    world.spawn(*findPropDef("dev_cube"), makeXf({6, 0.6f, 7}), &u);
    world.spawn(*findPropDef("chromeball"), makeXf({4.5f, 0.5f, 8}), &u);
    world.spawn(*findPropDef("cone"), makeXf({1.0f, 0.4f, 8.5f}), &u);
    world.spawn(*findPropDef("dynamite"), makeXf({-3.2f, 0.2f, 4.9f}), &u);
    Prop* lamp = world.spawn(*findPropDef("lamp"), makeXf({-1.0f, 0.3f, 7.0f}), &u);
    lamp->color = glm::vec4(1.0f, 0.4f, 0.2f, 1);
    cartId = world.spawn(*findPropDef("cart"), makeXf({0, 1.3f, 0}), &u)->id;
    world.pushUndo(u);
    check(world.props.size() == 26, "spawned props (incl. 11 ragdoll parts)");
    // Imported models (only when the asset folder is present)
    struct M {
      const char* id;
      glm::vec3 pos;
      float yaw;
    };
    const M models[] = {{"truck_red", {-9, 1.2f, 2}, 0.6f},  {"statue", {9, 1.1f, 5}, -0.5f},
                        {"tree", {-11, 1.9f, 9}, 0},        {"stairs", {10, 0.6f, -3}, 1.57f},
                        {"house_a", {-16, 2.3f, -6}, 0.3f},  {"question_block", {7, 3.0f, 9}, 0.2f},
                        {"motorcycle", {-6, 0.7f, -2}, 1.0f}, {"fountain", {16, 1.2f, 8}, 0}};
    UndoEntry mu;
    int placed = 0;
    for (const M& m : models)
      if (const PropDef* d = findPropDef(m.id); d && propAvailable(*d)) {
        world.spawn(*d, makeXf(m.pos, yawQuat(m.yaw)), &mu);
        placed++;
      }
    world.pushUndo(mu);
    modelProps_ = placed;
    if (!assetDir().empty()) check(placed == 8, "spawned imported models");
    check(world.joints.size() == 10, "ragdoll has 10 joints");
  }
  if (f == 40) {
    Prop* cart = world.prop(cartId);
    glm::vec3 c = cart->pos();
    Tool* wheel = findTool("Wheel");
    for (float sx : {-0.85f, 0.85f})
      for (float sz : {-1.0f, 1.0f}) {
        GameCtx cx = ctxFacing(glm::vec3(1, 0, 0));  // "forward" = +X for this cart
        ToolHit h = aim(c + glm::vec3(sx, 0.15f, sz * 3.0f), c + glm::vec3(sx, 0.15f, 0));
        check(h.prop && h.prop->id == cartId && wheel->primary(cx, h), "attach wheel");
      }
    Tool* balloon = findTool("Balloon");
    Prop* top = world.prop(topCrate);
    {
      GameCtx cx = makeCtx();
      ToolHit h = aim(top->pos() + glm::vec3(0, 3, 0.05f), top->pos());
      check(h.prop && balloon->primary(cx, h), "attach balloon");
    }
    Tool* rope = findTool("Rope");
    {
      GameCtx cx = makeCtx();
      Prop* ball = world.prop(ballId);
      ToolHit a = aim(ball->pos() + glm::vec3(0, 3, 0), ball->pos());
      ToolHit b = aim(glm::vec3(5, 3, 0), glm::vec3(5, -1, 0));
      check(rope->primary(cx, a) && rope->primary(cx, b), "rope ball to ground");
    }
    Tool* thr = findTool("Thruster");
    {
      GameCtx cx = makeCtx();
      ToolHit h = aim(glm::vec3(6, 0.6f, 9), glm::vec3(6, 0.6f, 7));
      check(h.prop && thr->primary(cx, h), "attach thruster to dev cube");
    }
  }
  if (f == 160) {
    cartStart = world.prop(cartId)->pos();
    input.down[SDL_SCANCODE_UP] = true;  // drives wheels + thruster
  }
  if (f == 280) {
    input.down[SDL_SCANCODE_UP] = false;
    glm::vec3 d = world.prop(cartId)->pos() - cartStart;
    printf("[autotest] cart moved (%.2f, %.2f, %.2f)\n", d.x, d.y, d.z);
    check(d.x > 1.5f && std::fabs(d.z) < std::fabs(d.x) * 0.5f, "wheels drive the cart forward (+X)");
    player.teleport(glm::vec3(4, 0.1f, 16), glm::radians(-15.0f), glm::radians(-12.0f));
  }
  if (f == 285) screenshot(shotDir_ + "/autotest_scene.bmp");
  if (f == 290) {
    size_t before = world.props.size();
    world.keyEvent(SDL_SCANCODE_B, true);  // default dynamite key
    check(world.props.size() == before - 1, "dynamite detonates");
  }
  if (f == 296) screenshot(shotDir_ + "/autotest_explosion.bmp");
  // Physics gun: grab a crate, swing it around, freeze it, unfreeze it.
  static int pgCrate = -1;
  auto lookAt = [&](const glm::vec3& target) {
    glm::vec3 d = target - player.cam.pos;
    player.yaw = std::atan2(d.x, -d.z);
    player.pitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
  };
  if (f == 300) {
    selectWeapon(0);
    player.setNoclip(false);
    player.teleport(glm::vec3(10, 0.1f, 20), 0, 0);
    UndoEntry u;
    pgCrate = world.spawn(*findPropDef("crate_small"), makeXf({10, 0.32f, 16}), &u)->id;
    world.pushUndo(u);
  }
  if (f == 305) lookAt(world.prop(pgCrate)->pos());
  if (f == 306) {
    input.mouseDown[SDL_BUTTON_LEFT] = true;
    input.mousePressed[SDL_BUTTON_LEFT] = true;
  }
  if (f == 307) check(physgun()->held() == pgCrate, "physgun grabs the crate");
  if (f > 307 && f < 330) {
    player.yaw += 0.03f;
    player.pitch += 0.015f;
  }
  if (f == 331) {
    glm::vec3 p = world.prop(pgCrate)->pos();
    printf("[autotest] crate carried to (%.2f %.2f %.2f)\n", p.x, p.y, p.z);
    check(p.x > 11.0f && p.y > 1.0f, "crate follows the physgun");
    input.mousePressed[SDL_BUTTON_RIGHT] = true;
  }
  if (f == 332) {
    check(world.prop(pgCrate)->frozen && physgun()->held() < 0, "right click freezes it");
    input.mouseDown[SDL_BUTTON_LEFT] = false;
    input.mouseReleased[SDL_BUTTON_LEFT] = true;
  }
  if (f == 340) {
    glm::vec3 before = world.prop(pgCrate)->pos();
    check(world.prop(pgCrate)->frozen, "frozen crate stays put");
    lookAt(before);
  }
  if (f == 341) input.pressed[SDL_SCANCODE_R] = true;
  if (f == 342) check(!world.prop(pgCrate)->frozen, "R unfreezes it");
  if (f == 360) {
    savedProps = world.props.size();
    savedJoints = world.joints.size();
    check(saveGame("autotest"), "save game");
    world.clearProps();
    check(world.props.empty(), "clear props");
    check(loadGame("autotest"), "load game");
    check(world.props.size() == savedProps, "same prop count after load (" + std::to_string(savedProps) + ")");
    check(world.joints.size() == savedJoints, "same constraint count after load (" + std::to_string(savedJoints) + ")");
    deleteSave("autotest");
  }
  if (f == 365) {
    // find the cart again (ids change on load)
    cartId = -1;
    for (auto& kv : world.props)
      if (kv.second->type == "cart") cartId = kv.first;
    check(cartId > 0, "cart survived save/load");
    Tool* dup = findTool("Duplicator");
    size_t before = world.props.size();
    GameCtx cx = makeCtx();
    Prop* cart = world.prop(cartId);
    ToolHit h = aim(cart->pos() + glm::vec3(0, 3, 0), cart->pos());
    ToolHit g = aim(glm::vec3(12, 3, 2), glm::vec3(12, -1, 2));
    check(dup->secondary(cx, h) && dup->primary(cx, g), "duplicate the car");
    check(world.props.size() == before + 5, "duplicated 5 props (cart + 4 wheels)");
    undoStartProps = before;
    player.teleport(glm::vec3(-14, 6, 22), glm::radians(30.0f), glm::radians(-18.0f));
    player.setNoclip(true);
  }
  if (f == 400) screenshot(shotDir_ + "/autotest_overview.bmp");
  if (f == 405) {
    spawnMenu = true;
    selectWeapon(2);
    findTool("Wheel");
  }
  if (f == 408) screenshot(shotDir_ + "/autotest_menu.bmp");
  if (f == 410) {
    spawnMenu = false;
    doUndo();
    check(world.props.size() == undoStartProps, "undo removes the duplication");
  }
  if (f == 412) {
    player.setNoclip(false);
    player.teleport(glm::vec3(2, 0.1f, 14), glm::radians(-40.0f), glm::radians(-5.0f));
  }
  if (f == 418) screenshot(shotDir_ + "/autotest_toolgun.bmp");
  if (f == 425) {
    printf("[autotest] finished with %d failure(s)\n", g_autotestFailures);
    running = false;
  }
}

// ---------------------------------------------------------------------------
// Two-process multiplayer self-test:
//   gmodclone --host 27999 --autotest-host   and   gmodclone --connect 127.0.0.1:27999 --autotest-client
// ---------------------------------------------------------------------------
void Game::netAutotestStep(float dt) {
  int f = autoFrame_++;
  static int stage = 0, stageFrame = 0, target = -1;
  static float startY = 0;
  auto check = [&](bool ok, const std::string& what) {
    printf("[netest-%s] %-46s %s\n", netTest_ == 1 ? "host" : "client", what.c_str(), ok ? "OK" : "FAIL");
    fflush(stdout);
    if (!ok) g_autotestFailures++;
  };
  auto next = [&]() {
    stage++;
    stageFrame = f;
  };
  auto lookAt = [&](const glm::vec3& t) {
    glm::vec3 d = t - player.cam.pos;
    player.yaw = std::atan2(d.x, -d.z);
    player.pitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
  };
  auto chatHas = [&](const std::string& s) {
    for (auto& c : net->chatLines)
      if (c.text.find(s) != std::string::npos) return true;
    return false;
  };
  int waited = f - stageFrame;
  if (f > 5000) {
    check(false, "timed out in stage " + std::to_string(stage));
    running = false;
    return;
  }

  if (netTest_ == 1) {  // ---------------- host ----------------
    if (stage == 0) {
      player.teleport(glm::vec3(0, 0.1f, 18), 0, -0.1f);
      UndoEntry u;
      for (int i = 0; i < 5; i++) world.spawn(*findPropDef("crate_small"), makeXf({-2.0f + i, 0.32f, 8}), &u);
      world.pushUndo(u);
      check(net->isHost(), "hosting");
      next();
    } else if (stage == 1) {
      if (net->remoteCount() > 0) {
        check(true, "client connected");
        next();
      }
    } else if (stage == 2) {
      bool spawned = false, lifted = false, boom = false;
      for (auto& kv : world.props) {
        if (kv.second->type == "ball") spawned = true;
        if (kv.second->type == "crate_small" && kv.second->pos().y > 1.5f) lifted = true;
      }
      static bool sawSpawn = false, sawLift = false;
      if (spawned && !sawSpawn) check(sawSpawn = true, "client spawned a prop");
      if (lifted && !sawLift) check(sawLift = true, "client lifted a crate with the physgun");
      boom = chatHas("hello from client");
      if (sawSpawn && sawLift && boom) {
        check(true, "got chat from client");
        player.teleport(glm::vec3(3, 0.1f, 18), glm::radians(10.0f), -0.15f);
        next();
      }
    } else if (stage == 3 && waited == 20) {
      screenshot(shotDir_ + "/net_host.bmp");
    } else if (stage == 3 && waited == 30) {
      net->chat("bye from host");
    } else if (stage == 3 && waited == 120) {
      printf("[netest-host] finished with %d failure(s)\n", g_autotestFailures);
      running = false;
    }
    return;
  }

  // ---------------- client ----------------
  if (stage == 0) {
    if (net->connected() && world.props.size() >= 5) {
      check(true, "connected and received the world");
      player.teleport(glm::vec3(1, 0.1f, 13), 0, 0);
      next();
    }
  } else if (stage == 1 && waited == 10) {
    lookAt(glm::vec3(3, 0, 10));
    spawnProp(*findPropDef("ball"));
    next();
  } else if (stage == 2) {
    for (auto& kv : world.props)
      if (kv.second->type == "ball") {
        check(true, "spawned prop replicated back");
        selectWeapon(0);
        for (auto& kv2 : world.props)
          if (kv2.second->type == "crate_small" && target < 0) target = kv2.first;
        next();
        break;
      }
  } else if (stage == 3) {
    Prop* p = world.prop(target);
    if (!p) return;
    if (waited == 5) {
      lookAt(p->pos());
      startY = p->pos().y;
    }
    if (waited == 8) net->sendInput(1, SDL_BUTTON_LEFT, true);
    if (waited == 30) check(net->self.held >= 0, "host says we hold the crate");
    if (waited > 30 && waited < 70) player.pitch = std::min(1.2f, player.pitch + 0.02f);
    if (waited == 90) {
      float y = p->pos().y;
      printf("[netest-client] crate y %.2f -> %.2f\n", startY, y);
      check(y > startY + 1.0f, "crate movement replicated");
      net->sendInput(1, SDL_BUTTON_LEFT, false);
      next();
    }
  } else if (stage == 4) {
    if (waited == 5) {
      selectWeapon(2);
      ToolGun* tg = toolgun();
      for (size_t i = 0; i < tg->tools.size(); i++)
        if (std::string(tg->tools[i]->name()) == "Dynamite") tg->select((int)i);
      lookAt(glm::vec3(-1, 0, 9.5f));
    }
    if (waited == 15) {
      net->sendToolSettings(true);
      net->sendInput(1, SDL_BUTTON_LEFT, true);
    }
    if (waited == 17) net->sendInput(1, SDL_BUTTON_LEFT, false);
    if (waited > 17) {
      for (auto& kv : world.props)
        if (kv.second->type == "dynamite") {
          check(true, "placed dynamite with the tool gun");
          next();
          break;
        }
    }
  } else if (stage == 5) {
    if (waited == 10) {
      net->sendInput(0, SDL_SCANCODE_B, true);
      net->sendInput(0, SDL_SCANCODE_B, false);
    }
    if (waited > 10 && !world.effects.empty()) {
      check(true, "dynamite explosion replicated");
      lookAt(glm::vec3(0, 1.0f, 18));
      next();
    }
  } else if (stage == 6) {
    if (waited == 30) screenshot(shotDir_ + "/net_client.bmp");
    if (waited == 35) net->chat("hello from client");
    if (waited > 35 && chatHas("bye from host")) {
      check(true, "got chat from host");
      next();
    }
  } else if (stage == 7) {
    printf("[netest-client] finished with %d failure(s)\n", g_autotestFailures);
    running = false;
  }
}
