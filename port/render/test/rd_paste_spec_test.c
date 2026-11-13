/* rd_paste_spec_test.c: the pure parts of the glow's paste by its own entry
 * (port/render/rd_paste_spec.c, CPU only).
 *   the bits fx_paste_ps holds (RD_PASTE_SPEC_FIXED, RD_PASTE_SPEC_FREE):
 *     their values from fx_sprite_ps's flags (rd_internal_interp.h RD_FXF_*,
 *     fx_sprite.hlsl FXF_*), apart, and every other flag held clear
 *   rd__paste_spec_why: the glow's paste as rd_fx_plan.c fxDraw fills its
 *     DrawCB taken; each word fx_paste_ps holds as a constant refused when it
 *     differs, with its reason; the words it reads at run time free
 *   the keys: the general key is rd__fx_enumerate's premultiplied add at Z
 *     ALWAYS without the Z write; with the module (a stand-in id) the set
 *     has its fx_paste_ps twin, one key more, and without it none
 *   ICO_RD_PASTE_SPEC's words
 *   the attempt ledger's row through its decider: each state and its words */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_paste_spec.h"
#include "shader_consts.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the glow's paste's DrawCB as fxDraw fills it on the phone (list 7:355 of
 * v746: FLARE, RGBAQ 128, MODULATE with TCC, ALPHA Cs * FIX + Cd with FIX
 * 128, WORK3 424 x 128 under TEX0 512 x 128, the 848 x 448 frame's rect) */
static void pasteCb(IcoDrawCB *cb)
{
    memset(cb, 0, sizeof(*cb));
    for (int k = 0; k < 4; k++) {
        cb->col[k] = 128u;
    }
    cb->mode[0] =
        RD_PASTE_SPEC_FIXED | RD_FXF_TCC | RD_FXF_LINEAR | RD_FXF_CLAMP_S | RD_FXF_CLAMP_T;
    cb->mode[1] = 0x2Au; /* a TEXA mode word: RGBA32 ignores it */
    cb->mode[2] = 0u;
    cb->blend[0] = rd__alpha_register(RD_BLEND_CS_FIX_ADD_CD);
    cb->blend[1] = 128u;
    cb->blend[2] = 1u;
    cb->uvRect[0] = 8.0f;
    cb->uvRect[1] = 8.0f;
    cb->uvRect[2] = 6784.0f;
    cb->uvRect[3] = 2048.0f;
    cb->tex[0] = 512.0f;
    cb->tex[1] = 128.0f;
    cb->tex[2] = 424.0f;
    cb->tex[3] = 128.0f;
    cb->scale[0] = 1.0f;
    cb->scale[1] = 1.0f;
    cb->param[0] = 25984.0f;
    cb->param[1] = 29184.0f;
    cb->param[2] = 39552.0f;
    cb->param[3] = 36352.0f;
}

static void testBits(void)
{
    CHECK(RD_PASTE_SPEC_FIXED == 0x8001u); /* TEXTURED 1, OUT_PREMUL 32768 */
    CHECK(RD_PASTE_SPEC_FREE == 0x27Eu);   /* TFX 6, TCC 8, LINEAR 16, CLAMP 32 64, FBA 512 */
    CHECK((RD_PASTE_SPEC_FIXED & RD_PASTE_SPEC_FREE) == 0u);
    /* the branches fx_paste_ps leaves out: never free, never set */
    const uint32_t held[] = {RD_FXF_ABE,       RD_FXF_PABE,     RD_FXF_DATE,       RD_FXF_DATM,
                             RD_FXF_DST,       RD_FXF_ABOVE1X,  RD_FXF_ALPHA_ONLY, RD_FXF_FOLD,
                             RD_FXF_GLOW_MASK, RD_FXF_GLOW_KEPT};
    for (size_t i = 0; i < sizeof held / sizeof held[0]; i++) {
        CHECK((held[i] & (RD_PASTE_SPEC_FIXED | RD_PASTE_SPEC_FREE)) == 0u);
    }
}

