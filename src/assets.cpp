#include "assets.h"
#include <SDL.h>
#include <BulletCollision/CollisionShapes/btShapeHull.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

const char* materialName(int m) {
  static const char* names[MAT_COUNT] = {"Plastic", "Dev Grid", "Wood",   "Crate",  "Metal",  "Concrete", "Grass",
                                         "Checker", "Rubber",   "Glow",   "Chrome", "Brick",  "Painted"};
  return (m >= 0 && m < MAT_COUNT) ? names[m] : "?";
}

static const char* kKindNames[] = {"box", "sphere", "cylinder", "capsule", "cone",
                                   "wedge", "torus", "compound", "model"};

const char* shapeKindName(ShapeKind k) { return kKindNames[(int)k]; }

ShapeKind shapeKindFromName(const std::string& s) {
  for (int i = 0; i < 9; i++)
    if (s == kKindNames[i]) return (ShapeKind)i;
  return ShapeKind::Box;
}

std::string ShapeDesc::key() const {
  char buf[160];
  snprintf(buf, sizeof(buf), "%d:%.4f:%.4f:%.4f:%s", (int)kind, size.x, size.y, size.z, compound.c_str());
  return buf;
}

// ---------------------------------------------------------------------------
// Compound props (built out of boxes, origin roughly at centre of mass)
// ---------------------------------------------------------------------------
static const std::map<std::string, std::vector<SubBox>>& compounds() {
  static std::map<std::string, std::vector<SubBox>> c = [] {
    std::map<std::string, std::vector<SubBox>> m;
    // table: 1.6 x 0.8 x 0.9
    m["table"] = {
        {{0, 0.37f, 0}, {0.8f, 0.03f, 0.45f}},
        {{-0.72f, -0.03f, -0.38f}, {0.04f, 0.37f, 0.04f}},
        {{0.72f, -0.03f, -0.38f}, {0.04f, 0.37f, 0.04f}},
        {{-0.72f, -0.03f, 0.38f}, {0.04f, 0.37f, 0.04f}},
        {{0.72f, -0.03f, 0.38f}, {0.04f, 0.37f, 0.04f}},
    };
    m["chair"] = {
        {{0, 0, 0}, {0.25f, 0.03f, 0.25f}},
        {{-0.21f, -0.25f, -0.21f}, {0.025f, 0.22f, 0.025f}},
        {{0.21f, -0.25f, -0.21f}, {0.025f, 0.22f, 0.025f}},
        {{-0.21f, -0.25f, 0.21f}, {0.025f, 0.22f, 0.025f}},
        {{0.21f, -0.25f, 0.21f}, {0.025f, 0.22f, 0.025f}},
        {{0, 0.3f, 0.225f}, {0.25f, 0.27f, 0.025f}},
    };
    m["shelf"] = {
        {{-0.58f, 0, 0}, {0.02f, 1.0f, 0.2f}},  {{0.58f, 0, 0}, {0.02f, 1.0f, 0.2f}},
        {{0, -0.98f, 0}, {0.56f, 0.02f, 0.2f}}, {{0, -0.33f, 0}, {0.56f, 0.02f, 0.2f}},
        {{0, 0.33f, 0}, {0.56f, 0.02f, 0.2f}},  {{0, 0.98f, 0}, {0.56f, 0.02f, 0.2f}},
        {{0, 0, -0.19f}, {0.56f, 1.0f, 0.01f}},
    };
    m["bathtub"] = {
        {{0, -0.25f, 0}, {0.85f, 0.04f, 0.4f}},  {{-0.82f, 0, 0}, {0.04f, 0.28f, 0.4f}},
        {{0.82f, 0, 0}, {0.04f, 0.28f, 0.4f}},   {{0, 0, -0.37f}, {0.85f, 0.28f, 0.04f}},
        {{0, 0, 0.37f}, {0.85f, 0.28f, 0.04f}},
    };
    m["fence"] = {
        {{-1.2f, 0, 0}, {0.05f, 0.6f, 0.05f}}, {{0, 0, 0}, {0.05f, 0.6f, 0.05f}},
        {{1.2f, 0, 0}, {0.05f, 0.6f, 0.05f}},  {{0, 0.35f, 0}, {1.25f, 0.06f, 0.02f}},
        {{0, -0.15f, 0}, {1.25f, 0.06f, 0.02f}},
    };
    m["bench"] = {
        {{0, 0.05f, 0}, {0.9f, 0.04f, 0.2f}},
        {{-0.75f, -0.17f, 0}, {0.04f, 0.18f, 0.18f}},
        {{0.75f, -0.17f, 0}, {0.04f, 0.18f, 0.18f}},
    };
    m["cart"] = {  // flat-bed chassis, good base for wheels
        {{0, 0, 0}, {1.2f, 0.06f, 0.7f}},       {{0, 0.2f, -0.68f}, {1.2f, 0.16f, 0.02f}},
        {{0, 0.2f, 0.68f}, {1.2f, 0.16f, 0.02f}}, {{-1.18f, 0.2f, 0}, {0.02f, 0.16f, 0.7f}},
        {{1.18f, 0.2f, 0}, {0.02f, 0.16f, 0.7f}},
    };
    m["dumpster"] = {
        {{0, -0.55f, 0}, {1.0f, 0.05f, 0.6f}},  {{-0.97f, 0, 0}, {0.03f, 0.6f, 0.6f}},
        {{0.97f, 0, 0}, {0.03f, 0.6f, 0.6f}},   {{0, 0, -0.57f}, {1.0f, 0.6f, 0.03f}},
        {{0, 0, 0.57f}, {1.0f, 0.6f, 0.03f}},
    };
    return m;
  }();
  return c;
}

