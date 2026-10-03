#pragma once
#include "gl.h"
#include "mathutil.h"
#include <cstdint>
#include <vector>

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
};

struct MeshData {
  std::vector<Vertex> verts;
  std::vector<uint32_t> idx;

  void append(const MeshData& o, const glm::mat4& xf = glm::mat4(1.0f));
  // Adds a flat convex polygon (points in order); winding is fixed to face away from `inside`.
  void addFlatPoly(const std::vector<glm::vec3>& pts, const glm::vec3& inside);
  // Flips any triangle whose geometric normal disagrees with its vertex normals.
  void fixWinding();
};

struct Mesh {
  GLuint vao = 0, vbo = 0, ebo = 0;
  GLsizei count = 0;
  glm::vec3 bmin{0}, bmax{0};

  void upload(const MeshData& d);
  void draw() const;
  void destroy();
};

namespace meshgen {
MeshData box(const glm::vec3& half);
MeshData sphere(float r, int seg = 28, int rings = 18);
MeshData cylinder(float r, float halfH, int seg = 28);           // along Y
MeshData capsule(float r, float halfH, int seg = 20, int rings = 8);  // along Y, halfH = half of cylinder part
MeshData cone(float r, float h, int seg = 28);                   // along Y, centred like btConeShape
MeshData wedge(const glm::vec3& half);                           // ramp rising towards -X
MeshData torus(float R, float r, int seg = 32, int sides = 14);  // around Y
}  // namespace meshgen