static void testGate(void)
{
    const RdPipeKeyInt kg = rd__paste_spec_general_key();
    IcoDrawCB cb;
    pasteCb(&cb);
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_NONE);
    /* the free words: any value */
    for (uint32_t t = 0; t < 4u; t++) {
        pasteCb(&cb);
        cb.mode[0] = RD_PASTE_SPEC_FIXED | (t << RD_FXF_TFX_SHIFT) | RD_FXF_FBA;
        cb.blend[0] = rd__alpha_register(RD_BLEND_CS_AS_ADD_CD);
        cb.blend[1] = 7u;
        cb.mode[3] = 99u; /* AREF without ATE */
        cb.mode[2] = 5u | (2u << 16);
        CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_NONE);
    }
    pasteCb(&cb);
    cb.scale[0] = cb.scale[1] = 0.0f; /* unset: fx_src_scale's 1 */
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_NONE);
    /* every held flag refused */
    const uint32_t held[] = {RD_FXF_ABE,       RD_FXF_PABE,      RD_FXF_DATE,       RD_FXF_DATM,
                             RD_FXF_DST,       RD_FXF_ABOVE1X,   RD_FXF_ALPHA_ONLY, RD_FXF_FOLD,
                             RD_FXF_GLOW_MASK, RD_FXF_GLOW_KEPT, 1u << 20};
    for (size_t i = 0; i < sizeof held / sizeof held[0]; i++) {
        pasteCb(&cb);
        cb.mode[0] |= held[i];
        CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_FLAGS);
    }
    pasteCb(&cb);
    cb.mode[0] &= ~(uint32_t)RD_FXF_OUT_PREMUL;
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_FLAGS);
    pasteCb(&cb);
    cb.mode[0] &= ~(uint32_t)RD_FXF_TEXTURED;
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_FLAGS);
    pasteCb(&cb);
    cb.mode[1] = 0x2Au | (1u << 8); /* a 24-bit source */
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_TEXFMT);
    pasteCb(&cb);
    cb.mode[2] = 1u << 8; /* ATE */
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_ATEST);
    pasteCb(&cb);
    cb.scale[0] = 2.0f;
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_SOURCE);
    pasteCb(&cb);
    cb.scale[1] = 2.41071f;
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_SOURCE);
    pasteCb(&cb);
    cb.scale[2] = 3.73f / 2366.0f; /* a Full pixel crop */
    CHECK(rd__paste_spec_why(&cb, &kg, &kg) == RD_PASTE_SPEC_WHY_SOURCE);
    /* keys: a depth, the subtract, another entry, another format, samples */
    pasteCb(&cb);
    RdPipeKeyInt k = kg;
    k.depthFmt = (uint8_t)RHI_FMT_D32F_S8;
    k.gs.ztst = RD_ZTST_GEQUAL;
    CHECK(rd__paste_spec_why(&cb, &k, &kg) == RD_PASTE_SPEC_WHY_KEY);
    k = kg;
    k.gs.blend = RD_BLEND_CD_SUB_CS_AS;
    CHECK(rd__paste_spec_why(&cb, &k, &kg) == RD_PASTE_SPEC_WHY_KEY);
    k = kg;
    k.fs = RD_FS_FX_SPRITE;
    CHECK(rd__paste_spec_why(&cb, &k, &kg) == RD_PASTE_SPEC_WHY_KEY);
    k = kg;
    k.colorFmt = (uint8_t)RHI_FMT_RGBA16F;
    CHECK(rd__paste_spec_why(&cb, &k, &kg) == RD_PASTE_SPEC_WHY_KEY);
    k = kg;
    k.gs.msaa = 4;
    CHECK(rd__paste_spec_why(&cb, &k, &kg) == RD_PASTE_SPEC_WHY_KEY);
    for (int w = 0; w < RD_PASTE_SPEC_WHY_COUNT; w++) {
        CHECK(rd__paste_spec_why_text(w) != NULL && rd__paste_spec_why_text(w)[0] != '?');
    }
    CHECK(strcmp(rd__paste_spec_why_text(RD_PASTE_SPEC_WHY_COUNT), "?") == 0);
}

static bool keyIn(const RdPipeKeyInt *keys, uint32_t n, const RdPipeKeyInt *k)
{
    for (uint32_t i = 0; i < n; i++) {
        if (memcmp(&keys[i], k, sizeof(*k)) == 0) {
            return true;
        }
    }
    return false;
}

