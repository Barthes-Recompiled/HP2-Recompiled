/**
 * The title's CRC32 (sub_00016100(data, length): zlib's, the reflected 0xEDB88320 table at 0x1CF0D0, from ~0 and
 * inverted after; 0 for a length of 0 or less) natively, eight bytes a step. The lifted loop is a byte at a time
 * through the guest memory macros, and the title runs it over what it loads: ~1.6 ms of each inventory open
 * (2026-10-25, RECOMP_GUEST_PROF). Same results, bit for bit.
 *
 * Built into the recompile with --exclude-manual ../hpcos/src/hle_crc.c; gen/recomp_0000.c's generated body is
 * renamed sub_00016100_gen by hand until then.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECOMP_GENERATED_CODE
#include "recomp/gen/recomp_types.h"

extern void sub_00016100_gen(void);

static uint32_t g_crc[8][256];

static void crc_tables(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        g_crc[0][n] = c;
    }
    for (uint32_t n = 0; n < 256; n++)
        for (int t = 1; t < 8; t++) g_crc[t][n] = (g_crc[t - 1][n] >> 8) ^ g_crc[0][g_crc[t - 1][n] & 0xFF];
}

void sub_00016100(void)
{
    static int ready;
    uint32_t va = MEM32(g_esp + 4);
    int32_t len = (int32_t)MEM32(g_esp + 8);
    uint32_t c = 0xFFFFFFFFu;
    if (!ready) crc_tables(), ready = 1;
    if (len > 0) {
        const uint8_t *p = (const uint8_t *)XBOX_PTR(va);
        size_t n = (size_t)len;
        for (; n >= 8; n -= 8, p += 8) {
            uint32_t lo, hi;
            memcpy(&lo, p, 4);
            memcpy(&hi, p + 4, 4);
            lo ^= c;
            c = g_crc[7][lo & 0xFF] ^ g_crc[6][(lo >> 8) & 0xFF] ^ g_crc[5][(lo >> 16) & 0xFF] ^ g_crc[4][lo >> 24] ^
                g_crc[3][hi & 0xFF] ^ g_crc[2][(hi >> 8) & 0xFF] ^ g_crc[1][(hi >> 16) & 0xFF] ^ g_crc[0][hi >> 24];
        }
        for (; n; n--) c = (c >> 8) ^ g_crc[0][(c ^ *p++) & 0xFF];
    }
    c = ~c;
    /* the first 256 calls also run the lifted loop and compare: [CRC] says if any differs */
    static int checked;
    if (checked < 256) {
        checked++;
        uint32_t s_esp = g_esp;
        sub_00016100_gen();
        g_esp = s_esp;
        if (g_eax != c) fprintf(stderr, "[CRC] differs: %08X+%d native %08X lifted %08X\n", va, len, c, g_eax);
        else if (checked == 256) fprintf(stderr, "[CRC] native CRC32 matched the lifted one on 256 calls\n");
    }
    g_eax = c;
    g_esp += 4;
}
