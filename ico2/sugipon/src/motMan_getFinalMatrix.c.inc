/* motMan_getFinalMatrix.c.inc: the motion manager's final node matrices and their angle limits; motionManager.c includes this file. */
inline int SetDirectMotionProgramInterpInfo(GObj *self, int focus, float rate)
{
    int n = GetSkeltonFocusNode(self, focus);
    MotIk *e = &GOBJ_SUB(self)->nodeRotElem[n];
    if (n < 0)
        return 0;
    e->rate = rate;
    CopyQuaternion(e->step, IdentityQuaternion);
    CopyQuaternion(e->offset, IdentityQuaternion);
    return 1;
}

inline int ResetMotionProgramInterpInfo(GObj *self, int focus)
{
    int n = GetSkeltonFocusNode(self, focus);
    MotIk *e = &GOBJ_SUB(self)->nodeRotElem[n];
    if (n < 0)
        return 0;
    e->rate = 0.0f;
    CopyQuaternion(e->step, IdentityQuaternion);
    CopyQuaternion(e->offset, IdentityQuaternion);
    return 1;
}

/* Only _getFinalMatrix uses these two.  The first gives a point relative to
 * the root placed at the matrix's translation, the second the same vector in
 * the current matrix's local axes. */
static inline void getRootRelativePos(float *out, sceVu0FVECTOR *mtx, float *pos) /* derived name */
{
    float w[4];

    AddVectorXYZ(w, mtx[3], skelRoot->pos);
    SubVectorXYZ(out, pos, w);
    out[3] = 0.0f;
}

static inline void getLocalTargetDir(float *out, float *pos) /* derived name */
{
    float tmp[4];

    getRootRelativePos(tmp, MatrixDrive_GetMatrix(), pos);
    MatrixDrive_PushMatrixWithNoCopy();
    MatrixDrive_SetTransposeMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetLastMatrix());
    sceVu0ApplyMatrix(out, MatrixDrive_GetMatrix(), tmp);
    MatrixDrive_PopMatrix();
}

/* the degree conversion and the clamped ratio limitHPAngleAndSetB repeats;
   32768 units are 180 degrees */
#define ANG_OF_DEG(x) ((x) * 32768.0f / 180.0f) /* derived name */
#define ANG_RATE(v, lim)                        /* derived name */                                 \
    ((lim) == 0.0f ? 0.0f : ((v) / (lim) < 0.0f ? -((v) / (lim)) : (v) / (lim)))

/* lim is the motion program's interp record, the same one limitInterpAngle
   reads at +0x30 and +0x48.  hp and pp are the heading and pitch of the turn
   being limited, bp the bank the record's tables interpolate from them;
   prevH is the heading the turn had, whose side the new one may not cross to,
   and prevP the pitch it had. */
static void limitHPAngleAndSetB(short *hp, short *pp, short *bp, MotLimAng *lim, short h,
                                short prevH, short p, short prevP, int setb, int setflag,
                                float rate)
{
    *hp = h;
    *pp = p;
    *bp = 0;

    if (lim != 0) {
        /* PC port (AN-19): every float stored to a short below goes through
           int, (short)(int)f: the EE's cvt.w.s and halfword store wrap a
           value past 32767, and so does the int's conversion to short on
           every host, where clang may keep a (short)f conversion's result
           as the unwrapped int (issue 21's mirrored opening; rev is 32768
           at a rate of 0) */
        short rev = (short)(int)((1.0f - rate) * 32768.0f);

        float lo = ANG_OF_DEG(lim[4].h);
        float hi = ANG_OF_DEG(lim[6].h);
        float pdn = lim[9].p;
        float pup = lim[1].p;
        float dnlim;
        float uplim;
        float t;
        float rp;

        if (prevH < 0 && *hp > 0) {
            int base = rev - 32768;
            short d = (short)(int)((-*hp - base) / rate);

            if ((float)d < ANG_OF_DEG(lim[7].p)) {
                *hp = (short)(int)lo;
                if (setflag) {
                    skelMotCtrl->flags = skelMotCtrl->flags | 0x100;
                }
            }
        }
        if (prevH > 0 && *hp < 0) {
            int base = rev - 32768;
            short d = (short)(int)((*hp - base) / rate);

            if ((float)d < ANG_OF_DEG(lim[7].p)) {
                *hp = (short)(int)hi;
                if (setflag) {
                    skelMotCtrl->flags = skelMotCtrl->flags | 0x200;
                }
            }
        }
        if ((float)*hp < lo) {
            *hp = (short)(int)lo;
            if (setflag) {
                skelMotCtrl->flags = skelMotCtrl->flags | 0x100;
            }
        }
        if (hi < (float)*hp) {
            *hp = (short)(int)hi;
            if (setflag) {
                skelMotCtrl->flags = skelMotCtrl->flags | 0x200;
            }
        }

        if (*hp >= 0) {
            t = ANG_RATE((float)*hp, ANG_OF_DEG(lim[6].h));

            dnlim = ANG_OF_DEG(pdn) + (ANG_OF_DEG(lim[10].p) - ANG_OF_DEG(pdn)) * t;

            uplim = ANG_OF_DEG(pup) + (ANG_OF_DEG(lim[2].p) - ANG_OF_DEG(pup)) * t;

        } else {
            /* the guard is the +0x48 limit, the division the +0x30 one, as in the
               positive arm above */
            /* PC port: the guard does not test the divisor, so a record
               with a zero +0x30 limit and a non-zero +0x48 one divides by
               zero: +-Fmax on the EE */
            t = ANG_OF_DEG(lim[6].h) == 0.0f ? 0.0f
                                             : (ps2_div((float)*hp, ANG_OF_DEG(lim[4].h)) < 0.0f
                                                    ? -ps2_div((float)*hp, ANG_OF_DEG(lim[4].h))
                                                    : ps2_div((float)*hp, ANG_OF_DEG(lim[4].h)));

            dnlim = ANG_OF_DEG(pdn) + (ANG_OF_DEG(lim[8].p) - ANG_OF_DEG(pdn)) * t;

            uplim = ANG_OF_DEG(pup) + (ANG_OF_DEG(lim[0].p) - ANG_OF_DEG(pup)) * t;
        }

        if ((float)*pp < dnlim) {
            *pp = (short)(int)dnlim;
        }
        if (uplim < (float)*pp) {
            *pp = (short)(int)uplim;
        }

        if (*hp >= 0) {
            float rh = ANG_RATE((float)*hp, hi);
            float va = (ANG_OF_DEG(lim[6].b) - ANG_OF_DEG(lim[5].b)) * rh + ANG_OF_DEG(lim[5].b);

            if (*pp >= 0) {
                float vb =
                    (ANG_OF_DEG(lim[2].b) - ANG_OF_DEG(lim[1].b)) * rh + ANG_OF_DEG(lim[1].b);

                rp = ANG_RATE((float)*pp, uplim);
                *bp = (short)(int)((vb - va) * rp + va);

            } else {
                float vb =
                    (ANG_OF_DEG(lim[10].b) - ANG_OF_DEG(lim[9].b)) * rh + ANG_OF_DEG(lim[9].b);

                rp = ANG_RATE((float)*pp, dnlim);
                *bp = (short)(int)((vb - va) * rp + va);
            }
        } else {
            float rh = ANG_RATE((float)*hp, lo);
            float va = (ANG_OF_DEG(lim[4].b) - ANG_OF_DEG(lim[5].b)) * rh + ANG_OF_DEG(lim[5].b);

            if (*pp >= 0) {
                float vb =
                    (ANG_OF_DEG(lim[0].b) - ANG_OF_DEG(lim[1].b)) * rh + ANG_OF_DEG(lim[1].b);

                rp = ANG_RATE((float)*pp, uplim);
                *bp = (short)(int)((vb - va) * rp + va);

            } else {
                float vb =
                    (ANG_OF_DEG(lim[8].b) - ANG_OF_DEG(lim[9].b)) * rh + ANG_OF_DEG(lim[9].b);

                rp = ANG_RATE((float)*pp, dnlim);
                *bp = (short)(int)((vb - va) * rp + va);
            }
        }

        if (setb) {
            if (*pp >= 0)
                *bp = (short)(int)((float)*bp * (1.0f - rp) + ANG_OF_DEG(lim[1].b) * rp);
            else
                *bp = (short)(int)((float)*bp * (1.0f - rp) + ANG_OF_DEG(lim[9].b) * rp);
        }
    }
}

