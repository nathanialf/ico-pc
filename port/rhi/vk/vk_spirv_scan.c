/* vk_spirv_scan.c: vk_spirv_scan.h.  The module is walked instruction by
 * instruction (each word's high half its word count, the low half its
 * opcode; SPIR-V 1.6 section 2.3); the header is five words. */
#include "vk_spirv_scan.h"

#include <string.h>

enum {
    SPV_MAGIC = 0x07230203u,
    SPV_OP_CAPABILITY = 17,
    SPV_OP_ENTRY_POINT = 15,
    SPV_OP_TYPE_POINTER = 32,
    SPV_OP_VARIABLE = 59,
    SPV_OP_DECORATE = 71,
    SPV_OP_MEMBER_DECORATE = 72,
    SPV_MODEL_FRAGMENT = 4,
    SPV_CLASS_STORAGE_BUFFER = 12,
    SPV_CLASS_PHYSICAL_STORAGE_BUFFER = 5349,
    SPV_DECORATION_BUFFER_BLOCK = 3,
    SPV_CAP_PHYSICAL_STORAGE_BUFFER_ADDRESSES = 5347
};

static bool storageClass(uint32_t c)
{
    return c == SPV_CLASS_STORAGE_BUFFER || c == SPV_CLASS_PHYSICAL_STORAGE_BUFFER;
}

bool vkr_spirv_scan(const uint32_t *words, size_t count, VkrSpirvUse *out)
{
    VkrSpirvUse use;
    memset(&use, 0, sizeof(use));
    if (out) {
        *out = use;
    }
    if (!words || !out || count < 5 || words[0] != SPV_MAGIC) {
        return false;
    }
    size_t i = 5;
    while (i < count) {
        const uint32_t n = words[i] >> 16;
        const uint32_t op = words[i] & 0xFFFFu;
        if (n == 0 || n > count - i) {
            return false; /* a zero length or past the module's end */
        }
        switch (op) {
        case SPV_OP_CAPABILITY:
            if (n >= 2 && words[i + 1] == SPV_CAP_PHYSICAL_STORAGE_BUFFER_ADDRESSES) {
                use.storageBuffers = true;
            }
            break;
        case SPV_OP_ENTRY_POINT:
            if (n >= 2 && words[i + 1] == SPV_MODEL_FRAGMENT) {
                use.fragment = true;
            }
            break;
        case SPV_OP_TYPE_POINTER: /* result, storage class, type */
            if (n >= 3 && storageClass(words[i + 2])) {
                use.storageBuffers = true;
            }
            break;
        case SPV_OP_VARIABLE: /* result type, result, storage class */
            if (n >= 4 && storageClass(words[i + 3])) {
                use.storageBuffers = true;
            }
            break;
        case SPV_OP_DECORATE: /* target, decoration */
            if (n >= 3 && words[i + 2] == SPV_DECORATION_BUFFER_BLOCK) {
                use.storageBuffers = true;
            }
            break;
        case SPV_OP_MEMBER_DECORATE: /* structure, member, decoration */
            if (n >= 4 && words[i + 3] == SPV_DECORATION_BUFFER_BLOCK) {
                use.storageBuffers = true;
            }
            break;
        default:
            break;
        }
        i += n;
    }
    *out = use;
    return true;
}
