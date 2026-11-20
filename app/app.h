/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Chordism on the FM-1: one key in, a chord out (the Chordism module for Schwung, app/dsp/chd.c). The app's half of
 * the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "chd.h"

#define APP_NAME "CHORDISM"
#define APP_SPLASH_RGB RGB(80, 255, 170)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */
#define APP_BTDEV                        /* Bluetooth MIDI devices (lib/btdev.h) */
#define APP_BLE_AT_BOOT (!chd_ble_off())  /* Bluetooth at power-on: as the player left it (REC held) */
int chd_ble_off(void);

/* the store: the settings (the sound playing, the player's choices), the panel calibration (the platform's), Bluetooth's
 * address and pairing, and the user sounds, eight to an object, in the data area (0x97000..) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_USER0, OBJ_USER1, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0x97000u, 0x99000u}
#define NUSER 16                         /* user sounds: SAVE held + white key 1..16 */

/* the objects a computer may keep as files (companion): the settings and the user sounds */
#define APP_FILE_OBJS {{OBJ_SETTINGS, PLAT_FK_SETTINGS, PLAT_FF_READ | PLAT_FF_WRITE, "settings"}, \
                       {OBJ_USER0, PLAT_FK_BANK, PLAT_FF_READ | PLAT_FF_WRITE, "sounds-1"}, \
                       {OBJ_USER1, PLAT_FK_BANK, PLAT_FF_READ | PLAT_FF_WRITE, "sounds-2"}}
int app_file_busy(void);
void app_file_changed(uint32_t obj);

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or the first preset) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

#define APP_REDRAW                       /* the screen mirror lost a part (or a viewer came): every band again */
void app_redraw(void);

/* the host simulator's checks (app.c app_expect) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
