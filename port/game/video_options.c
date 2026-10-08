/*
 * port/game/video_options.c
 *
 * The display options (video_options.h).
 */
#include "video_options.h"
#include <stdio.h>
#include <string.h>
#include "config.h"

#define ASPECT_4_3 (4.0f / 3.0f)
#define ASPECT_16_9 (16.0f / 9.0f)
#define ASPECT_21_9 (64.0f / 27.0f)
#define ASPECT_32_9 (32.0f / 9.0f)

static IcoVideoOptions s_opt;

static int s_read;

static unsigned s_serial;

static int s_winW, s_winH;

/* v0.4.2 (N2): the Android rules (a 60 a second frame rate default, and
   Enhanced's resolution "auto"); the build's, or a test's
   (ico_video_set_android) */
#ifdef __ANDROID__
static int s_android = 1;
#else
static int s_android = 0;
#endif

/* v0.4.2 (N2): resolution "auto"'s scene scale in force (0: the window's) */
static int s_autoScale;

static void set_preset(IcoVideoOptions *o, int preset, int android);

void ico_video_set_android(int android)
{
    s_android = android != 0;
}

int ico_video_android(void)
{
    return s_android;
}

void ico_video_set_auto_scale(int scale)
{
    s_autoScale = scale > 0 ? scale : 0;
}

int ico_video_auto_scale(void)
{
    return s_autoScale;
}

int ico_video_default_framerate(int android)
{
    /* R7b: "uncapped"; v0.4.2 (N2): 60 on Android, where "uncapped" in
       mailbox mode presented twice a display refresh (window_host.c pace),
       two full replays a refresh on the one thread that also runs the game */
    return android ? 60 : ICO_FRAMERATE_UNCAPPED;
}

void ico_video_defaults_for(IcoVideoOptions *o, int android)
{
    memset(o, 0, sizeof(*o));
    set_preset(o, ICO_VIDEO_ORIGINAL, android);
    o->vsync = 1;
    o->framerate = ico_video_default_framerate(android);
    o->crt = 0;
    o->crtMode = ICO_CRT_CONSUMER;
    o->crtStrength = 1.0f;
    o->crtScanlines = o->crtMask = o->crtHalation = o->crtBloom = o->crtCurvature = -1.0f;
    o->texturePack = 1;
    o->dumpTextures = 0;
    o->modelPack = 1;
    o->dumpModels = 0;
    o->texturePackBudgetMb = ICO_TEXPACK_BUDGET_DEFAULT;
    o->texturePackPrecache = 1;
    o->texturePackCacheMb = 0;
    o->effectGlow = 1;
    o->effectDepthOfField = 1;
    o->effectSoftening = 1;
    o->effectMotionBlur = 1;
    o->effectFog = 1;
    o->effectsDepth = 1;
}

void ico_video_defaults(IcoVideoOptions *o)
{
    ico_video_defaults_for(o, s_android);
}

static int lower_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char c = *a >= 'A' && *a <= 'Z' ? (char)(*a - 'A' + 'a') : *a;

        if (c != *b) {
            return 0;
        }
    }
    return *a == 0 && *b == 0;
}

int ico_video_parse_resolution(const char *s, IcoVideoOptions *o)
{
    unsigned w = 0, h = 0, n = 0;
    char tail = 0;

    if (s == NULL) {
        return -1;
    }
    if (lower_eq(s, "window")) {
        o->resW = o->resH = o->resScale = 0;
        return 0;
    }
    if (lower_eq(s, "auto")) {
        o->resW = o->resH = 0;
        o->resScale = ICO_RES_AUTO;
        return 0;
    }
    if (sscanf(s, "%ux%u%c", &w, &h, &tail) == 2 && w >= 64 && h >= 64 && w <= 7680 && h <= 4320) {
        o->resW = (int)w;
        o->resH = (int)h;
        o->resScale = 0;
        return 0;
    }
    if (sscanf(s, "%u%c%c", &n, &tail, &tail) == 2 && (tail == 'x' || tail == 'X') && n >= 1 &&
        n <= 8) {
        o->resW = o->resH = 0;
        o->resScale = (int)n;
        return 0;
    }
    return -1;
}

static const char *const kAspect[] = {"4:3", "16:10", "16:9", "21:9", "32:9", "auto"};

static const char *const kFilter[] = {"original", "trilinear", "anisotropic"};