/* Clamp a turn angle to the motion-program interp range; 0 when the angle
 * is already inside it. */
static inline short limitInterpAngle(MotLimAng *info, short ang) /* derived name */
{
    float lo;
    float hi;

    if (info != 0) {
        lo = info[4].h * 32768.0f / 180.0f;
        hi = info[6].h * 32768.0f / 180.0f;
        if ((float)ang < lo) {
            return (short)lo;
        }
        if (hi < (float)ang) {
            return (short)hi;
        }
    }
    return 0;
}

static int avoidReverseInterpOnCurrentMatrix(float *q, MotLimAng *info, void *out)
{
    float mtx[16];
    Vec16 up;
    Vec16 dir;
    /* PC port (AN-19): MatrixDrive_GetTurnYAngleXZ leaves ang alone for a
       direction along x (or a NaN one); the stack word the EE left there is
       made 0 so no build depends on its frame (issue 19) */
    short ang = 0;
    short ang2 = 0;

    GetMatrixFromQuaternion(mtx, q);
    _ApplyMatrix(&up, mtx, YUnitVector);
    MatrixDrive_SetTransposeMatrix(mtx, MatrixDrive_GetMatrix());
    _ApplyMatrix(&dir, mtx, &up);
    MatrixDrive_GetTurnYAngleXZ(&ang, &ang2, dir.f[0], dir.f[1], dir.f[2]);
    if (limitInterpAngle(info, ang) != 0) {
        MultiQuaternion(q, GetCurrentQuaternion(), out);
        return 1;
    }
    return 0;
}

/* 0 = inside the range, 1 = outside the plain range, 2 = outside the range
 * scaled by the limit factor; avoidReverseInterpOnCurrentMatrixWithLimit's
 * helper. */
static inline short classifyInterpAngle(MotLimAng *info, short ang, float k) /* derived name */
{
    float lo;
    float hi;
    float a;
    float b;

    if (info != 0) {
        lo = info[4].h * 32768.0f / 180.0f;
        hi = info[6].h * 32768.0f / 180.0f;
        a = lo * k;
        b = hi * k;
        if ((float)ang < a || b < (float)ang)
            return 2;
        if ((float)ang < lo || hi < (float)ang)
            return 1;
    }
    return 0;
}

static void avoidReverseInterpOnCurrentMatrixWithLimit(float *q, MotLimAng *info, void *outA,
                                                       void *outB, float k)
{
    float mtx[16];
    Vec16 up;
    Vec16 dir;
    /* PC port (AN-19): MatrixDrive_GetTurnYAngleXZ leaves ang alone for a
       direction along x (or a NaN one); the stack word the EE left there is
       made 0 so no build depends on its frame (issue 19) */
    short ang = 0;
    short ang2 = 0;

    GetMatrixFromQuaternion(mtx, q);
    _ApplyMatrix(&up, mtx, YUnitVector);
    MatrixDrive_SetTransposeMatrix(mtx, MatrixDrive_GetMatrix());
    _ApplyMatrix(&dir, mtx, &up);
    MatrixDrive_GetTurnYAngleXZ(&ang, &ang2, dir.f[0], dir.f[1], dir.f[2]);
    switch (classifyInterpAngle(info, ang, k)) {
    case 1:
        MultiQuaternion(q, GetCurrentQuaternion(), outA);
        break;
    case 2:
        MultiQuaternion(q, GetCurrentQuaternion(), outB);
        break;
    }
}

/* The arm IK's angles: the shoulder's turns about Z and Y and the two
   joints' bends. */
static short ikRotZ = 0; /* derived name */

static short ikRotY = 0; /* derived name */

static short ikBend0 = 0; /* derived name */

static short ikBend1 = 0; /* derived name */

/* declared here: motionOrientManager.h reaches ico2/fumi's files through
   typedef.h, and commonact.c declares the table char [] */
