/*
 * port/data/df_pack.h
 *
 * Reading one member of a stage pack from the disc, for the port's own
 * screens (the music gallery, port/ui/gallery_play.c), outside the game's
 * cdvd thread.
 *
 * DATA.DF ("DFDATAS/DATA.DF") starts with a directory: a count, then
 * 40-byte entries of a 32-byte name, a byte offset into DATA.DF and a byte
 * size (fumi/ios/cdvd.c, unifile_read_func).  The entries named *.DF are
 * packs: a raw deflate stream whose output is a 16-byte header (the member
 * count first), the 0x224-byte member entries (id, kind, word08, size, the
 * name) and the members back to back (cdvd.c, iosCdvdMgrPackLoad).  The other entries
 * are loose files (the .int streams, .smb, .pss, .jim).
 *
 * The index of the packs' members is built once per volume, the first time
 * a member is looked up, by inflating each pack's header and directory
 * (about 70 packs).  A member is read by inflating its pack from the start
 * up to the member's end.
 */
#ifndef ICO_PORT_DF_PACK_H
#define ICO_PORT_DF_PACK_H

#include <stdint.h>

#include "vfs.h"

/* Where a member is: the pack's DATA.DF entry, the member's offset in the
   pack's inflated stream, its size and the directory entry's id and kind. */
typedef struct IcoDfMember {
    int pack;      /* the DATA.DF directory entry */
    uint32_t off;  /* in the inflated pack */
    uint32_t size; /* bytes */
    int id;        /* PackEnt.id: the loader's file number (seFile row for a sound bank) */
    int kind;      /* PackEnt.kind: 11 an SE bank, 10 BGM */
} IcoDfMember;

/* Whether DATA.DF's directory has an entry `name` (case-insensitive, the
   directory's spelling: "01.int", "STGTTL.DF").  1, 0, or -1 when DATA.DF
   cannot be read. */
int ico_df_has(IcoVfs *vfs, const char *name);

/* The byte size of DATA.DF's directory entry `name` (a loose file, as
   "01.int"), or -1 when it has none or DATA.DF cannot be read. */
int64_t ico_df_size(IcoVfs *vfs, const char *name);

/* The name of DATA.DF's directory entry i (a pack or a loose file), or NULL
   past the last one or when DATA.DF cannot be read. */
const char *ico_df_entry_name(IcoVfs *vfs, int i);

/* Reads up to n bytes of the loose file `name` from its byte `off`: the
   bytes read (0 past its end), or -1 when DATA.DF has no such entry or
   cannot be read. */
int64_t ico_df_read(IcoVfs *vfs, const char *name, uint64_t off, void *dst, size_t n);

/* The first pack member named `name` (the member's full name, as
   "sound/ICO_SE/com_v.hd"; case-insensitive) in DATA.DF's directory order.
   0 and *out filled, or -1 when no pack has it or the disc cannot be read. */
int ico_df_find_member(IcoVfs *vfs, const char *name, IcoDfMember *out);

/* Reads a member found by ico_df_find_member into dst (m->size bytes).
   0, or -1 on a read or inflate error. */
int ico_df_read_member(IcoVfs *vfs, const IcoDfMember *m, void *dst);

/* Packs indexed and members recorded (0 before the first look-up). */
int ico_df_index_packs(void);
int ico_df_index_members(void);
/* The name of indexed member i (0 .. ico_df_index_members() - 1), NULL. */
const char *ico_df_member_name(int i);
/* Where indexed member i is (every member, also one whose name an earlier
   pack has too): 0 and *out filled, or -1 for an i out of range. */
int ico_df_member(int i, IcoDfMember *out);

/* Forgets the index (a new volume; tests). */
void ico_df_reset(void);

#endif /* ICO_PORT_DF_PACK_H */
