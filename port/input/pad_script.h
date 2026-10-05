/*
 * port/input/pad_script.h
 *
 * A scripted controller for the headless build (--pad-script FILE). The
 * null pad (port/input/pad_host.c) reports a DualShock in port 0 when a
 * script is loaded and hands libpad's scePadRead the script's values; with
 * no script it stays "no controller in either port".
 *
 * Script format, one entry per line:
 *
 *     <tick> <buttons-hex> [lx ly rx ry]
 *
 *   - `#` starts a comment; blank lines are ignored.
 *   - <tick>: a decimal Main tick (below). From that tick on, the pad
 *     returns this line's values, until the next line's tick. Ticks must
 *     increase strictly from line to line.
 *   - <buttons-hex>: the game's logical button mask, active high (1 =
 *     pressed), in hex with or without a 0x prefix, 0 to ffff (bits below).
 *   - lx ly rx ry: the analog sticks, 0 to 255 each, decimal or 0x-hex
 *     (128 = centre; 0 = left/up). All four or none; omitted means centred.
 *   - Before the first line's tick the pad is connected with nothing
 *     pressed and the sticks centred.
 *
 * Tick. A Main tick is one pass of the game's Main loop (common/src/
 * main.c, Main, the while loop from iosThreadSleep to frameReady = 1),
 * normally once every systemStatus[1] vsyncs (2: 25 per second in PAL).
 * Ticks count from 0: tick t is the (t+1)-th pass. The host counts a tick
 * when Main reports it done (ico_host_main_tick, port/platform/
 * trace_host.c), so while Main runs tick t the counter reads t, and the
 * pad read of tick t (Main -> ExecKeyInput -> iosPadDevRead -> the pad
 * manager thread -> scePadRead, sugipon/src/keyInput.c:40) sees the line
 * for tick t. --trace writes its line for tick t after that tick, and
 * --ticks N stops after ticks 0..N-1: the numbering is the same in all
 * three. Ticks are not vsyncs: a movie (main.c:167-193) or a stage load
 * may take many vsyncs and no Main tick.
 *
 * Buttons. <buttons-hex> is the word the game itself works with:
 * pad[0].now / pad[0].flags (keyInput.c:45-46), which iosPadRead builds as
 * ((byte 2 << 8) | byte 3) ^ 0xFFFF of libpad's buffer (fumi/ios/pad.c:369-
 * 382, fumi/include/pad.h:15-24, through iosPadConfDefault's identity bit
 * table, pad.c:62-63). That is SCE libpad's own order (byte 2 high), not
 * the ps2sdk order (byte 2 low). The game confirms it: movies skip on
 * flags & 0x0800 = START (common/src/main.c:392); the menus move on 0x1000
 * up, 0x4000 down, 0x8000 left, 0x2000 right (common/src/kanban.c:127-
 * 133, fumi/ios/pad.c:484-494); 0x0040 confirms and 0x0010 cancels
 * (kanban.c:138-141, layout_action.c:128-139, the PAL release's CROSS and
 * TRIANGLE).
 */
#ifndef ICO_PORT_INPUT_PAD_SCRIPT_H
#define ICO_PORT_INPUT_PAD_SCRIPT_H
/* The logical buttons: libpad byte 3 (active low on the wire) is the low
   byte, byte 2 the high byte. */
#define ICO_PAD_L2 0x0001
#define ICO_PAD_R2 0x0002
#define ICO_PAD_L1 0x0004
#define ICO_PAD_R1 0x0008
#define ICO_PAD_TRIANGLE 0x0010
#define ICO_PAD_CIRCLE 0x0020
#define ICO_PAD_CROSS 0x0040
#define ICO_PAD_SQUARE 0x0080
#define ICO_PAD_SELECT 0x0100
#define ICO_PAD_L3 0x0200
#define ICO_PAD_R3 0x0400
#define ICO_PAD_START 0x0800
#define ICO_PAD_UP 0x1000
#define ICO_PAD_RIGHT 0x2000
#define ICO_PAD_DOWN 0x4000
#define ICO_PAD_LEFT 0x8000
#define ICO_PAD_STICK_CENTRE 0x80

/* What the scripted pad holds at one tick. */
typedef struct IcoPadFrame {
    unsigned int buttons; /* logical, active high */
    unsigned char lx, ly, rx, ry;
} IcoPadFrame;

/* Load a script file, replacing any loaded script. 0, or -1 with a message
   on stderr naming the file and line (nothing is loaded then). */
int ico_pad_script_load(const char *path);
/* The same from a NUL-terminated text; `name` is used in messages. */
int ico_pad_script_parse(const char *text, const char *name);
/* Forget the script: the pad is unplugged again. */
void ico_pad_script_clear(void);
/* 1 while a script is loaded (a controller is plugged into port 0). */
int ico_pad_script_active(void);
/* Number of entries loaded. */
int ico_pad_script_count(void);
/* The current Main tick, which the host sets as ticks complete. */
void ico_pad_script_set_tick(unsigned int tick);
unsigned int ico_pad_script_tick(void);
/* The values at `tick` (or at the current tick). Released and centred
   before the first entry, or with no script. */
void ico_pad_script_frame_at(unsigned int tick, IcoPadFrame *out);
void ico_pad_script_frame(IcoPadFrame *out);

#endif /* ICO_PORT_INPUT_PAD_SCRIPT_H */
