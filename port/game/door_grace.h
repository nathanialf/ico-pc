/*
 * port/game/door_grace.h
 *
 * The doorway fix ([gameplay] door_fix, options.h; issue 53).
 *
 * The original game (on a real PS2 too) has a doorway that sends Ico and
 * Yorda straight back out when they go through it holding hands: the East
 * Reflector's door at the top of the ladder.  Exits are floor attributes
 * that Ico checks every frame (CheckCollisionAttr, ico2/fumi/src/boyact.c).
 * After an arrival the first exit floor he touches is only remembered, and
 * once he has been off every exit floor for a frame, any exit floor he steps
 * on changes the stage.  Holding hands, Yorda is placed on Ico's arrival
 * point and the hand connect starts at once, so a push onto the floor of the
 * door back fires that door and both go back.
 *
 * With the option on, an exit taken while holding hands arms a grace; the
 * next stage start begins it when the stage is the exit's destination.  For
 * the grace's frames any exit floor leading back to the stage left is only
 * remembered, as the game does with the first floor after an arrival.  When
 * the grace ends, a floor he still stands on needs a step off and back on
 * (the game's own rule); every other floor works as before.  Scripted stage
 * changes never arm it, and an arm is used up by the next stage start.
 *
 * Pure state: no game headers, game frames only, nothing enters a save.
 */
#ifndef ICO_PORT_GAME_DOOR_GRACE_H
#define ICO_PORT_GAME_DOOR_GRACE_H

/* An exit fired from from_stage to to_stage; holding_hands non-zero when
   Ico held Yorda's hand.  Replaces any earlier arm. */
void ico_door_grace_arm(int from_stage, int to_stage, int holding_hands);
/* A stage start: always clears the arm.  Starts a grace of `frames` game
   frames (and returns 1) only when the option is on, an arm is pending, it
   was made holding hands, `stage` is its destination and frames > 0;
   otherwise ends any grace and returns 0. */
int ico_door_grace_begin(int stage, int frames);
/* 1 while a grace has frames left */
int ico_door_grace_active(void);
/* One game frame of the grace passes */
void ico_door_grace_tick(void);
/* 1 when an exit leading to dest_stage must only be remembered: a grace is
   active and dest_stage is the stage it came from (0 for dest_stage < 0) */
int ico_door_grace_holds(int dest_stage);
/* No arm, no grace */
void ico_door_grace_reset(void);

#endif /* ICO_PORT_GAME_DOOR_GRACE_H */
