#include "renderer.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
glm::vec3 Camera::forward() const {
  return glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
}
glm::vec3 Camera::right() const { return glm::vec3(std::cos(yaw), 0, std::sin(yaw)); }
glm::vec3 Camera::up() const { return glm::cross(right(), forward()); }
glm::mat4 Camera::view() const { return glm::lookAt(pos, pos + forward(), glm::vec3(0, 1, 0)); }
glm::mat4 Camera::proj(float aspect) const {
  return glm::perspective(glm::radians(fov), aspect, znear, zfar);
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
static const char* kCommonGLSL = R"(
uniform vec3 uSunDir;
float hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float noise(vec3 x) {
  vec3 i = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                 mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                 mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p) { float v = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { v += a * noise(p); p *= 2.03; a *= 0.5; } return v; }
vec3 skyColor(vec3 d) {
  float t = clamp(d.y, -1.0, 1.0);
  vec3 zenith = vec3(0.20, 0.42, 0.80), horizon = vec3(0.70, 0.81, 0.93), ground = vec3(0.36, 0.38, 0.38);
  vec3 c = t > 0.0 ? mix(horizon, zenith, pow(t, 0.55)) : mix(horizon, ground, pow(-t, 0.4));
  float s = max(dot(d, uSunDir), 0.0);
  c += vec3(1.0, 0.92, 0.75) * (pow(s, 900.0) * 10.0 + pow(s, 14.0) * 0.22);
  return c;
}
)";

static const char* kMainVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uVP;
out vec3 vWorldPos; out vec3 vWorldNormal; out vec3 vLocalPos; out vec3 vLocalNormal;
void main() {
  vec4 wp = uModel * vec4(aPos, 1.0);
  vWorldPos = wp.xyz;
  vWorldNormal = mat3(uModel) * aNormal;
  vLocalPos = aPos;
  vLocalNormal = aNormal;
  gl_Position = uVP * wp;
}
)";

static const char* kMainFSHead = R"(#version 330 core
in vec3 vWorldPos; in vec3 vWorldNormal; in vec3 vLocalPos; in vec3 vLocalNormal;
out vec4 FragColor;
uniform vec4 uColor; uniform int uMaterial; uniform bool uWorldTex; uniform vec4 uHighlight; uniform float uEmissive;
uniform vec3 uBMin; uniform vec3 uBMax;
uniform vec3 uCamPos; uniform vec3 uFogColor; uniform float uFogDensity;
uniform sampler2DShadow uShadowMap; uniform mat4 uLightVP; uniform bool uShadows;
uniform int uNumLights; uniform vec3 uLightPos[16]; uniform vec3 uLightColor[16]; uniform float uLightRadius[16];
)";

static const char* kMainFSBody = R"(
vec2 triUV(vec3 p, vec3 n) { vec3 a = abs(n); if (a.x > a.y && a.x > a.z) return p.zy; if (a.y > a.z) return p.xz; return p.xy; }
float lineAA(float x, float w) { float d = abs(fract(x - 0.5) - 0.5); float fw = max(fwidth(x), 1e-4); return 1.0 - smoothstep(w, w + fw * 1.5, d); }

float shadowFactor(vec3 P, vec3 N) {
  if (!uShadows) return 1.0;
  vec4 lp = uLightVP * vec4(P + N * 0.05, 1.0);
  vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
  if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
  vec2 ts = 1.0 / vec2(textureSize(uShadowMap, 0));
  float s = 0.0;
  for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++)
    s += texture(uShadowMap, vec3(c.xy + vec2(x, y) * ts, c.z - 0.0008));
  float fade = smoothstep(0.85, 1.0, max(abs(c.x * 2.0 - 1.0), abs(c.y * 2.0 - 1.0)));
  return mix(s / 9.0, 1.0, fade);
}

