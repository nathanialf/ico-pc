/*
 * port/game/door_grace.c
 *
 * The doorway fix's arm and grace (door_grace.h).
 */
#include "door_grace.h"

#include "options.h"

/* the pending arm from the last exit fired by Ico */
static int s_armed;
static int s_arm_from = -1;
static int s_arm_to = -1;
static int s_arm_holding;

/* the grace: frames left and the stage it leads back to */
static int s_left;
static int s_from = -1;

void ico_door_grace_arm(int from_stage, int to_stage, int holding_hands)
{
    s_armed = 1;
    s_arm_from = from_stage;
    s_arm_to = to_stage;
    s_arm_holding = holding_hands != 0;
}

int ico_door_grace_begin(int stage, int frames)
{
    int start = s_armed && s_arm_holding && stage == s_arm_to && frames > 0 && ico_opt_door_fix();

    s_left = start ? frames : 0;
    s_from = start ? s_arm_from : -1;
    s_armed = 0;
    s_arm_from = s_arm_to = -1;
    s_arm_holding = 0;
    return start;
}

int ico_door_grace_active(void)
{
    return s_left > 0;
}

void ico_door_grace_tick(void)
{
    if (s_left > 0) {
        s_left--;
    }
}

int ico_door_grace_holds(int dest_stage)
{
    return dest_stage >= 0 && s_left > 0 && dest_stage == s_from;
}

void ico_door_grace_reset(void)
{
    s_armed = 0;
    s_arm_from = s_arm_to = -1;
    s_arm_holding = 0;
    s_left = 0;
    s_from = -1;
}
