/*
 * port/platform/window_keep_block.c
 *
 * The loss keep's blocks (window_keep_block.h).
 */
#include "window_keep_block.h"
#include "diag_host.h" /* ico_diag_background_memory */
#include "dump_auto.h"
#include "rd_loss_keep.h"

static const RdLossKeepHost s_host = {
    ico_dump_auto_keep_ask,
    ico_dump_auto_keep_take,
    ico_dump_auto_keep_give_back,
    ico_diag_background_memory,
};

void window_keep_block_start(void)
{
    rd_loss_keep_set_host(&s_host);
}