void main() {
  vec3 N = normalize(vWorldNormal);
  vec3 V = normalize(uCamPos - vWorldPos);
  vec3 P = uWorldTex ? vWorldPos : vLocalPos;
  vec3 PN = uWorldTex ? N : normalize(vLocalNormal);
  vec2 uv = triUV(P, PN);
  vec3 albedo = uColor.rgb;
  float spec = 0.25, shin = 32.0, emissive = uEmissive, envAmt = 0.0;

  if (uMaterial == 1) { // dev grid
    float major = lineAA(uv.x, 0.012) ; major = max(major, lineAA(uv.y, 0.012));
    float minor = max(lineAA(uv.x * 4.0, 0.025), lineAA(uv.y * 4.0, 0.025));
    float chk = mod(floor(uv.x) + floor(uv.y), 2.0);
    albedo *= 0.93 + 0.07 * chk;
    albedo = mix(albedo, albedo * 0.82, minor * 0.6);
    albedo = mix(albedo, min(albedo * 1.4 + 0.12, vec3(1.0)), major);
    spec = 0.1;
  } else if (uMaterial == 2 || uMaterial == 3) { // wood / crate
    float rings = sin(uv.y * 55.0 + noise(vec3(uv * vec2(2.0, 9.0), 0.0)) * 7.0) * 0.5 + 0.5;
    albedo *= 0.8 + 0.22 * rings;
    albedo *= 0.88 + 0.22 * noise(vec3(uv * vec2(45.0, 3.0), 1.0));
    spec = 0.08; shin = 12.0;
    if (uMaterial == 3) {
      float seam = lineAA(uv.y / 0.15, 0.04);
      albedo *= 1.0 - 0.5 * seam;
      vec3 dmin = vLocalPos - uBMin, dmax = uBMax - vLocalPos;
      vec3 d = min(dmin, dmax);
      vec3 a = abs(normalize(vLocalNormal));
      float e;
      if (a.x > a.y && a.x > a.z) e = min(d.y, d.z); else if (a.y > a.z) e = min(d.x, d.z); else e = min(d.x, d.y);
      float frame = 1.0 - smoothstep(0.055, 0.065, e);
      albedo = mix(albedo, albedo * 0.72, frame);
      albedo *= 1.0 - 0.6 * (smoothstep(0.05, 0.055, e) - smoothstep(0.065, 0.07, e));
    }
  } else if (uMaterial == 4) { // metal
    albedo *= 0.86 + 0.14 * noise(vec3(uv.x * 3.0, uv.y * 260.0, 0.0));
    spec = 0.9; shin = 70.0; envAmt = 0.18;
  } else if (uMaterial == 5) { // concrete
    albedo *= 0.74 + 0.36 * fbm(P * 3.0);
    albedo *= 0.94 + 0.06 * noise(P * 45.0);
    spec = 0.04; shin = 8.0;
  } else if (uMaterial == 6) { // grass
    float n = fbm(P * 0.3);
    albedo = mix(vec3(0.20, 0.36, 0.11), vec3(0.38, 0.54, 0.20), n);
    albedo *= 0.82 + 0.3 * noise(P * 14.0);
    albedo = mix(albedo, vec3(0.45, 0.42, 0.30), smoothstep(0.62, 0.8, fbm(P * 0.07 + 3.0)) * 0.5);
    spec = 0.02; shin = 4.0;
  } else if (uMaterial == 7) { // checker
    float c = mod(floor(uv.x * 4.0) + floor(uv.y * 4.0), 2.0);
    albedo = mix(albedo, vec3(0.95), c);
    spec = 0.45; shin = 48.0;
  } else if (uMaterial == 8) { // rubber
    albedo *= 0.9 + 0.1 * noise(P * 30.0);
    spec = 0.06; shin = 8.0;
  } else if (uMaterial == 9) { // glow
    emissive = max(emissive, 1.0);
  } else if (uMaterial == 10) { // chrome
    spec = 1.4; shin = 120.0; envAmt = 0.85;
  } else if (uMaterial == 11) { // brick
    float row = floor(uv.y / 0.085);
    float bx = uv.x / 0.25 + 0.5 * mod(row, 2.0);
    float mortar = max(lineAA(uv.y / 0.085, 0.07), lineAA(bx, 0.03));
    float h = hash(vec3(floor(bx), row, 3.0));
    albedo = mix(albedo * (0.78 + 0.32 * h), vec3(0.72, 0.70, 0.66), mortar);
    spec = 0.03; shin = 6.0;
  } else { // plastic
    spec = 0.5; shin = 48.0;
  }

  vec3 lin = pow(albedo, vec3(2.2));
  vec3 col;
  if (emissive >= 1.0) {
    col = lin * 2.2;
  } else {
    float ndl = max(dot(N, uSunDir), 0.0);
    float sh = ndl > 0.0 ? shadowFactor(vWorldPos, N) : 0.0;
    vec3 sun = vec3(1.0, 0.95, 0.86) * 2.3;
    vec3 amb = mix(vec3(0.32, 0.29, 0.25) * 0.55, vec3(0.55, 0.66, 0.88) * 0.85, N.y * 0.5 + 0.5);
    vec3 H = normalize(uSunDir + V);
    float s = pow(max(dot(N, H), 0.0), shin) * spec;
    col = lin * (amb + sun * ndl * sh) + sun * s * sh;
    for (int i = 0; i < uNumLights; i++) {
      vec3 L = uLightPos[i] - vWorldPos;
      float d = length(L);
      float att = clamp(1.0 - d / uLightRadius[i], 0.0, 1.0);
      att *= att;
      vec3 Ln = L / max(d, 1e-3);
      float nl = max(dot(N, Ln), 0.0);
      float ps = pow(max(dot(N, normalize(Ln + V)), 0.0), shin) * spec;
      col += (lin * nl + ps) * uLightColor[i] * att;
    }
    if (envAmt > 0.0) {
      vec3 R = reflect(-V, N);
      vec3 env = pow(skyColor(R), vec3(2.2)) * 1.4;
      col = mix(col, env * lin * 1.6 + col * 0.3, envAmt);
    }
    col = mix(col, lin * 2.2, emissive);
  }
  float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0);
  col += pow(uHighlight.rgb, vec3(2.2)) * (rim * 2.0 + 0.25) * uHighlight.a;

  col = vec3(1.0) - exp(-col * 1.0);
  col = pow(col, vec3(1.0 / 2.2));
  float dist = length(uCamPos - vWorldPos);
  float fog = 1.0 - exp(-dist * uFogDensity);
  col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
  FragColor = vec4(col * (uEmissive >= 1.0 ? uColor.a : 1.0), uColor.a);
}
)";