static const char *const kCrtMode[ICO_CRT_MODES] = {"scanlines", "consumer", "trinitron", "pvm",
                                                    "shadow"};

int ico_video_parse_crt_mode(const char *s, int *mode)
{
    for (int i = 0; s && i < ICO_CRT_MODES; i++) {
        if (lower_eq(s, kCrtMode[i])) {
            *mode = i;
            return 0;
        }
    }
    return -1;
}

const char *ico_video_crt_mode_name(int mode)
{
    return mode >= 0 && mode < ICO_CRT_MODES ? kCrtMode[mode] : kCrtMode[ICO_CRT_CONSUMER];
}

/* an override: < 0 (or not a number) is "the mode's", else at most hi */
static float crt_override(float v, float hi)
{
    if (!(v >= 0.0f)) {
        return -1.0f;
    }
    return v > hi ? hi : v;
}

int ico_video_parse_aspect(const char *s, int *aspect)
{
    for (int i = 0; s && i < ICO_ASPECT_COUNT; i++) {
        if (lower_eq(s, kAspect[i])) {
            *aspect = i;
            return 0;
        }
    }
    return -1;
}

int ico_video_parse_filter(const char *s, int *filter)
{
    for (int i = 0; s && i < 3; i++) {
        if (lower_eq(s, kFilter[i])) {
            *filter = i;
            return 0;
        }
    }
    return -1;
}

#define FRAMERATE_MIN 30
#define FRAMERATE_MAX 1000

int ico_video_parse_framerate(const char *s, int *framerate)
{
    unsigned n = 0;
    char tail = 0;

    if (s == NULL) {
        return -1;
    }
    if (lower_eq(s, "original")) {
        *framerate = ICO_FRAMERATE_ORIGINAL;
        return 0;
    }
    if (lower_eq(s, "uncapped")) {
        *framerate = ICO_FRAMERATE_UNCAPPED;
        return 0;
    }
    if (sscanf(s, "%u%c", &n, &tail) == 1 && n >= FRAMERATE_MIN && n <= FRAMERATE_MAX) {
        *framerate = (int)n;
        return 0;
    }
    return -1;
}

const char *ico_video_framerate_name(int framerate, char *buf, unsigned size)
{
    if (framerate == ICO_FRAMERATE_ORIGINAL) {
        snprintf(buf, size, "original");
    } else if (framerate > 0) {
        snprintf(buf, size, "%d", framerate);
    } else {
        snprintf(buf, size, "uncapped");
    }
    return buf;
}

const char *ico_video_aspect_name(int aspect)
{
    return aspect >= 0 && aspect < ICO_ASPECT_COUNT ? kAspect[aspect] : kAspect[0];
}

const char *ico_video_filter_name(int filter)
{
    return filter >= 0 && filter < 3 ? kFilter[filter] : kFilter[0];
}

int ico_video_preset(const IcoVideoOptions *o)
{
    if (o->resW != 0 || o->resH != 0) {
        return ICO_VIDEO_CUSTOM;
    }
    if (o->resScale == 1 && o->aspect == ICO_ASPECT_4_3 && o->filter == ICO_FILTER_ORIGINAL &&
        !o->fullHeight) {
        return ICO_VIDEO_ORIGINAL;
    }
    /* Enhanced's resolution: the window's, "auto" on Android (N2) */
    if (o->resScale == (s_android ? ICO_RES_AUTO : 0) && o->aspect == ICO_ASPECT_AUTO &&
        o->filter == ICO_FILTER_ANISOTROPIC && o->fullHeight) {
        return ICO_VIDEO_ENHANCED;
    }
    return ICO_VIDEO_CUSTOM;
}

static void set_preset(IcoVideoOptions *o, int preset, int android)
{
    if (preset == ICO_VIDEO_ORIGINAL) {
        /* the PS2 picture: 1x, 4:3, original filtering, half height */
        o->resW = o->resH = 0;
        o->resScale = 1;
        o->aspect = ICO_ASPECT_4_3;
        o->filter = ICO_FILTER_ORIGINAL;
        o->fullHeight = 0;
    } else if (preset == ICO_VIDEO_ENHANCED) {
        /* the window's size (on Android "auto": the window's, lowered
           while the phone falls behind), its aspect, anisotropic, full
           height */
        o->resW = o->resH = 0;
        o->resScale = android ? ICO_RES_AUTO : 0;
        o->aspect = ICO_ASPECT_AUTO;
        o->filter = ICO_FILTER_ANISOTROPIC;
        o->fullHeight = 1;
    }
}

