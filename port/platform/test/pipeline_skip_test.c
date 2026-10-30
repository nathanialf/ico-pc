/*
 * port/platform/test/pipeline_skip_test.c
 *
 * The record of a pipeline family a device's driver stopped in
 * (pipeline_skip.h, issue 161):
 *   note     the family of a start-up activity note: a careful set's batch
 *            of one sprite planes build and a lazy run's single creation of
 *            one name the family; the v0.9.5 Retroid Pocket 6 batch (planes
 *            and plain builds mixed), v0.9.4's plain sprite pipeline, a VU
 *            batch, a batch with unnamed vertex shaders (" and N more"), a
 *            cut note and the notes outside a creation name none;
 *   record   the value written at the start after such an end reads back
 *            the same (family, device, driver, date); separators and quotes
 *            in a device name become spaces; the driver word from the
 *            compiler line, else the driver text's version;
 *   match    the record applies on the same device and driver (case
 *            ignored), not after a driver update (E031.41.03.64) or on
 *            another device; an empty field matches any;
 *   clear    "" (the [dev] clear) and a family this version does not know
 *            are no record.
 */
#include <stdio.h>
#include <string.h>
#include "pipeline_skip.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the v0.9.5 start that ended on the Retroid Pocket 6
   (build-host/tmp/v096/evidence/161/ico-pc.log line 38) */
static const char k_v095_batch[] =
    "creating pipelines 212-219 of 587: program 9 blend 5 vs 24 fs 30 fmt 1/8 msaa 0 dst 0 to "
    "program 9 blend 6 vs 24 fs 30 fmt 1/8 msaa 0 dst 0; vertex sprite_stq_plane_world_vs 2028 "
    "words (448 instructions), sprite_world_vs 1231 words (279 instructions), "
    "sprite_plane_world_vs 2040 words (450 instructions); largest fragment sprite_stq_ps_planes "
    "14954 words";
/* v0.9.6's careful set: a sprite planes build alone in its batch */
static const char k_alone[] =
    "creating pipelines 540 of 587: program 9 blend 5 vs 24 fs 30 fmt 1/8 msaa 0 dst 0; vertex "
    "sprite_stq_plane_world_vs 2028 words (448 instructions); largest fragment "
    "sprite_stq_ps_planes 14954 words";
/* a batch of planes builds only (the full set's order puts none together;
   a list like this still names only the family) */
static const char k_planes_batch[] =
    "creating pipelines 500-507 of 587: program 9 blend 5 vs 21 fs 29 fmt 1/8 msaa 0 dst 0 to "
    "program 9 blend 6 vs 22 fs 28 fmt 1/8 msaa 0 dst 0; vertex sprite_plane_ui_vs 2038 words "
    "(450 instructions), sprite_plane_world_vs 2040 words (450 instructions); largest fragment "
    "sprite_texa_ps_planes 11518 words";
/* a lazy run's creation (evidence/161/startup-activity.txt, lazy batch 7) */
static const char k_lazy_planes[] =
    "creating pipeline program 9 blend 4 vs 22 fs 28 fmt 1/8 msaa 0 dst 0 (sprite_plane_world_vs "
    "2040 words + sprite_ps_planes 8886 words)";
/* v0.9.4's freeze (evidence/161/startup-activity-first.txt): a plain build */
static const char k_v094_plain[] =
    "creating pipeline program 11 blend 12 vs 1 fs 0 fmt 1/8 msaa 0 dst 0 (sprite_world_vs 1231 "
    "words + sprite_ps 8268 words)";
static const char k_vu_batch[] =
    "creating pipelines 400-407 of 587: program 0 blend 12 vs 4 fs 5 fmt 1/8 msaa 0 dst 0 to "
    "program 1 blend 12 vs 5 fs 5 fmt 1/8 msaa 0 dst 0; vertex vu_prelit_merge_plain_vs 22909 "
    "words (5000 instructions); largest fragment vu_ps 5649 words; compiler E031.41.03.47";
static const char k_more[] =
    "creating pipelines 1-32 of 587: program 9 blend 5 vs 21 fs 29 fmt 1/8 msaa 0 dst 0 to "
    "program 9 blend 6 vs 22 fs 28 fmt 1/8 msaa 0 dst 0; vertex sprite_plane_ui_vs 2038 words "
    "(450 instructions), sprite_plane_world_vs 2040 words (450 instructions), "
    "sprite_stq_plane_ui_vs 2028 words (448 instructions), sprite_stq_plane_world_vs 2028 words "
    "(448 instructions) and 2 more; largest fragment sprite_stq_ps_planes 14954 words";
static const char k_cut[] = "creating pipelines 540 of 587: program 9 blend 5 vs 24 fs 30 fmt 1/8 "
                            "msaa 0 dst 0; vertex sprite_stq_plane_world_vs 2028 words (448 ins";

static void testNote(void)
{
    CHECK(pipeline_skip_family_of_note(k_alone) == PIPELINE_SKIP_SPRITE_PLANES);
    CHECK(pipeline_skip_family_of_note(k_planes_batch) == PIPELINE_SKIP_SPRITE_PLANES);
    CHECK(pipeline_skip_family_of_note(k_lazy_planes) == PIPELINE_SKIP_SPRITE_PLANES);
    /* the hung pipeline of the v0.9.5 batch is not known: no family */
    CHECK(pipeline_skip_family_of_note(k_v095_batch) == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note(k_v094_plain) == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note(k_vu_batch) == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note(k_more) == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note(k_cut) == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note("creating the start-up pipelines (3 of 587)") ==
          PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note("running with the pipelines created as the frames need "
                                       "them (none was being created)") == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note("creating pipeline the progress screen") ==
          PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note("") == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_family_of_note(NULL) == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_vs_is_sprite_planes("sprite_stq_plane_ui_vs"));
    CHECK(!pipeline_skip_vs_is_sprite_planes("sprite_stq_ui_vs"));
    CHECK(!pipeline_skip_vs_is_sprite_planes("vu_tri_planes_pre_vs"));
}