static const char* kShadowVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel; uniform mat4 uVP;
void main() { gl_Position = uVP * uModel * vec4(aPos, 1.0); }
)";
static const char* kShadowFS = R"(#version 330 core
void main() {}
)";

static const char* kSkyVS = R"(#version 330 core
out vec2 vNdc;
void main() {
  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
  vNdc = p;
  gl_Position = vec4(p, 1.0, 1.0);
}
)";
static const char* kSkyFSHead = R"(#version 330 core
in vec2 vNdc; out vec4 FragColor;
uniform mat4 uInvVP; uniform float uTime;
)";
static const char* kSkyFSBody = R"(
void main() {
  vec4 a = uInvVP * vec4(vNdc, -1.0, 1.0);
  vec4 b = uInvVP * vec4(vNdc, 1.0, 1.0);
  vec3 d = normalize(b.xyz / b.w - a.xyz / a.w);
  vec3 c = skyColor(d);
  if (d.y > 0.0) {
    vec2 cuv = d.xz / (d.y + 0.1) * 1.6 + vec2(uTime * 0.01, uTime * 0.004);
    float n = fbm(vec3(cuv, uTime * 0.01));
    float cl = smoothstep(0.48, 0.78, n) * smoothstep(0.0, 0.3, d.y);
    float lit = 0.85 + 0.15 * max(dot(d, uSunDir), 0.0);
    c = mix(c, vec3(lit), cl * 0.85);
  }
  FragColor = vec4(c, 1.0);
}
)";

static const char* kBeamVS = R"(#version 330 core
layout(location = 0) in vec3 aPos; layout(location = 1) in vec4 aColor; layout(location = 2) in vec2 aUV;
uniform mat4 uVP; out vec4 vColor; out vec2 vUV;
void main() { vColor = aColor; vUV = aUV; gl_Position = uVP * vec4(aPos, 1.0); }
)";
static const char* kBeamFS = R"(#version 330 core
in vec4 vColor; in vec2 vUV; out vec4 FragColor;
uniform bool uAdditive;
void main() {
  float e = 1.0 - abs(vUV.y * 2.0 - 1.0);
  if (uAdditive) {
    float a = pow(e, 1.6) * vColor.a;
    FragColor = vec4(vColor.rgb * a + vec3(1.0) * pow(e, 10.0) * 0.7 * vColor.a, 1.0);
  } else {
    FragColor = vec4(vColor.rgb * (0.55 + 0.45 * sqrt(e)), 1.0);
  }
}
)";