const std::vector<SubBox>* compoundParts(const std::string& name) {
  auto it = compounds().find(name);
  return it == compounds().end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// Imported models
// ---------------------------------------------------------------------------
static std::string g_assetDir;

std::string findAssetDir(const char* argv0) {
  namespace fs = std::filesystem;
  std::vector<fs::path> candidates;
  if (const char* e = getenv("GMODCLONE_ASSETS"); e && *e) candidates.push_back(e);
  if (char* base = SDL_GetBasePath()) {
    candidates.push_back(fs::path(base) / "assets");
    candidates.push_back(fs::path(base) / ".." / "assets");
    candidates.push_back(fs::path(base) / ".." / "share" / "better-gmod-clone" / "assets");
    SDL_free(base);
  }
  candidates.push_back("assets");
#ifdef GMOD_ASSET_DIR
  candidates.push_back(GMOD_ASSET_DIR);
#endif
  (void)argv0;
  for (auto& c : candidates) {
    std::error_code ec;
    if (fs::is_directory(c / "models", ec) || fs::is_directory(c / "sounds", ec))
      return fs::weakly_canonical(c, ec).string();
  }
  return "";
}

void setAssetDir(const std::string& dir) { g_assetDir = dir; }
const std::string& assetDir() { return g_assetDir; }

static std::map<std::string, std::unique_ptr<MeshData>>& modelCache() {
  static std::map<std::string, std::unique_ptr<MeshData>> c;
  return c;
}

const MeshData* loadModel(const std::string& name) {
  auto& cache = modelCache();
  auto it = cache.find(name);
  if (it != cache.end()) return it->second.get();
  std::unique_ptr<MeshData> md;
  if (!g_assetDir.empty()) {
    std::ifstream f(g_assetDir + "/models/" + name + ".gmd", std::ios::binary);
    char magic[4];
    uint32_t nv = 0, ni = 0;
    if (f.read(magic, 4) && memcmp(magic, "GMD1", 4) == 0 && f.read((char*)&nv, 4) && f.read((char*)&ni, 4) &&
        nv < 10000000 && ni < 30000000) {
      md = std::make_unique<MeshData>();
      md->verts.resize(nv);
      for (uint32_t i = 0; i < nv && f; i++) {
        float pn[6];
        unsigned char c[4];
        f.read((char*)pn, sizeof(pn));
        f.read((char*)c, 4);
        Vertex& v = md->verts[i];
        v.pos = glm::vec3(pn[0], pn[1], pn[2]);
        glm::vec3 n(pn[3], pn[4], pn[5]);
        v.normal = glm::length(n) > 1e-6f ? glm::normalize(n) : glm::vec3(0, 1, 0);
        v.color = glm::vec3(c[0], c[1], c[2]) / 255.0f;
      }
      md->idx.resize(ni);
      f.read((char*)md->idx.data(), ni * 4);
      if (!f) {
        fprintf(stderr, "Truncated model file: %s\n", name.c_str());
        md.reset();
      } else {
        for (uint32_t& i : md->idx)
          if (i >= nv) i = 0;
      }
    }
  }
  const MeshData* r = md.get();
  cache[name] = std::move(md);
  return r;
}

bool modelExists(const std::string& name) { return loadModel(name) != nullptr; }

// ---------------------------------------------------------------------------
// Spawn catalog
// ---------------------------------------------------------------------------
static PropDef def(const char* id, const char* name, const char* cat, ShapeKind k, glm::vec3 size, int mat,
                   glm::vec3 color, float mass, float restitution = 0.1f, float friction = 0.7f,
                   Special sp = Special::None, const char* compound = "") {
  PropDef d;
  d.id = id;
  d.name = name;
  d.category = cat;
  d.shape.kind = k;
  d.shape.size = size;
  d.shape.compound = compound;
  d.material = mat;
  d.color = glm::vec4(color, 1.0f);
  d.mass = mass;
  d.restitution = restitution;
  d.friction = friction;
  d.special = sp;
  return d;
}

static PropDef model(const char* name, const char* title, const char* cat, float mass) {
  return def(name, title, cat, ShapeKind::Model, {1, 1, 1}, MAT_PAINTED, {1, 1, 1}, mass, 0.15f, 0.8f,
             Special::None, name);
}

const std::vector<PropDef>& propCatalog() {
  using K = ShapeKind;
  static std::vector<PropDef> c = {
      // Construction
      def("crate_small", "Small Crate", "Construction", K::Box, {0.3f, 0.3f, 0.3f}, MAT_CRATE, {0.76f, 0.58f, 0.36f}, 25),
      def("crate_large", "Large Crate", "Construction", K::Box, {0.6f, 0.6f, 0.6f}, MAT_CRATE, {0.76f, 0.58f, 0.36f}, 90),
      def("plank", "Wood Plank", "Construction", K::Box, {1.5f, 0.05f, 0.2f}, MAT_WOOD, {0.78f, 0.62f, 0.42f}, 12),
      def("plank_long", "Long Plank", "Construction", K::Box, {3.0f, 0.05f, 0.2f}, MAT_WOOD, {0.78f, 0.62f, 0.42f}, 24),
      def("plate_1", "Plate 1x1", "Construction", K::Box, {0.5f, 0.025f, 0.5f}, MAT_METAL, {0.7f, 0.72f, 0.75f}, 20),
      def("plate_2", "Plate 2x2", "Construction", K::Box, {1.0f, 0.025f, 1.0f}, MAT_METAL, {0.7f, 0.72f, 0.75f}, 60),
      def("plate_2x4", "Plate 2x4", "Construction", K::Box, {1.0f, 0.025f, 2.0f}, MAT_METAL, {0.7f, 0.72f, 0.75f}, 110),
      def("plate_4", "Plate 4x4", "Construction", K::Box, {2.0f, 0.025f, 2.0f}, MAT_METAL, {0.7f, 0.72f, 0.75f}, 200),
      def("beam", "Steel Beam", "Construction", K::Box, {2.0f, 0.1f, 0.1f}, MAT_METAL, {0.55f, 0.35f, 0.3f}, 40),
      def("block", "Concrete Block", "Construction", K::Box, {0.4f, 0.4f, 0.4f}, MAT_CONCRETE, {0.75f, 0.75f, 0.72f}, 120),
      def("brick_wall", "Brick Wall", "Construction", K::Box, {1.5f, 1.0f, 0.1f}, MAT_BRICK, {0.7f, 0.35f, 0.25f}, 250),
      def("ramp", "Ramp", "Construction", K::Wedge, {1.5f, 0.5f, 1.0f}, MAT_WOOD, {0.8f, 0.65f, 0.45f}, 80),
      def("dev_cube", "Dev Cube", "Construction", K::Box, {0.5f, 0.5f, 0.5f}, MAT_DEV, {0.95f, 0.55f, 0.15f}, 50),
      def("cart", "Cart Chassis", "Construction", K::Compound, {1, 1, 1}, MAT_METAL, {0.3f, 0.45f, 0.7f}, 120, 0.1f, 0.7f, Special::None, "cart"),
      // Round things
      def("ball", "Ball", "Round", K::Sphere, {0.25f, 0, 0}, MAT_RUBBER, {0.85f, 0.15f, 0.12f}, 4, 0.75f, 0.9f),
      def("ball_big", "Big Ball", "Round", K::Sphere, {0.75f, 0, 0}, MAT_PLASTIC, {0.2f, 0.5f, 0.9f}, 40, 0.5f),
      def("beachball", "Beach Ball", "Round", K::Sphere, {0.45f, 0, 0}, MAT_CHECKER, {1.0f, 0.9f, 0.3f}, 0.5f, 0.8f),
      def("chromeball", "Chrome Ball", "Round", K::Sphere, {0.4f, 0, 0}, MAT_CHROME, {0.9f, 0.9f, 0.95f}, 30, 0.3f),
      def("barrel", "Barrel", "Round", K::Cylinder, {0.3f, 0.45f, 0}, MAT_METAL, {0.2f, 0.35f, 0.65f}, 30),
      def("oildrum", "Oil Drum", "Round", K::Cylinder, {0.3f, 0.45f, 0}, MAT_METAL, {0.75f, 0.15f, 0.1f}, 35),
      def("pipe", "Pipe", "Round", K::Cylinder, {0.08f, 1.5f, 0}, MAT_METAL, {0.6f, 0.6f, 0.6f}, 15),
      def("cone", "Traffic Cone", "Round", K::Cone, {0.22f, 0.7f, 0}, MAT_PLASTIC, {1.0f, 0.45f, 0.05f}, 2),
      def("capsule", "Capsule", "Round", K::Capsule, {0.25f, 0.5f, 0}, MAT_PLASTIC, {0.4f, 0.8f, 0.4f}, 15),
      def("wheel", "Wheel", "Round", K::Cylinder, {0.45f, 0.12f, 0}, MAT_RUBBER, {0.15f, 0.15f, 0.15f}, 20, 0.2f, 1.2f),
      def("tire", "Tire", "Round", K::Torus, {0.35f, 0.15f, 0}, MAT_RUBBER, {0.12f, 0.12f, 0.12f}, 15, 0.4f, 1.1f),
      def("sawblade", "Sawblade", "Round", K::Cylinder, {0.5f, 0.015f, 0}, MAT_CHROME, {0.85f, 0.85f, 0.9f}, 10),
      // Furniture
      def("table", "Table", "Furniture", K::Compound, {1, 1, 1}, MAT_WOOD, {0.6f, 0.42f, 0.25f}, 30, 0.1f, 0.7f, Special::None, "table"),
      def("chair", "Chair", "Furniture", K::Compound, {1, 1, 1}, MAT_WOOD, {0.55f, 0.38f, 0.22f}, 8, 0.1f, 0.7f, Special::None, "chair"),
      def("shelf", "Shelf", "Furniture", K::Compound, {1, 1, 1}, MAT_WOOD, {0.7f, 0.55f, 0.35f}, 40, 0.1f, 0.7f, Special::None, "shelf"),
      def("bathtub", "Bathtub", "Furniture", K::Compound, {1, 1, 1}, MAT_PLASTIC, {0.95f, 0.95f, 0.92f}, 70, 0.1f, 0.7f, Special::None, "bathtub"),
      def("bench", "Bench", "Furniture", K::Compound, {1, 1, 1}, MAT_WOOD, {0.5f, 0.35f, 0.2f}, 25, 0.1f, 0.7f, Special::None, "bench"),
      def("fence", "Fence", "Furniture", K::Compound, {1, 1, 1}, MAT_WOOD, {0.85f, 0.85f, 0.8f}, 20, 0.1f, 0.7f, Special::None, "fence"),
      def("dumpster", "Dumpster", "Furniture", K::Compound, {1, 1, 1}, MAT_METAL, {0.2f, 0.45f, 0.25f}, 150, 0.1f, 0.7f, Special::None, "dumpster"),
      // Fun
      def("ragdoll", "Ragdoll", "Fun", K::Capsule, {0.15f, 0.1f, 0}, MAT_PLASTIC, {0.85f, 0.7f, 0.55f}, 60, 0.0f, 0.8f, Special::Ragdoll),
      def("melon", "Watermelon", "Fun", K::Sphere, {0.18f, 0, 0}, MAT_PLASTIC, {0.2f, 0.55f, 0.15f}, 4, 0.2f),
      def("dynamite", "Dynamite", "Fun", K::Box, {0.08f, 0.15f, 0.08f}, MAT_PLASTIC, {0.8f, 0.1f, 0.08f}, 2, 0.1f, 0.7f, Special::Dynamite),
      def("lamp", "Glow Lamp", "Fun", K::Sphere, {0.2f, 0, 0}, MAT_GLOW, {1.0f, 0.85f, 0.6f}, 3, 0.2f, 0.7f, Special::Lamp),
      def("balloon", "Balloon", "Fun", K::Sphere, {0.3f, 0, 0}, MAT_PLASTIC, {0.9f, 0.2f, 0.3f}, 0.2f, 0.5f, 0.7f, Special::Balloon),
      def("bowling", "Bowling Ball", "Fun", K::Sphere, {0.11f, 0, 0}, MAT_CHROME, {0.15f, 0.1f, 0.3f}, 7, 0.1f),
      // Imported (Kenney, CC0) models
      model("truck_red", "Red Truck", "Vehicles", 900),
      model("truck_green", "Green Truck", "Vehicles", 900),
      model("truck_purple", "Purple Truck", "Vehicles", 900),
      model("truck_yellow", "Yellow Truck", "Vehicles", 900),
      model("motorcycle", "Motorcycle", "Vehicles", 180),
      model("drone", "Drone", "Vehicles", 25),
      model("track_bump", "Track Bump", "Vehicles", 400),
      model("statue", "Statue", "Decoration", 400),
      model("column", "Column", "Decoration", 500),
      model("column_damaged", "Broken Column", "Decoration", 350),
      model("tree", "Tree", "Decoration", 150),
      model("trees", "Bushy Trees", "Decoration", 300),
      model("fountain", "Fountain", "Decoration", 1500),
      model("banner", "Banner", "Decoration", 20),
      model("flag", "Flag", "Decoration", 15),
      model("cloud", "Cloud", "Decoration", 5),
      model("trophy", "Trophy", "Decoration", 8),
      model("coin", "Coin", "Decoration", 2),
      model("question_block", "? Block", "Decoration", 40),
      model("brick_block", "Brick Block", "Decoration", 40),
      model("soldier", "Soldier", "Decoration", 70),
      model("character", "Character", "Decoration", 60),
      model("sword", "Sword", "Decoration", 3),
      model("spear", "Spear", "Decoration", 4),
      model("weapon_rack", "Weapon Rack", "Decoration", 40),
      model("blaster", "Blaster", "Decoration", 4),
      model("blaster_repeater", "Repeater", "Decoration", 5),
      model("stairs", "Stairs", "Buildings", 800),
      model("stone_wall", "Stone Wall", "Buildings", 1200),
      model("wall_gate", "Wall Gate", "Buildings", 1100),
      model("wall_low", "Low Wall", "Buildings", 600),
      model("bricks", "Brick Pile", "Buildings", 150),
      model("platform", "Platform", "Buildings", 700),
      model("house_a", "House", "Buildings", 5000),
      model("house_b", "Tall House", "Buildings", 6000),
      model("house_c", "Tower House", "Buildings", 7000),
      model("garage", "Garage", "Buildings", 4000),
  };
  return c;
}

const PropDef* findPropDef(const std::string& id) {
  for (auto& d : propCatalog())
    if (d.id == id) return &d;
  return nullptr;
}

bool propAvailable(const PropDef& d) { return d.shape.kind != ShapeKind::Model || modelExists(d.shape.compound); }

std::vector<std::string> propCategories() {
  std::vector<std::string> cats;
  for (auto& d : propCatalog())
    if (propAvailable(d))
    if (std::find(cats.begin(), cats.end(), d.category) == cats.end()) cats.push_back(d.category);
  return cats;
}

// ---------------------------------------------------------------------------
MeshData buildShapeMesh(const ShapeDesc& d) {
  switch (d.kind) {
    case ShapeKind::Box: return meshgen::box(d.size);
    case ShapeKind::Sphere: return meshgen::sphere(d.size.x);
    case ShapeKind::Cylinder: return meshgen::cylinder(d.size.x, d.size.y);
    case ShapeKind::Capsule: return meshgen::capsule(d.size.x, d.size.y);
    case ShapeKind::Cone: return meshgen::cone(d.size.x, d.size.y);
    case ShapeKind::Wedge: return meshgen::wedge(d.size);
    case ShapeKind::Torus: return meshgen::torus(d.size.x, d.size.y);
    case ShapeKind::Compound: {
      MeshData m;
      if (auto* parts = compoundParts(d.compound))
        for (auto& p : *parts) m.append(meshgen::box(p.half), glm::translate(glm::mat4(1), p.pos));
      return m;
    }
    case ShapeKind::Model:
      if (const MeshData* md = loadModel(d.compound)) return *md;
      return meshgen::box(glm::vec3(0.5f));
  }
  return meshgen::box(glm::vec3(0.5f));
}

const Mesh* Assets::mesh(const ShapeDesc& d) {
  std::string k = d.key();
  auto it = meshes_.find(k);
  if (it != meshes_.end()) return it->second.get();
  auto m = std::make_unique<Mesh>();
  m->upload(buildShapeMesh(d));
  const Mesh* r = m.get();
  meshes_[k] = std::move(m);
  return r;
}

static btBoxShape* makeBox(const glm::vec3& h) {
  auto* b = new btBoxShape(toBt(h));
  float mn = std::min(h.x, std::min(h.y, h.z));
  b->setMargin(std::min(0.04f, mn * 0.3f));
  return b;
}

btCollisionShape* Assets::shape(const ShapeDesc& d) {
  std::string k = d.key();
  auto it = shapes_.find(k);
  if (it != shapes_.end()) return it->second;
  btCollisionShape* s = nullptr;
  switch (d.kind) {
    case ShapeKind::Box: s = makeBox(d.size); break;
    case ShapeKind::Sphere: s = new btSphereShape(d.size.x); break;
    case ShapeKind::Cylinder: {
      auto* c = new btCylinderShape(btVector3(d.size.x, d.size.y, d.size.x));
      c->setMargin(std::min(0.04f, std::min(d.size.x, d.size.y) * 0.3f));
      s = c;
      break;
    }
    case ShapeKind::Capsule: s = new btCapsuleShape(d.size.x, d.size.y * 2.0f); break;
    case ShapeKind::Cone: s = new btConeShape(d.size.x, d.size.y); break;
    case ShapeKind::Wedge: {
      auto* h = new btConvexHullShape();
      glm::vec3 e = d.size;
      glm::vec3 pts[6] = {{-e.x, -e.y, -e.z}, {e.x, -e.y, -e.z}, {e.x, -e.y, e.z},
                          {-e.x, -e.y, e.z},  {-e.x, e.y, -e.z}, {-e.x, e.y, e.z}};
      for (auto& p : pts) h->addPoint(toBt(p), false);
      h->recalcLocalAabb();
      h->setMargin(0.01f);
      s = h;
      break;
    }
    case ShapeKind::Torus: {
      auto* c = new btCylinderShape(btVector3(d.size.x + d.size.y, d.size.y, d.size.x + d.size.y));
      c->setMargin(0.02f);
      s = c;
      break;
    }
    case ShapeKind::Compound: {
      auto* c = new btCompoundShape();
      if (auto* parts = compoundParts(d.compound)) {
        for (auto& p : *parts) {
          btBoxShape* b = makeBox(p.half);
          owned_.push_back(b);
          c->addChildShape(btTransform(btQuaternion::getIdentity(), toBt(p.pos)), b);
        }
      }
      s = c;
      break;
    }
    case ShapeKind::Model: {
      const MeshData* md = loadModel(d.compound);
      btConvexHullShape full;
      if (md)
        for (auto& v : md->verts) full.addPoint(toBt(v.pos), false);
      else
        for (int i = 0; i < 8; i++) full.addPoint(btVector3(i & 1 ? .5f : -.5f, i & 2 ? .5f : -.5f, i & 4 ? .5f : -.5f), false);
      full.recalcLocalAabb();
      // Reduce to a cheap hull (~40 points).
      btShapeHull hull(&full);
      hull.buildHull(full.getMargin());
      auto* h = new btConvexHullShape((const btScalar*)hull.getVertexPointer(), hull.numVertices(), sizeof(btVector3));
      h->setMargin(0.01f);
      h->recalcLocalAabb();
      s = h;
      break;
    }
  }
  shapes_[k] = s;
  return s;
}

void Assets::shutdown() {
  for (auto& kv : meshes_) kv.second->destroy();
  meshes_.clear();
  for (auto& kv : shapes_) delete kv.second;
  shapes_.clear();
  for (auto* s : owned_) delete s;
  owned_.clear();
}
