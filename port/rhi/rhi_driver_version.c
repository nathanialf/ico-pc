/* rhi_driver_version.c: the driver version text and the drivers' rules
 * (rhi_driver_version.h). */
#include "rhi_driver_version.h"
#include <stdio.h>
#include <string.h>

char *rhi_driver_version_text(uint32_t vendorId, uint32_t driverId, uint32_t driverVersion,
                              char *buf, size_t n)
{
    const uint32_t v = driverVersion;
    if (n == 0) {
        return buf;
    }
    const int nvidia = driverId != 0 ? driverId == RHI_DRIVER_ID_NVIDIA_PROPRIETARY
                                     : vendorId == RHI_VENDOR_NVIDIA;
    const int intelWin = driverId != 0 ? driverId == RHI_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS
#ifdef _WIN32
                                       : vendorId == RHI_VENDOR_INTEL;
#else
                                       : 0;
#endif
    if (nvidia) {
        const unsigned major = (v >> 22) & 0x3ffu, minor = (v >> 14) & 0xffu;
        const unsigned third = (v >> 6) & 0xffu, fourth = v & 0x3fu;
        if (third != 0 || fourth != 0) {
            snprintf(buf, n, "%u.%02u.%u.%u", major, minor, third, fourth);
        } else {
            snprintf(buf, n, "%u.%02u", major, minor);
        }
    } else if (intelWin) {
        snprintf(buf, n, "%u.%u", v >> 14, v & 0x3fffu);
    } else {
        snprintf(buf, n, "%u.%u.%u", v >> 22, (v >> 12) & 0x3ffu, v & 0xfffu);
    }
    return buf;
}

int rhi_driver_is_qualcomm_proprietary(uint32_t vendorId, uint32_t driverId)
{
    return vendorId == RHI_VENDOR_QUALCOMM &&
           (driverId == RHI_DRIVER_ID_QUALCOMM_PROPRIETARY || driverId == 0);
}

int rhi_driver_is_moltenvk(uint32_t vendorId, uint32_t driverId, int moltenvkBuild)
{
    return driverId == RHI_DRIVER_ID_MOLTENVK ||
           (moltenvkBuild && vendorId == RHI_VENDOR_APPLE && driverId == 0);
}

uint32_t rhi_metal_color_bytes(const char *deviceName)
{
    if (deviceName == NULL || strncmp(deviceName, "Apple ", 6) != 0) {
        return 0u;
    }
    const char *p = deviceName + 6;
    if (*p == 'M' && p[1] >= '1' && p[1] <= '9') {
        return 128u; /* the M-series: Apple7 and later */
    }
    if (*p != 'A' || p[1] < '0' || p[1] > '9') {
        return 0u;
    }
    unsigned n = 0;
    for (p++; *p >= '0' && *p <= '9' && n < 1000u; p++) {
        n = n * 10u + (unsigned)(*p - '0');
    }
    return n <= 10u ? 32u : n <= 13u ? 64u : 128u;
}

/* a case-insensitive strstr for an ASCII needle */
static const char *findNoCase(const char *s, const char *needle)
{
    const size_t n = strlen(needle);
    for (; *s != '\0'; s++) {
        size_t i = 0;
        while (i < n && s[i] != '\0' &&
               (s[i] | 0x20) == (needle[i] | 0x20) /* needle is lower-case letters */) {
            i++;
        }
        if (i == n) {
            return s;
        }
    }
    return NULL;
}

static int isDigit(char c)
{
    return c >= '0' && c <= '9';
}

/* major.minor[.patch][-devel] at s; 1 when at least major.minor is there */
static int readVersion(const char *s, RhiMesaVersion *out)
{
    unsigned part[3] = {0, 0, 0};
    int got = 0;
    while (got < 3 && isDigit(*s)) {
        unsigned v = 0;
        while (isDigit(*s)) {
            if (v < 10000u) {
                v = v * 10u + (unsigned)(*s - '0');
            }
            s++;
        }
        part[got++] = v;
        if (*s == '.' && isDigit(s[1])) {
            s++;
        } else {
            break;
        }
    }
    if (got < 2) {
        return 0;
    }
    out->known = 1;
    out->major = part[0];
    out->minor = part[1];
    out->patch = part[2];
    out->devel = s[0] == '-' && findNoCase(s, "devel") == s + 1;
    return 1;
}

int rhi_mesa_version_parse(const char *driverName, const char *driverInfo, RhiMesaVersion *out)
{
    memset(out, 0, sizeof(*out));
    if (driverInfo != NULL) {
        const char *m = driverInfo;
        while ((m = findNoCase(m, "mesa")) != NULL) {
            const char *p = m + 4;
            while (*p == ' ' || *p == '-' || *p == '_' || *p == 'v' || *p == 'V') {
                p++;
            }
            if (readVersion(p, out)) {
                return 1;
            }
            m += 4;
        }
        if (driverName != NULL && findNoCase(driverName, "mesa") != NULL &&
            readVersion(driverInfo, out)) {
            return 1;
        }
    }
    return 0;
}

char *rhi_mesa_version_text(const RhiMesaVersion *v, char *buf, size_t n)
{
    if (n == 0) {
        return buf;
    }
    if (v == NULL || !v->known) {
        buf[0] = '\0';
    } else {
        snprintf(buf, n, "Mesa %u.%u.%u%s", v->major, v->minor, v->patch, v->devel ? "-devel" : "");
    }
    return buf;
}

char *rhi_text_one_line(char *s)
{
    size_t w = 0;
    int gap = 0;

    if (s == NULL) {
        return s;
    }
    for (size_t r = 0; s[r] != '\0'; r++) {
        const unsigned char ch = (unsigned char)s[r];

        if (ch < 0x20 || ch == 0x7f) {
            gap = 1;
            continue;
        }
        if (gap && w > 0) {
            s[w++] = ' ';
        }
        if (ch == ' ' && w == 0) {
            continue;
        }
        gap = 0;
        s[w++] = (char)ch;
    }
    while (w > 0 && s[w - 1] == ' ') {
        w--;
    }
    s[w] = '\0';
    return s;
}
