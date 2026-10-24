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
 *
 * The calls that take an IcoVfs share one index (the game thread's: the
 * music gallery's); passing them another volume starts it over.  A reader
 * on another thread (the texture export, port/render/texpack_export.c)
 * opens an index of its own over a volume of its own (ico_df_index_open on
 * ico_vfs_reopen's mount), so the two never touch each other's state.
 */
#ifndef ICO_PORT_DF_PACK_H
#define ICO_PORT_DF_PACK_H

#include <stddef.h>
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

/* The index, for the tests (gallery_test.c, texpack_disc_test.c,
   appearance_disc_test.c).  Packs indexed and members recorded (0 before
   the first look-up). */
int ico_df_index_packs(void);
int ico_df_index_members(void);
/* The name of indexed member i (0 .. ico_df_index_members() - 1), NULL. */
const char *ico_df_member_name(int i);
/* Where indexed member i is (every member, also one whose name an earlier
   pack has too): 0 and *out filled, or -1 for an i out of range. */
int ico_df_member(int i, IcoDfMember *out);

/* --- an index of its own ---------------------------------------------------
 * The same directory and member index as the calls above, held by the
 * caller: nothing is shared with the global index or with another
 * IcoDfIndex, so one thread may use one while another uses the global one
 * (each over its own IcoVfs: a mount is not thread-safe either).  Member i
 * is the i-th member of the packs in DATA.DF's directory order, then in
 * each pack's own order, as ico_df_member_name(i) numbers them. */
typedef struct IcoDfIndex IcoDfIndex;

/* Reads DATA.DF's directory and indexes every pack's members (vfs must
   stay mounted until the close).  NULL when DATA.DF cannot be read or
   memory runs out. */
IcoDfIndex *ico_df_index_open(IcoVfs *vfs);
/* Frees the index (NULL: nothing); the volume stays mounted. */
void ico_df_index_close(IcoDfIndex *idx);

/* DATA.DF's directory entries (packs and loose files): the count, entry i's
   name (NULL out of range), its byte size (-1 out of range), the entry
   named `name` (case-insensitive) or -1, and up to n bytes of entry i from
   its byte off (the bytes read, 0 past its end, -1 on an error). */
int ico_df_index_entries(const IcoDfIndex *idx);
const char *ico_df_index_entry_name(const IcoDfIndex *idx, int i);
int64_t ico_df_index_entry_size(const IcoDfIndex *idx, int i);
int ico_df_index_entry_find(const IcoDfIndex *idx, const char *name);
int64_t ico_df_index_read_entry(IcoDfIndex *idx, int i, uint64_t off, void *dst, size_t n);

/* Packs indexed and members recorded. */
int ico_df_index_pack_count(const IcoDfIndex *idx);
int ico_df_index_member_count(const IcoDfIndex *idx);
/* Member i's name (NULL out of range) and where it is (0, or -1 out of
   range). */
const char *ico_df_index_member_name(const IcoDfIndex *idx, int i);
int ico_df_index_member_at(const IcoDfIndex *idx, int i, IcoDfMember *out);
/* The members of directory entry `pack`: their count (0 for a loose file,
   a pack that would not inflate or an entry out of range) and in *first
   (when not NULL) the number of the first; they are numbered on from it. */
int ico_df_index_pack_members(const IcoDfIndex *idx, int pack, int *first);
/* The first member named `name` (case-insensitive) in index order: 0 and
   *out filled, or -1. */
int ico_df_index_find(const IcoDfIndex *idx, const char *name, IcoDfMember *out);
/* Reads member m into dst (m->size bytes) by inflating its pack from the
   start: 0, or -1 on a read or inflate error. */
int ico_df_index_read_member(IcoDfIndex *idx, const IcoDfMember *m, void *dst);

/* Called once per member by ico_df_index_read_pack, in the pack's order:
   member is its number in the index, data its m->size bytes (valid until
   the callback returns).  Return 0 to go on, anything else to stop. */
typedef int (*IcoDfMemberFn)(void *user, int member, const char *name, const IcoDfMember *m,
                             const uint8_t *data);

/* Inflates directory entry `pack` once, start to end, and hands every
   member to fn in order (instead of one inflate from the start per member,
   as ico_df_index_read_member does).  It holds one buffer the size of the
   pack's largest member.  0 when every member was handed (also for a pack
   without members in the index: one whose directory would not inflate
   when the index was built, logged then), 1 when fn stopped it, -1 for an entry that is no pack,
   a read or an inflate error (the members before it were handed). */
int ico_df_index_read_pack(IcoDfIndex *idx, int pack, IcoDfMemberFn fn, void *user);

#endif /* ICO_PORT_DF_PACK_H */