void ico_video_set_preset(IcoVideoOptions *o, int preset)
{
    set_preset(o, preset, s_android);
}

const char *ico_video_preset_name(int preset)
{
    return preset == ICO_VIDEO_ORIGINAL ? "original"
                                        : (preset == ICO_VIDEO_ENHANCED ? "enhanced" : "custom");
}

const char *ico_video_resolution_name(const IcoVideoOptions *o, char *buf, unsigned size)
{
    if (o->resScale == ICO_RES_AUTO) {
        snprintf(buf, size, "auto");
    } else if (o->resScale > 0) {
        snprintf(buf, size, "%dx", o->resScale);
    } else if (o->resW > 0 && o->resH > 0) {
        snprintf(buf, size, "%dx%d", o->resW, o->resH);
    } else {
        snprintf(buf, size, "window");
    }
    return buf;
}

static void sanitize(IcoVideoOptions *o)
{
    IcoVideoOptions d;

    ico_video_defaults(&d);
    if (o->aspect < 0 || o->aspect >= ICO_ASPECT_COUNT) {
        o->aspect = d.aspect;
    }
    if (o->filter < 0 || o->filter > ICO_FILTER_ANISOTROPIC) {
        o->filter = d.filter;
    }
    if (o->resScale == ICO_RES_AUTO) {
        o->resW = o->resH = 0;
    } else if (o->resScale < 0 || o->resScale > 8 || o->resW < 0 || o->resH < 0) {
        o->resScale = o->resW = o->resH = 0;
    }
    o->fullscreen = o->fullscreen != 0;
    o->vsync = o->vsync != 0;
    o->fullHeight = o->fullHeight != 0;
    if (o->framerate != ICO_FRAMERATE_ORIGINAL && o->framerate != ICO_FRAMERATE_UNCAPPED &&
        (o->framerate < FRAMERATE_MIN || o->framerate > FRAMERATE_MAX)) {
        o->framerate = d.framerate;
    }
    o->crt = o->crt != 0;
    if (o->crtMode < 0 || o->crtMode >= ICO_CRT_MODES) {
        o->crtMode = d.crtMode;
    }
    if (!(o->crtStrength >= 0.0f)) {
        o->crtStrength = 0.0f;
    } else if (o->crtStrength > 1.0f) {
        o->crtStrength = 1.0f;
    }
    o->crtScanlines = crt_override(o->crtScanlines, 1.0f);
    o->crtMask = crt_override(o->crtMask, 1.0f);
    o->crtHalation = crt_override(o->crtHalation, 1.0f);
    o->crtBloom = crt_override(o->crtBloom, 1.0f);
    o->crtCurvature = crt_override(o->crtCurvature, 0.25f);
    o->texturePack = o->texturePack != 0;
    o->dumpTextures = o->dumpTextures != 0;
    o->modelPack = o->modelPack != 0;
    o->dumpModels = o->dumpModels != 0;
    if (o->texturePackBudgetMb < ICO_TEXPACK_BUDGET_MIN) {
        o->texturePackBudgetMb = ICO_TEXPACK_BUDGET_MIN;
    } else if (o->texturePackBudgetMb > ICO_TEXPACK_BUDGET_MAX) {
        o->texturePackBudgetMb = ICO_TEXPACK_BUDGET_MAX;
    }
    o->texturePackPrecache = o->texturePackPrecache != 0;
    if (o->texturePackCacheMb < 0) {
        o->texturePackCacheMb = 0;
    } else if (o->texturePackCacheMb > 0 && o->texturePackCacheMb < ICO_TEXPACK_CACHE_MIN) {
        o->texturePackCacheMb = ICO_TEXPACK_CACHE_MIN;
    } else if (o->texturePackCacheMb > ICO_TEXPACK_CACHE_MAX) {
        o->texturePackCacheMb = ICO_TEXPACK_CACHE_MAX;
    }
    o->effectGlow = !!o->effectGlow;
    o->effectDepthOfField = !!o->effectDepthOfField;
    o->effectSoftening = !!o->effectSoftening;
    o->effectMotionBlur = !!o->effectMotionBlur;
    o->effectFog = !!o->effectFog;
    o->effectsDepth = !!o->effectsDepth;
}

