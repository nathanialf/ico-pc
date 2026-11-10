/* rhi_vk_spirv_scan_test.c: vk_spirv_scan.h over real and hand-built
 * modules (pure: no device).
 *
 *   - the RHI tests' own modules (rhi_test_spv.h): every fragment shader
 *     is a fragment module that declares no storage buffer (ps_rov's
 *     storage image is not one); vs_store and cs_store declare one; vs_main
 *     neither;
 *   - a fragment shader that reads a StructuredBuffer (rhi_vk_ps_storage.h,
 *     DXC's SPIR-V 1.0 form: a block decorated BufferBlock) is found; its
 *     vertex shader (vs_tri) declares none;
 *   - hand-built modules: the StorageBuffer storage class on a pointer and
 *     on a variable, the PhysicalStorageBuffer class, the
 *     PhysicalStorageBufferAddresses capability, BufferBlock on a member
 *     decoration; the Fragment model among several entry points;
 *   - modules that cannot be read whole (no magic, a header only short of
 *     its five words, an instruction of length 0, one running past the
 *     end, NULL) return false with nothing found, which the backend counts
 *     as a module that may read storage buffers (vk_resource.c).
 * Exit 0 on success, 1 on any mismatch. */
#include "rhi_test_spv.h"
#include "rhi_vk_ps_storage.h"
#include "vk/vk_spirv_scan.h"
#include <stdio.h>
#include <string.h>

static int s_failures;

static void expect(const char *what, const uint32_t *w, size_t n, bool ok, bool fragment,
                   bool storage)
{
    VkrSpirvUse u = {true, true}; /* the scan must clear what it does not find */
    const bool got = vkr_spirv_scan(w, n, &u);
    if (got != ok || u.fragment != fragment || u.storageBuffers != storage) {
        printf("FAIL %s: read %d fragment %d storage buffers %d, want %d %d %d\n", what, (int)got,
               (int)u.fragment, (int)u.storageBuffers, (int)ok, (int)fragment, (int)storage);
        s_failures++;
    }
}

#define REAL(a, frag, storage) expect(#a, a, sizeof(a) / sizeof(a[0]), true, frag, storage)

/* an instruction's first word */
#define OP(words, op) (((uint32_t)(words) << 16) | (uint32_t)(op))

int main(void)
{
    REAL(spv_vs_main, false, false);
    REAL(spv_ps_dual, true, false);
    REAL(spv_ps_color, true, false);
    REAL(spv_ps_tex, true, false);
    REAL(spv_ps_uint, true, false);
    REAL(spv_ps_depth_resolve, true, false);
    REAL(spv_ps_rov, true, false);
    REAL(spv_ps_subpass, true, false);
    REAL(spv_vs_store, false, true);
    REAL(spv_cs_store, false, true);
    REAL(spv_ps_storage, true, true);
    REAL(spv_vs_tri, false, false);

    /* header: magic, version 1.0, generator, bound, schema */
    enum { H0 = 0x07230203u, H1 = 0x00010000u, H2 = 0u, H3 = 16u, H4 = 0u };

    /* OpCapability Shader; OpEntryPoint Vertex %1 "v"; OpEntryPoint Fragment
       %2 "f" (the name a word each: "v\0\0\0", "f\0\0\0") */
    const uint32_t frag[] = {H0, H1, H2,    H3,        H4, OP(2, 17), 1u,   OP(4, 15),
                             0u, 1u, 0x76u, OP(4, 15), 4u, 2u,        0x66u};
    expect("two entry points, one Fragment", frag, sizeof(frag) / 4, true, true, false);
    /* OpTypePointer %3 StorageBuffer %4 */
    const uint32_t ptr[] = {H0, H1, H2, H3, H4, OP(4, 32), 3u, 12u, 4u};
    expect("a StorageBuffer pointer", ptr, sizeof(ptr) / 4, true, false, true);
    /* OpVariable %3 %5 StorageBuffer */
    const uint32_t var[] = {H0, H1, H2, H3, H4, OP(4, 59), 3u, 5u, 12u};
    expect("a StorageBuffer variable", var, sizeof(var) / 4, true, false, true);
    /* OpTypePointer %3 PhysicalStorageBuffer %4 */
    const uint32_t psb[] = {H0, H1, H2, H3, H4, OP(4, 32), 3u, 5349u, 4u};
    expect("a PhysicalStorageBuffer pointer", psb, sizeof(psb) / 4, true, false, true);
    /* OpCapability PhysicalStorageBufferAddresses */
    const uint32_t cap[] = {H0, H1, H2, H3, H4, OP(2, 17), 5347u};
    expect("the buffer device address capability", cap, sizeof(cap) / 4, true, false, true);
    /* OpMemberDecorate %6 0 BufferBlock (not a valid placement, counted
       all the same) */
    const uint32_t mem[] = {H0, H1, H2, H3, H4, OP(4, 72), 6u, 0u, 3u};
    expect("BufferBlock as a member decoration", mem, sizeof(mem) / 4, true, false, true);
    /* OpDecorate %6 Block (a uniform block: not a storage buffer);
       OpTypePointer %7 Uniform %6 */
    const uint32_t ubo[] = {H0, H1, H2, H3, H4, OP(3, 71), 6u, 2u, OP(4, 32), 7u, 2u, 6u};
    expect("a uniform block", ubo, sizeof(ubo) / 4, true, false, false);

    const uint32_t notSpirv[] = {0x12345678u, H1, H2, H3, H4};
    expect("no magic", notSpirv, 5, false, false, false);
    expect("a short header", frag, 4, false, false, false);
    const uint32_t zero[] = {H0, H1, H2, H3, H4, OP(0, 17)};
    expect("an instruction of length 0", zero, sizeof(zero) / 4, false, false, false);
    expect("an instruction past the end", ptr, sizeof(ptr) / 4 - 1, false, false, false);
    expect("NULL", NULL, 0, false, false, false);
    if (vkr_spirv_scan(frag, sizeof(frag) / 4, NULL)) {
        printf("FAIL no output: read\n");
        s_failures++;
    }

    if (s_failures) {
        printf("rhi_vk_spirv_scan: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("rhi_vk_spirv_scan: ok (the RHI tests' 10 modules, the storage fragment shader and "
           "its vertex shader as expected; 7 hand-built modules; 6 unreadable ones refused)\n");
    return 0;
}
