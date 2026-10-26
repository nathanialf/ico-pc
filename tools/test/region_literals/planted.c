/* tools/test/region_literals_test.py: a port file with one planted finding
   per category, and lines the check must leave alone */
int planted(int stage_no, int layout)
{
    /* findings */
    if (gflagChk(349)) {
        return texLayout[57].first;
    }
    lt_mask_property(294, 1);
    if (stage_no == 60) {
        return 1;
    }
    if (pad[0].flags & 0x40) {
        return 2;
    }
    return confirm == LT_PAD_CROSS;
    /* routed or not a number of a disc's table: no finding */
    gflagOn(ico_region_cur()->gflag_new_game);
    if (layout == PAL_JPNKORCN(57, 54)) {
        return 3;
    }
    if (layout != 0 && stage_no == -1) {
        return 4;
    }
    /* gflagOn(382) in a comment */
    return pad_bits & ico_region_cur()->pad_confirm;
}

/* a disc's own arm: its numbers are its own */
int planted_ntsc(void)
{
    gflagOn(349);
    return 0;
}
