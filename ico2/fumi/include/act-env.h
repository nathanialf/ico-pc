/*
 * ico2/fumi/include/act-env.h
 *
 * The declarations of what act-env.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ACT_ENV_H
#define ACT_ENV_H

struct GObj;

/* act-env.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
void ACTSetEnvAllmighty(struct GObj *self);
void GetSofaPosition(struct GObj *self, struct GObj *sofa);
void GetCollisCenterPositionSimple(void *out, void *obj, void *corners);

int CheckWallAttributeEdegWall(struct GObj *obj);

/* ACTGetEnvironment's flag words (the caller passes the actor's status block
 * + 0x47C), set both bit by bit and by whole-word ORs.  The bits are named by
 * position. */
typedef union { /* field names derived */
    int w;

    struct {
        unsigned int b0 : 1;
        unsigned int b1 : 1;
        unsigned int b2 : 1;
        unsigned int b3 : 1;
        unsigned int b4 : 1;
        unsigned int b5 : 1;
        unsigned int b6 : 1;
        unsigned int b7 : 1;
        unsigned int b8 : 1;
        unsigned int b9 : 1;
        unsigned int b10 : 1;
        unsigned int b11 : 1;
        unsigned int b12 : 1;
        unsigned int b13 : 1;
        unsigned int b14 : 1;
        unsigned int b15 : 1;
        unsigned int b16 : 1;
        unsigned int b17 : 1;
        unsigned int b18 : 1;
        unsigned int b19 : 1;
        unsigned int b20 : 1;
        unsigned int b21 : 1;
        unsigned int b22 : 1;
        unsigned int b23 : 1;
        unsigned int b24 : 1;
        unsigned int b25 : 1;
        unsigned int b26 : 1;
        unsigned int b27 : 1;
        unsigned int b28 : 1;
        unsigned int b29 : 1;
        unsigned int b30 : 1;
        unsigned int b31 : 1;
    } bit;
} EnvFlag; /* derived name */

void ACTGetEnvironment(void *self, void *dir, float *orient, EnvFlag *flags, ActEnv *env);

#endif /* ACT_ENV_H */
