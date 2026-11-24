/*
 * port/game/yorda_look.h
 *
 * Look at Yorda ([gameplay] yorda_look, options.h; GAME_FEATURES.md "Look
 * at Yorda").
 *
 * In the game the boy looks at Yorda while he calls her: each copy of
 * subBoyCollision (ico2/fumi/src/boyact.c, the European body and the _ntsc,
 * _jun26 and _aug6 twins) turns his head to her (_ACTLookTarget_Set) and,
 * when the stage's script allows the call camera, makes the camera follow
 * the boy and her together (Camctrl_SetTarget at priority 1, held only
 * while it is asked for again each tick).  The call itself is the boy's
 * wish bits and the mails sent to her, and her brain answers those.  The
 * port already has one look without a call: with Players 2, the second
 * player's R1 (Yorda's pad) runs the same head turn and camera.
 *
 * With the option on, L3 (the left stick click, the game's pad bit 0x200,
 * read raw from pad 0 as Camera reset reads R3) asks for that same look:
 * Hold looks while L3 is held, Toggle starts the look with one press and
 * ends it with the next.  No wish bit is set and no mail is sent: Yorda
 * does not come.  When she is not on the stage the look turns the camera
 * toward her other stage, as the call button does there.
 *
 * The look only runs while the boy is under the player's control (the arm
 * passes that as live: not in a cutscene, not during the cut to her
 * capture, not with Developer > Free movement, which has its own L3), and
 * never while he holds her hand.  A toggled look ends on the next press, a
 * stretch without control, a stage change, a Camera reset (R3; camera_reset.h)
 * and her hand taken; a call (R1) leaves it on.
 *
 * The press is the L3 level against the previous tick's, with the Main tick
 * clock as Camera reset's: a call whose tick does not follow the last one
 * (after a pause, photo mode, a menu or a load) takes L3 already down as
 * held, not as a press.
 *
 * The state (the last level, the toggle, the stage, the last answer) is the
 * port's: one FRESH save-state block, "yorda_look.state".  Off, every call
 * returns 0 and the arms run as the game shipped.
 */
#ifndef ICO_PORT_GAME_YORDA_LOOK_H
#define ICO_PORT_GAME_YORDA_LOOK_H

/* One tick of the boy's look block.  l3: the pad's L3 bit (pad[0].now &
   0x200); live: the boy is under the player's control (the arm's test);
   hand: he holds Yorda's hand (ACTGame_FLAG_TETSUNAGI); stage: the stage
   number (a change ends a toggled look).  Returns 1 when the arm should run
   the look this tick, else 0. */
int ico_yorda_look(int l3, int live, int hand, int stage);
/* Camera reset fired (camera_reset.c): a toggled look ends. */
void ico_yorda_look_drop(void);
/* The Main tick count (trace_host.h ico_host_main_ticks; the program sets
   it at start-up, a test its fake; NULL: none), as ico_cam_reset_set_clock. */
void ico_yorda_look_set_clock(unsigned (*clock)(void));
/* Nothing recorded, no toggle (tests). */
void ico_yorda_look_clear(void);
/* The save-state block (state_port.h): 0, or -1 when refused. */
int ico_yorda_look_state_register(void);
/* Looks started this run (the test and the log). */
unsigned long ico_yorda_look_count(void);

#endif /* ICO_PORT_GAME_YORDA_LOOK_H */