static GLuint compile(GLenum type, const std::vector<const char*>& srcs) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, (GLsizei)srcs.size(), srcs.data(), nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    fprintf(stderr, "Shader compile error:\n%s\n", log);
  }
  return s;
}

static GLuint link(std::vector<const char*> vs, std::vector<const char*> fs) {
  GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    fprintf(stderr, "Program link error:\n%s\n", log);
    p = 0;
  }
  glDeleteShader(v);
  glDeleteShader(f);
  return p;
}

static inline GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }

namespace {
struct MainLocs {
  GLint model, vp, color, material, worldTex, highlight, emissive, bmin, bmax, camPos, fogColor, fogDensity, shadowMap,
      lightVP, shadows, numLights, lightPos, lightColor, lightRadius, sunDir;
} L;
}  // namespace

static const glm::vec3 kFogColor(0.70f, 0.81f, 0.93f);

bool Renderer::init() {
  mainProg_ = link({kMainVS}, {kMainFSHead, kCommonGLSL, kMainFSBody});
  shadowProg_ = link({kShadowVS}, {kShadowFS});
  skyProg_ = link({kSkyVS}, {kSkyFSHead, kCommonGLSL, kSkyFSBody});
  beamProg_ = link({kBeamVS}, {kBeamFS});
  if (!mainProg_ || !shadowProg_ || !skyProg_ || !beamProg_) return false;

  GLuint p = mainProg_;
  L = {U(p, "uModel"),     U(p, "uVP"),          U(p, "uColor"),      U(p, "uMaterial"),   U(p, "uWorldTex"),
       U(p, "uHighlight"), U(p, "uEmissive"),    U(p, "uBMin"),       U(p, "uBMax"),       U(p, "uCamPos"),
       U(p, "uFogColor"),  U(p, "uFogDensity"),  U(p, "uShadowMap"),  U(p, "uLightVP"),    U(p, "uShadows"),
       U(p, "uNumLights"), U(p, "uLightPos"),    U(p, "uLightColor"), U(p, "uLightRadius"), U(p, "uSunDir")};

  // Shadow map
  glGenTextures(1, &shadowTex_);
  glBindTexture(GL_TEXTURE_2D, shadowTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenFramebuffers(1, &shadowFbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "Shadow framebuffer incomplete, disabling shadows\n");
    shadows = false;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glGenVertexArrays(1, &emptyVao_);
  glGenVertexArrays(1, &beamVao_);
  glGenBuffers(1, &beamVbo_);
  glBindVertexArray(beamVao_);
  glBindBuffer(GL_ARRAY_BUFFER, beamVbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BeamVert), (void*)offsetof(BeamVert, pos));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BeamVert), (void*)offsetof(BeamVert, color));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BeamVert), (void*)offsetof(BeamVert, uv));
  glBindVertexArray(0);
  return true;
}

void Renderer::shutdown() {
  glDeleteProgram(mainProg_);
  glDeleteProgram(shadowProg_);
  glDeleteProgram(skyProg_);
  glDeleteProgram(beamProg_);
  glDeleteFramebuffers(1, &shadowFbo_);
  glDeleteTextures(1, &shadowTex_);
  glDeleteVertexArrays(1, &emptyVao_);
  glDeleteVertexArrays(1, &beamVao_);
  glDeleteBuffers(1, &beamVbo_);
}

void Renderer::resize(int w, int h) {
  width = std::max(1, w);
  height = std::max(1, h);
}

void Renderer::begin(const Camera& cam, float time) {
  cam_ = cam;
  time_ = time;
  opaque_.clear();
  viewmodel_.clear();
  glow_.clear();
  lights_.clear();
  beamAdd_.clear();
  beamSolid_.clear();
  view_ = cam.view();
  proj_ = cam.proj((float)width / (float)height);
  vp_ = proj_ * view_;
}

void Renderer::draw(const DrawCmd& c) { opaque_.push_back(c); }
void Renderer::drawViewmodel(const DrawCmd& c) { viewmodel_.push_back(c); }
void Renderer::drawGlow(const DrawCmd& c) { glow_.push_back(c); }
void Renderer::light(const PointLight& l) { lights_.push_back(l); }

