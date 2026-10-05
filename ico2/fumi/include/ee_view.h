/*
 * ico2/fumi/include/ee_view.h
 *
 * A raw EE-offset view of a record that has a named field at that offset
 * (package 2D, docs/port/SWEEP_2D.md).  Most of the decompiled actor code
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
#endif /* EE_VIEW_H */
