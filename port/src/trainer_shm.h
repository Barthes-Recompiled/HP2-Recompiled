/*
 * The debug trainer's shared block: hpcos.exe creates the named mapping
 * "Local\hpcos_trainer" at boot; hpcos_trainer.exe (src/trainer) opens it, shows
 * the status the game writes and leaves requests the game carries out on its
 * own thread (hle_trainer.c). Nothing here changes the game unless the trainer
 * asks for it.
 */
#ifndef HPCOS_TRAINER_SHM_H
#define HPCOS_TRAINER_SHM_H

#include <stdint.h>

#define HPCOS_TRAINER_NAME    "Local\\hpcos_trainer"
#define HPCOS_TRAINER_MAGIC   0x52544348u  /* "HCTR" */
#define HPCOS_TRAINER_VERSION 4u

/* the spells' bits in the progress block's spell word (0x527F0C): every bit some level's
 * info record grants ("Pink Room", the test level, has them all) */
#define HPCOS_ALL_SPELLS 0x00D04017u

/* guest memory the report copies: the progress block (what a save holds) */
#define HPCOS_PROGRESS_VA   0x527B78u
#define HPCOS_PROGRESS_SIZE 0x2064u

#define HPCOS_LEVELS 59  /* the level table's records (the title's own level selector stops there too) */

typedef struct HpcosTrainerShm {
    uint32_t magic, version;
    uint32_t pid;                  /* hpcos.exe */
    uint32_t pad1;
    uint64_t mem_offset;           /* host address = guest address + mem_offset */

    /* game -> trainer, every frame Harry is updated */
    volatile uint32_t frame;       /* counts Harry's updates: stops while paused or loading */
    volatile uint32_t player;      /* guest pointer of Harry's entity, 0 when there is none */
    volatile float pos[3];         /* world position, y up */
    volatile float yaw;
    volatile int16_t health, health_max;
    volatile uint32_t spells;      /* the spell word */
    volatile uint16_t cards;       /* wizard cards held (of 101) */
    volatile uint16_t cards_given; /* of them, given by the trainer */
    volatile int16_t beans, beans_max;
    volatile uint16_t level;       /* the level table index being played */
    volatile uint8_t noclip_on;    /* the game is flying Harry */
    volatile uint8_t undo_ready;   /* the values from before the first trainer change are kept */

    /* trainer -> game */
    volatile uint32_t noclip;      /* 1: Harry's update is skipped, the left stick flies him */
    volatile float fly_speed;      /* units a second (0: default) */
    volatile uint32_t god;         /* 1: health kept at its maximum */
    volatile uint32_t all_spells;  /* 1: every spell bit kept set */
    volatile uint32_t tp_seq;      /* bumped to move Harry to tp_pos */
    volatile float tp_pos[3];
    volatile uint32_t cards_seq;   /* bumped: every missing wizard card, the title's way (more health each 10th) */
    volatile uint32_t cards_remove_seq;  /* bumped: the cards the trainer gave taken back */
    volatile uint32_t health_fill_seq;   /* bumped: health up to the maximum */
    volatile uint32_t health_max_seq;    /* bumped: the maximum up to the title's cap (255) and full */
    volatile uint32_t beans_seq;   /* bumped: the bean bag filled */
    volatile uint32_t bag_seq;     /* bumped: a bigger bag (+50, the title's upgrade step) */
    volatile uint32_t beans_set_seq;     /* bumped: beans = beans_value (up to the bag) */
    volatile int16_t beans_value, pad2;
    volatile uint32_t undo_seq;    /* bumped: everything back as before the first trainer change */
    volatile uint32_t warp_seq;    /* bumped: load level warp_level at start point warp_start */
    volatile uint16_t warp_level, warp_start;
    volatile uint16_t warp_replay, pad3;  /* 1: the level's progress forgotten first, so the start sets it as new */

    /* the debug report: the trainer bumps report_seq with report_dir set; the game's own thread (not the title's,
     * so a hung game still answers) writes its stacks and state there, asks for a frame capture and sets
     * report_done = report_seq */
    volatile uint32_t report_seq, report_done;
    char report_dir[260];

    /* trainer -> game: with cards_seq, a card left out (101: Harry Potter's, Dumbledore's to give; 0 none) */
    volatile uint32_t cards_skip;

    /* trainer / tools -> game: pad input added to port 0 (hle_input.c), for driving the game without a person
     * (profiling, tests): buttons (XINPUT wButtons), the 8 analog buttons (A B X Y Black White LT RT, 0-255) and
     * the left / right stick, held for inject_ms from when inject_seq is bumped */
    volatile uint32_t inject_seq, inject_ms;
    volatile uint16_t inject_buttons, pad4;
    volatile uint8_t inject_analog[8];
    volatile int16_t inject_stick[4];
} HpcosTrainerShm;

#endif
