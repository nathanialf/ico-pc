/* libgcc.a member __main.o */
#include "libgcc2.h"

void __do_global_dtors(void)
{
    static func_ptr *p = __DTOR_LIST__ + 1;
    while (*p) {
        p++;
        (*(p - 1))();
    }
}

/* libgcc2.c's __do_global_ctors is one line, the DO_GLOBAL_CTORS_BODY macro
   gbl-ctors.h defines, and that macro is written in the ordinary multi-statement
   `do { ... } while (0)` form.  The wrapper is transcribed here because it is
   the member's public source. */
void __do_global_ctors(void)
{
    do {
        unsigned long nptrs = (unsigned long)__CTOR_LIST__[0];
        unsigned i;

        if (nptrs == (unsigned long)-1)
            for (nptrs = 0; __CTOR_LIST__[nptrs + 1] != 0; nptrs++)
                ;
        for (i = nptrs; i >= 1; i--)
            __CTOR_LIST__[i]();
    } while (0);
}

/* libgcc2.c's SYMBOL__MAIN: the flag is the member's whole .bss, VMA 0x736168 (USA 0x71EB68, Aug-6 prototype 0x7181E8, Jun-26 demo 0x76CBE8), 4 B. */
void __main(void)
{
    /* Support recursive calls to `main': run initializers just once.  */
    static int initialized;
    if (!initialized) {
        initialized = 1;
        __do_global_ctors();
    }
}
