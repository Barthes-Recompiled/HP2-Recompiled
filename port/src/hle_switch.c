/**
 * No chest (locker) sound for a chest a level load puts back open.
 *
 * sub_0001F140(position) is an animation channel's sound step (this = the channel): its time at +0, last update's at
 * +0x54, and every sound event of its clip (id, frame) with a frame in (last, now] plays at the position through
 * sub_00041A40(id, position, volume). A level load sets an opened chest's lid straight to its open pose, a jump from 0
 * to the clip's end in one update, which passed its "lid opens" event (0x3000001), heard as the level appears.
 *
 * A clip that moves more than SEEK_FRAMES in one update was set, not played (a level load, or a part of one streaming
 * in as Harry walks): of the events it passes, the chest's sound is left out. A chest Harry opens plays its clip a
 * frame at a time. Every other sound plays as on the Xbox (the user's choice: the fires lit again on a load and the
 * doors keep theirs). RECOMP_CHEST_LOAD_SOUND=1 keeps it too.
 *
 * Built into the recompile with --exclude-manual ../hpcos/src/hle_switch.c; gen/recomp_0000.c's sub_0001F140 and
 * gen/recomp_0001.c's sub_00041A40 bodies are renamed _gen by hand until then.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"

#define SND_CHEST   0x3000001u   /* a chest's lid opening */
#define RET_ANIM    0x0001F1CFu  /* sub_0001F140's call of sub_00041A40 */
#define SEEK_FRAMES 10.0f        /* an animation never plays this many frames in one update */

extern void sub_00041A40_gen(void);
extern void sub_0001F140_gen(void);

static int g_anim_set;  /* this animation was set (a level load), not played */

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_CHEST_LOAD_SOUND");
        on = !(e && *e == '1');
    }
    return on;
}

/* sub_00041A40(id, position, volume): a sound at a place */
void sub_00041A40(void)
{
    if (g_anim_set && MEM32(g_esp) == RET_ANIM && MEM32(g_esp + 4) == SND_CHEST) {
        fprintf(stderr, "[SND] chest sound on a level load left out\n");
        g_eax = 0;
        g_esp += 4;  /* its ret: the caller (cdecl) pops the arguments */
        return;
    }
    sub_00041A40_gen();
}

/* sub_0001F140(position), __thiscall (ecx = the animation channel), ret 4 */
void sub_0001F140(void)
{
    uint32_t ch = g_ecx;
    float now = MEMF(ch), last = MEMF(ch + 0x54);
    g_anim_set = enabled() && now - last > SEEK_FRAMES;
    sub_0001F140_gen();
    g_anim_set = 0;
}
