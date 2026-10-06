/*
 * port/null/libgcc_null.c
 *
 * What the host build still needs from the PS2 build's libgcc and has no
 * host meaning for.  Until renderer wave 6 (package R6a) this sat in
 * port/null/debug_null.c beside the debug.c stand-ins, which are gone:
 * common/src/debug.c and debug_exception.c are compiled on the host.
 */

/* sce/libgcc/fp-bit.c:622: a float widened to a soft-float double, which
   the EE build passes to %f.  Its callers (boyact.c, girl_act.c) hand the
   result only to debug printfs, which print nothing. */
int fptodp(float v)
{
    (void)v;
    return 0;
}
