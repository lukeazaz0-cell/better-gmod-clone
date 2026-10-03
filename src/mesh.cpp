#include "mesh.h"
#include <cmath>

static const float PI = 3.14159265358979f;

void MeshData::append(const MeshData& o, const glm::mat4& xf) {
  uint32_t base = (uint32_t)verts.size();
  glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(xf)));
  for (const Vertex& v : o.verts) {
    Vertex w;
    w.pos = glm::vec3(xf * glm::vec4(v.pos, 1.0f));
    w.normal = glm::normalize(nm * v.normal);
    w.color = v.color;
    verts.push_back(w);
  }
  for (uint32_t i : o.idx) idx.push_back(base + i);
}

void MeshData::addFlatPoly(const std::vector<glm::vec3>& pts, const glm::vec3& inside) {
  if (pts.size() < 3) return;
  glm::vec3 n = glm::normalize(glm::cross(pts[1] - pts[0], pts[2] - pts[0]));
  glm::vec3 c(0);
  for (auto& p : pts) c += p;
  c /= (float)pts.size();
  bool flip = glm::dot(n, c - inside) < 0;
  if (flip) n = -n;
  uint32_t base = (uint32_t)verts.size();
  for (auto& p : pts) verts.push_back({p, n, glm::vec3(1.0f)});
  for (size_t i = 1; i + 1 < pts.size(); i++) {
    if (!flip) {
      idx.push_back(base); idx.push_back(base + (uint32_t)i); idx.push_back(base + (uint32_t)i + 1);
    } else {
      idx.push_back(base); idx.push_back(base + (uint32_t)i + 1); idx.push_back(base + (uint32_t)i);
    }
  }
}

void MeshData::fixWinding() {
  for (size_t t = 0; t + 2 < idx.size(); t += 3) {
    const Vertex& a = verts[idx[t]];
    const Vertex& b = verts[idx[t + 1]];
    const Vertex& c = verts[idx[t + 2]];
    glm::vec3 g = glm::cross(b.pos - a.pos, c.pos - a.pos);
    if (glm::dot(g, g) < 1e-14f) continue;
    if (glm::dot(g, a.normal + b.normal + c.normal) < 0) std::swap(idx[t + 1], idx[t + 2]);
  }
}

void Mesh::upload(const MeshData& d) {
  if (!vao) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
  }
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, d.verts.size() * sizeof(Vertex), d.verts.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, d.idx.size() * sizeof(uint32_t), d.idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
  glBindVertexArray(0);
  count = (GLsizei)d.idx.size();
  bmin = glm::vec3(1e9f);
  bmax = glm::vec3(-1e9f);
  for (auto& v : d.verts) {
    bmin = glm::min(bmin, v.pos);
    bmax = glm::max(bmax, v.pos);
  }
}

void Mesh::draw() const {
  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
}

void Mesh::destroy() {
  if (vao) {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
  }
  vao = vbo = ebo = 0;
}

