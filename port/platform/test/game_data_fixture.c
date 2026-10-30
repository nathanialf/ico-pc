/*
 * port/platform/test/game_data_fixture.c: a stand-in for the game library
 * in game_data_range_test.c, built into a STATIC library and given the
 * game's section treatment (cmake/IcoStateSections.cmake) with the game's
 * semantics options (-fno-common: every uninitialised global is .bss).  One
 * object of each kind the game has: initialised data, data holding
 * addresses (.data.rel.local on a position-independent ELF build), zero
 * and uninitialised data, file and function statics, and two read-only
 * objects (a constant table of addresses, a constant scalar) that must stay
 * outside the game's ranges.
 */
int fixture_d = 5;
int fixture_d2[64] = {1, 2, 3};
int fixture_b[1000];
int fixture_z[16] = {0};
static int fixture_sb[37];
static int fixture_sd = 11;
int *fixture_ptrs[3] = {&fixture_d, fixture_b, &fixture_sd};
int *const fixture_cptrs[2] = {&fixture_d, fixture_b};
const int fixture_k = 3;

int *fixture_static_bss(void)
{
    return fixture_sb;
}

int *fixture_static_data(void)
{
    return &fixture_sd;
}

int *fixture_local_bss(void)
{
    static int local_b[9];
    return local_b;
}

int *fixture_local_data(void)
{
    static int local_d = 4;
    return &local_d;
}
