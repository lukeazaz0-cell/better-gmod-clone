#pragma once
// OpenGL 3.3 core. On Linux, libGL (glvnd/Mesa) exports every core entry point,
// so we can call them directly with prototypes instead of a loader.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <SDL_opengl.h>
