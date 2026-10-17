/* Copyright (C) 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#ifndef _FREEDRENO_TRACEPOINTS_H
#define _FREEDRENO_TRACEPOINTS_H

#include "util/u_dump.h"
#include "freedreno_batch.h"

#include "util/perf/u_trace.h"

#ifdef __cplusplus
extern "C" {
#endif


enum fd_gpu_tracepoint {
   FD_GPU_TRACEPOINT_STATE_RESTORE = 1ull << 0,
   FD_GPU_TRACEPOINT_FLUSH_BATCH = 1ull << 1,
   FD_GPU_TRACEPOINT_RENDER_GMEM = 1ull << 2,
   FD_GPU_TRACEPOINT_RENDER_SYSMEM = 1ull << 3,
   FD_GPU_TRACEPOINT_RENDER_PASS = 1ull << 4,
   FD_GPU_TRACEPOINT_NONDRAW = 1ull << 5,
   FD_GPU_TRACEPOINT_BINNING_IB = 1ull << 6,
   FD_GPU_TRACEPOINT_VSC_OVERFLOW_TEST = 1ull << 7,
   FD_GPU_TRACEPOINT_PROLOGUE = 1ull << 8,
   FD_GPU_TRACEPOINT_CLEARS = 1ull << 9,
   FD_GPU_TRACEPOINT_TILE_LOADS = 1ull << 10,
   FD_GPU_TRACEPOINT_TILE_STORES = 1ull << 11,
   FD_GPU_TRACEPOINT_START_TILE = 1ull << 12,
   FD_GPU_TRACEPOINT_DRAW_IB = 1ull << 13,
   FD_GPU_TRACEPOINT_BLIT = 1ull << 14,
   FD_GPU_TRACEPOINT_COMPUTE = 1ull << 15,
};

extern uint64_t fd_gpu_tracepoint;

void fd_gpu_tracepoint_config_variable(void);


/*
 * start_state_restore
 */
struct trace_start_state_restore {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_start_state_restore(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_state_restore *payload,
   const void *indirect_data);
#endif
void __trace_start_state_restore(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_start_state_restore(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_STATE_RESTORE)))
      return;
   __trace_start_state_restore(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * end_state_restore
 */
struct trace_end_state_restore {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_state_restore(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_state_restore *payload,
   const void *indirect_data);
#endif
void __trace_end_state_restore(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_state_restore(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_STATE_RESTORE)))
      return;
   __trace_end_state_restore(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * flush_batch
 */
struct trace_flush_batch {
   struct fd_batch * batch;
   uint16_t cleared;
   uint16_t gmem_reason;
   uint16_t num_draws;
};
void __trace_flush_batch(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , struct fd_batch * batch
   , uint16_t cleared
   , uint16_t gmem_reason
   , uint16_t num_draws
);
static ALWAYS_INLINE void trace_flush_batch(
     struct u_trace *ut
   , void *cs
   , struct fd_batch * batch
   , uint16_t cleared
   , uint16_t gmem_reason
   , uint16_t num_draws
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_FLUSH_BATCH)))
      return;
   __trace_flush_batch(
        ut
      , enabled_traces
      , cs
      , batch
      , cleared
      , gmem_reason
      , num_draws
   );
}

/*
 * render_gmem
 */
struct trace_render_gmem {
   uint16_t nbins_x;
   uint16_t nbins_y;
   uint16_t bin_w;
   uint16_t bin_h;
};
void __trace_render_gmem(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t nbins_x
   , uint16_t nbins_y
   , uint16_t bin_w
   , uint16_t bin_h
);
static ALWAYS_INLINE void trace_render_gmem(
     struct u_trace *ut
   , void *cs
   , uint16_t nbins_x
   , uint16_t nbins_y
   , uint16_t bin_w
   , uint16_t bin_h
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_RENDER_GMEM)))
      return;
   __trace_render_gmem(
        ut
      , enabled_traces
      , cs
      , nbins_x
      , nbins_y
      , bin_w
      , bin_h
   );
}

/*
 * render_sysmem
 */
struct trace_render_sysmem {
   uint8_t dummy;
};
void __trace_render_sysmem(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_render_sysmem(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_RENDER_SYSMEM)))
      return;
   __trace_render_sysmem(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_render_pass
 */
struct trace_start_render_pass {
   uint32_t submit_id;
   enum pipe_format cbuf0_format;
   enum pipe_format zs_format;
   uint16_t width;
   uint16_t height;
   uint8_t mrts;
   uint8_t samples;
   uint16_t nbins;
   uint16_t binw;
   uint16_t binh;
};
#ifdef HAVE_PERFETTO
void fd_start_render_pass(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_render_pass *payload,
   const void *indirect_data);
#endif
void __trace_start_render_pass(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint32_t submit_id
   , enum pipe_format cbuf0_format
   , enum pipe_format zs_format
   , uint16_t width
   , uint16_t height
   , uint8_t mrts
   , uint8_t samples
   , uint16_t nbins
   , uint16_t binw
   , uint16_t binh
);
static ALWAYS_INLINE void trace_start_render_pass(
     struct u_trace *ut
   , void *cs
   , uint32_t submit_id
   , enum pipe_format cbuf0_format
   , enum pipe_format zs_format
   , uint16_t width
   , uint16_t height
   , uint8_t mrts
   , uint8_t samples
   , uint16_t nbins
   , uint16_t binw
   , uint16_t binh
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_RENDER_PASS)))
      return;
   __trace_start_render_pass(
        ut
      , enabled_traces
      , cs
      , submit_id
      , cbuf0_format
      , zs_format
      , width
      , height
      , mrts
      , samples
      , nbins
      , binw
      , binh
   );
}

