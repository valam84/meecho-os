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


#ifndef _PANFROST_TRACEPOINTS_H
#define _PANFROST_TRACEPOINTS_H

#include "util/u_dump.h"
#include "pan_job.h"

#include "util/perf/u_trace.h"

#ifdef __cplusplus
extern "C" {
#endif


enum panfrost_gpu_tracepoint {
   PANFROST_GPU_TRACEPOINT_VERTEX_TILER = 1ull << 0,
   PANFROST_GPU_TRACEPOINT_FRAGMENT = 1ull << 1,
   PANFROST_GPU_TRACEPOINT_BATCH = 1ull << 2,
   PANFROST_GPU_TRACEPOINT_BARRIER = 1ull << 3,
   PANFROST_GPU_TRACEPOINT_CACHE_FLUSH = 1ull << 4,
   PANFROST_GPU_TRACEPOINT_COMPUTE = 1ull << 5,
   PANFROST_GPU_TRACEPOINT_COMPUTE_INDIRECT = 1ull << 6,
};

extern uint64_t panfrost_gpu_tracepoint;

void panfrost_gpu_tracepoint_config_variable(void);


/*
 * panfrost_start_vertex_tiler
 */
struct trace_panfrost_start_vertex_tiler {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_start_vertex_tiler(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_start_vertex_tiler *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_start_vertex_tiler(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_start_vertex_tiler(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_VERTEX_TILER)))
      return;
   __trace_panfrost_start_vertex_tiler(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_end_vertex_tiler
 */
struct trace_panfrost_end_vertex_tiler {
   uint32_t draw_count;
};
#ifdef HAVE_PERFETTO
void panfrost_end_vertex_tiler(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_end_vertex_tiler *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_end_vertex_tiler(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint32_t draw_count
);
static ALWAYS_INLINE void trace_panfrost_end_vertex_tiler(
     struct u_trace *ut
   , void *cs
   , uint32_t draw_count
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_VERTEX_TILER)))
      return;
   __trace_panfrost_end_vertex_tiler(
        ut
      , enabled_traces
      , cs
      , draw_count
   );
}

/*
 * panfrost_start_fragment
 */
struct trace_panfrost_start_fragment {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_start_fragment(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_start_fragment *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_start_fragment(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_start_fragment(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_FRAGMENT)))
      return;
   __trace_panfrost_start_fragment(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_end_fragment
 */
struct trace_panfrost_end_fragment {
   uint32_t submission_id;
   enum pipe_format cbuf0_format;
   enum pipe_format zs_format;
   uint16_t width;
   uint16_t height;
   uint8_t mrts;
   uint8_t samples;
};
#ifdef HAVE_PERFETTO
void panfrost_end_fragment(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_end_fragment *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_end_fragment(
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
);
static ALWAYS_INLINE void trace_panfrost_end_fragment(
     struct u_trace *ut
   , void *cs
   , uint32_t submit_id
   , enum pipe_format cbuf0_format
   , enum pipe_format zs_format
   , uint16_t width
   , uint16_t height
   , uint8_t mrts
   , uint8_t samples
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_FRAGMENT)))
      return;
   __trace_panfrost_end_fragment(
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
   );
}

/*
 * panfrost_start_batch
 */
struct trace_panfrost_start_batch {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_start_batch(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_start_batch *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_start_batch(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_start_batch(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_BATCH)))
      return;
   __trace_panfrost_start_batch(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_end_batch
 */
struct trace_panfrost_end_batch {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_end_batch(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_end_batch *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_end_batch(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_end_batch(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_BATCH)))
      return;
   __trace_panfrost_end_batch(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_start_barrier
 */
struct trace_panfrost_start_barrier {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_start_barrier(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_start_barrier *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_start_barrier(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_start_barrier(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_BARRIER)))
      return;
   __trace_panfrost_start_barrier(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_end_barrier
 */
struct trace_panfrost_end_barrier {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_end_barrier(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_end_barrier *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_end_barrier(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_end_barrier(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_BARRIER)))
      return;
   __trace_panfrost_end_barrier(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_start_cache_flush
 */
struct trace_panfrost_start_cache_flush {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_start_cache_flush(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_start_cache_flush *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_start_cache_flush(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_start_cache_flush(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_CACHE_FLUSH)))
      return;
   __trace_panfrost_start_cache_flush(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_end_cache_flush
 */
struct trace_panfrost_end_cache_flush {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_end_cache_flush(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_end_cache_flush *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_end_cache_flush(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_end_cache_flush(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_CACHE_FLUSH)))
      return;
   __trace_panfrost_end_cache_flush(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_start_compute
 */
struct trace_panfrost_start_compute {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_start_compute(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_start_compute *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_start_compute(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_start_compute(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_COMPUTE)))
      return;
   __trace_panfrost_start_compute(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_end_compute
 */
struct trace_panfrost_end_compute {
   uint16_t local_size_x;
   uint16_t local_size_y;
   uint16_t local_size_z;
   uint32_t num_groups_x;
   uint32_t num_groups_y;
   uint32_t num_groups_z;
};
#ifdef HAVE_PERFETTO
void panfrost_end_compute(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_end_compute *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_end_compute(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t local_size_x
   , uint16_t local_size_y
   , uint16_t local_size_z
   , uint32_t num_groups_x
   , uint32_t num_groups_y
   , uint32_t num_groups_z
);
static ALWAYS_INLINE void trace_panfrost_end_compute(
     struct u_trace *ut
   , void *cs
   , uint16_t local_size_x
   , uint16_t local_size_y
   , uint16_t local_size_z
   , uint32_t num_groups_x
   , uint32_t num_groups_y
   , uint32_t num_groups_z
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_COMPUTE)))
      return;
   __trace_panfrost_end_compute(
        ut
      , enabled_traces
      , cs
      , local_size_x
      , local_size_y
      , local_size_z
      , num_groups_x
      , num_groups_y
      , num_groups_z
   );
}

/*
 * panfrost_start_compute_indirect
 */
struct trace_panfrost_start_compute_indirect {
   uint8_t dummy;
};
#ifdef HAVE_PERFETTO
void panfrost_start_compute_indirect(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_start_compute_indirect *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_start_compute_indirect(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
);
static ALWAYS_INLINE void trace_panfrost_start_compute_indirect(
     struct u_trace *ut
   , void *cs
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_COMPUTE_INDIRECT)))
      return;
   __trace_panfrost_start_compute_indirect(
        ut
      , enabled_traces
      , cs
   );
}

/*
 * panfrost_end_compute_indirect
 */
struct trace_panfrost_end_compute_indirect {
   uint16_t local_size_x;
   uint16_t local_size_y;
   uint16_t local_size_z;
};
#ifdef HAVE_PERFETTO
void panfrost_end_compute_indirect(
   struct pipe_context *pctx,
   uint64_t ts_ns,
   uint16_t tp_idx,
   const void *flush_data,
   const struct trace_panfrost_end_compute_indirect *payload,
   const void *indirect_data);
#endif
void __trace_panfrost_end_compute_indirect(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t local_size_x
   , uint16_t local_size_y
   , uint16_t local_size_z
   , struct u_trace_address num_groups_x
   , struct u_trace_address num_groups_y
   , struct u_trace_address num_groups_z
);
static ALWAYS_INLINE void trace_panfrost_end_compute_indirect(
     struct u_trace *ut
   , void *cs
   , uint16_t local_size_x
   , uint16_t local_size_y
   , uint16_t local_size_z
   , struct u_trace_address num_groups_x
   , struct u_trace_address num_groups_y
   , struct u_trace_address num_groups_z
) {
   enum u_trace_type enabled_traces = p_atomic_read_relaxed(&ut->utctx->enabled_traces);
   if (!unlikely(enabled_traces != 0 &&
                 (panfrost_gpu_tracepoint & PANFROST_GPU_TRACEPOINT_COMPUTE_INDIRECT)))
      return;
   __trace_panfrost_end_compute_indirect(
        ut
      , enabled_traces
      , cs
      , local_size_x
      , local_size_y
      , local_size_z
      , num_groups_x
      , num_groups_y
      , num_groups_z
   );
}

#ifdef __cplusplus
}
#endif



#endif /* _PANFROST_TRACEPOINTS_H */
