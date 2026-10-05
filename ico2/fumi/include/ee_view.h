/*
 * ico2/fumi/include/ee_view.h
 *
 * A raw EE-offset view of a record that has a named field at that offset
 * (docs/port/OFFSET_AUDIT.md, "Conventions in ico2/").  Most of the decompiled actor code
 * reached Act, ActWork and the other runtime records through
 * `*(T *)((char *)p + 0xNN)`: right on the EE, wrong on a 64-bit host where
 * the record's pointer fields are 8 bytes wide.  Where the named field
 * compiles to the same EE bytes the code uses the field.  Where it does not
 * (the EE compiler's scheduling depends on how the access is spelled), the
 * access goes through these macros: the EE expands to the original view, the
 * host to the field expression the second form gives.
 *
 *   ICO_RAW(T, p, off, field)    the lvalue `*(T *)((char *)p + off)`
 *   ICO_RAWP(T, p, off, field)   the pointer `(T)((char *)p + off)` (T a
 *                                pointer type)
 */
#ifndef EE_VIEW_H
#define EE_VIEW_H
#ifdef ICO_HOST
#ifdef ICO_OFFSET_AUDIT

/* tools/offset_audit.py preprocesses the game with ICO_OFFSET_AUDIT: the EE
 * offset stays in the text next to the field, so the audit can check that the
 * field is the one at that EE offset (docs/port/OFFSET_AUDIT.md). Never
 * compiled into the game. */
int __ico_audit_raw();

#define ICO_RAW(T, p, off, field) (*(__ico_audit_raw((p), (off)), &(field)))
#define ICO_RAWP(T, p, off, field) (__ico_audit_raw((p), (off)), (field))
#else
#define ICO_RAW(T, p, off, field) (field)
#define ICO_RAWP(T, p, off, field) (field)
#endif
#else
#define ICO_RAW(T, p, off, field) (*(T *)((char *)(p) + (off)))
#define ICO_RAWP(T, p, off, field) ((T)((char *)(p) + (off)))
#endif
/* ICO_MAX_SIZE(T, lit): the allocation size of a record the EE code allocates
 * with a literal.  On the EE the literal is at least sizeof(T), so the value is
 * the literal; a host record can be wider (8-byte pointers), and then it is
 * sizeof(T). */
#define ICO_MAX_SIZE(T, lit) (sizeof(T) > (lit) ? sizeof(T) : (lit))
#ifdef ICO_HOST
/* A record copied whole over storage of another record type (a template
 * moved through a view, `*(T *)&u = tmpl`) is only right while the two host
 * layouts agree; on the EE they always did (docs/port/OFFSET_AUDIT.md,
 * "Whole-record copies").  The registrations tools/template_audit.py asks
 * for:
 *
 *   ICO_LAYOUT_AT(T, tm, U, um)             T's member tm is at U's um
 *   ICO_LAYOUT_AT_FROM(T, tm, U, base, um)  the same for T laid over U from
 *                                           U's member base
 *   ICO_LAYOUT_SIZE(T, U)                   the two records' sizes agree
 */
#define ICO_LAYOUT_AT(T, tm, U, um)                                                                \
    _Static_assert(__builtin_offsetof(T, tm) == __builtin_offsetof(U, um),                         \
                   #T "." #tm " is not at " #U "." #um " on the host")
#define ICO_LAYOUT_AT_FROM(T, tm, U, base, um)                                                     \
    _Static_assert(__builtin_offsetof(T, tm) ==                                                    \
                       __builtin_offsetof(U, um) - __builtin_offsetof(U, base),                    \
                   #T "." #tm " is not at " #U "." #um " (from " #base ") on the host")
#define ICO_LAYOUT_SIZE(T, U)                                                                      \
    _Static_assert(sizeof(T) == sizeof(U), #T " and " #U " differ in size on the host")
#endif
#endif /* EE_VIEW_H */