namespace meshgen {

MeshData box(const glm::vec3& h) {
  MeshData m;
  struct F { glm::vec3 n, u, v; };
  const F faces[6] = {
      {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
      {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
      {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
  };
  for (const F& f : faces) {
    uint32_t base = (uint32_t)m.verts.size();
    const float s[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (auto& c : s) m.verts.push_back({(f.n + c[0] * f.u + c[1] * f.v) * h, f.n});
    uint32_t q[6] = {0, 1, 2, 0, 2, 3};
    for (uint32_t i : q) m.idx.push_back(base + i);
  }
  return m;
}

// Generic lat/long surface from rows of (radius-at-row, y, normal-y) style samples.
static void latLong(MeshData& m, int seg, const std::vector<std::pair<glm::vec2, glm::vec2>>& rows) {
  // rows: (radius, y) position profile and (nr, ny) normal profile
  uint32_t base = (uint32_t)m.verts.size();
  for (auto& row : rows) {
    for (int j = 0; j <= seg; j++) {
      float phi = (float)j / seg * 2.0f * PI;
      float c = std::cos(phi), s = std::sin(phi);
      glm::vec3 p(row.first.x * c, row.first.y, row.first.x * s);
      glm::vec3 n(row.second.x * c, row.second.y, row.second.x * s);
      float l = glm::length(n);
      m.verts.push_back({p, l > 0 ? n / l : glm::vec3(0, 1, 0)});
    }
  }
  for (size_t i = 0; i + 1 < rows.size(); i++) {
    for (int j = 0; j < seg; j++) {
      uint32_t a = base + (uint32_t)(i * (seg + 1) + j);
      uint32_t b = a + seg + 1;
      m.idx.insert(m.idx.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
  }
}

static void disc(MeshData& m, float r, float y, float ny, int seg) {
  uint32_t base = (uint32_t)m.verts.size();
  m.verts.push_back({{0, y, 0}, {0, ny, 0}});
  for (int j = 0; j <= seg; j++) {
    float phi = (float)j / seg * 2.0f * PI;
    m.verts.push_back({{r * std::cos(phi), y, r * std::sin(phi)}, {0, ny, 0}});
  }
  for (int j = 0; j < seg; j++) m.idx.insert(m.idx.end(), {base, base + 1 + j, base + 2 + j});
}

MeshData sphere(float r, int seg, int rings) {
  MeshData m;
  std::vector<std::pair<glm::vec2, glm::vec2>> rows;
  for (int i = 0; i <= rings; i++) {
    float th = (float)i / rings * PI;
    float s = std::sin(th), c = std::cos(th);
    rows.push_back({{r * s, r * c}, {s, c}});
  }
  latLong(m, seg, rows);
  m.fixWinding();
  return m;
}

MeshData cylinder(float r, float hh, int seg) {
  MeshData m;
  latLong(m, seg, {{{r, hh}, {1, 0}}, {{r, -hh}, {1, 0}}});
  disc(m, r, hh, 1, seg);
  disc(m, r, -hh, -1, seg);
  m.fixWinding();
  return m;
}

MeshData capsule(float r, float hh, int seg, int rings) {
  MeshData m;
  std::vector<std::pair<glm::vec2, glm::vec2>> rows;
  for (int i = 0; i <= rings; i++) {
    float th = (float)i / rings * PI * 0.5f;
    rows.push_back({{r * std::sin(th), hh + r * std::cos(th)}, {std::sin(th), std::cos(th)}});
  }
  for (int i = 0; i <= rings; i++) {
    float th = PI * 0.5f + (float)i / rings * PI * 0.5f;
    rows.push_back({{r * std::sin(th), -hh + r * std::cos(th)}, {std::sin(th), std::cos(th)}});
  }
  latLong(m, seg, rows);
  m.fixWinding();
  return m;
}

MeshData cone(float r, float h, int seg) {
  MeshData m;
  float hh = h * 0.5f;
  glm::vec2 n = glm::normalize(glm::vec2(h, r));
  latLong(m, seg, {{{0.0001f, hh}, {n.x, n.y}}, {{r, -hh}, {n.x, n.y}}});
  disc(m, r, -hh, -1, seg);
  m.fixWinding();
  return m;
}

MeshData wedge(const glm::vec3& h) {
  MeshData m;
  glm::vec3 b0(-h.x, -h.y, -h.z), b1(h.x, -h.y, -h.z), b2(h.x, -h.y, h.z), b3(-h.x, -h.y, h.z);
  glm::vec3 t0(-h.x, h.y, -h.z), t1(-h.x, h.y, h.z);
  glm::vec3 inside(-h.x * 0.33f, -h.y * 0.33f, 0);
  m.addFlatPoly({b0, b1, b2, b3}, inside);  // bottom
  m.addFlatPoly({b0, b3, t1, t0}, inside);  // back
  m.addFlatPoly({t0, t1, b2, b1}, inside);  // slope
  m.addFlatPoly({b0, t0, b1}, inside);      // side -z
  m.addFlatPoly({b3, b2, t1}, inside);      // side +z
  return m;
}

MeshData torus(float R, float r, int seg, int sides) {
  MeshData m;
  for (int i = 0; i <= seg; i++) {
    float u = (float)i / seg * 2 * PI;
    for (int j = 0; j <= sides; j++) {
      float v = (float)j / sides * 2 * PI;
      glm::vec3 n(std::cos(v) * std::cos(u), std::sin(v), std::cos(v) * std::sin(u));
      glm::vec3 p((R + r * std::cos(v)) * std::cos(u), r * std::sin(v), (R + r * std::cos(v)) * std::sin(u));
      m.verts.push_back({p, n});
    }
  }
  for (int i = 0; i < seg; i++) {
    for (int j = 0; j < sides; j++) {
      uint32_t a = i * (sides + 1) + j;
      uint32_t b = a + sides + 1;
      m.idx.insert(m.idx.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
  }
  m.fixWinding();
  return m;
}

}  // namespace meshgen
