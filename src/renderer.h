#pragma once
#include "gl.h"
#include "mathutil.h"
#include "mesh.h"
#include <string>
#include <vector>

struct Camera {
  glm::vec3 pos{0, 2, 0};
  float yaw = 0, pitch = 0;  // radians
  float fov = 75.0f;         // degrees (vertical)
  float znear = 0.05f, zfar = 3000.0f;

  glm::vec3 forward() const;
  glm::vec3 right() const;
  glm::vec3 up() const;
  glm::mat4 view() const;
  glm::mat4 proj(float aspect) const;
};

struct DrawCmd {
  const Mesh* mesh = nullptr;
  glm::mat4 model{1.0f};
  glm::vec4 color{1.0f};
  int material = 0;
  glm::vec4 highlight{0.0f};  // rgb + strength
  float emissive = 0.0f;
  bool worldTex = false;  // procedural texture in world space (map brushes) vs object space
  bool castShadow = true;
};

struct PointLight {
  glm::vec3 pos;
  glm::vec3 color;
  float radius;
};

class Renderer {
 public:
  bool init();
  void shutdown();
  void resize(int w, int h);

  void begin(const Camera& cam, float time);
  void draw(const DrawCmd& c);
  void drawViewmodel(const DrawCmd& c);  // model matrix in world space, drawn on top
  void drawGlow(const DrawCmd& c);       // additive, no depth write (explosions etc.)
  void light(const PointLight& l);
  // Camera-facing ribbon through pts. additive = glowing beam, else solid (ropes).
  void beam(const std::vector<glm::vec3>& pts, const glm::vec4& color, float width, bool additive);
  void render();

  bool saveScreenshot(const std::string& path);

  int width = 1280, height = 720;
  bool shadows = true;
  int shadowSize = 2048;
  float shadowRange = 70.0f;
  glm::vec3 sunDir = glm::normalize(glm::vec3(0.45f, 0.8f, 0.3f));
  float viewmodelFov = 62.0f;
  int drawCalls = 0;

 private:
  struct BeamVert {
    glm::vec3 pos;
    glm::vec4 color;
    glm::vec2 uv;
  };
  void drawList(const std::vector<DrawCmd>& list, const glm::mat4& vp, bool shadowsOn);
  void buildBeam(std::vector<BeamVert>& out, const std::vector<glm::vec3>& pts, const glm::vec4& color, float width);
  void flushBeams(std::vector<BeamVert>& verts, bool additive);

  GLuint mainProg_ = 0, shadowProg_ = 0, skyProg_ = 0, beamProg_ = 0;
  GLuint shadowFbo_ = 0, shadowTex_ = 0;
  GLuint emptyVao_ = 0, beamVao_ = 0, beamVbo_ = 0;
  Camera cam_;
  glm::mat4 view_{1}, proj_{1}, vp_{1}, lightVP_{1};
  float time_ = 0;
  std::vector<DrawCmd> opaque_, viewmodel_, glow_;
  std::vector<PointLight> lights_;
  std::vector<BeamVert> beamAdd_, beamSolid_;
};