void Renderer::beam(const std::vector<glm::vec3>& pts, const glm::vec4& color, float w, bool additive) {
  buildBeam(additive ? beamAdd_ : beamSolid_, pts, color, w);
}

void Renderer::buildBeam(std::vector<BeamVert>& out, const std::vector<glm::vec3>& pts, const glm::vec4& color,
                         float w) {
  if (pts.size() < 2) return;
  std::vector<glm::vec3> side(pts.size());
  for (size_t i = 0; i < pts.size(); i++) {
    glm::vec3 t = (i + 1 < pts.size()) ? pts[i + 1] - pts[i] : pts[i] - pts[i - 1];
    if (i > 0 && i + 1 < pts.size()) t = pts[i + 1] - pts[i - 1];
    glm::vec3 toCam = cam_.pos - pts[i];
    glm::vec3 s = glm::cross(t, toCam);
    float l = glm::length(s);
    side[i] = l > 1e-6f ? s / l * (w * 0.5f) : glm::vec3(0, w * 0.5f, 0);
  }
  for (size_t i = 0; i + 1 < pts.size(); i++) {
    float t0 = (float)i / (pts.size() - 1), t1 = (float)(i + 1) / (pts.size() - 1);
    BeamVert a{pts[i] - side[i], color, {t0, 0}}, b{pts[i] + side[i], color, {t0, 1}};
    BeamVert c{pts[i + 1] - side[i + 1], color, {t1, 0}}, d{pts[i + 1] + side[i + 1], color, {t1, 1}};
    out.insert(out.end(), {a, b, d, a, d, c});
  }
}

void Renderer::flushBeams(std::vector<BeamVert>& verts, bool additive) {
  if (verts.empty()) return;
  glUseProgram(beamProg_);
  glUniformMatrix4fv(U(beamProg_, "uVP"), 1, GL_FALSE, glm::value_ptr(vp_));
  glUniform1i(U(beamProg_, "uAdditive"), additive ? 1 : 0);
  glBindVertexArray(beamVao_);
  glBindBuffer(GL_ARRAY_BUFFER, beamVbo_);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(BeamVert), verts.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
  drawCalls++;
}

void Renderer::drawList(const std::vector<DrawCmd>& list, const glm::mat4& vp, bool shadowsOn) {
  glUniformMatrix4fv(L.vp, 1, GL_FALSE, glm::value_ptr(vp));
  glUniform1i(L.shadows, shadowsOn && shadows ? 1 : 0);
  for (const DrawCmd& c : list) {
    if (!c.mesh) continue;
    glUniformMatrix4fv(L.model, 1, GL_FALSE, glm::value_ptr(c.model));
    glUniform4fv(L.color, 1, glm::value_ptr(c.color));
    glUniform1i(L.material, c.material);
    glUniform1i(L.worldTex, c.worldTex ? 1 : 0);
    glUniform4fv(L.highlight, 1, glm::value_ptr(c.highlight));
    glUniform1f(L.emissive, c.emissive);
    glUniform3fv(L.bmin, 1, glm::value_ptr(c.mesh->bmin));
    glUniform3fv(L.bmax, 1, glm::value_ptr(c.mesh->bmax));
    c.mesh->draw();
    drawCalls++;
  }
}