static void testRecord(void)
{
    PipelineSkip a, b;
    char value[256], drv[64];

    memset(&a, 0, sizeof(a));
    a.family = PIPELINE_SKIP_SPRITE_PLANES;
    snprintf(a.device, sizeof(a.device), "Adreno (TM) 740");
    snprintf(a.driver, sizeof(a.driver), "E031.41.03.47");
    snprintf(a.date, sizeof(a.date), "2026-10-30");
    CHECK(pipeline_skip_format(value, sizeof(value), &a) == 0);
    CHECK(strcmp(value, "sprite_planes; Adreno (TM) 740; E031.41.03.47; 2026-10-30") == 0);
    CHECK(pipeline_skip_parse(value, &b) == 0);
    CHECK(b.family == PIPELINE_SKIP_SPRITE_PLANES);
    CHECK(strcmp(b.device, a.device) == 0 && strcmp(b.driver, a.driver) == 0 &&
          strcmp(b.date, a.date) == 0);
    /* a hand-written value with other spacing */
    CHECK(pipeline_skip_parse("  sprite_planes ;Adreno (TM) 740;E031.41.03.47 ", &b) == 0);
    CHECK(strcmp(b.device, "Adreno (TM) 740") == 0 && strcmp(b.driver, "E031.41.03.47") == 0 &&
          b.date[0] == '\0');
    /* the family alone: every device and driver */
    CHECK(pipeline_skip_parse("sprite_planes", &b) == 0 && b.device[0] == '\0' &&
          b.driver[0] == '\0');
    /* separators and quotes in a name */
    snprintf(a.device, sizeof(a.device), "GPU \"X\"; rev\\2");
    CHECK(pipeline_skip_format(value, sizeof(value), &a) == 0);
    CHECK(strchr(value, '"') == NULL && strchr(value, '\\') == NULL);
    CHECK(pipeline_skip_parse(value, &b) == 0 && strcmp(b.device, "GPU  X   rev 2") == 0 &&
          strcmp(b.driver, "E031.41.03.47") == 0);
    /* no family: nothing to write; too small: not written */
    a.family = PIPELINE_SKIP_NONE;
    CHECK(pipeline_skip_format(value, sizeof(value), &a) == -1);
    a.family = PIPELINE_SKIP_SPRITE_PLANES;
    CHECK(pipeline_skip_format(value, 8, &a) == -1);
    /* the driver word */
    pipeline_skip_driver_word(drv, sizeof(drv), "Compiler Version: E031.41.03.47", NULL);
    CHECK(strcmp(drv, "E031.41.03.47") == 0);
    pipeline_skip_driver_word(drv, sizeof(drv), NULL,
                              "device 10de:1b81; driver NVIDIA 566.36, version 566.36.0.0");
    CHECK(strcmp(drv, "566.36.0.0") == 0);
    pipeline_skip_driver_word(drv, sizeof(drv), "",
                              "device 5143:43050a01; driver Qualcomm, version 512.676.53");
    CHECK(strcmp(drv, "512.676.53") == 0);
    pipeline_skip_driver_word(drv, sizeof(drv), NULL, NULL);
    CHECK(drv[0] == '\0');
}

static void testMatch(void)
{
    PipelineSkip r;
    CHECK(pipeline_skip_parse("sprite_planes; Adreno (TM) 740; E031.41.03.47; 2026-10-30", &r) ==
          0);
    CHECK(pipeline_skip_matches(&r, "Adreno (TM) 740", "E031.41.03.47"));
    CHECK(pipeline_skip_matches(&r, "adreno (tm) 740", "e031.41.03.47"));
    /* the Xiaomi Pad's newer compiler (issue 123) compiles the set */
    CHECK(!pipeline_skip_matches(&r, "Adreno (TM) 740", "E031.41.03.64"));
    CHECK(!pipeline_skip_matches(&r, "Adreno (TM) 750", "E031.41.03.47"));
    CHECK(!pipeline_skip_matches(&r, NULL, "E031.41.03.47"));
    CHECK(pipeline_skip_parse("sprite_planes; ; ; ", &r) == 0);
    CHECK(pipeline_skip_matches(&r, "llvmpipe (LLVM 19.1.7, 256 bits)", "0.0.1"));
    CHECK(!pipeline_skip_matches(NULL, "x", "y"));
}

static void testClear(void)
{
    PipelineSkip r;
    CHECK(pipeline_skip_parse("", &r) == -1 && r.family == PIPELINE_SKIP_NONE);
    CHECK(pipeline_skip_parse("   ", &r) == -1);
    CHECK(pipeline_skip_parse(NULL, &r) == -1);
    CHECK(pipeline_skip_parse("vu_extended; Adreno (TM) 740; E031.41.03.47", &r) == -1);
    CHECK(!pipeline_skip_matches(&r, "Adreno (TM) 740", "E031.41.03.47"));
}

int main(void)
{
    testNote();
    testRecord();
    testMatch();
    testClear();
    printf("pipeline_skip_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
