#pragma once
#include <SDL.h>
#include <cstring>

struct Input {
  bool down[SDL_NUM_SCANCODES] = {};
  bool pressed[SDL_NUM_SCANCODES] = {};
  bool released[SDL_NUM_SCANCODES] = {};
  bool mouseDown[8] = {};
  bool mousePressed[8] = {};
  bool mouseReleased[8] = {};
  float mdx = 0, mdy = 0;
  int wheel = 0;

  void beginFrame() {
    memset(pressed, 0, sizeof(pressed));
    memset(released, 0, sizeof(released));
    memset(mousePressed, 0, sizeof(mousePressed));
    memset(mouseReleased, 0, sizeof(mouseReleased));
    mdx = mdy = 0;
    wheel = 0;
  }
  // Drops all held state (e.g. when a menu grabs focus).
  void releaseAll() {
    for (int i = 0; i < SDL_NUM_SCANCODES; i++)
      if (down[i]) {
        down[i] = false;
        released[i] = true;
      }
    for (int i = 0; i < 8; i++)
      if (mouseDown[i]) {
        mouseDown[i] = false;
        mouseReleased[i] = true;
      }
  }
};
