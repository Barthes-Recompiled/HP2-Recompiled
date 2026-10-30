/* The Archipelago bridge's C interface (bridge.cpp), used by hle_ap.c on the title's own thread.
 *
 * Off unless RECOMP_AP_SERVER and RECOMP_AP_SLOT are set (the password, if any, is read from
 * <exe dir>\saves\archipelago\connection.toml, password = "...", so it never passes through the command line or a
 * log). Everything here runs on the game thread: the network thread (client.cpp) only queues packets. */
#ifndef HPCOS_AP_BRIDGE_H
#define HPCOS_AP_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if RECOMP_AP_SERVER and RECOMP_AP_SLOT are set (read once): the rest does nothing otherwise */
int hpcos_ap_enabled(void);
/* once a frame Harry is updated: connect, drain packets, (re)send checks */
void hpcos_ap_frame(void);
/* a location's catalog key happened (unknown keys are ignored); safe to repeat */
void hpcos_ap_location(const char *key);
/* 1 if the catalog has that location key (whether or not it is checked yet) */
int hpcos_ap_known(const char *key);
/* the goal is done */
void hpcos_ap_goal(void);
/* the next received item to give in the game: its catalog kind ("card", "spell", "beans", ...) and value (card
 * number, spell bit, amount); returns 0 when there is none. hpcos_ap_item_given() after giving it moves the received
 * index on and saves it */
int hpcos_ap_next_item(const char **kind, int *value);
void hpcos_ap_item_given(void);
/* 1 while a slot is bound (connected or offline with a session), for the cards goal and the travel menu */
int hpcos_ap_active(void);
int hpcos_ap_cards_goal(void);
/* 1 if the slot owns that item (a pass, by catalog name) */
int hpcos_ap_has(const char *item_name);
/* the travel page's list, by level: name is the place, level and start (-1 the level's own) where the warp goes,
 * unlocked 1 when the slot owns its pass. Returns 0 past the end. */
int hpcos_ap_travel_entry(int i, const char **name, int *level, int *start, int *unlocked);

#ifdef __cplusplus
}
#endif

#endif
