#pragma once
#include "mathutil.h"
#include "mesh.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum Material {
  MAT_PLASTIC,
  MAT_DEV,
  MAT_WOOD,
  MAT_CRATE,
  MAT_METAL,
  MAT_CONCRETE,
  MAT_GRASS,
  MAT_CHECKER,
  MAT_RUBBER,
  MAT_GLOW,
  MAT_CHROME,
  MAT_BRICK,
  MAT_COUNT
};
const char* materialName(int m);

enum class ShapeKind { Box, Sphere, Cylinder, Capsule, Cone, Wedge, Torus, Compound };
const char* shapeKindName(ShapeKind k);
ShapeKind shapeKindFromName(const std::string& s);

// size meaning per kind:
//   Box/Wedge: half extents. Sphere: x = radius. Cylinder: x = radius, y = half height (Y axis).
//   Capsule: x = radius, y = half height of the straight section. Cone: x = radius, y = height.
//   Torus: x = major radius, y = tube radius. Compound: parts from compoundParts(compound).
struct ShapeDesc {
  ShapeKind kind = ShapeKind::Box;
  glm::vec3 size{0.5f};
  std::string compound;
  std::string key() const;
};

struct SubBox {
  glm::vec3 pos;
  glm::vec3 half;
};
const std::vector<SubBox>* compoundParts(const std::string& name);

enum class Special { None, Balloon, Dynamite, Lamp, Ragdoll };

struct PropDef {
  std::string id, name, category;
  ShapeDesc shape;
  int material = MAT_PLASTIC;
  glm::vec4 color{1};
  float mass = 10;
  float friction = 0.7f, restitution = 0.1f;
  Special special = Special::None;
};
const std::vector<PropDef>& propCatalog();
const PropDef* findPropDef(const std::string& id);
std::vector<std::string> propCategories();

MeshData buildShapeMesh(const ShapeDesc& d);

class Assets {
 public:
  const Mesh* mesh(const ShapeDesc& d);
  btCollisionShape* shape(const ShapeDesc& d);
  void shutdown();

 private:
  std::unordered_map<std::string, std::unique_ptr<Mesh>> meshes_;
  std::unordered_map<std::string, btCollisionShape*> shapes_;
  std::vector<btCollisionShape*> owned_;
};