extern const MotionDef motionKind[];

/* The frame-rate factors every blend in _getFinalMatrix scales by: the
 * frame counter pair gives the frames per second of the current mode. */
#define MOT_FPS ((float)((60 - systemStatus[0] * 10) / systemStatus[1])) /* derived name */
#define MOT_SPEED                                                        /* derived name */        \
    (debug_now_motion_viewer != 0 ? (float)debug_face_rot_w_ratio * 0.01f                          \
                                  : skelMotDef->faceRotRatio)
#define MOT_RATE(f) (skelRoot->f * (60.0f / MOT_FPS))                       /* derived name */
#define MOT_BLEND(off) (MOT_RATE(off) + (1.0f - MOT_RATE(off)) * MOT_SPEED) /* derived name */

/* Only _getFinalMatrix uses these two: a unit direction from a heading and
 * a pitch, and the rotation that turns the x axis onto a direction. */
static inline void getDirOfAngle(float *v, short h, short p) /* derived name */
{
    float c = GetTableCos(p);

    v[0] = GetTableCos(h) * c;
    v[1] = GetTableSin(h) * c;
    v[2] = -GetTableSin(p);
    v[3] = 0.0f;
}

static inline void getRotOfDir(float *q, float *v) /* derived name */
{
    float axis[4];
    short ang = GetTableArcCos(_InnerProduct(v, XUnitVector));

    _OuterProduct(axis, v, XUnitVector);
    SetQuaternionByAxisRotateV(q, ang, axis);
}

/* The motion's own rotation for the node, the matrix rebuilt from it, and
 * the stream's offset from the skeleton's rest rotation. */
#define GET_NODE_ROT(q, dq) /* derived name */                                                     \
    MultiQuaternion(GetCurrentQuaternion(), GetLastQuaternion(), node->quat);                      \
    GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());               \
    CopyQuaternion(q, IdentityQuaternion);                                                         \
    DivQuaternion(dq, elem + 16, node->quat)
#define APPLY_ROT(q) /* derived name */                                                            \
    MultiCurrentQuaternion(q);                                                                     \
    GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion())

