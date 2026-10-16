/*
 * port/platform/test/pump_watch_test.c
 *
 * The event pump's history (pump_watch.h, issue 105): the message names
 * (the system command's display power, the power and device-change
 * kinds, an unknown id), the noisy messages left out, a repeat of the
 * newest message or event counted instead of stored, the ring keeping
 * the newest 16 messages and 8 events newest first with their age, the
 * message part absent until a message was recorded, the handling stack
 * (how long a message took, the innermost one named while it runs) and
 * the watchdog's description of a pump that has not returned; the
 * controllers' arrivals and removals (a removal named from its arrival,
 * the driver from the GUID's signature byte) and SDL's own log ring (the
 * newest 64 lines oldest first, a time bound, newlines folded, a cut
 * buffer).
 */
#include <stdio.h>
#include <string.h>
#include "pump_watch.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define S(x) ((uint64_t)(x) * 1000000000ull)

static void names(void)
{
    char b[128];

    pump_watch_msg_name(0x0112, 0xF170, 2, b, sizeof(b));
    CHECK(strcmp(b, "WM_SYSCOMMAND SC_MONITORPOWER off(0x0112)") == 0);
    pump_watch_msg_name(0x0112, 0xF172, -1, b, sizeof(b)); /* the low bits are the system's */
    CHECK(strcmp(b, "WM_SYSCOMMAND SC_MONITORPOWER on(0x0112)") == 0);
    pump_watch_msg_name(0x0112, 0xF140, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_SYSCOMMAND SC_SCREENSAVE(0x0112)") == 0);
    pump_watch_msg_name(0x0112, 0x1234, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_SYSCOMMAND 0x1234(0x0112)") == 0);
    pump_watch_msg_name(0x0218, 0x8013, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_POWERBROADCAST PBT_POWERSETTINGCHANGE(0x0218)") == 0);
    pump_watch_msg_name(0x0219, 0x8004, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_DEVICECHANGE DBT_DEVICEREMOVECOMPLETE(0x0219)") == 0);
    pump_watch_msg_name(0x0219, 0x0007, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_DEVICECHANGE DBT_DEVNODES_CHANGED(0x0219)") == 0);
    pump_watch_msg_name(0x00FE, 2, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_INPUT_DEVICE_CHANGE GIDC_REMOVAL(0x00fe)") == 0);
    pump_watch_msg_name(0x0006, 0, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_ACTIVATE WA_INACTIVE(0x0006)") == 0);
    pump_watch_msg_name(0x007E, 32, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_DISPLAYCHANGE(0x007e)") == 0);
    pump_watch_msg_name(0x031E, 0, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_DWMCOMPOSITIONCHANGED(0x031e)") == 0);
    pump_watch_msg_name(0x0113, 1200, 0, b, sizeof(b));
    CHECK(strcmp(b, "WM_TIMER(0x0113)") == 0);
    pump_watch_msg_name(0xC0F3, 0, 0, b, sizeof(b));
    CHECK(strcmp(b, "message 0xc0f3") == 0);
    /* cut, never past the buffer */
    memset(b, 'x', sizeof(b));
    pump_watch_msg_name(0x0219, 0x8004, 0, b, 8);
    CHECK(strlen(b) == 7);
}

static void history(void)
{
    char b[2048];
    int i;

    pump_watch_reset();
    /* nothing yet: no message part, no events */
    pump_watch_recent(b, sizeof(b), S(10));
    CHECK(strcmp(b, "SDL events before it: none") == 0);

    /* noisy ones are not kept */
    CHECK(pump_watch_msg(0x0200, 0, 0, S(1)) == 0);
    CHECK(pump_watch_msg(0x00FF, 0, 0, S(1)) == 0);
    pump_watch_recent(b, sizeof(b), S(10));
    CHECK(strstr(b, "last messages") == NULL);

    /* the device went away, then three timer ticks (one entry), newest
       first with their age at the stall's end */
    CHECK(pump_watch_msg(0x0219, 0x8004, 0, S(2)) == 1);
    CHECK(pump_watch_msg(0x0113, 1200, 0, S(3)) == 1);
    CHECK(pump_watch_msg(0x0113, 1200, 0, S(4)) == 1);
    CHECK(pump_watch_msg(0x0113, 1200, 0, S(5)) == 1);
    pump_watch_event("gamepad removed", S(2));
    pump_watch_event("joystick removed", S(2));
    pump_watch_event("joystick removed", S(3));
    pump_watch_recent(b, sizeof(b), S(55));
    CHECK(strcmp(b, "last messages before it: WM_TIMER(0x0113) x3 50.0 s ago, WM_DEVICECHANGE "
                    "DBT_DEVICEREMOVECOMPLETE(0x0219) 53.0 s ago; SDL events before it: joystick "
                    "removed x2 52.0 s ago, gamepad removed 53.0 s ago") == 0);
    if (fails) {
        printf("got: %s\n", b);
    }

    /* the ring keeps the newest 16 messages and 8 events */
    pump_watch_reset();
    for (i = 0; i < 20; i++) {
        pump_watch_msg(0x0100 + (uint32_t)(i % 2) * 0x12, (uint64_t)i, 0, S(i));
    }
    for (i = 0; i < 10; i++) {
        pump_watch_event(i % 2 ? "focus lost" : "focus gained", S(i));
    }
    pump_watch_recent(b, sizeof(b), S(20));
    {
        int count = 0;
        const char *p = b;

        while ((p = strstr(p, "s ago")) != NULL) {
            count++;
            p++;
        }
        CHECK(count == 16 + 8);
    }
    CHECK(strstr(b, " 1.0 s ago,") != NULL);  /* message 19, the newest */
    CHECK(strstr(b, " 16.0 s ago;") != NULL); /* message 4, the oldest kept */
    CHECK(strstr(b, "WM_SYSCOMMAND 0x13(0x0112) 1.0 s ago,") != NULL);
    CHECK(strstr(b, "WM_SYSCOMMAND 0x3(0x0112)") == NULL); /* message 3 is gone */
    CHECK(strstr(b, "focus lost 11.0 s ago,") != NULL);    /* event 9 */
    CHECK(strstr(b, "focus gained 18.0 s ago") != NULL);   /* event 2, the oldest kept */

    /* a cut line stays terminated inside the buffer */
    memset(b, 'x', sizeof(b));
    pump_watch_recent(b, 40, S(20));
    CHECK(strlen(b) == 39);
}

static void handling(void)
{
    char b[1024];
    char name[96];
    uint64_t took;

    pump_watch_reset();
    pump_watch_part("the system's messages and SDL's own work (SDL_PollEvent)");
    pump_watch_describe(b, sizeof(b), S(1));
    CHECK(strcmp(b, "in the system's messages and SDL's own work (SDL_PollEvent), no window "
                    "message being handled; SDL events before it: none") == 0);
    if (fails) {
        printf("got: %s\n", b);
    }

    /* a sent message inside a posted one: the innermost is named */
    pump_watch_msg(0x0219, 0x8004, 0, S(2));
    pump_watch_msg_enter(0x0219, 0x8004, 0, S(2));
    pump_watch_msg(0x0112, 0xF170, 2, S(3));
    pump_watch_msg_enter(0x0112, 0xF170, 2, S(3));
    pump_watch_describe(b, sizeof(b), S(53));
    CHECK(strncmp(b,
                  "in the system's messages and SDL's own work (SDL_PollEvent), handling "
                  "WM_SYSCOMMAND SC_MONITORPOWER off(0x0112) for 50.0 s; last messages before "
                  "it: WM_SYSCOMMAND SC_MONITORPOWER off(0x0112) 50.0 s ago, WM_DEVICECHANGE",
                  strlen("in the system's messages and SDL's own work (SDL_PollEvent), handling "
                         "WM_SYSCOMMAND SC_MONITORPOWER off(0x0112) for 50.0 s; last messages "
                         "before it: WM_SYSCOMMAND SC_MONITORPOWER off(0x0112) 50.0 s ago, "
                         "WM_DEVICECHANGE")) == 0);
    if (fails) {
        printf("got: %s\n", b);
    }
    took = pump_watch_msg_leave(S(53), name, sizeof(name));
    CHECK(took == S(50));
    CHECK(strcmp(name, "WM_SYSCOMMAND SC_MONITORPOWER off(0x0112)") == 0);
    took = pump_watch_msg_leave(S(54), name, sizeof(name));
    CHECK(took == S(52));
    CHECK(strcmp(name, "WM_DEVICECHANGE DBT_DEVICEREMOVECOMPLETE(0x0219)") == 0);
    /* one leave too many: nothing */
    CHECK(pump_watch_msg_leave(S(55), name, sizeof(name)) == 0);
    CHECK(name[0] == '\0');
    pump_watch_part(NULL);
    CHECK(pump_watch_part_now() == NULL);
    pump_watch_describe(b, sizeof(b), S(60));
    CHECK(strncmp(b, "in the pump's own code, no window message being handled; ", 57) == 0);
}

static void pads(void)
{
    char b[1024];
    unsigned char g[16];

    memset(g, 0, sizeof(g));
    g[14] = 'x';
    CHECK(strcmp(pump_watch_guid_driver(g, 1), "XInput") == 0);
    g[14] = 'h';
    CHECK(strcmp(pump_watch_guid_driver(g, 1), "HIDAPI") == 0);
    g[14] = 0;
    CHECK(strcmp(pump_watch_guid_driver(g, 1), "DirectInput") == 0);
    CHECK(strcmp(pump_watch_guid_driver(g, 0), "the system's") == 0);
    g[14] = 'q';
    CHECK(strcmp(pump_watch_guid_driver(g, 1), "driver 'q'") == 0);

    pump_watch_reset();
    CHECK(pump_watch_pad_last_ns() == 0);
    pump_watch_pads_recent(b, sizeof(b), S(10));
    CHECK(b[0] == '\0');
    pump_watch_pad(1, 7, "Xbox Controller", "030000005e0400000b0b000000007800", "XInput", S(1));
    /* the removal names the arrival's device */
    pump_watch_pad(0, 7, NULL, NULL, NULL, S(4));
    CHECK(pump_watch_pad_last_ns() == S(4));
    pump_watch_pads_recent(b, sizeof(b), S(54));
    CHECK(strcmp(b,
                 "controllers before it: removed \"Xbox Controller\" (GUID "
                 "030000005e0400000b0b000000007800, XInput) 50.0 s ago, added \"Xbox "
                 "Controller\" (GUID 030000005e0400000b0b000000007800, XInput) 53.0 s ago") == 0);
    if (fails) {
        printf("got: %s\n", b);
    }
    /* in the slow-pump line after the SDL events */
    pump_watch_event("joystick removed", S(4));
    pump_watch_recent(b, sizeof(b), S(54));
    CHECK(strncmp(b,
                  "SDL events before it: joystick removed 50.0 s ago; controllers before it: "
                  "removed \"Xbox Controller\"",
                  strlen("SDL events before it: joystick removed 50.0 s ago; controllers before "
                         "it: removed \"Xbox Controller\"")) == 0);
    /* an unknown id's removal */
    pump_watch_pad(0, 99, NULL, NULL, NULL, S(5));
    pump_watch_pads_recent(b, sizeof(b), S(6));
    CHECK(strncmp(b, "controllers before it: removed \"(unnamed)\" (GUID ?, ?) 1.0 s ago", 63) ==
          0);
}

static void sdl_ring(void)
{
    static char b[64 * (PUMP_WATCH_SDL_TEXT + 96)];
    char line[48];
    int i;

    pump_watch_reset();
    CHECK(pump_watch_sdl_text(b, sizeof(b), "", 0, S(1)) == 0);
    CHECK(b[0] == '\0');
    pump_watch_sdl_log(7, 3, "Added HIDAPI device 'Xbox'\nsecond", S(1));
    pump_watch_sdl_log(3, 5, "warning", S(2));
    CHECK(pump_watch_sdl_count() == 2);
    CHECK(pump_watch_sdl_text(b, sizeof(b), "  ", 0, S(11)) == 2);
    CHECK(strcmp(b, "  10.0 s ago INPUT debug: Added HIDAPI device 'Xbox' second\n"
                    "  9.0 s ago SYSTEM warn: warning\n") == 0);
    if (fails) {
        printf("got: %s\n", b);
    }
    /* the time bound */
    CHECK(pump_watch_sdl_text(b, sizeof(b), "", S(2), S(11)) == 1);
    CHECK(strstr(b, "SYSTEM warn") != NULL);
    /* the ring keeps the newest 64, oldest first */
    for (i = 0; i < 70; i++) {
        snprintf(line, sizeof(line), "line %d", i);
        pump_watch_sdl_log(99, 2, line, S(3));
    }
    CHECK(pump_watch_sdl_count() == 72);
    CHECK(pump_watch_sdl_text(b, sizeof(b), "", 0, S(4)) == 64);
    CHECK(strncmp(b, "1.0 s ago custom verbose: line 6\n", 33) == 0);
    CHECK(strstr(b, "line 69\n") != NULL);
    CHECK(strstr(b, "line 5\n") == NULL);
    /* a small buffer: whole lines only */
    CHECK(pump_watch_sdl_text(b, 60, "", 0, S(4)) == 1);
    CHECK(strcmp(b, "1.0 s ago custom verbose: line 6\n") == 0);
}

int main(void)
{
    names();
    history();
    handling();
    pads();
    sdl_ring();
    if (fails) {
        printf("pump_watch_test: %d failures\n", fails);
        return 1;
    }
    printf("pump_watch_test: ok\n");
    return 0;
}
