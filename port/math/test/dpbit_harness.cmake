# port/math/test/dpbit_harness.cmake: the softdouble oracle's copy of the
# EE's soft float (sce/libgcc/dp-bit.c, fp-bit.c, libgcc2.h), made to build
# on the host. Run as `cmake -DSRC=<sce/libgcc> -DDST=<dir> -P` at build
# time (port/math/CMakeLists.txt).
#
# The reconstructions match the EE objects byte for byte, so some of their
# functions are declared void (or int) and leave a 64-bit result in v0 or a
# float in $f0 from their last call, as the period compiler's code did; and
# the EE's `long` is 64 bits. Nothing else is changed: these edits give
# those functions a host return type and a `return`, and spell the EE's
# 64-bit `long` arguments `long long`. Each edit must apply exactly once.

function(dpbit_edit var from to)
    string(FIND "${${var}}" "${from}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "dpbit_harness: `${from}` not found")
    endif()
    string(LENGTH "${from}" len)
    math(EXPR next "${at} + ${len}")
    string(SUBSTRING "${${var}}" ${next} -1 rest)
    string(FIND "${rest}" "${from}" again)
    if(NOT again EQUAL -1)
        message(FATAL_ERROR "dpbit_harness: `${from}` found more than once")
    endif()
    string(REPLACE "${from}" "${to}" out "${${var}}")
    set(${var} "${out}" PARENT_SCOPE)
endfunction()

file(READ "${SRC}/dp-bit.c" dp)
file(READ "${SRC}/fp-bit.c" fp)
file(READ "${SRC}/libgcc2.h" hdr)

# the EE's `long` (a 64-bit register) as a host type
string(REGEX REPLACE "\\(long arg_a" "(long long arg_a" dp "${dp}")
string(REGEX REPLACE ", long arg_b\\)" ", long long arg_b)" dp "${dp}")

dpbit_edit(dp "void dpadd(" "long long dpadd(")
dpbit_edit(dp "\n    __pack_d(_fpadd_parts(&x, &y, &z));" "\n    return __pack_d(_fpadd_parts(&x, &y, &z));")
dpbit_edit(dp "void dpdiv(" "long long dpdiv(")
dpbit_edit(dp "pack:\n    __pack_d(r);" "pack:\n    return __pack_d(r);")
dpbit_edit(dp "void __negdf2(" "long long __negdf2(")
dpbit_edit(dp "s.sign = (s.sign == 0);\n    __pack_d(&s);" "s.sign = (s.sign == 0);\n    return __pack_d(&s);")
dpbit_edit(dp "int __make_dp(" "long long __make_dp(")
dpbit_edit(dp "s.fraction.ll = frac;\n    __pack_d(&s);" "s.fraction.ll = frac;\n    return __pack_d(&s);")
dpbit_edit(dp "float dptofp(double arg_a)" "float dptofp(long long arg_a)")
dpbit_edit(dp "    __make_fp(buf.class, buf.sign, buf.normal_exp, t);" "    return __make_fp(buf.class, buf.sign, buf.normal_exp, t);")

dpbit_edit(fp "void __make_fp(" "float __make_fp(")
dpbit_edit(fp "buf.fraction = frac;\n    __pack_f(&buf);" "buf.fraction = frac;\n    return __pack_f(&buf);")
dpbit_edit(fp "int fptodp(float arg_a)" "long long fptodp(float arg_a)")

dpbit_edit(hdr "int __make_dp(int class" "long long __make_dp(int class")
dpbit_edit(hdr "void __make_fp(int class" "float __make_fp(int class")

file(MAKE_DIRECTORY "${DST}")
file(WRITE "${DST}/dp-bit.c" "${dp}")
file(WRITE "${DST}/fp-bit.c" "${fp}")
file(WRITE "${DST}/libgcc2.h" "${hdr}")