static void _getFinalMatrix(int id)
{
    SkelNode *node = &skelNode[id];
    char *elem = skelMotion + id * 32;
    /* Sub15C nodeRotElem and nodeLimit, reached from the root block (the ROM adds 160 first) */
    char *geo = ICO_RAWP(char *, GOBJ_SUB(skelGObj), 0xA0, (char *)GOBJ_SUB(skelGObj)->root.pos);
    MotIk *ik = ICO_RAW(MotIk *, geo, 0x76C, GOBJ_SUB(skelGObj)->nodeRotElem) + id;
    /* the host's nodeLimit holds the one-based row of motionLimitDef (motionOrientManager.c) */
    MotLimAng *lim = GOBJ_SUB(skelGObj)->nodeLimit[id] == 0
                         ? 0
                         : (MotLimAng *)&motionLimitDef[GOBJ_SUB(skelGObj)->nodeLimit[id] - 1];
    float v[4];
    float dq[4];
    float q[4];
    int dbg = 0;

    if (node->parent != -1) {
        sceVu0ScaleVectorXYZ(v, node->pos, skelScale);
        sceVu0ApplyMatrix(MatrixDrive_GetMatrix()[3], MatrixDrive_GetLastMatrix(), v);
    } else {
        ikRotZ = ikRotY = ikBend0 = ikBend1 = 0;
    }

    switch (node->kind) {
    case 0:
        MultiQuaternion(GetCurrentQuaternion(), GetLastQuaternion(), elem + 16);
        MultiQuaternion(GetCurrentQuaternion(), twistQuatLow, GetCurrentQuaternion());
        GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        break;

    case 44:
        MultiQuaternion(GetCurrentQuaternion(), GetLastQuaternion(), elem + 16);
        DivQuaternion(GetCurrentQuaternion(), GetCurrentQuaternion(), twistQuat);
        GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        break;

    case 34: {
        GET_NODE_ROT(q, dq);
        if (skelRoot->lookIK != 0 && skelRoot->lookMode != 0) {
            ik->rate = ik->rate + (1.0f - ik->rate) * MOT_BLEND(ikRate0);
        } else {
            ik->rate = ik->rate * (1.0f - MOT_BLEND(ikRate0));
        }
        {
            float q3[4];
            float q2[4];

            memset(q2, 0, 16);
            q2[3] = 1.0f;
            if (skelGeoType == 2) {
                RotQuaternionZ(q2, 1820);
            }
            GetSlerpQuaternion(q3, q2, dq, ik->rate);
            MultiQuaternion(q, q, q3);
            APPLY_ROT(q);
            GetInverseQuaternion(q3, skelRoot->armTwist);
            MultiQuaternion(GetCurrentQuaternion(), q3, GetCurrentQuaternion());
            GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        }
        break;
    }

    case 35: {
        /* read only by the DEBUG build's report of the rolled quaternion */
        float work[4];

        GET_NODE_ROT(q, dq);
        RotQuaternionZ(GetCurrentQuaternion(), skelRoot->b);
        GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
#ifdef DEBUG
        CopyQuaternion(work, GetCurrentQuaternion());
        scePrintf("node roll %f %f %f %f\n", work[0], work[1], work[2], work[3]);
#endif
        if (skelRoot->lookIK != 0 && skelRoot->lookMode != 0) {
            ik->rate = ik->rate + (1.0f - ik->rate) * (skelRoot->ikRate0 * (60.0f / MOT_FPS));
        } else {
            ik->rate = ik->rate * (1.0f - skelRoot->ikRate0 * (60.0f / MOT_FPS));
        }
        if (ik->rate < 0.01f) {
            CopyQuaternion(ik->q, GetCurrentQuaternion());
            SetIdentityQuaternion(ik->offset);
            CopyQuaternion(ik->step, ik->offset);
        } else {
            float out[4];
            /* PC port (AN-19): the turn below leaves h alone for a target
               straight ahead (or a NaN one); 0 rather than the host's stack
               word (issue 19) */
            short h = 0;
            short p = 0;
            short ha = 0;
            short pa = 0;
            short ba = 0;

            if (skelGeoType == 4) {
                MatrixDrive_RotMatrixX(-32768);
            }
            getLocalTargetDir(out, (float *)skelRoot->lookPos);
            MatrixDrive_GetTurnYAngleXZ(&h, &p, out[0], out[1], out[2]);
            limitHPAngleAndSetB(&ha, &pa, &ba, lim, h, 0, -p, 0, 0, 1, 1.0f);
            pa *= -1;
            if (skelGeoType == 4) {
                pa *= -1;
            }
            skelRoot->h = h - ha;
            skelRoot->p = p - pa;
            {
                float rq[4];
                float t[4];
                float t2[4];
                float t3[4];

                SetIdentityQuaternion(rq);
                RotQuaternionX(rq, -ha);
                RotQuaternionZ(rq, -pa);
                RotQuaternionY(rq, -ba);
                avoidReverseInterpOnCurrentMatrixWithLimit(ik->q, lim, ik->offset, rq, 1.2f);
                MultiQuaternion(t, GetCurrentQuaternion(), rq);
                DivQuaternion(t2, t, ik->q);
                GetSlerpQuaternion(t3, t2, IdentityQuaternion, skelRoot->ikRate1 * 30.0f / MOT_FPS);
                GetSlerpQuaternion(ik->step, t3, ik->step, skelRoot->ikRate2 * 30.0f / MOT_FPS);
                MultiQuaternion(ik->q, ik->q, ik->step);
                RegularizeQuaternion(ik->q);
                DivQuaternion(ik->offset, ik->q, GetCurrentQuaternion());
            }
        }
        {
            float r[4];

            RotQuaternionX(GetCurrentQuaternion(), -skelRoot->twist * 2.5f);
            MultiQuaternion(r, GetCurrentQuaternion(), dq);
            GetSlerpQuaternion(GetCurrentQuaternion(), ik->q, r, ik->rate);
            MultiQuaternion(GetCurrentQuaternion(), twistQuatHigh, GetCurrentQuaternion());
            GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        }
        break;
    }

    case 1: {
        GET_NODE_ROT(q, dq);
        MultiQuaternion(GetCurrentQuaternion(), GetCurrentQuaternion(), dq);
        GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        if (skelRoot->handTurnIK != 0 &&
            ((skelRoot->lookIK != 0 && skelRoot->lookMode != 0) || skelRoot->hand1.ikMode != 0 ||
             skelRoot->hand0.ikMode != 0)) {
            ik->rate = ik->rate + (1.0f - ik->rate) * MOT_BLEND(ikRate0);
        } else {
            ik->rate = ik->rate * (1.0f - MOT_BLEND(ikRate0));
        }
        if (ik->rate < 0.01f) {
            CopyQuaternion(ik->q, GetCurrentQuaternion());
            SetIdentityQuaternion(ik->offset);
            CopyQuaternion(ik->step, ik->offset);
        } else {
            /* work0 and work1 are read only by the DEBUG build's report of the
               interpolation's two quaternions */
            float work0[4];
            float out[4];
            float work1[4];
            int h;
            int p;
            int n;
            int hv;
            /* PC port (AN-19): each turn below leaves its first angle alone
               for a target on its axis (or a NaN one); 0 rather than the
               host's stack word (issue 19) */
            short a0 = 0, a1 = 0;
            short b0 = 0, b1 = 0;
            short c0 = 0, c1 = 0;
            short ha = 0, pa = 0, ba = 0;
            short hb = 0, pb = 0, bb = 0;
            float rate = 1.0f;

            avoidReverseInterpOnCurrentMatrix(ik->q, lim, ik->offset);
#ifdef DEBUG
            CopyQuaternion(work0, ik->q);
            CopyQuaternion(work1, ik->offset);
            scePrintf("interp %f %f %f %f -> %f %f %f %f\n", work0[0], work0[1], work0[2], work0[3],
                      work1[0], work1[1], work1[2], work1[3]);
#endif
            p = h = 0;
            n = 0;
            if (skelRoot->handTurnIK == 1 && skelRoot->hand0.ikMode != 0) {
                n++;
                getLocalTargetDir(out, (float *)skelRoot->hand0.ikDir);
                MatrixDrive_GetTurnMinusZAngleXY(&a0, &a1, out[0], out[1], out[2]);
                h = a0;
            }
            if (skelRoot->handTurnIK == 1 && skelRoot->hand1.ikMode != 0) {
                float out2[4];

                n++;
                getLocalTargetDir(out2, (float *)skelRoot->hand1.ikDir);
                MatrixDrive_GetTurnZAngleXY(&b0, &b1, out2[0], out2[1], out2[2]);
                h -= b0;
            }
            hv = h;
            if (skelRoot->lookIK != 0 && skelRoot->lookMode != 0) {
                float out3[4];

                getLocalTargetDir(out3, (float *)skelRoot->lookPos);
                MatrixDrive_GetTurnYAngleXZ(&c0, &c1, out3[0], out3[1], out3[2]);
                if (skelRoot->lookMode == 2) {
                    h = hv + c0;
                    p = c1;
                    hv = h;
                } else {
                    h = hv + skelRoot->h;
                    p = skelRoot->p;
                    hv += c0 / 2;
                }
                n++;
            }
            if (n != 0) {
                h /= n;
                hv /= n;
            }
            limitHPAngleAndSetB(&ha, &pa, &ba, lim, h, ik->prevH, -p, -ik->pitch, 0, 0, rate);
            limitHPAngleAndSetB(&hb, &pb, &bb, lim, hv, ik->prevH, -p, -ik->pitch, 0, 0, rate);
            pb *= -1;
            {
                float r[4];
                float t[4];
                float t2[4];
                float t3[4];

                SetIdentityQuaternion(r);
                RotQuaternionX(r, -ha);
                RotQuaternionZ(r, -pb);
                RotQuaternionY(r, -ba);
                MultiQuaternion(t, GetCurrentQuaternion(), r);
                DivQuaternion(t2, t, ik->q);
                GetSlerpQuaternion(t3, t2, IdentityQuaternion, skelRoot->ikRate1 * 30.0f / MOT_FPS);
                GetSlerpQuaternion(ik->step, t3, ik->step, skelRoot->ikRate2 * 30.0f / MOT_FPS);
                MultiQuaternion(ik->q, ik->q, ik->step);
                RegularizeQuaternion(ik->q);
                DivQuaternion(ik->offset, ik->q, GetCurrentQuaternion());
            }
            ik->pitchSpeed =
                ik->pitchSpeed +
                ((pb - ik->pitch) * (skelRoot->ikRate1 * 30.0f / MOT_FPS) - ik->pitchSpeed) *
                    (skelRoot->ikRate2 * 30.0f / MOT_FPS);
            ik->pitch += ik->pitchSpeed;
            skelRoot->b = ik->pitch * ik->rate;
        }
        {
            RotQuaternionX(GetCurrentQuaternion(), skelRoot->twist * 2.5f);
            GetSlerpQuaternion(GetCurrentQuaternion(), ik->q, GetCurrentQuaternion(), ik->rate);
            MultiQuaternion(GetCurrentQuaternion(), skelRoot->armTwist, GetCurrentQuaternion());
            GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
            _ApplyMatrix(ikXAxis, MatrixDrive_GetMatrix(), XUnitVector);
        }
        break;
    }

    case 18:
        GET_NODE_ROT(q, dq);
        MultiQuaternion(q, q, dq);
        APPLY_ROT(q);
        break;

    case 19: {
        float s[4];
        int flag;

        GET_NODE_ROT(q, dq);
        {
            float out[4], r[4], work0[4], qa[4], work1[4], dv[4];
            /* PC port (AN-19): MatrixDrive_GetTurnXAngleZY leaves a0 alone
               for a target along x (or a NaN one); 0 rather than the host's
               stack word (issue 19) */
            short a0 = 0, a1 = 0;
            short ha = 0, pa = 0, ba = 0;

            memset(qa, 0, 16);
            qa[3] = 1.0f;
            /* a turn computed from the work vectors, under the function's
             * local debug switch (case 3 has the same window with one
             * vector) */
            if (dbg) {
                short w0, w1;

                CopyVector(work0, work1);
                MatrixDrive_GetTurnXAngleZY(&w0, &w1, work0[0], work0[1], work0[2]);
            }
            getLocalTargetDir(out, (float *)skelRoot->hand0.ikDir);
            MatrixDrive_GetTurnXAngleZY(&a0, &a1, out[0], out[1], out[2]);
            limitHPAngleAndSetB(&ha, &pa, &ba, lim, a0, 0, a1, 0, 1, 0, 1.0f);
            if (skelRoot->hand0.ikLock != 0) {
                _NormalizeVector(dv, out);
            } else {
                getDirOfAngle(dv, ha, pa);
            }
            getRotOfDir(r, dv);
            MultiQuaternion(qa, qa, r);
            switch (skelRoot->hand0.ikMode) {
            case 1: {
                /* PC port (AN-19): a node without children leaves the bone
                   lengths unset; 0 rather than the host's stack words */
                float len[4] = {0.0f, 0.0f}, t[4], p[4];
                float d;
                float l;
                float dd;
                int i;
                int ci;
                SkelNode *cn;

                d = VectorLength(out);
                for (i = 0, cn = node; i < 2; i++) {
                    ci = cn->child;
                    if (ci == -1) {
                        break;
                    }
                    cn = &skelNode[ci];
                    len[i] = skelScale * VectorLength(cn->pos);
                }
                len[1] += skelScale *
                          VectorLength(skelNode[GetSkeltonFocusNode((GObj *)skelGObj, 32)].pos);
                {
                    float ax[4], w[4];
                    short ang;
                    int n;

                    {
                        MatrixDrive_PushMatrixWithNoCopy();
                        n = GetSkeltonFocusNode((GObj *)skelGObj, 22);
                        GetMatrixFromQuaternion((void *)MatrixDrive_GetMatrix(),
                                                skelMotion + n * 32 + 16);
                        MatrixDrive_TransMatrix(5.0f, -3.0f, 0.0f);
                        CopyVector(p, (void *)(MatrixDrive_GetMatrix()[3]));
                        _AddVector(t, skelNode[node->child].pos, p);
                        l = skelScale * VectorLength(t);
                        w[0] = l;
                        w[1] = 0.0f;
                        w[2] = 0.0f;
                        w[3] = 1.0f;
                        _SubVectorXYZ(w, w, p);
                        _NormalizeVector(w, w);
                        _OuterProduct(ax, w, XUnitVector);
                        ang = GetTableArcCos(_InnerProduct(XUnitVector, w));

                        SetQuaternionByAxisRotateV(ikBendQuat, ang, ax);
                        len[1] = l;
                        MatrixDrive_PopMatrix();
                    }
                }
                if (skelRoot->hand0.ikReached == 0) {
                    if (len[0] + len[1] < d) {
                        d = len[0] + len[1];
                    }
                }
                dd = d * d;
                /* PC port: a target at the shoulder (d 0) or a zero bone
                   length (skelScale 0) divides by zero: +-Fmax on the EE,
                   Inf or NaN under IEEE */
                ikBend0 = GetTableArcCos(
                    ps2_div(dd + len[0] * len[0] - len[1] * len[1], 2.0f * len[0] * d));
                ikBend1 = GetTableArcCos(
                    ps2_div(dd + len[1] * len[1] - len[0] * len[0], 2.0f * len[1] * d));
                RotQuaternionX(qa, ba);
                RotQuaternionZ(qa, ikBend0);
                CopyQuaternion(s, qa);
                getRootRelativePos(t, MatrixDrive_GetMatrix(), (float *)skelRoot->hand0.ikDir);
                _NormalizeVector(t, t);
                _OuterProduct(p, t, ikXAxis);
                if (skelRoot->handIK == 2 && d + 0.0f > len[0] + len[1]) {
                    float rq[4];

                    SetQuaternionByAxisRotateV(rq, (d + 0.0f - (len[0] + len[1])) * 208.60756f, p);
                    GetSlerpQuaternion(skelRoot->armTwist, skelRoot->armTwist, rq, 0.6f);
                } else {
                    GetSlerpQuaternion(skelRoot->armTwist, skelRoot->armTwist, IdentityQuaternion,
                                       0.9f);
                }
                break;
            }
            case 2:
                MultiQuaternion(s, qa, skelRoot->hand0.ikQuat);
                MultiQuaternion(s, s, dq);
                GetSlerpQuaternion(skelRoot->armTwist, skelRoot->armTwist, IdentityQuaternion,
                                   0.9f);
                break;
            default:
                CopyQuaternion(s, dq);
                GetSlerpQuaternion(skelRoot->armTwist, skelRoot->armTwist, IdentityQuaternion,
                                   0.9f);
                break;
            }
        }
        GetSlerpQuaternion(ik->q, s, ik->q, skelRoot->hand0.ikRate * (60.0f / MOT_FPS));
        if (skelRoot->hand0.ikMode != 0) {
            ik->rate = ik->rate + (1.0f - ik->rate) * (skelRoot->handRate * (60.0f / MOT_FPS));
        } else {
            ik->rate = ik->rate * (1.0f - skelRoot->handRate * (60.0f / MOT_FPS));
        }
        {
            float r2[4];

            GetSlerpQuaternion(r2, ik->q, dq, ik->rate);
            MultiQuaternion(q, q, r2);
            APPLY_ROT(q);
            ikSlerpRate = ik->rate;
            flag = ikSlerpRate > 0.99f;
            if (flag && skelRoot->hand0.ikFlag == 0) {
                skelRoot->hand0.ikReached = 1;
            } else {
                skelRoot->hand0.ikReached = 0;
            }
            skelRoot->hand0.ikFlag = flag;
        }
        break;
    }

    case 20: {
        GET_NODE_ROT(q, dq);
        {
            float t[4];
            float r[4];

            CopyQuaternion(t, dq);
            if (skelRoot->hand0.ikMode == 1) {
                SetQuaternionByAxisRotateWithNoRegularize(t, -(ikBend0 + ikBend1), 0.0f, 0.0f,
                                                          -1.0f);
                MultiQuaternion(t, t, ikBendQuat);
            }
            GetSlerpQuaternion(ik->q, t, ik->q, skelRoot->hand0.ikRate * (60.0f / MOT_FPS));
            GetSlerpQuaternion(r, ik->q, dq, ikSlerpRate);
            MultiQuaternion(q, q, r);
            APPLY_ROT(q);
        }
        break;
    }

    case 22: {
        float t[4];
        float w;

        GET_NODE_ROT(q, dq);
        if (skelRoot->flag330 != 0)
            CopyQuaternion(t, IdentityQuaternion);
        else
            CopyQuaternion(t, dq);
        {
            float r[4];

            GetSlerpQuaternion(r, t, dq, ikSlerpRate);
            MultiQuaternion(q, q, r);
            APPLY_ROT(q);
            if (skelMotCtrl->pickedWeapon != 0 &&
                !motionKind[skelMotCtrl->motion].flags2.bits.weaponSwing) {
                switch (skelMotCtrl->rootUpdateMode) {
                case 0:
                case 6:
                case 14:
                case 19:
                    break;
                default:
                    w = GetWeaponWeight(skelMotCtrl->pickedWeapon) * 0.00016666666f;
                    w = 0.8f < w ? 0.8f : w;
                    GetSlerpQuaternion(ik->q, ik->q, GetCurrentQuaternion(), w);
                    CopyQuaternion(GetCurrentQuaternion(), ik->q);
                    GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
                    return;
                }
            }
            CopyQuaternion(ik->q, GetCurrentQuaternion());
        }
        break;
    }

    case 21:
        CopyQuaternion(GetCurrentQuaternion(), GetLastQuaternion());
        MultiQuaternion(GetCurrentQuaternion(), GetCurrentQuaternion(), elem + 16);
        GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        break;

    case 2:
        GET_NODE_ROT(q, dq);
        MultiQuaternion(q, q, dq);
        APPLY_ROT(q);
        break;

    case 3: {
        float t[4];

        GET_NODE_ROT(q, dq);
        {
            float qa[4], out[4], r[4], work[4], dv[4];
            /* PC port (AN-19): as in case 19 */
            short a0 = 0, a1 = 0;
            short ha = 0, pa = 0, ba = 0;

            memset(qa, 0, 16);
            qa[3] = 1.0f;
            /* case 19's debug window again, with one work vector */
            if (dbg) {
                CopyVector(work, out);
            }
            getLocalTargetDir(out, (float *)skelRoot->hand1.ikDir);
            MatrixDrive_GetTurnXAngleZY(&a0, &a1, out[0], out[1], out[2]);
            limitHPAngleAndSetB(&ha, &pa, &ba, lim, a0, 0, a1, 0, 1, 0, 1.0f);
            if (skelRoot->hand1.ikLock != 0) {
                _NormalizeVector(dv, out);
            } else {
                getDirOfAngle(dv, ha, pa);
            }
            getRotOfDir(r, dv);
            MultiQuaternion(qa, qa, r);
            switch (skelRoot->hand1.ikMode) {
            case 1: {
                /* PC port (AN-19): as in case 19 */
                float len[4] = {0.0f, 0.0f};
                float d;
                int i;
                int ci;
                SkelNode *cn;

                d = VectorLength(out);
                for (i = 0, cn = node; i < 2; i++) {
                    ci = cn->child;
                    if (ci == -1) {
                        break;
                    }
                    cn = &skelNode[ci];
                    len[i] = skelScale * VectorLength(cn->pos);
                }
                len[1] += skelScale *
                          VectorLength(skelNode[GetSkeltonFocusNode((GObj *)skelGObj, 16)].pos);
                /* PC port (AN-19): as hand 0's above, a target at the
                   shoulder (d 0) or a zero bone length divides by zero:
                   +-Fmax on the EE, NaN under IEEE for 0/0, an arm raised
                   straight up on arm64 (issue 19) */
                ikBend0 = GetTableArcCos(
                    ps2_div(d * d + len[0] * len[0] - len[1] * len[1], 2.0f * len[0] * d));
                ikBend1 = GetTableArcCos(
                    ps2_div(d * d + len[1] * len[1] - len[0] * len[0], 2.0f * len[1] * d));
                RotQuaternionX(qa, ba);
                RotQuaternionZ(qa, ikBend0);
                CopyQuaternion(t, qa);
                break;
            }
            case 2:
                MultiQuaternion(t, qa, skelRoot->hand1.ikQuat);
                MultiQuaternion(t, t, dq);
                break;
            default:
                CopyQuaternion(t, dq);
                break;
            }
        }
        GetSlerpQuaternion(ik->q, t, ik->q, skelRoot->handRate * (60.0f / MOT_FPS));
        if (skelRoot->hand1.ikMode != 0) {
            ik->rate = ik->rate + (1.0f - ik->rate) * (skelRoot->handRate * (60.0f / MOT_FPS));
        } else {
            ik->rate = ik->rate * (1.0f - skelRoot->handRate * (60.0f / MOT_FPS));
        }
        {
            float r2[4];

            GetSlerpQuaternion(r2, ik->q, dq, ik->rate);
            MultiQuaternion(q, q, r2);
            APPLY_ROT(q);
            ikSlerpRate = ik->rate;
        }
        break;
    }

    case 4: {
        GET_NODE_ROT(q, dq);
        {
            float t[4];
            float r[4];

            CopyQuaternion(t, dq);
            if (skelRoot->hand1.ikMode == 1) {
                SetQuaternionByAxisRotateWithNoRegularize(t, -(ikBend0 + ikBend1), 0.0f, 0.0f,
                                                          -1.0f);
            }
            GetSlerpQuaternion(ik->q, t, ik->q, skelRoot->handRate * (60.0f / MOT_FPS));
            GetSlerpQuaternion(r, ik->q, dq, ikSlerpRate);
            MultiQuaternion(q, q, r);
            APPLY_ROT(q);
        }
        break;
    }

    case 6: {
        GET_NODE_ROT(q, dq);
        if (skelRoot->flag334 != 0) {
            CopyQuaternion(ik->q, IdentityQuaternion);
        } else {
            CopyQuaternion(ik->q, dq);
        }
        {
            float r[4];

            GetSlerpQuaternion(r, ik->q, dq, ikSlerpRate);
            MultiQuaternion(q, q, r);
            APPLY_ROT(q);
        }
        break;
    }

    case 5:
        GET_NODE_ROT(q, dq);
        if (skelRoot->hand1.ikMode != 0) {
            if (skelRoot->hand1.ikMode != 1) {
                MultiQuaternion(q, q, dq);
            }
        } else {
            MultiQuaternion(q, q, dq);
        }
        APPLY_ROT(q);
        break;

    case 45:
    case 49:
        if (skelRoot->lifting != 0) {
            int i;
            int flag;
            SkelNode *cn;
            int ci;
            float len[2];
            float p[4];
            float sv[4];
            float d;
            float dd;
            float d2;
            float aa;
            float bb;
            float (*m)[4];

            GET_NODE_ROT(q, dq);
            flag = 0;
            MatrixDrive_PushMatrix();
            CopyVector((void *)(MatrixDrive_GetMatrix()[3]), ZeroPoint);
            MultiMatrixByQuaternion(dq);
            MatrixDrive_PushMatrix();
            for (i = 0, cn = node; i < 2; i++) {
                ci = cn->child;
                if (ci == -1) {
                    flag = 1;
                    break;
                }
                cn = &skelNode[ci];
                sceVu0ScaleVectorXYZ(sv, cn->pos, skelScale);
                MatrixDrive_TransMatrixV(sv);
                MultiMatrixByQuaternion(skelMotion + ci * 32 + 16);
                len[i] = skelScale * VectorLength(cn->pos);
            }
            CopyVector(p, (void *)(MatrixDrive_GetMatrix()[3]));
            switch (node->kind) {
            case 45:
                p[1] += skelRoot->lift[0];
                break;
            case 49:
                p[1] += skelRoot->lift[1];
                break;
            }
            d = VectorLength(p);
            MatrixDrive_PopMatrix();
            dd = d * d;
            m = MatrixDrive_GetMatrix();
            sceVu0TransposeMatrix(m, m);
            sceVu0ApplyMatrix(p, m, p);
            sceVu0Normalize(p, p);
            ikRotZ = asinf(p[1]) * 10430.378f;
            ikRotY = -asinf(p[2]) * 10430.378f;
            aa = len[0] * len[0];
            bb = len[1] * len[1];
            d2 = d + d;
            /* PC port: the same zero distance or bone length as the arm IK
               above */
            ikBend0 = acosf(ps2_div(dd + aa - bb, d2 * len[0])) * 10430.378f;
            ikBend1 = acosf(ps2_div(dd + bb - aa, d2 * len[1])) * 10430.378f;
            MatrixDrive_PopMatrix();
            if (flag == 0) {
                MultiQuaternion(q, q, dq);
                RotQuaternionZ(q, ikRotZ);
                RotQuaternionY(q, ikRotY);
                RotQuaternionZ(q, -ikBend0);
            } else {
                MultiQuaternion(q, q, dq);
            }
            APPLY_ROT(q);
            break;
        }
    case 46:
    case 50:
        if (skelRoot->lifting != 0) {
            GET_NODE_ROT(q, dq);
            RotQuaternionZ(q, ikBend0 + ikBend1);
            APPLY_ROT(q);
            break;
        }
    case 47:
    case 51:
        if (skelRoot->lifting != 0) {
            GET_NODE_ROT(q, dq);
            RotQuaternionZ(q, -ikBend1);
            RotQuaternionY(q, -ikRotY);
            RotQuaternionZ(q, -ikRotZ);
            MultiQuaternion(q, q, skelMotion + node->parent * 32 + 16);
            MultiQuaternion(q, q, dq);
            APPLY_ROT(q);
            break;
        }
        MultiQuaternion(GetCurrentQuaternion(), GetLastQuaternion(), elem + 16);
        GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        break;

    default:
        MultiQuaternion(GetCurrentQuaternion(), GetLastQuaternion(), elem + 16);
        GetMatrixFromQuaternionRotElem(MatrixDrive_GetMatrix(), GetCurrentQuaternion());
        break;
    }
}

