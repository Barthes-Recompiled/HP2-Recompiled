// Host test of the Archipelago bridge without the game (src/ap/bridge.cpp + client.cpp against a local AP server).
//   test_bridge <phase>   with RECOMP_AP_SERVER / RECOMP_AP_SLOT set, run from a folder with saves\ beside the exe
// phase 1: connect, send two checks by key, take every received item ("give" it), report the counts
// send <key>...: make those checks as this slot (another slot's items, for a game test)
// phase 2: a new run of the same slot: the session must come back (checks made, items given) and give nothing twice
#include "bridge.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static int pump(int seconds, int* given) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (std::chrono::steady_clock::now() < end) {
    hpcos_ap_frame();
    const char* kind;
    int value;
    while (hpcos_ap_next_item(&kind, &value)) {
      std::printf("give %s %d\n", kind, value);
      hpcos_ap_item_given();
      ++*given;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  return hpcos_ap_active();
}

int main(int argc, char** argv) {
  if (argc > 2 && !std::strcmp(argv[1], "send")) {  // test_bridge send <key>...: make these checks as this slot
    int given = 0;
    pump(4, &given);
    for (int i = 2; i < argc; i++) hpcos_ap_location(argv[i]);
    pump(3, &given);
    return hpcos_ap_active() ? 0 : 1;
  }
  int phase = argc > 1 ? std::atoi(argv[1]) : 1;
  int given = 0;
  if (!hpcos_ap_enabled()) {
    std::printf("RECOMP_AP_SERVER / RECOMP_AP_SLOT not set\n");
    return 2;
  }
  if (phase == 1) hpcos_ap_location("cards:1");  // before connecting: kept as pending
  int bound = pump(4, &given);
  std::printf("phase %d: bound %d, items given this run %d\n", phase, bound, given);
  if (phase == 1) {
    hpcos_ap_location("task:6:added");
    hpcos_ap_location("beans:10");
    pump(3, &given);
    std::printf("phase 1: after checks, items given this run %d\n", given);
  }
  std::printf("has Day 0 Pass: %d, has Night 0 Pass: %d\n", hpcos_ap_has("Day 0 Pass"), hpcos_ap_has("Night 0 Pass"));
  return bound ? 0 : 1;
}
