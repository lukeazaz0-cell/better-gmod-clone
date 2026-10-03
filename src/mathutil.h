#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <btBulletDynamicsCommon.h>

inline btVector3 toBt(const glm::vec3& v) { return btVector3(v.x, v.y, v.z); }
inline glm::vec3 toGlm(const btVector3& v) { return glm::vec3((float)v.x(), (float)v.y(), (float)v.z()); }
inline btQuaternion toBt(const glm::quat& q) { return btQuaternion(q.x, q.y, q.z, q.w); }
inline glm::quat toGlm(const btQuaternion& q) { return glm::quat((float)q.w(), (float)q.x(), (float)q.y(), (float)q.z()); }

inline glm::mat4 toGlm(const btTransform& t) {
  btScalar m[16];
  t.getOpenGLMatrix(m);
  glm::mat4 r;
  float* p = glm::value_ptr(r);
  for (int i = 0; i < 16; i++) p[i] = (float)m[i];
  return r;
}

inline btTransform makeXf(const glm::vec3& p, const glm::quat& q = glm::quat(1, 0, 0, 0)) {
  return btTransform(toBt(q), toBt(p));
}

// Shortest rotation taking unit vector a onto unit vector b.
inline glm::quat rotationBetween(const glm::vec3& a, const glm::vec3& b) {
  float d = glm::dot(a, b);
  if (d > 0.99999f) return glm::quat(1, 0, 0, 0);
  if (d < -0.99999f) {
    glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), a);
    if (glm::length(axis) < 1e-3f) axis = glm::cross(glm::vec3(0, 0, 1), a);
    return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
  }
  glm::vec3 c = glm::cross(a, b);
  glm::quat q(1.0f + d, c.x, c.y, c.z);
  return glm::normalize(q);
}

// Yaw convention: yaw = 0 looks down -Z, positive yaw turns right (towards +X).
inline glm::quat yawQuat(float yaw) { return glm::angleAxis(-yaw, glm::vec3(0, 1, 0)); }