extern double fptodp(float v);

/* EditRotEmphasys's tuning: the two coefficients it shows and edits, the
   switch that turns the editor on and the coefficient selected. */
static float emphasysAccel = 0.1f; /* derived name */

static float emphasysVelocity = 0.97f; /* derived name */

static int emphasysEdit = 0; /* derived name */

static int emphasysSelect = 0; /* derived name */

void EditRotEmphasys(void)
{
    int sel;

    if (pad[1].flags & 0x20) {
        emphasysEdit = emphasysEdit == 0;
    }
    if (emphasysEdit != 0) {
        if (debug_font_flag & 1) {
            debug_Printf(300, 170, 0xFFFFFF00u, "Emphasys Accel   Velocity");
        }
        if (debug_font_flag & 1) {
            debug_Printf(300, 180, emphasysSelect != 0 ? 0xFFFFFF00u : 0x80808000u,
                         "         %1.5f ", emphasysAccel);
        }
        if (debug_font_flag & 1) {
            debug_Printf(300, 180, emphasysSelect != 0 ? 0x80808000u : 0xFFFFFF00u,
                         "                 %1.5f", emphasysVelocity);
        }
        sel = emphasysSelect;
        if (pad[1].flags & 0xA000) {
            sel++;
            sel &= 1;
            emphasysSelect = sel;
        }
        switch (sel) {
        default:
        case 0:
            if (pad[1].rep & 0x1000) {
                emphasysVelocity = emphasysVelocity + 0.01f;
            }
            if (pad[1].rep & 0x4000) {
                emphasysVelocity = emphasysVelocity - 0.01f;
            }
            break;
        case 1:
            if (pad[1].rep & 0x1000) {
                emphasysAccel = emphasysAccel + 0.01f;
            }
            if (pad[1].rep & 0x4000) {
                emphasysAccel = emphasysAccel - 0.01f;
            }
            break;
        }
    }
}