void Renderer::render() {
  drawCalls = 0;
  // --- Shadow pass ---
  if (shadows) {
    float R = shadowRange;
    glm::vec3 center = cam_.pos + cam_.forward() * (R * 0.5f);
    center.y = cam_.pos.y;
    glm::vec3 up = std::fabs(sunDir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::mat4 lv = glm::lookAt(center + sunDir * 250.0f, center, up);
    glm::mat4 lp = glm::ortho(-R, R, -R, R, 1.0f, 600.0f);
    glm::vec4 o = (lp * lv) * glm::vec4(0, 0, 0, 1);
    o *= shadowSize / 2.0f;
    glm::vec4 off = (glm::round(o) - o) * (2.0f / shadowSize);
    lp[3][0] += off.x;
    lp[3][1] += off.y;
    lightVP_ = lp * lv;

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize, shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    glUseProgram(shadowProg_);
    GLint um = U(shadowProg_, "uModel");
    glUniformMatrix4fv(U(shadowProg_, "uVP"), 1, GL_FALSE, glm::value_ptr(lightVP_));
    for (const DrawCmd& c : opaque_) {
      if (!c.castShadow || !c.mesh) continue;
      glUniformMatrix4fv(um, 1, GL_FALSE, glm::value_ptr(c.model));
      c.mesh->draw();
      drawCalls++;
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  glViewport(0, 0, width, height);
  glClearColor(kFogColor.r, kFogColor.g, kFogColor.b, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // --- Sky ---
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glUseProgram(skyProg_);
  glm::mat4 skyVP = proj_ * glm::mat4(glm::mat3(view_));
  glm::mat4 inv = glm::inverse(skyVP);
  glUniformMatrix4fv(U(skyProg_, "uInvVP"), 1, GL_FALSE, glm::value_ptr(inv));
  glUniform1f(U(skyProg_, "uTime"), time_);
  glUniform3fv(U(skyProg_, "uSunDir"), 1, glm::value_ptr(sunDir));
  glBindVertexArray(emptyVao_);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDepthMask(GL_TRUE);

  // --- Opaque ---
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glUseProgram(mainProg_);
  glUniform3fv(L.camPos, 1, glm::value_ptr(cam_.pos));
  glUniform3fv(L.sunDir, 1, glm::value_ptr(sunDir));
  glUniform3fv(L.fogColor, 1, glm::value_ptr(kFogColor));
  glUniform1f(L.fogDensity, 0.0022f);
  glUniformMatrix4fv(L.lightVP, 1, GL_FALSE, glm::value_ptr(lightVP_));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, shadowTex_);
  glUniform1i(L.shadowMap, 0);

  // pick the 16 lights nearest to the camera
  std::sort(lights_.begin(), lights_.end(), [&](const PointLight& a, const PointLight& b) {
    return glm::length(a.pos - cam_.pos) - a.radius < glm::length(b.pos - cam_.pos) - b.radius;
  });
  int n = std::min<int>(16, (int)lights_.size());
  std::vector<glm::vec3> lp(16), lc(16);
  std::vector<float> lr(16, 1.0f);
  for (int i = 0; i < n; i++) {
    lp[i] = lights_[i].pos;
    lc[i] = lights_[i].color;
    lr[i] = lights_[i].radius;
  }
  glUniform1i(L.numLights, n);
  glUniform3fv(L.lightPos, 16, glm::value_ptr(lp[0]));
  glUniform3fv(L.lightColor, 16, glm::value_ptr(lc[0]));
  glUniform1fv(L.lightRadius, 16, lr.data());

  drawList(opaque_, vp_, true);

  // --- Solid ribbons (ropes) ---
  glDisable(GL_CULL_FACE);
  flushBeams(beamSolid_, false);

  // --- Additive: glow meshes and beams ---
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glDepthMask(GL_FALSE);
  if (!glow_.empty()) {
    glUseProgram(mainProg_);
    drawList(glow_, vp_, false);
  }
  flushBeams(beamAdd_, true);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  // --- Viewmodel ---
  if (!viewmodel_.empty()) {
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_CULL_FACE);
    glUseProgram(mainProg_);
    glm::mat4 vmProj = glm::perspective(glm::radians(viewmodelFov), (float)width / height, 0.01f, 10.0f);
    glUniform1i(L.numLights, 0);
    drawList(viewmodel_, vmProj * view_, false);
  }
  glDisable(GL_CULL_FACE);
  glBindVertexArray(0);
  glUseProgram(0);
}

bool Renderer::saveScreenshot(const std::string& path) {
  std::vector<unsigned char> px((size_t)width * height * 3), flipped(px.size());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  for (int y = 0; y < height; y++)
    std::copy(px.begin() + (size_t)y * width * 3, px.begin() + (size_t)(y + 1) * width * 3,
              flipped.begin() + (size_t)(height - 1 - y) * width * 3);
  SDL_Surface* s =
      SDL_CreateRGBSurfaceWithFormatFrom(flipped.data(), width, height, 24, width * 3, SDL_PIXELFORMAT_RGB24);
  if (!s) return false;
  int r = SDL_SaveBMP(s, path.c_str());
  SDL_FreeSurface(s);
  return r == 0;
}
