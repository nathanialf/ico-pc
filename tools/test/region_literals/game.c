/* tools/test/region_literals_test.py: a game file (copied in as
   ico2/common/src/layout_action.c) with port blocks and game code */
int la_vibe_select(void)
{
    gflagOn(382); /* the game's own line: not a port site */
    return 6;
}

/* PC port: a port function under a top-level comment */
void la_host_new_game_go(void)
{
    gflagInit();
    gflagOn(382);
}

int la_key_config(int first)
{
    if (first) {
        /* PC port: back to the port's screen */
        return 58;
    }
    return 50; /* the game's own */
}

int la_host_check(int n)
{
    return n > 105;
}
