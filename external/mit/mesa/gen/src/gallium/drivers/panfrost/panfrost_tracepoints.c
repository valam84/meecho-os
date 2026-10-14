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

#include "panfrost_tracepoints.h"


#define __NEEDS_TRACE_PRIV
#include "util/u_debug.h"
#include "util/perf/u_trace_priv.h"

static const struct debug_control config_control[] = {
   { "vertex_tiler", PANFROST_GPU_TRACEPOINT_VERTEX_TILER, },
   { "fragment", PANFROST_GPU_TRACEPOINT_FRAGMENT, },
   { "batch", PANFROST_GPU_TRACEPOINT_BATCH, },
   { "barrier", PANFROST_GPU_TRACEPOINT_BARRIER, },
   { "cache_flush", PANFROST_GPU_TRACEPOINT_CACHE_FLUSH, },
   { "compute", PANFROST_GPU_TRACEPOINT_COMPUTE, },
   { "compute_indirect", PANFROST_GPU_TRACEPOINT_COMPUTE_INDIRECT, },
   { NULL, 0, },
};
uint64_t panfrost_gpu_tracepoint = 0;

static void
panfrost_gpu_tracepoint_variable_once(void)
{
   uint64_t default_value = 0
     | PANFROST_GPU_TRACEPOINT_VERTEX_TILER
     | PANFROST_GPU_TRACEPOINT_FRAGMENT
     | PANFROST_GPU_TRACEPOINT_BATCH
     | PANFROST_GPU_TRACEPOINT_BARRIER
     | PANFROST_GPU_TRACEPOINT_CACHE_FLUSH
     | PANFROST_GPU_TRACEPOINT_COMPUTE
     | PANFROST_GPU_TRACEPOINT_COMPUTE_INDIRECT
     ;

   panfrost_gpu_tracepoint =
      parse_enable_string(os_get_option("PANFROST_GPU_TRACEPOINT"),
                          default_value,
                          config_control);
}

void
panfrost_gpu_tracepoint_config_variable(void)
{
   static once_flag process_panfrost_gpu_tracepoint_variable_flag = ONCE_FLAG_INIT;

   call_once(&process_panfrost_gpu_tracepoint_variable_flag,
             panfrost_gpu_tracepoint_variable_once);
}

/*
 * panfrost_start_vertex_tiler
 */
  
#define __print_panfrost_start_vertex_tiler NULL
  
#define __print_json_panfrost_start_vertex_tiler NULL
#define __print_fuzzy_hash_args_panfrost_start_vertex_tiler NULL