static inline void getFinalMatrixCore(int id)
{
    SkelNode *e = &skelNode[id];
    MatrixDrive_PushMatrixWithNoCopy();
    PushQuaternionWithNoCopy();
    _getFinalMatrix(id);
    CopyMatrix((char *)GOBJ_SUB(skelGObj)->nodeMtx + id * 64, (void *)MatrixDrive_GetMatrix());
    CopyQuaternion(skelQuat + id * 16, GetCurrentQuaternion());
    if (e->child != -1) {
        getFinalMatrixCore(e->child);
    }
    MatrixDrive_PopMatrix();
    PopQuaternion();
    if (e->sibling != -1) {
        getFinalMatrixCore(e->sibling);
    }
}

static void getFinalMatrix(int id)
{
    float v[4];

    MatrixDrive_PushMatrix();
    _ApplyMatrix(v, MatrixDrive_GetMatrix(), ZUnitVector);
    SetQuaternionByAxisRotateV(twistQuat, skelRoot->twist, v);
    SetQuaternionByAxisRotateV(twistQuatLow, skelRoot->twist * (skelRoot->twistRate - 1.0f - 0.5f),
                               v);
    SetQuaternionByAxisRotateV(twistQuatHigh, skelRoot->twist * (skelRoot->twistRate - 1.0f + 0.5f),
                               v);
    MatrixDrive_PopMatrix();
    getFinalMatrixCore(id);
}