/*
 * end_render_pass
 */
struct trace_end_render_pass {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_render_pass(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_render_pass *payload,
   const void *indirect_data);
#endif
void __trace_end_render_pass(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_render_pass(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_RENDER_PASS)))
      return;
   __trace_end_render_pass(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_nondraw
 */
struct trace_start_nondraw {
   uint32_t submit_id;
};
#ifdef HAVE_PERFETTO
void fd_start_nondraw(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_nondraw *payload,
   const void *indirect_data);
#endif
void __trace_start_nondraw(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint32_t submit_id
);
static ALWAYS_INLINE void trace_start_nondraw(
     struct u_trace *ut
   , void *cs
   , uint32_t submit_id
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_NONDRAW)))
      return;
   __trace_start_nondraw(
        ut
      , enabled_traces
      , cs
      , submit_id
   );
}

/*
 * end_nondraw
 */
struct trace_end_nondraw {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_nondraw(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_nondraw *payload,
   const void *indirect_data);
#endif
void __trace_end_nondraw(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_nondraw(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_NONDRAW)))
      return;
   __trace_end_nondraw(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_binning_ib
 */
struct trace_start_binning_ib {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_start_binning_ib(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_binning_ib *payload,
   const void *indirect_data);
#endif
void __trace_start_binning_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_start_binning_ib(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_BINNING_IB)))
      return;
   __trace_start_binning_ib(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * end_binning_ib
 */
struct trace_end_binning_ib {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_binning_ib(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_binning_ib *payload,
   const void *indirect_data);
#endif
void __trace_end_binning_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_binning_ib(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_BINNING_IB)))
      return;
   __trace_end_binning_ib(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_vsc_overflow_test
 */
struct trace_start_vsc_overflow_test {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_start_vsc_overflow_test(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_vsc_overflow_test *payload,
   const void *indirect_data);
#endif
void __trace_start_vsc_overflow_test(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_start_vsc_overflow_test(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_VSC_OVERFLOW_TEST)))
      return;
   __trace_start_vsc_overflow_test(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * end_vsc_overflow_test
 */
struct trace_end_vsc_overflow_test {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_vsc_overflow_test(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_vsc_overflow_test *payload,
   const void *indirect_data);
#endif
void __trace_end_vsc_overflow_test(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_vsc_overflow_test(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_VSC_OVERFLOW_TEST)))
      return;
   __trace_end_vsc_overflow_test(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_prologue
 */
struct trace_start_prologue {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_start_prologue(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_prologue *payload,
   const void *indirect_data);
#endif
void __trace_start_prologue(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_start_prologue(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_PROLOGUE)))
      return;
   __trace_start_prologue(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * end_prologue
 */
struct trace_end_prologue {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_prologue(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_prologue *payload,
   const void *indirect_data);
#endif
void __trace_end_prologue(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_prologue(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_PROLOGUE)))
      return;
   __trace_end_prologue(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_clears
 */
struct trace_start_clears {
   uint16_t fast_cleared;
};
#ifdef HAVE_PERFETTO
void fd_start_clears(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_clears *payload,
   const void *indirect_data);
#endif
void __trace_start_clears(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t fast_cleared
);
static ALWAYS_INLINE void trace_start_clears(
     struct u_trace *ut
   , void *cs
   , uint16_t fast_cleared
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_CLEARS)))
      return;
   __trace_start_clears(
        ut
      , enabled_traces
      , cs
      , fast_cleared
   );
}

/*
 * end_clears
 */
struct trace_end_clears {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_clears(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_clears *payload,
   const void *indirect_data);
#endif
void __trace_end_clears(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_clears(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_CLEARS)))
      return;
   __trace_end_clears(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_tile_loads
 */
struct trace_start_tile_loads {
   uint16_t load;
};
#ifdef HAVE_PERFETTO
void fd_start_tile_loads(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_tile_loads *payload,
   const void *indirect_data);
#endif
void __trace_start_tile_loads(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t load
);
static ALWAYS_INLINE void trace_start_tile_loads(
     struct u_trace *ut
   , void *cs
   , uint16_t load
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_TILE_LOADS)))
      return;
   __trace_start_tile_loads(
        ut
      , enabled_traces
      , cs
      , load
   );
}

/*
 * end_tile_loads
 */