static void read_config(void)
{
    IcoVideoOptions o;
    const char *s;
    int preset;

    ico_video_defaults(&o);
    /* no key: Enhanced's resolution (N2: "auto" on Android) */
    {
        const char *dres = s_android ? "auto" : "window";

        if (ico_video_parse_resolution(ico_config_get_string("video.resolution", dres), &o) != 0) {
            ico_video_parse_resolution(dres, &o);
            fprintf(stderr, "video: resolution not understood; \"%s\" used\n", dres);
        }
    }
    if (ico_video_parse_aspect(ico_config_get_string("video.aspect", "4:3"), &o.aspect) != 0) {
        fprintf(stderr, "video: aspect not understood; \"4:3\" used\n");
    }
    if (ico_video_parse_filter(ico_config_get_string("video.texture_filter", "original"),
                               &o.filter) != 0) {
        fprintf(stderr, "video: texture_filter not understood; \"original\" used\n");
    }
    o.fullscreen = ico_config_get_bool("video.fullscreen", 0) != 0;
    o.vsync = ico_config_get_bool("video.vsync", 1) != 0;
    o.fullHeight = ico_config_get_bool("video.full_height", 0) != 0;
    {
        /* no key: the default (N2: 60 on Android, else "uncapped") */
        char dfr[16];

        ico_video_framerate_name(ico_video_default_framerate(s_android), dfr, sizeof(dfr));
        if (ico_video_parse_framerate(ico_config_get_string("video.framerate", dfr),
                                      &o.framerate) != 0) {
            o.framerate = ico_video_default_framerate(s_android);
            fprintf(stderr, "video: framerate not understood; \"%s\" used\n", dfr);
        }
    }
    /* package CRT */
    o.crt = ico_config_get_bool("video.crt", 0) != 0;
    if (ico_video_parse_crt_mode(ico_config_get_string("video.crt_mode", "consumer"), &o.crtMode) !=
        0) {
        fprintf(stderr, "video: crt_mode not understood; \"consumer\" used\n");
    }
    o.crtStrength = (float)ico_config_get_float("video.crt_strength", 1.0);
    o.crtScanlines = (float)ico_config_get_float("video.crt_scanlines", -1.0);
    o.crtMask = (float)ico_config_get_float("video.crt_mask", -1.0);
    o.crtHalation = (float)ico_config_get_float("video.crt_halation", -1.0);
    o.crtBloom = (float)ico_config_get_float("video.crt_bloom", -1.0);
    o.crtCurvature = (float)ico_config_get_float("video.crt_curvature", -1.0);
    /* texture packs */
    o.texturePack = ico_config_get_bool("video.texture_pack", 1) != 0;
    o.dumpTextures = ico_config_get_bool("video.dump_textures", 0) != 0;
    o.modelPack = ico_config_get_bool("video.model_pack", 1) != 0;
    o.dumpModels = ico_config_get_bool("video.dump_models", 0) != 0;
    {
        long long mb =
            ico_config_get_int("video.texture_pack_budget_mb", ICO_TEXPACK_BUDGET_DEFAULT);
        if (mb < ICO_TEXPACK_BUDGET_MIN || mb > ICO_TEXPACK_BUDGET_MAX) {
            fprintf(stderr, "video: texture_pack_budget_mb %lld outside %d..%d; clamped\n", mb,
                    ICO_TEXPACK_BUDGET_MIN, ICO_TEXPACK_BUDGET_MAX);
            mb = mb < ICO_TEXPACK_BUDGET_MIN ? ICO_TEXPACK_BUDGET_MIN : ICO_TEXPACK_BUDGET_MAX;
        }
        o.texturePackBudgetMb = (int)mb;
    }
    o.texturePackPrecache = ico_config_get_bool("video.texture_pack_precache", 1) != 0;
    {
        /* 0: half the computer's memory (texpack.c) */
        long long mb = ico_config_get_int("video.texture_pack_cache_mb", 0);
        if (mb != 0 && (mb < ICO_TEXPACK_CACHE_MIN || mb > ICO_TEXPACK_CACHE_MAX)) {
            fprintf(stderr, "video: texture_pack_cache_mb %lld is not 0 or %d..%d; clamped\n", mb,
                    ICO_TEXPACK_CACHE_MIN, ICO_TEXPACK_CACHE_MAX);
            mb = mb < ICO_TEXPACK_CACHE_MIN ? ICO_TEXPACK_CACHE_MIN : ICO_TEXPACK_CACHE_MAX;
        }
        o.texturePackCacheMb = (int)mb;
    }
    /* the effects (issue 11) */
    o.effectGlow = ico_config_get_bool("video.effect_glow", 1);
    o.effectDepthOfField = ico_config_get_bool("video.effect_depth_of_field", 1);
    o.effectSoftening = ico_config_get_bool("video.effect_softening", 1);
    o.effectMotionBlur = ico_config_get_bool("video.effect_motion_blur", 1);
    o.effectFog = ico_config_get_bool("video.effect_fog", 1);
    /* v0.4.1 (R1): the depth handed to an effects program (ReShade) */
    o.effectsDepth = ico_config_get_bool("video.effects_depth", 1);
    /* the preset is a shortcut over the four rows: only "enhanced" and
       "custom" take them as written; "original", no key, or anything else
       (a misspelling) is the PS2 picture whatever they say */
    s = ico_config_get_string("video.preset", "original");
    if (s == NULL || !(lower_eq(s, "enhanced") || lower_eq(s, "custom"))) {
        ico_video_set_preset(&o, ICO_VIDEO_ORIGINAL);
    }
    sanitize(&o);
    s_opt = o;
    s_read = 1;
    s_serial++;
    preset = ico_video_preset(&o);
    if (preset != ICO_VIDEO_ORIGINAL) {
        char res[32], fr[16];

        fprintf(stderr,
                "video: %s preset: resolution %s, aspect %s, texture filter %s, %s "
                "height, framerate %s\n",
                preset == ICO_VIDEO_ENHANCED ? "Enhanced" : "Custom",
                ico_video_resolution_name(&o, res, sizeof(res)), ico_video_aspect_name(o.aspect),
                ico_video_filter_name(o.filter), o.fullHeight ? "full" : "half",
                ico_video_framerate_name(o.framerate, fr, sizeof(fr)));
    }
}

