#include "game.h"

extern int g_autotestFailures;

int main(int argc, char** argv) {
  Game game;
  if (!game.init(argc, argv)) return 1;
  game.run();
  game.shutdown();
  return g_autotestFailures > 0 ? 1 : 0;
}
