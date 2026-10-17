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

#include "freedreno_tracepoints.h"


#define __NEEDS_TRACE_PRIV
#include "util/u_debug.h"
#include "util/perf/u_trace_priv.h"

static const struct debug_control config_control[] = {
   { "state_restore", FD_GPU_TRACEPOINT_STATE_RESTORE, },
   { "flush_batch", FD_GPU_TRACEPOINT_FLUSH_BATCH, },
   { "render_gmem", FD_GPU_TRACEPOINT_RENDER_GMEM, },
   { "render_sysmem", FD_GPU_TRACEPOINT_RENDER_SYSMEM, },
   { "render_pass", FD_GPU_TRACEPOINT_RENDER_PASS, },
   { "nondraw", FD_GPU_TRACEPOINT_NONDRAW, },
   { "binning_ib", FD_GPU_TRACEPOINT_BINNING_IB, },
   { "vsc_overflow_test", FD_GPU_TRACEPOINT_VSC_OVERFLOW_TEST, },
   { "prologue", FD_GPU_TRACEPOINT_PROLOGUE, },
   { "clears", FD_GPU_TRACEPOINT_CLEARS, },
   { "tile_loads", FD_GPU_TRACEPOINT_TILE_LOADS, },
   { "tile_stores", FD_GPU_TRACEPOINT_TILE_STORES, },
   { "start_tile", FD_GPU_TRACEPOINT_START_TILE, },
   { "draw_ib", FD_GPU_TRACEPOINT_DRAW_IB, },
   { "blit", FD_GPU_TRACEPOINT_BLIT, },
   { "compute", FD_GPU_TRACEPOINT_COMPUTE, },
   { NULL, 0, },
};
uint64_t fd_gpu_tracepoint = 0;

static void
fd_gpu_tracepoint_variable_once(void)
{
   uint64_t default_value = 0
     | FD_GPU_TRACEPOINT_STATE_RESTORE
     | FD_GPU_TRACEPOINT_FLUSH_BATCH
     | FD_GPU_TRACEPOINT_RENDER_GMEM
     | FD_GPU_TRACEPOINT_RENDER_SYSMEM
     | FD_GPU_TRACEPOINT_RENDER_PASS
     | FD_GPU_TRACEPOINT_NONDRAW
     | FD_GPU_TRACEPOINT_BINNING_IB
     | FD_GPU_TRACEPOINT_VSC_OVERFLOW_TEST
     | FD_GPU_TRACEPOINT_PROLOGUE
     | FD_GPU_TRACEPOINT_CLEARS
     | FD_GPU_TRACEPOINT_TILE_LOADS
     | FD_GPU_TRACEPOINT_TILE_STORES
     | FD_GPU_TRACEPOINT_START_TILE
     | FD_GPU_TRACEPOINT_DRAW_IB
     | FD_GPU_TRACEPOINT_BLIT
     | FD_GPU_TRACEPOINT_COMPUTE
     ;

   fd_gpu_tracepoint =
      parse_enable_string(os_get_option("FD_GPU_TRACEPOINT"),
                          default_value,
                          config_control);
}

void
fd_gpu_tracepoint_config_variable(void)
{
   static once_flag process_fd_gpu_tracepoint_variable_flag = ONCE_FLAG_INIT;

   call_once(&process_fd_gpu_tracepoint_variable_flag,
             fd_gpu_tracepoint_variable_once);
}

/*
 * start_state_restore
 */
  
#define __print_start_state_restore NULL
  
#define __print_json_start_state_restore NULL
#define __print_fuzzy_hash_args_start_state_restore NULL

#define __fuzzy_hash_start_state_restore NULL
#define __fuzzy_equals_start_state_restore NULL
#define __print_fuzzy_hash_args_start_state_restore NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_state_restore(struct u_trace_context *utctx, void *cs, struct trace_start_state_restore *entry) {
   fd_cs_trace_start(utctx, cs, "start_state_restore("
      ")"
   );
}

static const struct u_tracepoint __tp_start_state_restore = {
    "start_state_restore",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    0,
    u_tracepoint_type_marker,
    __print_start_state_restore,
    __print_json_start_state_restore,
    __fuzzy_hash_start_state_restore,
    __fuzzy_equals_start_state_restore,
    __print_fuzzy_hash_args_start_state_restore,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_state_restore,
#endif
};
void __trace_start_state_restore(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_start_state_restore entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_state_restore *__entry =
      queueing ?
      (struct trace_start_state_restore *)u_trace_appendv(ut, cs, &__tp_start_state_restore,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_state_restore(ut->utctx, cs, __entry);
}

/*
 * end_state_restore
 */
  
#define __print_end_state_restore NULL
  
#define __print_json_end_state_restore NULL
#define __print_fuzzy_hash_args_end_state_restore NULL

#define __fuzzy_hash_end_state_restore NULL
#define __fuzzy_equals_end_state_restore NULL
#define __print_fuzzy_hash_args_end_state_restore NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_state_restore(struct u_trace_context *utctx, void *cs, struct trace_end_state_restore *entry) {
   fd_cs_trace_end(utctx, cs, "end_state_restore("
      ")"
   );
}

