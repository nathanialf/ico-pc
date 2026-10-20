/* rhi_pipeline_pool.h: how many pipelines the RHI holds at once, the one
 * number both backends size their pipeline pool with (vk_device.c and
 * d3d12_device.c, rhi_init) and the renderer sizes its pipeline cache from
 * (port/render/rd_state.h RD_PIPELINE_CACHE_MAX).  No other header: rhi.h
 * and rd_state.h include it.
 *
 * The pool holds the renderer's whole cache and the pipelines the renderer
 * makes outside it, so no pipeline the cache can keep is refused by the
 * pool:
 *   RHI_PIPELINE_POOL_CACHE     the cache's keys (rd_pipeline.c s_cache);
 *   RHI_PIPELINE_POOL_HEADROOM  the most alive outside the cache at once:
 *     a batch of the start-up set or of a replay's gathered keys holds an
 *     id for each of its RD_PIPE_BATCH (32) descriptions until it ends,
 *     its duplicates destroyed then (rd_pipeline.c createKeys), the
 *     COLCLAMP 0 pipelines (RD_WRAP_PIPES, 16; rd_replay.c s_wrapPipes)
 *     and the movie's two output pipelines and its field pipeline (3;
 *     rd_video.c s_v.pipe, fieldPipe): 51, rounded up to 64
 *     (rd_pipeline.c checks the sum against it).
 * A handle indexes 2^20 - 1 slots (vk_internal.h VKR_INDEX_BITS,
 * d3d12_plan.h D3DP_INDEX_BITS), far above the pool. */
#ifndef PORT_RHI_RHI_PIPELINE_POOL_H
#define PORT_RHI_RHI_PIPELINE_POOL_H

#define RHI_PIPELINE_POOL_CACHE 4096
#define RHI_PIPELINE_POOL_HEADROOM 64
#define RHI_PIPELINE_POOL_MAX (RHI_PIPELINE_POOL_CACHE + RHI_PIPELINE_POOL_HEADROOM)

#endif /* PORT_RHI_RHI_PIPELINE_POOL_H */