#define __fuzzy_hash_panfrost_start_vertex_tiler NULL
#define __fuzzy_equals_panfrost_start_vertex_tiler NULL
#define __print_fuzzy_hash_args_panfrost_start_vertex_tiler NULL
static const struct u_tracepoint __tp_panfrost_start_vertex_tiler = {
    "panfrost_start_vertex_tiler",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    0,
    u_tracepoint_type_marker,
    __print_panfrost_start_vertex_tiler,
    __print_json_panfrost_start_vertex_tiler,
    __fuzzy_hash_panfrost_start_vertex_tiler,
    __fuzzy_equals_panfrost_start_vertex_tiler,
    __print_fuzzy_hash_args_panfrost_start_vertex_tiler,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_start_vertex_tiler,
#endif
};
void __trace_panfrost_start_vertex_tiler(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_start_vertex_tiler entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_start_vertex_tiler *__entry =
      queueing ?
      (struct trace_panfrost_start_vertex_tiler *)u_trace_appendv(ut, cs, &__tp_panfrost_start_vertex_tiler,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_end_vertex_tiler
 */
  
static void __print_panfrost_end_vertex_tiler(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_vertex_tiler *__entry =
      (const struct trace_panfrost_end_vertex_tiler *)arg;
   fprintf(out, ""
      "draw_count=%u, "
         "\n"
   ,__entry->draw_count
   );
}

  
static void __print_json_panfrost_end_vertex_tiler(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_vertex_tiler *__entry =
      (const struct trace_panfrost_end_vertex_tiler *)arg;
   fprintf(out, ""
      "\"draw_count\": \"%u\""
   ,__entry->draw_count
   );
}

#define __print_fuzzy_hash_args_panfrost_end_vertex_tiler NULL

static uint32_t __fuzzy_hash_panfrost_end_vertex_tiler(const void *_event) {
   uint32_t hash = 1;
   const struct trace_panfrost_end_vertex_tiler *__entry =
      (const struct trace_panfrost_end_vertex_tiler *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_panfrost_end_vertex_tiler(const void *_a, const void *_b) {
   const struct trace_panfrost_end_vertex_tiler *__entry_a =
      (const struct trace_panfrost_end_vertex_tiler *)_a;
   (void)__entry_a;
   const struct trace_panfrost_end_vertex_tiler *__entry_b =
      (const struct trace_panfrost_end_vertex_tiler *)_b;
   (void)__entry_b;
   return true;
}

static const struct u_tracepoint __tp_panfrost_end_vertex_tiler = {
    "panfrost_end_vertex_tiler",
    ALIGN_POT(sizeof(struct trace_panfrost_end_vertex_tiler), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    1,
    u_tracepoint_type_marker,
    __print_panfrost_end_vertex_tiler,
    __print_json_panfrost_end_vertex_tiler,
    __fuzzy_hash_panfrost_end_vertex_tiler,
    __fuzzy_equals_panfrost_end_vertex_tiler,
    __print_fuzzy_hash_args_panfrost_end_vertex_tiler,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_end_vertex_tiler,
#endif
};
void __trace_panfrost_end_vertex_tiler(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint32_t draw_count
) {
   struct trace_panfrost_end_vertex_tiler entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_end_vertex_tiler *__entry =
      queueing ?
      (struct trace_panfrost_end_vertex_tiler *)u_trace_appendv(ut, cs, &__tp_panfrost_end_vertex_tiler,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->draw_count = draw_count;
}

/*
 * panfrost_start_fragment
 */
  
#define __print_panfrost_start_fragment NULL
  
#define __print_json_panfrost_start_fragment NULL
#define __print_fuzzy_hash_args_panfrost_start_fragment NULL

#define __fuzzy_hash_panfrost_start_fragment NULL
#define __fuzzy_equals_panfrost_start_fragment NULL
#define __print_fuzzy_hash_args_panfrost_start_fragment NULL
static const struct u_tracepoint __tp_panfrost_start_fragment = {
    "panfrost_start_fragment",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    2,
    u_tracepoint_type_marker,
    __print_panfrost_start_fragment,
    __print_json_panfrost_start_fragment,
    __fuzzy_hash_panfrost_start_fragment,
    __fuzzy_equals_panfrost_start_fragment,
    __print_fuzzy_hash_args_panfrost_start_fragment,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_start_fragment,
#endif
};
void __trace_panfrost_start_fragment(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_start_fragment entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_start_fragment *__entry =
      queueing ?
      (struct trace_panfrost_start_fragment *)u_trace_appendv(ut, cs, &__tp_panfrost_start_fragment,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_end_fragment
 */
  
static void __print_panfrost_end_fragment(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_fragment *__entry =
      (const struct trace_panfrost_end_fragment *)arg;
   fprintf(out, ""
      "submission_id=%u, "
      "cbuf0_format=%s, "
      "zs_format=%s, "
      "width=%u, "
      "height=%u, "
      "mrts=%u, "
      "samples=%u, "
         "\n"
   ,__entry->submission_id
   ,util_format_description(__entry->cbuf0_format)->short_name
   ,util_format_description(__entry->zs_format)->short_name
   ,__entry->width
   ,__entry->height
   ,__entry->mrts
   ,__entry->samples
   );
}

  
static void __print_json_panfrost_end_fragment(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_fragment *__entry =
      (const struct trace_panfrost_end_fragment *)arg;
   fprintf(out, ""
      "\"submission_id\": \"%u\""
         ", "
      "\"cbuf0_format\": \"%s\""
         ", "
      "\"zs_format\": \"%s\""
         ", "
      "\"width\": \"%u\""
         ", "
      "\"height\": \"%u\""
         ", "
      "\"mrts\": \"%u\""
         ", "
      "\"samples\": \"%u\""
   ,__entry->submission_id
   ,util_format_description(__entry->cbuf0_format)->short_name
   ,util_format_description(__entry->zs_format)->short_name
   ,__entry->width
   ,__entry->height
   ,__entry->mrts
   ,__entry->samples
   );
}

#define __print_fuzzy_hash_args_panfrost_end_fragment NULL

static uint32_t __fuzzy_hash_panfrost_end_fragment(const void *_event) {
   uint32_t hash = 1;
   const struct trace_panfrost_end_fragment *__entry =
      (const struct trace_panfrost_end_fragment *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_panfrost_end_fragment(const void *_a, const void *_b) {
   const struct trace_panfrost_end_fragment *__entry_a =
      (const struct trace_panfrost_end_fragment *)_a;
   (void)__entry_a;
   const struct trace_panfrost_end_fragment *__entry_b =
      (const struct trace_panfrost_end_fragment *)_b;
   (void)__entry_b;
   return true;
}

static const struct u_tracepoint __tp_panfrost_end_fragment = {
    "panfrost_end_fragment",
    ALIGN_POT(sizeof(struct trace_panfrost_end_fragment), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    3,
    u_tracepoint_type_marker,
    __print_panfrost_end_fragment,
    __print_json_panfrost_end_fragment,
    __fuzzy_hash_panfrost_end_fragment,
    __fuzzy_equals_panfrost_end_fragment,
    __print_fuzzy_hash_args_panfrost_end_fragment,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_end_fragment,
#endif
};
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
) {
   struct trace_panfrost_end_fragment entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_end_fragment *__entry =
      queueing ?
      (struct trace_panfrost_end_fragment *)u_trace_appendv(ut, cs, &__tp_panfrost_end_fragment,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->submission_id = submit_id;
   __entry->cbuf0_format = cbuf0_format;
   __entry->zs_format = zs_format;
   __entry->width = width;
   __entry->height = height;
   __entry->mrts = mrts;
   __entry->samples = samples;
}

/*
 * panfrost_start_batch
 */
  
#define __print_panfrost_start_batch NULL
  
#define __print_json_panfrost_start_batch NULL
#define __print_fuzzy_hash_args_panfrost_start_batch NULL

#define __fuzzy_hash_panfrost_start_batch NULL
#define __fuzzy_equals_panfrost_start_batch NULL
#define __print_fuzzy_hash_args_panfrost_start_batch NULL
static const struct u_tracepoint __tp_panfrost_start_batch = {
    "panfrost_start_batch",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    4,
    u_tracepoint_type_marker,
    __print_panfrost_start_batch,
    __print_json_panfrost_start_batch,
    __fuzzy_hash_panfrost_start_batch,
    __fuzzy_equals_panfrost_start_batch,
    __print_fuzzy_hash_args_panfrost_start_batch,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_start_batch,
#endif
};
void __trace_panfrost_start_batch(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_start_batch entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_start_batch *__entry =
      queueing ?
      (struct trace_panfrost_start_batch *)u_trace_appendv(ut, cs, &__tp_panfrost_start_batch,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_end_batch
 */
  
#define __print_panfrost_end_batch NULL
  
#define __print_json_panfrost_end_batch NULL
#define __print_fuzzy_hash_args_panfrost_end_batch NULL

#define __fuzzy_hash_panfrost_end_batch NULL
#define __fuzzy_equals_panfrost_end_batch NULL
#define __print_fuzzy_hash_args_panfrost_end_batch NULL
static const struct u_tracepoint __tp_panfrost_end_batch = {
    "panfrost_end_batch",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    5,
    u_tracepoint_type_marker,
    __print_panfrost_end_batch,
    __print_json_panfrost_end_batch,
    __fuzzy_hash_panfrost_end_batch,
    __fuzzy_equals_panfrost_end_batch,
    __print_fuzzy_hash_args_panfrost_end_batch,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_end_batch,
#endif
};
void __trace_panfrost_end_batch(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_end_batch entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_end_batch *__entry =
      queueing ?
      (struct trace_panfrost_end_batch *)u_trace_appendv(ut, cs, &__tp_panfrost_end_batch,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_start_barrier
 */
  
#define __print_panfrost_start_barrier NULL
  
#define __print_json_panfrost_start_barrier NULL
#define __print_fuzzy_hash_args_panfrost_start_barrier NULL

#define __fuzzy_hash_panfrost_start_barrier NULL
#define __fuzzy_equals_panfrost_start_barrier NULL
#define __print_fuzzy_hash_args_panfrost_start_barrier NULL
static const struct u_tracepoint __tp_panfrost_start_barrier = {
    "panfrost_start_barrier",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    6,
    u_tracepoint_type_marker,
    __print_panfrost_start_barrier,
    __print_json_panfrost_start_barrier,
    __fuzzy_hash_panfrost_start_barrier,
    __fuzzy_equals_panfrost_start_barrier,
    __print_fuzzy_hash_args_panfrost_start_barrier,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_start_barrier,
#endif
};
void __trace_panfrost_start_barrier(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_start_barrier entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_start_barrier *__entry =
      queueing ?
      (struct trace_panfrost_start_barrier *)u_trace_appendv(ut, cs, &__tp_panfrost_start_barrier,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_end_barrier
 */
  
#define __print_panfrost_end_barrier NULL
  
#define __print_json_panfrost_end_barrier NULL
#define __print_fuzzy_hash_args_panfrost_end_barrier NULL

#define __fuzzy_hash_panfrost_end_barrier NULL
#define __fuzzy_equals_panfrost_end_barrier NULL
#define __print_fuzzy_hash_args_panfrost_end_barrier NULL
static const struct u_tracepoint __tp_panfrost_end_barrier = {
    "panfrost_end_barrier",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    7,
    u_tracepoint_type_marker,
    __print_panfrost_end_barrier,
    __print_json_panfrost_end_barrier,
    __fuzzy_hash_panfrost_end_barrier,
    __fuzzy_equals_panfrost_end_barrier,
    __print_fuzzy_hash_args_panfrost_end_barrier,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_end_barrier,
#endif
};
void __trace_panfrost_end_barrier(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_end_barrier entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_end_barrier *__entry =
      queueing ?
      (struct trace_panfrost_end_barrier *)u_trace_appendv(ut, cs, &__tp_panfrost_end_barrier,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_start_cache_flush
 */
  
#define __print_panfrost_start_cache_flush NULL
  
#define __print_json_panfrost_start_cache_flush NULL
#define __print_fuzzy_hash_args_panfrost_start_cache_flush NULL

#define __fuzzy_hash_panfrost_start_cache_flush NULL
#define __fuzzy_equals_panfrost_start_cache_flush NULL
#define __print_fuzzy_hash_args_panfrost_start_cache_flush NULL
static const struct u_tracepoint __tp_panfrost_start_cache_flush = {
    "panfrost_start_cache_flush",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    8,
    u_tracepoint_type_marker,
    __print_panfrost_start_cache_flush,
    __print_json_panfrost_start_cache_flush,
    __fuzzy_hash_panfrost_start_cache_flush,
    __fuzzy_equals_panfrost_start_cache_flush,
    __print_fuzzy_hash_args_panfrost_start_cache_flush,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_start_cache_flush,
#endif
};
void __trace_panfrost_start_cache_flush(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_start_cache_flush entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_start_cache_flush *__entry =
      queueing ?
      (struct trace_panfrost_start_cache_flush *)u_trace_appendv(ut, cs, &__tp_panfrost_start_cache_flush,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_end_cache_flush
 */
  
#define __print_panfrost_end_cache_flush NULL
  
#define __print_json_panfrost_end_cache_flush NULL
#define __print_fuzzy_hash_args_panfrost_end_cache_flush NULL

#define __fuzzy_hash_panfrost_end_cache_flush NULL
#define __fuzzy_equals_panfrost_end_cache_flush NULL
#define __print_fuzzy_hash_args_panfrost_end_cache_flush NULL
static const struct u_tracepoint __tp_panfrost_end_cache_flush = {
    "panfrost_end_cache_flush",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    9,
    u_tracepoint_type_marker,
    __print_panfrost_end_cache_flush,
    __print_json_panfrost_end_cache_flush,
    __fuzzy_hash_panfrost_end_cache_flush,
    __fuzzy_equals_panfrost_end_cache_flush,
    __print_fuzzy_hash_args_panfrost_end_cache_flush,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_end_cache_flush,
#endif
};
void __trace_panfrost_end_cache_flush(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_end_cache_flush entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_end_cache_flush *__entry =
      queueing ?
      (struct trace_panfrost_end_cache_flush *)u_trace_appendv(ut, cs, &__tp_panfrost_end_cache_flush,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_start_compute
 */
  
#define __print_panfrost_start_compute NULL
  
#define __print_json_panfrost_start_compute NULL
#define __print_fuzzy_hash_args_panfrost_start_compute NULL

#define __fuzzy_hash_panfrost_start_compute NULL
#define __fuzzy_equals_panfrost_start_compute NULL
#define __print_fuzzy_hash_args_panfrost_start_compute NULL
static const struct u_tracepoint __tp_panfrost_start_compute = {
    "panfrost_start_compute",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    10,
    u_tracepoint_type_marker,
    __print_panfrost_start_compute,
    __print_json_panfrost_start_compute,
    __fuzzy_hash_panfrost_start_compute,
    __fuzzy_equals_panfrost_start_compute,
    __print_fuzzy_hash_args_panfrost_start_compute,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_start_compute,
#endif
};
void __trace_panfrost_start_compute(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_start_compute entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_start_compute *__entry =
      queueing ?
      (struct trace_panfrost_start_compute *)u_trace_appendv(ut, cs, &__tp_panfrost_start_compute,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_end_compute
 */
  
static void __print_panfrost_end_compute(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_compute *__entry =
      (const struct trace_panfrost_end_compute *)arg;
   fprintf(out, ""
      "local_size_x=%u, "
      "local_size_y=%u, "
      "local_size_z=%u, "
      "num_groups_x=%u, "
      "num_groups_y=%u, "
      "num_groups_z=%u, "
         "\n"
   ,__entry->local_size_x
   ,__entry->local_size_y
   ,__entry->local_size_z
   ,__entry->num_groups_x
   ,__entry->num_groups_y
   ,__entry->num_groups_z
   );
}

  
static void __print_json_panfrost_end_compute(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_compute *__entry =
      (const struct trace_panfrost_end_compute *)arg;
   fprintf(out, ""
      "\"local_size_x\": \"%u\""
         ", "
      "\"local_size_y\": \"%u\""
         ", "
      "\"local_size_z\": \"%u\""
         ", "
      "\"num_groups_x\": \"%u\""
         ", "
      "\"num_groups_y\": \"%u\""
         ", "
      "\"num_groups_z\": \"%u\""
   ,__entry->local_size_x
   ,__entry->local_size_y
   ,__entry->local_size_z
   ,__entry->num_groups_x
   ,__entry->num_groups_y
   ,__entry->num_groups_z
   );
}

#define __print_fuzzy_hash_args_panfrost_end_compute NULL

static uint32_t __fuzzy_hash_panfrost_end_compute(const void *_event) {
   uint32_t hash = 1;
   const struct trace_panfrost_end_compute *__entry =
      (const struct trace_panfrost_end_compute *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_panfrost_end_compute(const void *_a, const void *_b) {
   const struct trace_panfrost_end_compute *__entry_a =
      (const struct trace_panfrost_end_compute *)_a;
   (void)__entry_a;
   const struct trace_panfrost_end_compute *__entry_b =
      (const struct trace_panfrost_end_compute *)_b;
   (void)__entry_b;
   return true;
}

static const struct u_tracepoint __tp_panfrost_end_compute = {
    "panfrost_end_compute",
    ALIGN_POT(sizeof(struct trace_panfrost_end_compute), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    11,
    u_tracepoint_type_marker,
    __print_panfrost_end_compute,
    __print_json_panfrost_end_compute,
    __fuzzy_hash_panfrost_end_compute,
    __fuzzy_equals_panfrost_end_compute,
    __print_fuzzy_hash_args_panfrost_end_compute,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_end_compute,
#endif
};
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
) {
   struct trace_panfrost_end_compute entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_end_compute *__entry =
      queueing ?
      (struct trace_panfrost_end_compute *)u_trace_appendv(ut, cs, &__tp_panfrost_end_compute,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->local_size_x = local_size_x;
   __entry->local_size_y = local_size_y;
   __entry->local_size_z = local_size_z;
   __entry->num_groups_x = num_groups_x;
   __entry->num_groups_y = num_groups_y;
   __entry->num_groups_z = num_groups_z;
}

/*
 * panfrost_start_compute_indirect
 */
  
#define __print_panfrost_start_compute_indirect NULL
  
#define __print_json_panfrost_start_compute_indirect NULL
#define __print_fuzzy_hash_args_panfrost_start_compute_indirect NULL

#define __fuzzy_hash_panfrost_start_compute_indirect NULL
#define __fuzzy_equals_panfrost_start_compute_indirect NULL
#define __print_fuzzy_hash_args_panfrost_start_compute_indirect NULL
static const struct u_tracepoint __tp_panfrost_start_compute_indirect = {
    "panfrost_start_compute_indirect",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    12,
    u_tracepoint_type_marker,
    __print_panfrost_start_compute_indirect,
    __print_json_panfrost_start_compute_indirect,
    __fuzzy_hash_panfrost_start_compute_indirect,
    __fuzzy_equals_panfrost_start_compute_indirect,
    __print_fuzzy_hash_args_panfrost_start_compute_indirect,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_start_compute_indirect,
#endif
};
void __trace_panfrost_start_compute_indirect(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_panfrost_start_compute_indirect entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_start_compute_indirect *__entry =
      queueing ?
      (struct trace_panfrost_start_compute_indirect *)u_trace_appendv(ut, cs, &__tp_panfrost_start_compute_indirect,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
}

/*
 * panfrost_end_compute_indirect
 */
  
static void __print_panfrost_end_compute_indirect(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_compute_indirect *__entry =
      (const struct trace_panfrost_end_compute_indirect *)arg;
   const uint32_t *__num_groups_x = (const uint32_t *) ((char *)indirect + 0);
   const uint32_t *__num_groups_y = (const uint32_t *) ((char *)indirect + sizeof(uint32_t));
   const uint32_t *__num_groups_z = (const uint32_t *) ((char *)indirect + sizeof(uint32_t) + sizeof(uint32_t));
   fprintf(out, ""
      "local_size_x=%u, "
      "local_size_y=%u, "
      "local_size_z=%u, "
      "num_groups_x=%u, "
      "num_groups_y=%u, "
      "num_groups_z=%u, "
         "\n"
   ,__entry->local_size_x
   ,__entry->local_size_y
   ,__entry->local_size_z
   ,*__num_groups_x
   ,*__num_groups_y
   ,*__num_groups_z
   );
}

  
static void __print_json_panfrost_end_compute_indirect(FILE *out, const void *arg, const void *indirect) {
   const struct trace_panfrost_end_compute_indirect *__entry =
      (const struct trace_panfrost_end_compute_indirect *)arg;
   const uint32_t *__num_groups_x = (const uint32_t *) ((char *)indirect + 0);
   const uint32_t *__num_groups_y = (const uint32_t *) ((char *)indirect + sizeof(uint32_t));
   const uint32_t *__num_groups_z = (const uint32_t *) ((char *)indirect + sizeof(uint32_t) + sizeof(uint32_t));
   fprintf(out, ""
      "\"local_size_x\": \"%u\""
         ", "
      "\"local_size_y\": \"%u\""
         ", "
      "\"local_size_z\": \"%u\""
         ", "
      "\"num_groups_x\": \"%u\""
         ", "
      "\"num_groups_y\": \"%u\""
         ", "
      "\"num_groups_z\": \"%u\""
   ,__entry->local_size_x
   ,__entry->local_size_y
   ,__entry->local_size_z
   ,*__num_groups_x
   ,*__num_groups_y
   ,*__num_groups_z
   );
}

#define __print_fuzzy_hash_args_panfrost_end_compute_indirect NULL

static uint32_t __fuzzy_hash_panfrost_end_compute_indirect(const void *_event) {
   uint32_t hash = 1;
   const struct trace_panfrost_end_compute_indirect *__entry =
      (const struct trace_panfrost_end_compute_indirect *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_panfrost_end_compute_indirect(const void *_a, const void *_b) {
   const struct trace_panfrost_end_compute_indirect *__entry_a =
      (const struct trace_panfrost_end_compute_indirect *)_a;
   (void)__entry_a;
   const struct trace_panfrost_end_compute_indirect *__entry_b =
      (const struct trace_panfrost_end_compute_indirect *)_b;
   (void)__entry_b;
   return true;
}

static const struct u_tracepoint __tp_panfrost_end_compute_indirect = {
    "panfrost_end_compute_indirect",
    ALIGN_POT(sizeof(struct trace_panfrost_end_compute_indirect), 8),   /* keep size 64b aligned */
    0
    + sizeof(uint32_t)
    + sizeof(uint32_t)
    + sizeof(uint32_t)
    ,
    0,
    13,
    u_tracepoint_type_marker,
    __print_panfrost_end_compute_indirect,
    __print_json_panfrost_end_compute_indirect,
    __fuzzy_hash_panfrost_end_compute_indirect,
    __fuzzy_equals_panfrost_end_compute_indirect,
    __print_fuzzy_hash_args_panfrost_end_compute_indirect,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))panfrost_end_compute_indirect,
#endif
};
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
) {
   struct trace_panfrost_end_compute_indirect entry;
   struct u_trace_address indirects[] = {
      num_groups_x,
      num_groups_y,
      num_groups_z,
   };
   uint8_t indirect_sizes[] = {
      sizeof(uint32_t),
      sizeof(uint32_t),
      sizeof(uint32_t),
   };
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_panfrost_end_compute_indirect *__entry =
      queueing ?
      (struct trace_panfrost_end_compute_indirect *)u_trace_appendv(ut, cs, &__tp_panfrost_end_compute_indirect,
                                                    0
                                                    ,
                                                    ARRAY_SIZE(indirects), indirects, indirect_sizes
                                                    ) :
      &entry;
   __entry->local_size_x = local_size_x;
   __entry->local_size_y = local_size_y;
   __entry->local_size_z = local_size_z;
}