/* getFinalMatrixWithNaturalGeometry's helper */
static inline void applyNaturalGeometry(int id) /* derived name */
{
    SkelNode *e = &skelNode[id];
    Vec16 t;

    if (e->parent != -1) {
        _ScaleVectorXYZ(&t, e->pos, skelScale);
        _ApplyMatrix(MatrixDrive_GetMatrix()[3], MatrixDrive_GetLastMatrix(), &t);
    }
    MultiQuaternion(GetCurrentQuaternion(), GetLastQuaternion(), skelMotion + id * 32 + 16);
    GetMatrixFromQuaternionRotElem((char *)MatrixDrive_GetMatrix(), (char *)GetCurrentQuaternion());
}

static void getFinalMatrixWithNaturalGeometry(int id)
{
    SkelNode *e = &skelNode[id];
    int child = e->child;
    int next = e->sibling;

    MatrixDrive_PushMatrixWithNoCopy();
    PushQuaternionWithNoCopy();
    applyNaturalGeometry(id);
    CopyMatrix((char *)GOBJ_SUB(skelGObj)->nodeMtx + id * 64, (void *)MatrixDrive_GetMatrix());
    CopyQuaternion(skelQuat + id * 16, GetCurrentQuaternion());
    if (child != -1) {
        getFinalMatrixWithNaturalGeometry(child);
    }
    MatrixDrive_PopMatrix();
    PopQuaternion();
    if (next != -1) {
        getFinalMatrixWithNaturalGeometry(next);
    }
}

static void _calcNaturalGeometry(int id)
{
    SkelNode *elemA = &skelNode[id];
    char *elemB = naturalMotion + id * 32;
    float (*x)[4];
    if (elemA->parent != -1) {
        _ScaleVectorXYZ(naturalTrans, elemA->pos, skelScale);
        GetMatrixFromQuaternionPos(naturalMatrix, elemB + 16, naturalTrans);
    } else {
        GetMatrixFromQuaternion(naturalMatrix, elemB + 16);
    }
    x = MatrixDrive_GetMatrix();
    _MulMatrix(x, MatrixDrive_GetLastMatrix(), naturalMatrix);
}

static inline void pursueNaturalGeometry(int id)
{
    SkelNode *e = &skelNode[id];
    int child = e->child;
    int next = e->sibling;
    char *o;
    float (*m)[4];
    MatrixDrive_PushMatrixWithNoCopy();
    _calcNaturalGeometry(id);
    o = naturalNodePos + id * 16;
    m = MatrixDrive_GetMatrix();
    CopyVector(o, m[3]);
    if (child != -1) {
        pursueNaturalGeometry(child);
    }
    MatrixDrive_PopMatrix();
    if (next != -1) {
        pursueNaturalGeometry(next);
    }
}