void ico_video_get(IcoVideoOptions *o)
{
    if (!s_read) {
        read_config();
    }
    *o = s_opt;
}

void ico_video_set(const IcoVideoOptions *o)
{
    s_opt = *o;
    sanitize(&s_opt);
    s_read = 1;
    s_serial++;
}

unsigned ico_video_serial(void)
{
    if (!s_read) {
        read_config();
    }
    return s_serial;
}

int ico_video_save(void)
{
    IcoVideoOptions o;
    char res[32], fr[16];
    int r = 0;

    ico_video_get(&o);
    /* "original" only when the rows are the PS2's (an older build reads the
       rest as Enhanced with these rows: the same picture) */
    r |= ico_config_set_string(
        "video.preset", ico_video_preset(&o) == ICO_VIDEO_ORIGINAL ? "original" : "enhanced");
    r |= ico_config_set_string("video.resolution", ico_video_resolution_name(&o, res, sizeof(res)));
    r |= ico_config_set_string("video.aspect", ico_video_aspect_name(o.aspect));
    r |= ico_config_set_bool("video.fullscreen", o.fullscreen);
    r |= ico_config_set_bool("video.vsync", o.vsync);
    r |= ico_config_set_string("video.texture_filter", ico_video_filter_name(o.filter));
    r |= ico_config_set_bool("video.full_height", o.fullHeight);
    r |= ico_config_set_string("video.framerate",
                               ico_video_framerate_name(o.framerate, fr, sizeof(fr)));
    r |= ico_config_set_bool("video.crt", o.crt);
    r |= ico_config_set_string("video.crt_mode", ico_video_crt_mode_name(o.crtMode));
    /* to the hundredth, without float noise (the Settings row steps tenths;
       a hand-set 0.25 is kept) */
    r |= ico_config_set_float("video.crt_strength",
                              (double)(int)(o.crtStrength * 100.0f + 0.5f) / 100.0);
    /* the overrides only when set: an absent key is the mode's value */
    {
        static const char *const keys[5] = {"video.crt_scanlines", "video.crt_mask",
                                            "video.crt_halation", "video.crt_bloom",
                                            "video.crt_curvature"};
        const float v[5] = {o.crtScanlines, o.crtMask, o.crtHalation, o.crtBloom, o.crtCurvature};
        for (int i = 0; i < 5; i++) {
            if (v[i] >= 0.0f) {
                r |= ico_config_set_float(keys[i], (double)v[i]);
            }
        }
    }
    r |= ico_config_set_bool("video.texture_pack", o.texturePack);
    r |= ico_config_set_bool("video.dump_textures", o.dumpTextures);
    r |= ico_config_set_bool("video.model_pack", o.modelPack);
    r |= ico_config_set_bool("video.dump_models", o.dumpModels);
    /* the config-only keys only when not at their defaults, as the CRT
       overrides: an absent key is the default */
    if (o.texturePackBudgetMb != ICO_TEXPACK_BUDGET_DEFAULT) {
        r |= ico_config_set_int("video.texture_pack_budget_mb", o.texturePackBudgetMb);
    }
    if (!o.texturePackPrecache) {
        r |= ico_config_set_bool("video.texture_pack_precache", 0);
    }
    if (o.texturePackCacheMb != 0) {
        r |= ico_config_set_int("video.texture_pack_cache_mb", o.texturePackCacheMb);
    }
    /* the effects always: each has a row in the menu, as texture_pack */
    r |= ico_config_set_bool("video.effect_glow", o.effectGlow);
    r |= ico_config_set_bool("video.effect_depth_of_field", o.effectDepthOfField);
    r |= ico_config_set_bool("video.effect_softening", o.effectSoftening);
    r |= ico_config_set_bool("video.effect_motion_blur", o.effectMotionBlur);
    r |= ico_config_set_bool("video.effect_fog", o.effectFog);
    /* file-only, so only when not at its default, or to keep a key the
       file already has in step (2: no key) */
    if (!o.effectsDepth || ico_config_get_bool("video.effects_depth", 2) != 2) {
        r |= ico_config_set_bool("video.effects_depth", o.effectsDepth);
    }
    return r != 0 ? -1 : ico_config_save();
}

