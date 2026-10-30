/* Offline give-item hook test; never loads title code or starts a game.
 * UCRT64: cc -O2 -ffunction-sections -fdata-sections port/ap/test_give_hook.c
 *   -Wl,--gc-sections -o port/build-ap/test_give_hook.exe
 */
#include <assert.h>
#include "../src/hle_ap.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_esp;
static int enabled = 1, success = 1, pickup_calls, reported;
static const char *nearest;
static char last_key[100];

int hpcos_ap_enabled(void) { return enabled; }
int hpcos_ap_known(const char *key) { return !strcmp(key, "reward:2008"); }
const char *hpcos_ap_pickup(int level, unsigned item, float x, float y, float z)
{
    assert(level == 2 && item == 0x2008 && x == 1 && y == 2 && z == 3);
    pickup_calls++;
    return nearest;
}
void hpcos_ap_location(const char *key)
{
    reported++;
    snprintf(last_key, sizeof last_key, "%s", key);
}
void sub_00081A10_gen(void)
{
    assert(MEM32(g_esp + 4) == 0x2008);
    g_eax = 0x12345600u | success;
    g_esp += 4;
}
void sub_00081860(void) {}
void sub_000818C0(void) {}
void sub_0004AD50(void) { assert(0); }
int hpcos_ap_active(void) { return 0; }
void hpcos_ap_frame(void) { assert(0); }
void hpcos_ap_goal(void) { assert(0); }
int hpcos_ap_option(const char *key, int def) { (void)key; return def; }
void hpcos_ap_item_given(void) { assert(0); }
int hpcos_ap_next_item(const char **kind, int *value) { (void)kind; (void)value; return 0; }
int hpcos_ap_travel_entry(int i, const char **name, int *level, int *start, int *unlocked)
{
    (void)i; (void)name; (void)level; (void)start; (void)unlocked;
    return 0;
}
uint32_t hpcos_CallGuest(void (*fn)(void), int n, const uint32_t *args)
{
    assert(n == 1);
    g_esp = 0x528100;
    MEM32(g_esp + 4) = args[0];
    fn();
    return g_eax;
}
static void invoke(void)
{
    g_esp = 0x528100;
    MEM32(g_esp + 4) = 0x2008;
    sub_00081A10();
    assert(g_esp == 0x528104 && g_eax == (0x12345600u | success));
}
int main(void)
{
    char *base = VirtualAlloc(NULL, 0x84010000ull, MEM_RESERVE, PAGE_READWRITE);
    assert(base);
    g_xbox_mem_offset = (ptrdiff_t)base;
    assert(VirtualAlloc(base + 0x527000, 0x2000, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(base + 0x530000, 0x1000, MEM_COMMIT, PAGE_READWRITE));
    assert(VirtualAlloc(base + 0x80000000u, 0x2000, MEM_COMMIT, PAGE_READWRITE));
    MEM32(kProgress) = 2;
    MEM32(0x530408) = 0x80000000u;
    MEM32(0x800000ACu) = 0x80001000u;
    MEMF(0x80001014u) = 1;
    MEMF(0x80001018u) = 2;
    MEMF(0x8000101Cu) = 3;
    nearest = "pickup:2:2008:1:3";
    invoke();
    assert(reported == 1 && !strcmp(last_key, nearest));
    nearest = NULL;
    invoke();
    assert(reported == 2 && !strcmp(last_key, "reward:2008"));
    success = 0;
    invoke();
    assert(reported == 2 && pickup_calls == 2);
    success = 1;
    enabled = 0;
    invoke();
    assert(reported == 2 && pickup_calls == 2);
    enabled = 1;
    nearest = "pickup:2:2008:1:3";
    give("card", 8);  /* bridge-delivered card must never become a check */
    assert(reported == 2 && pickup_calls == 2 && !g_ap_giving);
    VirtualFree(base, 0, MEM_RELEASE);
    puts("give-item hook: success/failure, fallback, guest return, AP suppression passed");
    return 0;
}