static const struct u_tracepoint __tp_end_state_restore = {
    "end_state_restore",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    1,
    u_tracepoint_type_marker,
    __print_end_state_restore,
    __print_json_end_state_restore,
    __fuzzy_hash_end_state_restore,
    __fuzzy_equals_end_state_restore,
    __print_fuzzy_hash_args_end_state_restore,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_state_restore,
#endif
};
void __trace_end_state_restore(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_state_restore entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_state_restore *__entry =
      queueing ?
      (struct trace_end_state_restore *)u_trace_appendv(ut, cs, &__tp_end_state_restore,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_state_restore(ut->utctx, cs, __entry);
}

/*
 * flush_batch
 */
  
static void __print_flush_batch(FILE *out, const void *arg, const void *indirect) {
   const struct trace_flush_batch *__entry =
      (const struct trace_flush_batch *)arg;
   fprintf(out, "%p: cleared=%x, gmem_reason=%x, num_draws=%u\n"
           , __entry->batch
           , __entry->cleared
           , __entry->gmem_reason
           , __entry->num_draws
   );
}

  
static void __print_json_flush_batch(FILE *out, const void *arg, const void *indirect) {
   const struct trace_flush_batch *__entry =
      (const struct trace_flush_batch *)arg;
   fprintf(out, "\"unstructured\": \"%p: cleared=%x, gmem_reason=%x, num_draws=%u\""
           , __entry->batch
           , __entry->cleared
           , __entry->gmem_reason
           , __entry->num_draws
   );
}

#define __print_fuzzy_hash_args_flush_batch NULL

static uint32_t __fuzzy_hash_flush_batch(const void *_event) {
   uint32_t hash = 1;
   const struct trace_flush_batch *__entry =
      (const struct trace_flush_batch *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_flush_batch(const void *_a, const void *_b) {
   const struct trace_flush_batch *__entry_a =
      (const struct trace_flush_batch *)_a;
   (void)__entry_a;
   const struct trace_flush_batch *__entry_b =
      (const struct trace_flush_batch *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_msg(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_flush_batch(struct u_trace_context *utctx, void *cs, struct trace_flush_batch *entry) {
   fd_cs_trace_msg(utctx, cs, "flush_batch("
      "batch=%p"
      ",cleared=%x"
      ",gmem_reason=%x"
      ",num_draws=%u"
      ")"
      ,entry->batch
      ,entry->cleared
      ,entry->gmem_reason
      ,entry->num_draws
   );
}

static const struct u_tracepoint __tp_flush_batch = {
    "flush_batch",
    ALIGN_POT(sizeof(struct trace_flush_batch), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    2,
    u_tracepoint_type_marker,
    __print_flush_batch,
    __print_json_flush_batch,
    __fuzzy_hash_flush_batch,
    __fuzzy_equals_flush_batch,
    __print_fuzzy_hash_args_flush_batch,
};
void __trace_flush_batch(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , struct fd_batch * batch
   , uint16_t cleared
   , uint16_t gmem_reason
   , uint16_t num_draws
) {
   struct trace_flush_batch entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_flush_batch *__entry =
      queueing ?
      (struct trace_flush_batch *)u_trace_appendv(ut, cs, &__tp_flush_batch,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->batch = batch;
   __entry->cleared = cleared;
   __entry->gmem_reason = gmem_reason;
   __entry->num_draws = num_draws;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_flush_batch(ut->utctx, cs, __entry);
}

/*
 * render_gmem
 */
  
static void __print_render_gmem(FILE *out, const void *arg, const void *indirect) {
   const struct trace_render_gmem *__entry =
      (const struct trace_render_gmem *)arg;
   fprintf(out, "%ux%u bins of %ux%u\n"
           , __entry->nbins_x
           , __entry->nbins_y
           , __entry->bin_w
           , __entry->bin_h
   );
}

  
static void __print_json_render_gmem(FILE *out, const void *arg, const void *indirect) {
   const struct trace_render_gmem *__entry =
      (const struct trace_render_gmem *)arg;
   fprintf(out, "\"unstructured\": \"%ux%u bins of %ux%u\""
           , __entry->nbins_x
           , __entry->nbins_y
           , __entry->bin_w
           , __entry->bin_h
   );
}

#define __print_fuzzy_hash_args_render_gmem NULL

static uint32_t __fuzzy_hash_render_gmem(const void *_event) {
   uint32_t hash = 1;
   const struct trace_render_gmem *__entry =
      (const struct trace_render_gmem *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_render_gmem(const void *_a, const void *_b) {
   const struct trace_render_gmem *__entry_a =
      (const struct trace_render_gmem *)_a;
   (void)__entry_a;
   const struct trace_render_gmem *__entry_b =
      (const struct trace_render_gmem *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_msg(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_render_gmem(struct u_trace_context *utctx, void *cs, struct trace_render_gmem *entry) {
   fd_cs_trace_msg(utctx, cs, "render_gmem("
      "nbins_x=%u"
      ",nbins_y=%u"
      ",bin_w=%u"
      ",bin_h=%u"
      ")"
      ,entry->nbins_x
      ,entry->nbins_y
      ,entry->bin_w
      ,entry->bin_h
   );
}

static const struct u_tracepoint __tp_render_gmem = {
    "render_gmem",
    ALIGN_POT(sizeof(struct trace_render_gmem), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    3,
    u_tracepoint_type_marker,
    __print_render_gmem,
    __print_json_render_gmem,
    __fuzzy_hash_render_gmem,
    __fuzzy_equals_render_gmem,
    __print_fuzzy_hash_args_render_gmem,
};
void __trace_render_gmem(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t nbins_x
   , uint16_t nbins_y
   , uint16_t bin_w
   , uint16_t bin_h
) {
   struct trace_render_gmem entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_render_gmem *__entry =
      queueing ?
      (struct trace_render_gmem *)u_trace_appendv(ut, cs, &__tp_render_gmem,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->nbins_x = nbins_x;
   __entry->nbins_y = nbins_y;
   __entry->bin_w = bin_w;
   __entry->bin_h = bin_h;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_render_gmem(ut->utctx, cs, __entry);
}

/*
 * render_sysmem
 */
  
#define __print_render_sysmem NULL
  
#define __print_json_render_sysmem NULL
#define __print_fuzzy_hash_args_render_sysmem NULL

#define __fuzzy_hash_render_sysmem NULL
#define __fuzzy_equals_render_sysmem NULL
#define __print_fuzzy_hash_args_render_sysmem NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_msg(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_render_sysmem(struct u_trace_context *utctx, void *cs, struct trace_render_sysmem *entry) {
   fd_cs_trace_msg(utctx, cs, "render_sysmem("
      ")"
   );
}

static const struct u_tracepoint __tp_render_sysmem = {
    "render_sysmem",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    4,
    u_tracepoint_type_marker,
    __print_render_sysmem,
    __print_json_render_sysmem,
    __fuzzy_hash_render_sysmem,
    __fuzzy_equals_render_sysmem,
    __print_fuzzy_hash_args_render_sysmem,
};
void __trace_render_sysmem(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_render_sysmem entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_render_sysmem *__entry =
      queueing ?
      (struct trace_render_sysmem *)u_trace_appendv(ut, cs, &__tp_render_sysmem,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_render_sysmem(ut->utctx, cs, __entry);
}

/*
 * start_render_pass
 */
  
static void __print_start_render_pass(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_render_pass *__entry =
      (const struct trace_start_render_pass *)arg;
   fprintf(out, ""
      "submit_id=%u, "
      "cbuf0_format=%s, "
      "zs_format=%s, "
      "width=%u, "
      "height=%u, "
      "mrts=%u, "
      "samples=%u, "
      "nbins=%u, "
      "binw=%u, "
      "binh=%u, "
         "\n"
   ,__entry->submit_id
   ,util_format_description(__entry->cbuf0_format)->short_name
   ,util_format_description(__entry->zs_format)->short_name
   ,__entry->width
   ,__entry->height
   ,__entry->mrts
   ,__entry->samples
   ,__entry->nbins
   ,__entry->binw
   ,__entry->binh
   );
}

  
static void __print_json_start_render_pass(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_render_pass *__entry =
      (const struct trace_start_render_pass *)arg;
   fprintf(out, ""
      "\"submit_id\": \"%u\""
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
         ", "
      "\"nbins\": \"%u\""
         ", "
      "\"binw\": \"%u\""
         ", "
      "\"binh\": \"%u\""
   ,__entry->submit_id
   ,util_format_description(__entry->cbuf0_format)->short_name
   ,util_format_description(__entry->zs_format)->short_name
   ,__entry->width
   ,__entry->height
   ,__entry->mrts
   ,__entry->samples
   ,__entry->nbins
   ,__entry->binw
   ,__entry->binh
   );
}

#define __print_fuzzy_hash_args_start_render_pass NULL

static uint32_t __fuzzy_hash_start_render_pass(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_render_pass *__entry =
      (const struct trace_start_render_pass *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_render_pass(const void *_a, const void *_b) {
   const struct trace_start_render_pass *__entry_a =
      (const struct trace_start_render_pass *)_a;
   (void)__entry_a;
   const struct trace_start_render_pass *__entry_b =
      (const struct trace_start_render_pass *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_render_pass(struct u_trace_context *utctx, void *cs, struct trace_start_render_pass *entry) {
   fd_cs_trace_start(utctx, cs, "start_render_pass("
      "submit_id=%u"
      ",cbuf0_format=%s"
      ",zs_format=%s"
      ",width=%u"
      ",height=%u"
      ",mrts=%u"
      ",samples=%u"
      ",nbins=%u"
      ",binw=%u"
      ",binh=%u"
      ")"
      ,entry->submit_id
      ,util_format_description(entry->cbuf0_format)->short_name
      ,util_format_description(entry->zs_format)->short_name
      ,entry->width
      ,entry->height
      ,entry->mrts
      ,entry->samples
      ,entry->nbins
      ,entry->binw
      ,entry->binh
   );
}

static const struct u_tracepoint __tp_start_render_pass = {
    "start_render_pass",
    ALIGN_POT(sizeof(struct trace_start_render_pass), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    5,
    u_tracepoint_type_marker,
    __print_start_render_pass,
    __print_json_start_render_pass,
    __fuzzy_hash_start_render_pass,
    __fuzzy_equals_start_render_pass,
    __print_fuzzy_hash_args_start_render_pass,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_render_pass,
#endif
};
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
) {
   struct trace_start_render_pass entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_render_pass *__entry =
      queueing ?
      (struct trace_start_render_pass *)u_trace_appendv(ut, cs, &__tp_start_render_pass,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->submit_id = submit_id;
   __entry->cbuf0_format = cbuf0_format;
   __entry->zs_format = zs_format;
   __entry->width = width;
   __entry->height = height;
   __entry->mrts = mrts;
   __entry->samples = samples;
   __entry->nbins = nbins;
   __entry->binw = binw;
   __entry->binh = binh;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_render_pass(ut->utctx, cs, __entry);
}

/*
 * end_render_pass
 */
  
#define __print_end_render_pass NULL
  
#define __print_json_end_render_pass NULL
#define __print_fuzzy_hash_args_end_render_pass NULL

#define __fuzzy_hash_end_render_pass NULL
#define __fuzzy_equals_end_render_pass NULL
#define __print_fuzzy_hash_args_end_render_pass NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_render_pass(struct u_trace_context *utctx, void *cs, struct trace_end_render_pass *entry) {
   fd_cs_trace_end(utctx, cs, "end_render_pass("
      ")"
   );
}

static const struct u_tracepoint __tp_end_render_pass = {
    "end_render_pass",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    6,
    u_tracepoint_type_marker,
    __print_end_render_pass,
    __print_json_end_render_pass,
    __fuzzy_hash_end_render_pass,
    __fuzzy_equals_end_render_pass,
    __print_fuzzy_hash_args_end_render_pass,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_render_pass,
#endif
};
void __trace_end_render_pass(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_render_pass entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_render_pass *__entry =
      queueing ?
      (struct trace_end_render_pass *)u_trace_appendv(ut, cs, &__tp_end_render_pass,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_render_pass(ut->utctx, cs, __entry);
}

/*
 * start_nondraw
 */
  
static void __print_start_nondraw(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_nondraw *__entry =
      (const struct trace_start_nondraw *)arg;
   fprintf(out, ""
      "submit_id=%u, "
         "\n"
   ,__entry->submit_id
   );
}

  
static void __print_json_start_nondraw(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_nondraw *__entry =
      (const struct trace_start_nondraw *)arg;
   fprintf(out, ""
      "\"submit_id\": \"%u\""
   ,__entry->submit_id
   );
}

#define __print_fuzzy_hash_args_start_nondraw NULL

static uint32_t __fuzzy_hash_start_nondraw(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_nondraw *__entry =
      (const struct trace_start_nondraw *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_nondraw(const void *_a, const void *_b) {
   const struct trace_start_nondraw *__entry_a =
      (const struct trace_start_nondraw *)_a;
   (void)__entry_a;
   const struct trace_start_nondraw *__entry_b =
      (const struct trace_start_nondraw *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_nondraw(struct u_trace_context *utctx, void *cs, struct trace_start_nondraw *entry) {
   fd_cs_trace_start(utctx, cs, "start_nondraw("
      "submit_id=%u"
      ")"
      ,entry->submit_id
   );
}

static const struct u_tracepoint __tp_start_nondraw = {
    "start_nondraw",
    ALIGN_POT(sizeof(struct trace_start_nondraw), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    7,
    u_tracepoint_type_marker,
    __print_start_nondraw,
    __print_json_start_nondraw,
    __fuzzy_hash_start_nondraw,
    __fuzzy_equals_start_nondraw,
    __print_fuzzy_hash_args_start_nondraw,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_nondraw,
#endif
};
void __trace_start_nondraw(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint32_t submit_id
) {
   struct trace_start_nondraw entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_nondraw *__entry =
      queueing ?
      (struct trace_start_nondraw *)u_trace_appendv(ut, cs, &__tp_start_nondraw,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->submit_id = submit_id;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_nondraw(ut->utctx, cs, __entry);
}

/*
 * end_nondraw
 */
  
#define __print_end_nondraw NULL
  
#define __print_json_end_nondraw NULL
#define __print_fuzzy_hash_args_end_nondraw NULL

#define __fuzzy_hash_end_nondraw NULL
#define __fuzzy_equals_end_nondraw NULL
#define __print_fuzzy_hash_args_end_nondraw NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_nondraw(struct u_trace_context *utctx, void *cs, struct trace_end_nondraw *entry) {
   fd_cs_trace_end(utctx, cs, "end_nondraw("
      ")"
   );
}

static const struct u_tracepoint __tp_end_nondraw = {
    "end_nondraw",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    8,
    u_tracepoint_type_marker,
    __print_end_nondraw,
    __print_json_end_nondraw,
    __fuzzy_hash_end_nondraw,
    __fuzzy_equals_end_nondraw,
    __print_fuzzy_hash_args_end_nondraw,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_nondraw,
#endif
};
void __trace_end_nondraw(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_nondraw entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_nondraw *__entry =
      queueing ?
      (struct trace_end_nondraw *)u_trace_appendv(ut, cs, &__tp_end_nondraw,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_nondraw(ut->utctx, cs, __entry);
}

/*
 * start_binning_ib
 */
  
#define __print_start_binning_ib NULL
  
#define __print_json_start_binning_ib NULL
#define __print_fuzzy_hash_args_start_binning_ib NULL

#define __fuzzy_hash_start_binning_ib NULL
#define __fuzzy_equals_start_binning_ib NULL
#define __print_fuzzy_hash_args_start_binning_ib NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_binning_ib(struct u_trace_context *utctx, void *cs, struct trace_start_binning_ib *entry) {
   fd_cs_trace_start(utctx, cs, "start_binning_ib("
      ")"
   );
}

static const struct u_tracepoint __tp_start_binning_ib = {
    "start_binning_ib",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    9,
    u_tracepoint_type_marker,
    __print_start_binning_ib,
    __print_json_start_binning_ib,
    __fuzzy_hash_start_binning_ib,
    __fuzzy_equals_start_binning_ib,
    __print_fuzzy_hash_args_start_binning_ib,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_binning_ib,
#endif
};
void __trace_start_binning_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_start_binning_ib entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_binning_ib *__entry =
      queueing ?
      (struct trace_start_binning_ib *)u_trace_appendv(ut, cs, &__tp_start_binning_ib,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_binning_ib(ut->utctx, cs, __entry);
}

/*
 * end_binning_ib
 */
  
#define __print_end_binning_ib NULL
  
#define __print_json_end_binning_ib NULL
#define __print_fuzzy_hash_args_end_binning_ib NULL

#define __fuzzy_hash_end_binning_ib NULL
#define __fuzzy_equals_end_binning_ib NULL
#define __print_fuzzy_hash_args_end_binning_ib NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_binning_ib(struct u_trace_context *utctx, void *cs, struct trace_end_binning_ib *entry) {
   fd_cs_trace_end(utctx, cs, "end_binning_ib("
      ")"
   );
}

static const struct u_tracepoint __tp_end_binning_ib = {
    "end_binning_ib",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    10,
    u_tracepoint_type_marker,
    __print_end_binning_ib,
    __print_json_end_binning_ib,
    __fuzzy_hash_end_binning_ib,
    __fuzzy_equals_end_binning_ib,
    __print_fuzzy_hash_args_end_binning_ib,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_binning_ib,
#endif
};
void __trace_end_binning_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_binning_ib entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_binning_ib *__entry =
      queueing ?
      (struct trace_end_binning_ib *)u_trace_appendv(ut, cs, &__tp_end_binning_ib,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_binning_ib(ut->utctx, cs, __entry);
}

/*
 * start_vsc_overflow_test
 */
  
#define __print_start_vsc_overflow_test NULL
  
#define __print_json_start_vsc_overflow_test NULL
#define __print_fuzzy_hash_args_start_vsc_overflow_test NULL

#define __fuzzy_hash_start_vsc_overflow_test NULL
#define __fuzzy_equals_start_vsc_overflow_test NULL
#define __print_fuzzy_hash_args_start_vsc_overflow_test NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_vsc_overflow_test(struct u_trace_context *utctx, void *cs, struct trace_start_vsc_overflow_test *entry) {
   fd_cs_trace_start(utctx, cs, "start_vsc_overflow_test("
      ")"
   );
}

static const struct u_tracepoint __tp_start_vsc_overflow_test = {
    "start_vsc_overflow_test",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    11,
    u_tracepoint_type_marker,
    __print_start_vsc_overflow_test,
    __print_json_start_vsc_overflow_test,
    __fuzzy_hash_start_vsc_overflow_test,
    __fuzzy_equals_start_vsc_overflow_test,
    __print_fuzzy_hash_args_start_vsc_overflow_test,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_vsc_overflow_test,
#endif
};
void __trace_start_vsc_overflow_test(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_start_vsc_overflow_test entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_vsc_overflow_test *__entry =
      queueing ?
      (struct trace_start_vsc_overflow_test *)u_trace_appendv(ut, cs, &__tp_start_vsc_overflow_test,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_vsc_overflow_test(ut->utctx, cs, __entry);
}

/*
 * end_vsc_overflow_test
 */
  
#define __print_end_vsc_overflow_test NULL
  
#define __print_json_end_vsc_overflow_test NULL
#define __print_fuzzy_hash_args_end_vsc_overflow_test NULL

#define __fuzzy_hash_end_vsc_overflow_test NULL
#define __fuzzy_equals_end_vsc_overflow_test NULL
#define __print_fuzzy_hash_args_end_vsc_overflow_test NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_vsc_overflow_test(struct u_trace_context *utctx, void *cs, struct trace_end_vsc_overflow_test *entry) {
   fd_cs_trace_end(utctx, cs, "end_vsc_overflow_test("
      ")"
   );
}

static const struct u_tracepoint __tp_end_vsc_overflow_test = {
    "end_vsc_overflow_test",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    12,
    u_tracepoint_type_marker,
    __print_end_vsc_overflow_test,
    __print_json_end_vsc_overflow_test,
    __fuzzy_hash_end_vsc_overflow_test,
    __fuzzy_equals_end_vsc_overflow_test,
    __print_fuzzy_hash_args_end_vsc_overflow_test,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_vsc_overflow_test,
#endif
};
void __trace_end_vsc_overflow_test(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_vsc_overflow_test entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_vsc_overflow_test *__entry =
      queueing ?
      (struct trace_end_vsc_overflow_test *)u_trace_appendv(ut, cs, &__tp_end_vsc_overflow_test,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_vsc_overflow_test(ut->utctx, cs, __entry);
}

/*
 * start_prologue
 */
  
#define __print_start_prologue NULL
  
#define __print_json_start_prologue NULL
#define __print_fuzzy_hash_args_start_prologue NULL

#define __fuzzy_hash_start_prologue NULL
#define __fuzzy_equals_start_prologue NULL
#define __print_fuzzy_hash_args_start_prologue NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_prologue(struct u_trace_context *utctx, void *cs, struct trace_start_prologue *entry) {
   fd_cs_trace_start(utctx, cs, "start_prologue("
      ")"
   );
}

static const struct u_tracepoint __tp_start_prologue = {
    "start_prologue",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    13,
    u_tracepoint_type_marker,
    __print_start_prologue,
    __print_json_start_prologue,
    __fuzzy_hash_start_prologue,
    __fuzzy_equals_start_prologue,
    __print_fuzzy_hash_args_start_prologue,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_prologue,
#endif
};
void __trace_start_prologue(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_start_prologue entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_prologue *__entry =
      queueing ?
      (struct trace_start_prologue *)u_trace_appendv(ut, cs, &__tp_start_prologue,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_prologue(ut->utctx, cs, __entry);
}

/*
 * end_prologue
 */
  
#define __print_end_prologue NULL
  
#define __print_json_end_prologue NULL
#define __print_fuzzy_hash_args_end_prologue NULL

#define __fuzzy_hash_end_prologue NULL
#define __fuzzy_equals_end_prologue NULL
#define __print_fuzzy_hash_args_end_prologue NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_prologue(struct u_trace_context *utctx, void *cs, struct trace_end_prologue *entry) {
   fd_cs_trace_end(utctx, cs, "end_prologue("
      ")"
   );
}

static const struct u_tracepoint __tp_end_prologue = {
    "end_prologue",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    14,
    u_tracepoint_type_marker,
    __print_end_prologue,
    __print_json_end_prologue,
    __fuzzy_hash_end_prologue,
    __fuzzy_equals_end_prologue,
    __print_fuzzy_hash_args_end_prologue,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_prologue,
#endif
};
void __trace_end_prologue(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_prologue entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_prologue *__entry =
      queueing ?
      (struct trace_end_prologue *)u_trace_appendv(ut, cs, &__tp_end_prologue,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_prologue(ut->utctx, cs, __entry);
}

/*
 * start_clears
 */
  
static void __print_start_clears(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_clears *__entry =
      (const struct trace_start_clears *)arg;
   fprintf(out, "fast_cleared: 0x%x\n"
           , __entry->fast_cleared
   );
}

  
static void __print_json_start_clears(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_clears *__entry =
      (const struct trace_start_clears *)arg;
   fprintf(out, "\"unstructured\": \"fast_cleared: 0x%x\""
           , __entry->fast_cleared
   );
}

#define __print_fuzzy_hash_args_start_clears NULL

static uint32_t __fuzzy_hash_start_clears(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_clears *__entry =
      (const struct trace_start_clears *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_clears(const void *_a, const void *_b) {
   const struct trace_start_clears *__entry_a =
      (const struct trace_start_clears *)_a;
   (void)__entry_a;
   const struct trace_start_clears *__entry_b =
      (const struct trace_start_clears *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_clears(struct u_trace_context *utctx, void *cs, struct trace_start_clears *entry) {
   fd_cs_trace_start(utctx, cs, "start_clears("
      "fast_cleared=0x%x"
      ")"
      ,entry->fast_cleared
   );
}

static const struct u_tracepoint __tp_start_clears = {
    "start_clears",
    ALIGN_POT(sizeof(struct trace_start_clears), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    15,
    u_tracepoint_type_marker,
    __print_start_clears,
    __print_json_start_clears,
    __fuzzy_hash_start_clears,
    __fuzzy_equals_start_clears,
    __print_fuzzy_hash_args_start_clears,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_clears,
#endif
};
void __trace_start_clears(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t fast_cleared
) {
   struct trace_start_clears entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_clears *__entry =
      queueing ?
      (struct trace_start_clears *)u_trace_appendv(ut, cs, &__tp_start_clears,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->fast_cleared = fast_cleared;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_clears(ut->utctx, cs, __entry);
}

/*
 * end_clears
 */
  
#define __print_end_clears NULL
  
#define __print_json_end_clears NULL
#define __print_fuzzy_hash_args_end_clears NULL

#define __fuzzy_hash_end_clears NULL
#define __fuzzy_equals_end_clears NULL
#define __print_fuzzy_hash_args_end_clears NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_clears(struct u_trace_context *utctx, void *cs, struct trace_end_clears *entry) {
   fd_cs_trace_end(utctx, cs, "end_clears("
      ")"
   );
}

static const struct u_tracepoint __tp_end_clears = {
    "end_clears",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    16,
    u_tracepoint_type_marker,
    __print_end_clears,
    __print_json_end_clears,
    __fuzzy_hash_end_clears,
    __fuzzy_equals_end_clears,
    __print_fuzzy_hash_args_end_clears,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_clears,
#endif
};
void __trace_end_clears(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_clears entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_clears *__entry =
      queueing ?
      (struct trace_end_clears *)u_trace_appendv(ut, cs, &__tp_end_clears,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_clears(ut->utctx, cs, __entry);
}

/*
 * start_tile_loads
 */
  
static void __print_start_tile_loads(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_tile_loads *__entry =
      (const struct trace_start_tile_loads *)arg;
   fprintf(out, "load=0x%x\n"
           , __entry->load
   );
}

  
static void __print_json_start_tile_loads(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_tile_loads *__entry =
      (const struct trace_start_tile_loads *)arg;
   fprintf(out, "\"unstructured\": \"load=0x%x\""
           , __entry->load
   );
}

#define __print_fuzzy_hash_args_start_tile_loads NULL

static uint32_t __fuzzy_hash_start_tile_loads(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_tile_loads *__entry =
      (const struct trace_start_tile_loads *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_tile_loads(const void *_a, const void *_b) {
   const struct trace_start_tile_loads *__entry_a =
      (const struct trace_start_tile_loads *)_a;
   (void)__entry_a;
   const struct trace_start_tile_loads *__entry_b =
      (const struct trace_start_tile_loads *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_tile_loads(struct u_trace_context *utctx, void *cs, struct trace_start_tile_loads *entry) {
   fd_cs_trace_start(utctx, cs, "start_tile_loads("
      "load=0x%x"
      ")"
      ,entry->load
   );
}

static const struct u_tracepoint __tp_start_tile_loads = {
    "start_tile_loads",
    ALIGN_POT(sizeof(struct trace_start_tile_loads), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    17,
    u_tracepoint_type_marker,
    __print_start_tile_loads,
    __print_json_start_tile_loads,
    __fuzzy_hash_start_tile_loads,
    __fuzzy_equals_start_tile_loads,
    __print_fuzzy_hash_args_start_tile_loads,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_tile_loads,
#endif
};
void __trace_start_tile_loads(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t load
) {
   struct trace_start_tile_loads entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_tile_loads *__entry =
      queueing ?
      (struct trace_start_tile_loads *)u_trace_appendv(ut, cs, &__tp_start_tile_loads,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->load = load;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_tile_loads(ut->utctx, cs, __entry);
}

/*
 * end_tile_loads
 */
  
#define __print_end_tile_loads NULL
  
#define __print_json_end_tile_loads NULL
#define __print_fuzzy_hash_args_end_tile_loads NULL

#define __fuzzy_hash_end_tile_loads NULL
#define __fuzzy_equals_end_tile_loads NULL
#define __print_fuzzy_hash_args_end_tile_loads NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_tile_loads(struct u_trace_context *utctx, void *cs, struct trace_end_tile_loads *entry) {
   fd_cs_trace_end(utctx, cs, "end_tile_loads("
      ")"
   );
}

static const struct u_tracepoint __tp_end_tile_loads = {
    "end_tile_loads",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    18,
    u_tracepoint_type_marker,
    __print_end_tile_loads,
    __print_json_end_tile_loads,
    __fuzzy_hash_end_tile_loads,
    __fuzzy_equals_end_tile_loads,
    __print_fuzzy_hash_args_end_tile_loads,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_tile_loads,
#endif
};
void __trace_end_tile_loads(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_tile_loads entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_tile_loads *__entry =
      queueing ?
      (struct trace_end_tile_loads *)u_trace_appendv(ut, cs, &__tp_end_tile_loads,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_tile_loads(ut->utctx, cs, __entry);
}

/*
 * start_tile_stores
 */
  
static void __print_start_tile_stores(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_tile_stores *__entry =
      (const struct trace_start_tile_stores *)arg;
   fprintf(out, "store: 0x%x\n"
           , __entry->store
   );
}

  
static void __print_json_start_tile_stores(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_tile_stores *__entry =
      (const struct trace_start_tile_stores *)arg;
   fprintf(out, "\"unstructured\": \"store: 0x%x\""
           , __entry->store
   );
}

#define __print_fuzzy_hash_args_start_tile_stores NULL

static uint32_t __fuzzy_hash_start_tile_stores(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_tile_stores *__entry =
      (const struct trace_start_tile_stores *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_tile_stores(const void *_a, const void *_b) {
   const struct trace_start_tile_stores *__entry_a =
      (const struct trace_start_tile_stores *)_a;
   (void)__entry_a;
   const struct trace_start_tile_stores *__entry_b =
      (const struct trace_start_tile_stores *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_tile_stores(struct u_trace_context *utctx, void *cs, struct trace_start_tile_stores *entry) {
   fd_cs_trace_start(utctx, cs, "start_tile_stores("
      "store=0x%x"
      ")"
      ,entry->store
   );
}

static const struct u_tracepoint __tp_start_tile_stores = {
    "start_tile_stores",
    ALIGN_POT(sizeof(struct trace_start_tile_stores), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    19,
    u_tracepoint_type_marker,
    __print_start_tile_stores,
    __print_json_start_tile_stores,
    __fuzzy_hash_start_tile_stores,
    __fuzzy_equals_start_tile_stores,
    __print_fuzzy_hash_args_start_tile_stores,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_tile_stores,
#endif
};
void __trace_start_tile_stores(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t store
) {
   struct trace_start_tile_stores entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_tile_stores *__entry =
      queueing ?
      (struct trace_start_tile_stores *)u_trace_appendv(ut, cs, &__tp_start_tile_stores,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->store = store;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_tile_stores(ut->utctx, cs, __entry);
}

/*
 * end_tile_stores
 */
  
#define __print_end_tile_stores NULL
  
#define __print_json_end_tile_stores NULL
#define __print_fuzzy_hash_args_end_tile_stores NULL

#define __fuzzy_hash_end_tile_stores NULL
#define __fuzzy_equals_end_tile_stores NULL
#define __print_fuzzy_hash_args_end_tile_stores NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_tile_stores(struct u_trace_context *utctx, void *cs, struct trace_end_tile_stores *entry) {
   fd_cs_trace_end(utctx, cs, "end_tile_stores("
      ")"
   );
}

static const struct u_tracepoint __tp_end_tile_stores = {
    "end_tile_stores",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    20,
    u_tracepoint_type_marker,
    __print_end_tile_stores,
    __print_json_end_tile_stores,
    __fuzzy_hash_end_tile_stores,
    __fuzzy_equals_end_tile_stores,
    __print_fuzzy_hash_args_end_tile_stores,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_tile_stores,
#endif
};
void __trace_end_tile_stores(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_tile_stores entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_tile_stores *__entry =
      queueing ?
      (struct trace_end_tile_stores *)u_trace_appendv(ut, cs, &__tp_end_tile_stores,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_tile_stores(ut->utctx, cs, __entry);
}

/*
 * start_tile
 */
  
static void __print_start_tile(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_tile *__entry =
      (const struct trace_start_tile *)arg;
   fprintf(out, "bin_h=%d, yoff=%d, bin_w=%d, xoff=%d\n"
           , __entry->bin_h
           , __entry->yoff
           , __entry->bin_w
           , __entry->xoff
   );
}

  
static void __print_json_start_tile(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_tile *__entry =
      (const struct trace_start_tile *)arg;
   fprintf(out, "\"unstructured\": \"bin_h=%d, yoff=%d, bin_w=%d, xoff=%d\""
           , __entry->bin_h
           , __entry->yoff
           , __entry->bin_w
           , __entry->xoff
   );
}

#define __print_fuzzy_hash_args_start_tile NULL

static uint32_t __fuzzy_hash_start_tile(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_tile *__entry =
      (const struct trace_start_tile *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_tile(const void *_a, const void *_b) {
   const struct trace_start_tile *__entry_a =
      (const struct trace_start_tile *)_a;
   (void)__entry_a;
   const struct trace_start_tile *__entry_b =
      (const struct trace_start_tile *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_msg(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_tile(struct u_trace_context *utctx, void *cs, struct trace_start_tile *entry) {
   fd_cs_trace_msg(utctx, cs, "start_tile("
      "bin_h=%u"
      ",yoff=%u"
      ",bin_w=%u"
      ",xoff=%u"
      ")"
      ,entry->bin_h
      ,entry->yoff
      ,entry->bin_w
      ,entry->xoff
   );
}

static const struct u_tracepoint __tp_start_tile = {
    "start_tile",
    ALIGN_POT(sizeof(struct trace_start_tile), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    21,
    u_tracepoint_type_marker,
    __print_start_tile,
    __print_json_start_tile,
    __fuzzy_hash_start_tile,
    __fuzzy_equals_start_tile,
    __print_fuzzy_hash_args_start_tile,
};
void __trace_start_tile(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , uint16_t bin_h
   , uint16_t yoff
   , uint16_t bin_w
   , uint16_t xoff
) {
   struct trace_start_tile entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_tile *__entry =
      queueing ?
      (struct trace_start_tile *)u_trace_appendv(ut, cs, &__tp_start_tile,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->bin_h = bin_h;
   __entry->yoff = yoff;
   __entry->bin_w = bin_w;
   __entry->xoff = xoff;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_tile(ut->utctx, cs, __entry);
}

/*
 * start_draw_ib
 */
  
#define __print_start_draw_ib NULL
  
#define __print_json_start_draw_ib NULL
#define __print_fuzzy_hash_args_start_draw_ib NULL

#define __fuzzy_hash_start_draw_ib NULL
#define __fuzzy_equals_start_draw_ib NULL
#define __print_fuzzy_hash_args_start_draw_ib NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_draw_ib(struct u_trace_context *utctx, void *cs, struct trace_start_draw_ib *entry) {
   fd_cs_trace_start(utctx, cs, "start_draw_ib("
      ")"
   );
}

static const struct u_tracepoint __tp_start_draw_ib = {
    "start_draw_ib",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    22,
    u_tracepoint_type_marker,
    __print_start_draw_ib,
    __print_json_start_draw_ib,
    __fuzzy_hash_start_draw_ib,
    __fuzzy_equals_start_draw_ib,
    __print_fuzzy_hash_args_start_draw_ib,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_draw_ib,
#endif
};
void __trace_start_draw_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_start_draw_ib entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_draw_ib *__entry =
      queueing ?
      (struct trace_start_draw_ib *)u_trace_appendv(ut, cs, &__tp_start_draw_ib,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_draw_ib(ut->utctx, cs, __entry);
}

/*
 * end_draw_ib
 */
  
#define __print_end_draw_ib NULL
  
#define __print_json_end_draw_ib NULL
#define __print_fuzzy_hash_args_end_draw_ib NULL

#define __fuzzy_hash_end_draw_ib NULL
#define __fuzzy_equals_end_draw_ib NULL
#define __print_fuzzy_hash_args_end_draw_ib NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_draw_ib(struct u_trace_context *utctx, void *cs, struct trace_end_draw_ib *entry) {
   fd_cs_trace_end(utctx, cs, "end_draw_ib("
      ")"
   );
}

static const struct u_tracepoint __tp_end_draw_ib = {
    "end_draw_ib",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    23,
    u_tracepoint_type_marker,
    __print_end_draw_ib,
    __print_json_end_draw_ib,
    __fuzzy_hash_end_draw_ib,
    __fuzzy_equals_end_draw_ib,
    __print_fuzzy_hash_args_end_draw_ib,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_draw_ib,
#endif
};
void __trace_end_draw_ib(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_draw_ib entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_draw_ib *__entry =
      queueing ?
      (struct trace_end_draw_ib *)u_trace_appendv(ut, cs, &__tp_end_draw_ib,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_draw_ib(ut->utctx, cs, __entry);
}

/*
 * start_blit
 */
  
static void __print_start_blit(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_blit *__entry =
      (const struct trace_start_blit *)arg;
   fprintf(out, "%s -> %s\n"
           , util_str_tex_target(__entry->src_target, true)
           , util_str_tex_target(__entry->dst_target, true)
   );
}

  
static void __print_json_start_blit(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_blit *__entry =
      (const struct trace_start_blit *)arg;
   fprintf(out, "\"unstructured\": \"%s -> %s\""
           , util_str_tex_target(__entry->src_target, true)
           , util_str_tex_target(__entry->dst_target, true)
   );
}

#define __print_fuzzy_hash_args_start_blit NULL

static uint32_t __fuzzy_hash_start_blit(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_blit *__entry =
      (const struct trace_start_blit *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_blit(const void *_a, const void *_b) {
   const struct trace_start_blit *__entry_a =
      (const struct trace_start_blit *)_a;
   (void)__entry_a;
   const struct trace_start_blit *__entry_b =
      (const struct trace_start_blit *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_blit(struct u_trace_context *utctx, void *cs, struct trace_start_blit *entry) {
   fd_cs_trace_start(utctx, cs, "start_blit("
      "src_target=%s"
      ",dst_target=%s"
      ")"
      ,util_str_tex_target(entry->src_target, true)
      ,util_str_tex_target(entry->dst_target, true)
   );
}

static const struct u_tracepoint __tp_start_blit = {
    "start_blit",
    ALIGN_POT(sizeof(struct trace_start_blit), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    24,
    u_tracepoint_type_marker,
    __print_start_blit,
    __print_json_start_blit,
    __fuzzy_hash_start_blit,
    __fuzzy_equals_start_blit,
    __print_fuzzy_hash_args_start_blit,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_blit,
#endif
};
void __trace_start_blit(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
   , enum pipe_texture_target src_target
   , enum pipe_texture_target dst_target
) {
   struct trace_start_blit entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_blit *__entry =
      queueing ?
      (struct trace_start_blit *)u_trace_appendv(ut, cs, &__tp_start_blit,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->src_target = src_target;
   __entry->dst_target = dst_target;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_blit(ut->utctx, cs, __entry);
}

/*
 * end_blit
 */
  
#define __print_end_blit NULL
  
#define __print_json_end_blit NULL
#define __print_fuzzy_hash_args_end_blit NULL

#define __fuzzy_hash_end_blit NULL
#define __fuzzy_equals_end_blit NULL
#define __print_fuzzy_hash_args_end_blit NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_blit(struct u_trace_context *utctx, void *cs, struct trace_end_blit *entry) {
   fd_cs_trace_end(utctx, cs, "end_blit("
      ")"
   );
}

static const struct u_tracepoint __tp_end_blit = {
    "end_blit",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    25,
    u_tracepoint_type_marker,
    __print_end_blit,
    __print_json_end_blit,
    __fuzzy_hash_end_blit,
    __fuzzy_equals_end_blit,
    __print_fuzzy_hash_args_end_blit,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_blit,
#endif
};
void __trace_end_blit(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_blit entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_blit *__entry =
      queueing ?
      (struct trace_end_blit *)u_trace_appendv(ut, cs, &__tp_end_blit,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_blit(ut->utctx, cs, __entry);
}

/*
 * start_compute
 */
  
static void __print_start_compute(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_compute *__entry =
      (const struct trace_start_compute *)arg;
   fprintf(out, ""
      "indirect=%u, "
      "work_dim=%u, "
      "local_size_x=%u, "
      "local_size_y=%u, "
      "local_size_z=%u, "
      "num_groups_x=%u, "
      "num_groups_y=%u, "
      "num_groups_z=%u, "
      "shader_id=%u, "
         "\n"
   ,__entry->indirect
   ,__entry->work_dim
   ,__entry->local_size_x
   ,__entry->local_size_y
   ,__entry->local_size_z
   ,__entry->num_groups_x
   ,__entry->num_groups_y
   ,__entry->num_groups_z
   ,__entry->shader_id
   );
}

  
static void __print_json_start_compute(FILE *out, const void *arg, const void *indirect) {
   const struct trace_start_compute *__entry =
      (const struct trace_start_compute *)arg;
   fprintf(out, ""
      "\"indirect\": \"%u\""
         ", "
      "\"work_dim\": \"%u\""
         ", "
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
         ", "
      "\"shader_id\": \"%u\""
   ,__entry->indirect
   ,__entry->work_dim
   ,__entry->local_size_x
   ,__entry->local_size_y
   ,__entry->local_size_z
   ,__entry->num_groups_x
   ,__entry->num_groups_y
   ,__entry->num_groups_z
   ,__entry->shader_id
   );
}

#define __print_fuzzy_hash_args_start_compute NULL

static uint32_t __fuzzy_hash_start_compute(const void *_event) {
   uint32_t hash = 1;
   const struct trace_start_compute *__entry =
      (const struct trace_start_compute *)_event;
   (void)__entry;
   return hash;
}

static bool __fuzzy_equals_start_compute(const void *_a, const void *_b) {
   const struct trace_start_compute *__entry_a =
      (const struct trace_start_compute *)_a;
   (void)__entry_a;
   const struct trace_start_compute *__entry_b =
      (const struct trace_start_compute *)_b;
   (void)__entry_b;
   return true;
}


__attribute__((format(printf, 3, 4))) void fd_cs_trace_start(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_start_compute(struct u_trace_context *utctx, void *cs, struct trace_start_compute *entry) {
   fd_cs_trace_start(utctx, cs, "start_compute("
      "indirect=%u"
      ",work_dim=%u"
      ",local_size_x=%u"
      ",local_size_y=%u"
      ",local_size_z=%u"
      ",num_groups_x=%u"
      ",num_groups_y=%u"
      ",num_groups_z=%u"
      ",shader_id=%u"
      ")"
      ,entry->indirect
      ,entry->work_dim
      ,entry->local_size_x
      ,entry->local_size_y
      ,entry->local_size_z
      ,entry->num_groups_x
      ,entry->num_groups_y
      ,entry->num_groups_z
      ,entry->shader_id
   );
}

static const struct u_tracepoint __tp_start_compute = {
    "start_compute",
    ALIGN_POT(sizeof(struct trace_start_compute), 8),   /* keep size 64b aligned */
    0
    ,
    0,
    26,
    u_tracepoint_type_marker,
    __print_start_compute,
    __print_json_start_compute,
    __fuzzy_hash_start_compute,
    __fuzzy_equals_start_compute,
    __print_fuzzy_hash_args_start_compute,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_start_compute,
#endif
};
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
) {
   struct trace_start_compute entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_start_compute *__entry =
      queueing ?
      (struct trace_start_compute *)u_trace_appendv(ut, cs, &__tp_start_compute,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   __entry->indirect = indirect;
   __entry->work_dim = work_dim;
   __entry->local_size_x = local_size_x;
   __entry->local_size_y = local_size_y;
   __entry->local_size_z = local_size_z;
   __entry->num_groups_x = num_groups_x;
   __entry->num_groups_y = num_groups_y;
   __entry->num_groups_z = num_groups_z;
   __entry->shader_id = shader_id;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_start_compute(ut->utctx, cs, __entry);
}

/*
 * end_compute
 */
  
#define __print_end_compute NULL
  
#define __print_json_end_compute NULL
#define __print_fuzzy_hash_args_end_compute NULL

#define __fuzzy_hash_end_compute NULL
#define __fuzzy_equals_end_compute NULL
#define __print_fuzzy_hash_args_end_compute NULL

__attribute__((format(printf, 3, 4))) void fd_cs_trace_end(struct u_trace_context *utctx, void *, const char *, ...);

static void __emit_label_end_compute(struct u_trace_context *utctx, void *cs, struct trace_end_compute *entry) {
   fd_cs_trace_end(utctx, cs, "end_compute("
      ")"
   );
}

static const struct u_tracepoint __tp_end_compute = {
    "end_compute",
    ALIGN_POT(0, 8),   /* keep size 64b aligned */
    0
    ,
    0,
    27,
    u_tracepoint_type_marker,
    __print_end_compute,
    __print_json_end_compute,
    __fuzzy_hash_end_compute,
    __fuzzy_equals_end_compute,
    __print_fuzzy_hash_args_end_compute,
#ifdef HAVE_PERFETTO
    (void (*)(void *pctx, uint64_t, uint16_t, const void *, const void *, const void *))fd_end_compute,
#endif
};
void __trace_end_compute(
     struct u_trace *ut
   , enum u_trace_type enabled_traces
   , void *cs
) {
   struct trace_end_compute entry;
   const bool queueing = enabled_traces & U_TRACE_TYPE_REQUIRE_QUEUING;
   UNUSED struct trace_end_compute *__entry =
      queueing ?
      (struct trace_end_compute *)u_trace_appendv(ut, cs, &__tp_end_compute,
                                                    0
                                                    ,
                                                    0, NULL, NULL
                                                    ) :
      &entry;
   if (enabled_traces & U_TRACE_TYPE_MARKERS)
      __emit_label_end_compute(ut->utctx, cs, __entry);
}

