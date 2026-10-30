/**
 * Cutscene fast-forward (hpcos.toml cutscene_skip, on by default): holding Start or Back for half a second while a
 * cutscene plays runs it through to its own end at SKIP_SCALE times the speed, with the sound muted and the picture
 * held (a frame every quarter of a second shows how far it got). Nothing in the cutscene is left out: its scripts,
 * speech and camera all run as they would, only faster, so whatever the cutscene sets up is set up.
 *
 * A cutscene here is the game's camera / control mode [0x50C2E4] at 7, which sub_0003E330 sets as a cutscene takes
 * the camera (other values: 0 Harry's own, 4, 0xB, 0xC other cameras); the fast-forward stops as it changes.
 *
 * The title's logic advances one fixed step a frame: the world tick sub_00035110 (updates, then the 3D draw pass
 * sub_00032980 on the object list at 0x50C4C8). While fast-forwarding the frame pacing (hle_d3d8.cpp) runs SKIP_SCALE
 * times as fast, the draw pass is left out (it changes only the I/O queue and the heap, by a draw-twice test), the
 * renderer holds its presents (d3d12.cpp) and the DirectSound mixer (hle_dsound.c) mixes SKIP_SCALE blocks a block
 * and outputs silence, so speech ends in step. Start and Back still reach the game as always.
 *
 * Built into the recompile with --exclude-manual naming this file.
 */

#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"
#include "input/xinput_xbox.h"

extern int app_CutsceneSkip(void);  /* hpcos.toml cutscene_skip (renderer/app/app.cpp) */
extern void sub_00032980_gen(void);

#define CAMERA_MODE     0x0050C2E4u
#define MODE_CUTSCENE   7u
#define BTN_START       0x0010u
#define BTN_BACK        0x0020u
#define HOLD_MS         500u
#define SHOW_MS         250u
#define SKIP_SCALE      16
#define RESUME_MS       2000u    /* a cutscene that starts again this soon after one ends goes on fast too */
#define SKIP_MAX_MS     180000u  /* a cutscene is a few minutes at most */

static volatile LONG g_scale = 1;

int hpcos_FastForward(void) { return (int)g_scale; }

static int cutscene(void) { return MEM32(CAMERA_MODE) == MODE_CUTSCENE; }

/* hle_input.c, port 0's pad as the game reads it: the hold that starts the fast-forward, and its end */
void hpcos_SkipPad(const XBOX_GAMEPAD *g)
{
    static DWORD held_since, ff_since, ended;
    static int held;
    DWORD now = GetTickCount();
    /* the next part of a cutscene that came in two (a moment of no cutscene between its shots) */
    if (g_scale == 1 && ended && now - ended < RESUME_MS && cutscene() && app_CutsceneSkip()) {
        ended = 0;
        ff_since = now;
        InterlockedExchange(&g_scale, SKIP_SCALE);
        fprintf(stderr, "[SKIP] cutscene fast-forward goes on into the next part\n");
        return;
    }
    if (g_scale > 1) {
        if (!cutscene() || now - ff_since > SKIP_MAX_MS) {
            InterlockedExchange(&g_scale, 1);
            ended = cutscene() ? 0 : now;
            fprintf(stderr, "[SKIP] cutscene fast-forward ended after %lu ms (camera mode %u)\n",
                    (unsigned long)(now - ff_since), MEM32(CAMERA_MODE));
        }
        return;
    }
    int down = (g->wButtons & (BTN_START | BTN_BACK)) != 0;
    if (!down || !cutscene() || !app_CutsceneSkip()) {
        held = 0;
        return;
    }
    if (!held) {
        held = 1;
        held_since = now;
    } else if (now - held_since >= HOLD_MS) {
        held = 0;
        ff_since = now;
        InterlockedExchange(&g_scale, SKIP_SCALE);
        fprintf(stderr, "[SKIP] cutscene fast-forward, %dx\n", SKIP_SCALE);
    }
}

/* sub_00032980, __thiscall (ecx = the object list), ret: the world's 3D draw pass, left out while fast-forwarding
 * except for a frame every SHOW_MS */
void sub_00032980(void)
{
    static DWORD shown;
    if (g_scale > 1) {
        DWORD now = GetTickCount();
        if (now - shown < SHOW_MS) {
            g_esp += 4;
            return;
        }
        shown = now;
    }
    sub_00032980_gen();
}