struct trace_end_tile_loads {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_tile_loads(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_tile_loads *payload,
   const void *indirect_data);
#endif
void __trace_end_tile_loads(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_tile_loads(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_TILE_LOADS)))
      return;
   __trace_end_tile_loads(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_tile_stores
 */
struct trace_start_tile_stores {
   uint16_t store;
};
#ifdef HAVE_PERFETTO
void fd_start_tile_stores(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_tile_stores *payload,
   const void *indirect_data);
#endif
void __trace_start_tile_stores(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t store
);
static ALWAYS_INLINE void trace_start_tile_stores(
     struct u_trace *ut
   , void *cs
   , uint16_t store
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_TILE_STORES)))
      return;
   __trace_start_tile_stores(
        ut
      , enabled_traces
      , cs
      , store
   );
}

/*
 * end_tile_stores
 */
struct trace_end_tile_stores {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_tile_stores(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_tile_stores *payload,
   const void *indirect_data);
#endif
void __trace_end_tile_stores(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_tile_stores(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_TILE_STORES)))
      return;
   __trace_end_tile_stores(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_tile
 */
struct trace_start_tile {
   uint16_t bin_h;
   uint16_t yoff;
   uint16_t bin_w;
   uint16_t xoff;
};
void __trace_start_tile(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t bin_h
   , uint16_t yoff
   , uint16_t bin_w
   , uint16_t xoff
);
static ALWAYS_INLINE void trace_start_tile(
     struct u_trace *ut
   , void *cs
   , uint16_t bin_h
   , uint16_t yoff
   , uint16_t bin_w
   , uint16_t xoff
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_START_TILE)))
      return;
   __trace_start_tile(
        ut
      , enabled_traces
      , cs
      , bin_h
      , yoff
      , bin_w
      , xoff
   );
}

/*
 * start_draw_ib
 */
struct trace_start_draw_ib {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_start_draw_ib(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_draw_ib *payload,
   const void *indirect_data);
#endif
void __trace_start_draw_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_start_draw_ib(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_DRAW_IB)))
      return;
   __trace_start_draw_ib(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * end_draw_ib
 */
struct trace_end_draw_ib {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_draw_ib(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_draw_ib *payload,
   const void *indirect_data);
#endif
void __trace_end_draw_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_draw_ib(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_DRAW_IB)))
      return;
   __trace_end_draw_ib(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_blit
 */
struct trace_start_blit {
   enum pipe_texture_target src_target;
   enum pipe_texture_target dst_target;
};
#ifdef HAVE_PERFETTO
void fd_start_blit(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_blit *payload,
   const void *indirect_data);
#endif
void __trace_start_blit(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , enum pipe_texture_target src_target
   , enum pipe_texture_target dst_target
);
static ALWAYS_INLINE void trace_start_blit(
     struct u_trace *ut
   , void *cs
   , enum pipe_texture_target src_target
   , enum pipe_texture_target dst_target
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_BLIT)))
      return;
   __trace_start_blit(
        ut
      , enabled_traces
      , cs
      , src_target
      , dst_target
   );
}

/*
 * end_blit
 */
struct trace_end_blit {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_blit(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_blit *payload,
   const void *indirect_data);
#endif
void __trace_end_blit(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_blit(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_BLIT)))
      return;
   __trace_end_blit(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * start_compute
 */
struct trace_start_compute {
   uint8_t indirect;
   uint8_t work_dim;
   uint16_t local_size_x;
   uint16_t local_size_y;
   uint16_t local_size_z;
   uint32_t num_groups_x;
   uint32_t num_groups_y;
   uint32_t num_groups_z;
   uint32_t shader_id;
};
#ifdef HAVE_PERFETTO
void fd_start_compute(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_start_compute *payload,
   const void *indirect_data);
#endif
void __trace_start_compute(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint8_t indirect
   , uint8_t work_dim
   , uint16_t local_size_x
   , uint16_t local_size_y
   , uint16_t local_size_z
   , uint32_t num_groups_x
   , uint32_t num_groups_y
   , uint32_t num_groups_z
   , uint32_t shader_id
);
static ALWAYS_INLINE void trace_start_compute(
     struct u_trace *ut
   , void *cs
   , uint8_t indirect
   , uint8_t work_dim
   , uint16_t local_size_x
   , uint16_t local_size_y
   , uint16_t local_size_z
   , uint32_t num_groups_x
   , uint32_t num_groups_y
   , uint32_t num_groups_z
   , uint32_t shader_id
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_COMPUTE)))
      return;
   __trace_start_compute(
        ut
      , enabled_traces
      , cs
      , indirect
      , work_dim
      , local_size_x
      , local_size_y
      , local_size_z
      , num_groups_x
      , num_groups_y
      , num_groups_z
      , shader_id
   );
}

/*
 * end_compute
 */
struct trace_end_compute {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void fd_end_compute(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_end_compute *payload,
   const void *indirect_data);
#endif
void __trace_end_compute(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_end_compute(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (fd_gpu_tracepoint & FD_GPU_TRACEPOINT_COMPUTE)))
      return;
   __trace_end_compute(
        ut
      , enabled_traces
      , cs
   );
}

#ifdef __cplusplus
}
#endif



#endif /* _FREEDRENO_TRACEPOINTS_H */