static void testKeys(void)
{
    static RdPipeKeyInt keys[96];
    const RdPipeKeyInt kg = rd__paste_spec_general_key(), ks = rd__paste_spec_key();
    CHECK(kg.fs == RD_FS_FX_FRAME && ks.fs == RD_FS_FX_PASTE && kg.depthFmt == RHI_FMT_UNKNOWN &&
          kg.colorFmt == RHI_FMT_RGBA8_UNORM && kg.gs.blend == RD_BLEND_CS_AS_ADD_CD &&
          kg.gs.colorMask == 0xF && kg.vs == RD_VS_FX_RECT && kg.gs.msaa == 0);
    RdPipeKeyInt kt = ks;
    kt.fs = RD_FS_FX_FRAME;
    CHECK(memcmp(&kt, &kg, sizeof(kg)) == 0);
    const RhiShader keepFrame = g_rd.fs[RD_FS_FX_FRAME], keepPaste = g_rd.fs[RD_FS_FX_PASTE];
    g_rd.profile.fxPlan = true;
    g_rd.fs[RD_FS_FX_FRAME].id = 0x7F000010u; /* stand-ins: never made */
    g_rd.fs[RD_FS_FX_PASTE].id = 0;
    const uint32_t n0 = rd__fx_enumerate(keys, 96, 0);
    CHECK(n0 < 96 && keyIn(keys, n0, &kg) && !keyIn(keys, n0, &ks));
    g_rd.fs[RD_FS_FX_PASTE].id = 0x7F000011u;
    const uint32_t n1 = rd__fx_enumerate(keys, 96, 0);
    CHECK(n1 == n0 + 1u && keyIn(keys, n1, &kg) && keyIn(keys, n1, &ks));
    printf("  keys: %u without fx_paste_ps, %u with it\n", n0, n1);
    g_rd.fs[RD_FS_FX_FRAME] = keepFrame;
    g_rd.fs[RD_FS_FX_PASTE] = keepPaste;
    g_rd.profile.fxPlan = false;
}

static void testEnv(void)
{
    CHECK(rd__paste_spec_env("") == -1);
    CHECK(rd__paste_spec_env("0") == 0);
    CHECK(rd__paste_spec_env("1") == 1);
    CHECK(rd__paste_spec_env("sabotage") == 2);
}

static void testLedger(void)
{
    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_state_of(ICO_ATT_CHAIN3) == ICO_ATT_NOT_BUILT);
    CHECK(ico_attempt_decide_paste_spec(1, -1, 1, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_ON);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_PASTE_SPEC),
                 "self-test passed: the same bytes as fx_frame_ps") == 0);
    CHECK(ico_attempt_decide_paste_spec(1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_paste_spec(1, -1, 1, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_REFUSED);
    CHECK(ico_attempt_decide_paste_spec(0, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_NA);
    CHECK(ico_attempt_decide_paste_spec(1, 0, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_paste_spec(1, -1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_PASTE_SPEC) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_paste_spec(1, 1, 0, 1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_PASTE_SPEC), "ICO_RD_PASTE_SPEC=1, self-test passed") ==
          0);
    ico_attempt_fire(ICO_ATT_PASTE_SPEC, 3);
    ico_attempt_fire2(ICO_ATT_PASTE_SPEC, 1);
    char b[4096];
    (void)ico_attempt_block_text(b, sizeof b, -1);
    CHECK(
        strstr(b, "the glow's paste by its own entry on 3 pastes (1 kept on the general entry)") !=
        NULL);
    CHECK(strstr(b, "the glow chain in three passes") == NULL); /* not in this build: left out */
    (void)ico_attempt_start_text(b, sizeof b);
    CHECK(strstr(b, "; the glow chain in three passes not in this build") != NULL);
    ico_attempt_reset();
}

int main(void)
{
    testBits();
    testGate();
    testKeys();
    testEnv();
    testLedger();
    if (failures) {
        printf("rd_paste_spec_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_paste_spec_test: ok\n");
    return 0;
}