void ico_video_reload(void)
{
    s_read = 0;
}

void ico_video_set_window(int w, int h)
{
    s_winW = w > 0 ? w : 0;
    s_winH = h > 0 ? h : 0;
}

float ico_video_aspect(void)
{
    IcoVideoOptions o;
    float a;

    ico_video_get(&o);
    switch (o.aspect) {
    case ICO_ASPECT_16_10:
        return 16.0f / 10.0f;
    case ICO_ASPECT_16_9:
        return ASPECT_16_9;
    case ICO_ASPECT_21_9:
        return ASPECT_21_9;
    case ICO_ASPECT_32_9:
        return ASPECT_32_9;
    case ICO_ASPECT_AUTO:
        if (s_winW <= 0 || s_winH <= 0) {
            return ASPECT_4_3;
        }
        a = (float)s_winW / (float)s_winH;
        return a < ASPECT_4_3 ? ASPECT_4_3 : (a > ASPECT_32_9 ? ASPECT_32_9 : a);
    default:
        return ASPECT_4_3;
    }
}

float ico_video_wide_x(void)
{
    float k = ico_video_aspect() / ASPECT_4_3;

    return k > 1.0f + 1e-5f ? k : 1.0f;
}

/* issue 11: the effect switches, read once a frame at most by the game */
int ico_video_effect_glow(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.effectGlow;
}

int ico_video_effect_depth_of_field(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.effectDepthOfField;
}

int ico_video_effect_softening(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.effectSoftening;
}

int ico_video_effect_motion_blur(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.effectMotionBlur;
}

int ico_video_effect_fog(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.effectFog;
}

int ico_video_effects_depth(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.effectsDepth;
}

int ico_video_model_pack(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.modelPack;
}

int ico_video_dump_models(void)
{
    if (!s_read) {
        read_config();
    }
    return s_opt.dumpModels;
}

int ico_video_framerate(void)
{
    IcoVideoOptions o;

    ico_video_get(&o);
    /* whatever the preset (F2): the Original picture is the PS2's per tick,
       and "uncapped" presents it between ticks too */
    return o.framerate;
}

/* R7b: the camera-cut signal (video_options.h) */
static unsigned s_cutSerial;

void ico_video_camera_cut(void)
{
    s_cutSerial++;
}

unsigned ico_video_cut_serial(void)
{
    return s_cutSerial;
}
