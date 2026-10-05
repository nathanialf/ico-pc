/* the hand manager, included at file scope before actGirlHand */ /* clang-format off */
void HandMgr_GetDistHand(float *dist, float *height)
{
    union GAIF g[4], b[4];
    int n, m;
    n = GetSkeltonFocusNode(girlGObj, 0x12);

    g[0].f = *(float *)((n << 6) + GOBJ_SUB(girlGObj)->nodeMtx + 0x30); g[1].f = *(float *)((n << 6) + GOBJ_SUB(girlGObj)->nodeMtx + 0x34); g[2].f = *(float *)((n << 6) + GOBJ_SUB(girlGObj)->nodeMtx + 0x38);
    m = GetSkeltonFocusNode(boyGObj, 2);

    b[0].f = *(float *)((m << 6) + GOBJ_SUB(boyGObj)->nodeMtx + 0x30); b[1].f = *(float *)((m << 6) + GOBJ_SUB(boyGObj)->nodeMtx + 0x34); b[2].f = *(float *)((m << 6) + GOBJ_SUB(boyGObj)->nodeMtx + 0x38);


    *dist = _DistGV(g, b);
    *height = g[1].f - (b[1].f - 40.0f) < 0.0f ? -(g[1].f - (b[1].f - 40.0f))
                                               : g[1].f - (b[1].f - 40.0f);
}

__inline void HandMgr_Init(void) { GetRootPosition(handmgr.prev, girlGObj); }

void HandMgr_Update(void)
{
    GetRootPosition(handmgr.cur, girlGObj);
    handmgr.moveDist = _DistGV(handmgr.cur, handmgr.prev);
    handmgr.orient[0] = test_CURRENTORIENT(girlGObj)[0]; handmgr.orient[1] = test_CURRENTORIENT(girlGObj)[1]; handmgr.orient[2] = test_CURRENTORIENT(girlGObj)[2];
    _OrientXZGV(handmgr.toBoy, test_CURRENTROOT(boyGObj), handmgr.cur);
    HandMgr_GetDistHand(&handmgr.handDist, &handmgr.handHeight);

    handmgr.still = 0;
    handmgr.turned = 0;
    handmgr.far100 = 0;
    handmgr.far125 = 0;
    handmgr.far135 = 0;
    handmgr.near90 = 0;
    handmgr.heightGap = 0;


    handmgr.prev[0] = handmgr.cur[0]; handmgr.prev[1] = handmgr.cur[1]; handmgr.prev[2] = handmgr.cur[2];
}

void HandMgr_Judge(void)
{
    float one = 1.0f;
    if (handmgr.moveDist < 2.0f) {
        handmgr.still = 1;
    }
    if ((_RotyGV(handmgr.toBoy, handmgr.orient) < 0
             ? -_RotyGV(handmgr.toBoy, handmgr.orient)
             : _RotyGV(handmgr.toBoy, handmgr.orient)) >= 0x3D) {
        handmgr.turned = 1;
    }
    if (handmgr.handHeight > 15.0f) {
        handmgr.heightGap = 1;
    }
    if (one * 100.0f < handmgr.handDist) {
        handmgr.far100 = 1;
    }
    if (one * 125.0f < handmgr.handDist) {
        handmgr.far125 = 1;
    }
    if (one * 135.0f < handmgr.handDist) {
        handmgr.far135 = 1;
    }
    if (handmgr.handDist < 90.0f) {
        handmgr.near90 = 1;
    }
}

void HandMgr_Print(void)
{
    if (debug_font_flag & 1) {
        debug_Printf(10, 60, 0xFFFFFFF, "[%d] [%d]", (int)handmgr.handDist,
                     (int)handmgr.handHeight);
        if (debug_font_flag & 1) {
            debug_Printf(10, 70, 0xFFFFFFF, "gv[%d] mo[%d] hd[%d] hd2[%d]\n", handmgr.still,
                         handmgr.turned, handmgr.far125, handmgr.near90);
            if (debug_font_flag & 1) {
                debug_Printf(10, 80, 0xFFFFFFF, "\tgv[%d] mo[%d] hd[%d]\n", (int)handmgr.moveDist,
                             _RotyGV(handmgr.toBoy, handmgr.orient),
                             (int)handmgr.handDist);
            }
        }
    }
}

void HandMgr_Speed(GObj *self, float f)
{
    float lim = 1.5f;
    int m;
    if (handmgr.heightGap) {
        f = f + f;
        lim = 2.0f;
    }
    f = f < 1.0f ? 1.0f : (lim < f ? lim : f);
    f = f < 1.0f ? 1.0f : (_ACTGame_GetParamF(8) < f ? _ACTGame_GetParamF(8) : f);
    ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, f, 2);
    m = GOBJ_ACT(boyGObj)->actMode;
    if ((unsigned int)m < 4) {
        if (m != 0) {
            float one = 1.0f;
            float r;
            if (one < f) {
                f = one - (f - one);
            }
            r = f < _ACTGame_GetParamF(7) ? _ACTGame_GetParamF(7) : (one < f ? one : f);
            ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, r, 2);
        }
    }
    if (handmgr.handDist < 60.0f) {
        ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, 0.9f, 3);
    }
    if (90.0f < handmgr.handDist) {
        float r;
        if (GOBJ_ACT(boyGObj)->curItem != 0) {
            r = 2.0f;
        } else if (systemStatus[0] == 1) {
            r = (handmgr.handDist - 90.0f) / 120.0f + 1.1f;
            r = r < 1.0f ? 1.0f : (r > 1.4 ? 1.4f : r);
        } else {
            r = 1.3f;
        }
        ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, r, 3);
    }
}
