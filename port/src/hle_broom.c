/**
 * Free broom landing on the Hogwarts grounds (hpcos.toml broom_free_flight, off by default).
 *
 * On the Xbox Harry can mount his broom anywhere on the grounds (level 26) once it is on a button: sub_00050390
 * checks only the level and two progress flags. He can only get off it at a landing pad, though. While he flies
 * (states 0x7B fly_breathe and 0x7C fly), sub_0005B3E0 and sub_000623D0 ask sub_0002D3E0 each frame which trigger
 * volumes he is in (objects flagged 4 in the list at 0x5276B0, masked with the day bit 1 << [0x527B7C]) and keep the
 * answer at [0x53030C]; sub_00050300 lets the broom's button land him only when bit 0x1000000 (the pads' volumes,
 * landing1..7c in hogwarts.geo) is in it. The PS2 version lands anywhere.
 *
 * With the setting on, those two calls report that bit everywhere on the grounds. Flight keeps the height Harry took
 * off at and stays inside the grounds' flying-area walls (HEX_TERRAINFLYINGAREA in hogwarts.geo), so there is ground
 * under him wherever he can fly; getting off is the game's own dismount (state 0x76), which drops him onto it. The
 * flying lesson's controller (type 0x500001B9, made by sub_000E2A20 at takeoff) and its checks, and every other caller
 * of sub_0002D3E0, are left as they are.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"

extern int app_BroomFreeFlight(void); /* hpcos.toml broom_free_flight (renderer/app/app.cpp) */
extern void sub_0002D3E0_gen(void);

#define LEVEL_INDEX     0x00527B78u  /* the progress block's first word */
#define LEVEL_GROUNDS   26u          /* Hogwarts Exterior */
#define HARRY_STATE     0x00530298u  /* index into the state names at 0x1E9E68 */
#define STATE_FLY_IDLE  0x7Bu
#define STATE_FLY       0x7Cu
#define RET_FLY_IDLE    0x0005B53Bu  /* sub_0005B3E0's call */
#define RET_FLY         0x000624E0u  /* sub_000623D0's call */
#define FLAG_LANDABLE   0x01000000u

/* sub_0002D3E0(position, out, mask, flag, list, entity_only), cdecl: the trigger volumes at a position */
void sub_0002D3E0(void)
{
    uint32_t ret = MEM32(g_esp);
    sub_0002D3E0_gen();
    if ((ret != RET_FLY_IDLE && ret != RET_FLY) || !app_BroomFreeFlight() || MEM32(LEVEL_INDEX) != LEVEL_GROUNDS)
        return;
    uint32_t state = MEM16(HARRY_STATE);
    if (state == STATE_FLY_IDLE || state == STATE_FLY) g_eax |= FLAG_LANDABLE;
}
