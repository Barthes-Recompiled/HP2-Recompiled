/*
 * Region self-check (tools/region/README.md): logs once, as Harry first stands in a level, whether the game state
 * the hand-written code reads sits where it expects. These addresses are in bss, which a retargeted build maps by
 * the pointers into it rather than by comparing bytes, so a run on the other region's XBE confirms them here:
 *
 *   [REGION] cos-eur: level 3, player 80A1B2C0, health 5/5, spells 00000001, cards held 2 -> ok
 *
 * Reads only, from its own thread, every half second; it never changes the game.
 */
#include <stdint.h>
#include <stdio.h>

#include "recomp/gen/recomp_types.h"
#include "trainer_shm.h"

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <pthread.h>
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#ifndef REGION_PROFILE
#define REGION_PROFILE "canonical"
#endif

/* the same guest addresses as hle_trainer.c */
enum { kPlayer = 0x530408, kProgress = 0x527B78, kHealth = 0x527EF4, kHealthMax = 0x527EF6, kHealthCap = 255,
       kSpells = 0x527F0C, kCards = 0x527E0A, kCardCount = 101 };

extern size_t xbox_GetMappedSize(void);

static int guest_ptr(uint32_t p) { return p >= 0x80000000u && p < 0x84000000u; }

static void check(void)
{
    while (!xbox_GetMappedSize()) SLEEP_MS(500);
    for (int t = 0; t < 2 * 600; t++, SLEEP_MS(500)) {
        uint32_t level = MEM32(kProgress), pl = MEM32(kPlayer);
        if (level >= HPCOS_LEVELS || !guest_ptr(pl)) continue;
        int16_t h = (int16_t)MEM16(kHealth), hmax = (int16_t)MEM16(kHealthMax);
        uint32_t spells = MEM32(kSpells);
        int held = 0, cards_ok = 1;
        for (int c = 1; c <= kCardCount; c++) {
            uint16_t n = MEM16(kCards + 2 * c);
            held += n != 0;
            cards_ok &= n < 100;
        }
        int ok = h >= 0 && h <= hmax && hmax > 0 && hmax <= kHealthCap && !(spells & ~HPCOS_ALL_SPELLS) && cards_ok;
        fprintf(stderr, "[REGION] %s: level %u, player %08X, health %d/%d, spells %08X, cards held %d -> %s\n",
                REGION_PROFILE, level, pl, h, hmax, spells, held, ok ? "ok" : "SUSPECT: report this log");
        return;
    }
    fprintf(stderr, "[REGION] %s: no level with Harry in 10 minutes (level %u, player %08X): "
            "SUSPECT unless the game sat in menus\n", REGION_PROFILE, MEM32(kProgress), MEM32(kPlayer));
}

#ifdef _WIN32
static DWORD WINAPI check_thread(LPVOID arg) { (void)arg; check(); return 0; }
#else
static void *check_thread(void *arg) { (void)arg; check(); return NULL; }
#endif

__attribute__((constructor)) static void region_check_start(void)
{
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, check_thread, NULL) == 0) pthread_detach(th);
#endif
}
