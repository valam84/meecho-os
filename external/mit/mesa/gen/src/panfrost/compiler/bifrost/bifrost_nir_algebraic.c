#include "bifrost_nir.h"

#include "nir.h"
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

/* What follows is NIR algebraic transform code for the following 4
 * transforms:
 *    ('iand', 'a@1', 'b@1') => ('bcsel', 'a', 'b', False)
 *    ('ior', 'a@1', 'b@1') => ('bcsel', 'a', 'a', 'b')
 *    ('iand', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 0, 'a')
 *    ('ior', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 'a', True)
 */


static const nir_search_value_union bifrost_nir_opt_boolean_bitwise_values[] = {
   /* ('iand', 'a@1', 'b@1') => ('bcsel', 'a', 'b', False) */
   { .variable = {
      { nir_search_value_variable, 1 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 1 },
      1, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace0_0 -> 0 in the cache */
   /* replace0_1 -> 1 in the cache */
   { .constant = {
      { nir_search_value_constant, 1 },
      nir_type_bool, { NIR_FALSE /* False */ },
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel,
      -1, 0,
      { 0, 1, 3 },
      -1,
   } },

   /* ('ior', 'a@1', 'b@1') => ('bcsel', 'a', 'a', 'b') */
   /* search1_0 -> 0 in the cache */
   /* search1_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace1_0 -> 0 in the cache */
   /* replace1_1 -> 0 in the cache */
   /* replace1_2 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel,
      -1, 0,
      { 0, 0, 1 },
      -1,
   } },

   /* ('iand', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 0, 'a') */
   /* search2_0 -> 0 in the cache */
   /* search2_1_0 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 0, 7 },
      -1,
   } },

   /* replace2_0 -> 1 in the cache */
   { .constant = {
      { nir_search_value_constant, 1 },
      nir_type_int, { 0x0ull /* 0 */ },
   } },
   /* replace2_2 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel,
      -1, 0,
      { 1, 9, 0 },
      -1,
   } },

   /* ('ior', 'a@1', ('inot', 'b@1')) => ('bcsel', 'b', 'a', True) */
   /* search3_0 -> 0 in the cache */
   /* search3_1_0 -> 1 in the cache */
   /* search3_1 -> 7 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 0, 7 },
      -1,
   } },

   /* replace3_0 -> 1 in the cache */
   /* replace3_1 -> 0 in the cache */
   { .constant = {
      { nir_search_value_constant, 1 },
      nir_type_bool, { NIR_TRUE /* True */ },
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel,
      -1, 0,
      { 1, 0, 12 },
      -1,
   } },

};



static const struct transform bifrost_nir_opt_boolean_bitwise_transforms[] = {
   { ~0, ~0, ~0 }, /* Sentinel */

   { 2, 4, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 5, 6, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 2, 4, 0 },
   { 8, 10, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 5, 6, 0 },
   { 11, 13, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

};

static const struct per_op_table bifrost_nir_opt_boolean_bitwise_pass_op_table[nir_num_search_ops] = {
   [nir_op_iand] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         1,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         2,
         5,
         5,
         5,
      },
   },
   [nir_op_ior] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         1,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         3,
         6,
         6,
         6,
      },
   },
   [nir_op_inot] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         4,
      },
   },
};

/* Mapping from state index to offset in transforms (0 being no transforms) */
static const uint16_t bifrost_nir_opt_boolean_bitwise_transform_offsets[] = {
   0,
   0,
   1,
   3,
   0,
   5,
   8,
};

static const nir_algebraic_table bifrost_nir_opt_boolean_bitwise_table = {
   .transforms = bifrost_nir_opt_boolean_bitwise_transforms,
   .transform_offsets = bifrost_nir_opt_boolean_bitwise_transform_offsets,
   .pass_op_table = bifrost_nir_opt_boolean_bitwise_pass_op_table,
   .values = bifrost_nir_opt_boolean_bitwise_values,
   .expression_cond = NULL,
   .variable_cond = NULL,
};

bool
bifrost_nir_opt_boolean_bitwise(
   nir_shader *shader
) {
   bool progress = false;
   bool condition_flags[1];
   const nir_shader_compiler_options *options = shader->options;
   const shader_info *info = &shader->info;
   (void) options;
   (void) info;

   STATIC_ASSERT(14 == ARRAY_SIZE(bifrost_nir_opt_boolean_bitwise_values));
   condition_flags[0] = true;

   nir_foreach_function_impl(impl, shader) {
     progress |= nir_algebraic_impl(impl, condition_flags, &bifrost_nir_opt_boolean_bitwise_table);
   }

   return progress;
}


#include "nir.h"
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

/* What follows is NIR algebraic transform code for the following 98
 * transforms:
 *    ('fmul', 'a', 2.0) => ('fadd', 'a', 'a')
 *    ('fmin', ('fmax', 'a', -1.0), 1.0) => ('fsat_signed', 'a')
 *    ('fmax', ('fmin', 'a', 1.0), -1.0) => ('fsat_signed', 'a')
 *    ('fmax', 'a', 0.0) => ('fclamp_pos', 'a')
 *    ('bcsel_pan', 'b@32', ('iadd', 'a@32', 1), 'a') => ('iadd', 'a', ('b2i32', 'b'))
 *    ('imin@8', 'a', 'b') => ('bcsel_pan', ('ilt_pan', 'a', 'b'), 'a', 'b')
 *    ('imax@8', 'a', 'b') => ('bcsel_pan', ('ilt_pan', 'a', 'b'), 'b', 'a')
 *    ('umin@8', 'a', 'b') => ('bcsel_pan', ('ult_pan', 'a', 'b'), 'a', 'b')
 *    ('umax@8', 'a', 'b') => ('bcsel_pan', ('ult_pan', 'a', 'b'), 'b', 'a')
 *    ('f2i8', 'a') => ('i2i8', ('f2i16', 'a'))
 *    ('f2u8', 'a') => ('u2u8', ('f2u16', 'a'))
 *    ('unpack_64_2x32', 'a') => ('vec2', ('unpack_64_2x32_split_x', 'a'), ('unpack_64_2x32_split_y', 'a'))
 *    ('i2f32', 'a@8') => ('i2f32', ('i2i32', 'a'))
 *    ('i2f32', 'a@16') => ('i2f32', ('i2i32', 'a'))
 *    ('u2f32', 'a@8') => ('u2f32', ('u2u32', 'a'))
 *    ('u2f32', 'a@16') => ('u2f32', ('u2u32', 'a'))
 *    ('i2f16', 'a') => ('f2f16', ('i2f32', ('i2i32', 'a')))
 *    ('u2f16', 'a') => ('f2f16', ('u2f32', ('u2u32', 'a')))
 *    ('i2f16', 'a@32') => ('f2f16', ('i2f32', 'a'))
 *    ('u2f16', 'a@32') => ('f2f16', ('u2f32', 'a'))
 *    ('f2i16', 'a@16') => ('f2i16', ('f2f32', 'a'))
 *    ('f2u16', 'a@16') => ('f2u16', ('f2f32', 'a'))
 *    ('pack_half_2x16_split', 'a', 'b') => ('pack_32_2x16_split', ('f2f16', 'a'), ('f2f16', 'b'))
 *    ('f2i32', 'a@16') => ('f2i32', ('f2f32', 'a'))
 *    ('f2u32', 'a@16') => ('f2u32', ('f2f32', 'a'))
 *    ('f2u16', 'a') => ('u2u16', ('f2u32', 'a'))
 *    ('f2i16', 'a') => ('i2i16', ('f2i32', 'a'))
 *    ('pack_uvec2_to_uint', 'a') => ('pack_32_2x16', ('u2u16', 'a'))
 *    ('pack_uvec4_to_uint', 'a') => ('pack_32_4x8', ('u2u8', 'a'))
 *    ('ffract', 'a') => ('fadd', 'a', ('fneg', ('ffloor', 'a')))
 *    ('i2i8', 'a@8') => a
 *    ('i2i16', 'a@16') => a
 *    ('i2i32', 'a@32') => a
 *    ('ilt_pan@8', 'a', 'b') => ('i2i8', ('ilt_pan', ('i2i16', 'a'), ('i2i16', 'b')))
 *    ('ige_pan@8', 'a', 'b') => ('i2i8', ('ige_pan', ('i2i16', 'a'), ('i2i16', 'b')))
 *    ('ieq_pan@8', 'a', 'b') => ('i2i8', ('ieq_pan', ('i2i16', 'a'), ('i2i16', 'b')))
 *    ('ine_pan@8', 'a', 'b') => ('i2i8', ('ine_pan', ('i2i16', 'a'), ('i2i16', 'b')))
 *    ('ult_pan@8', 'a', 'b') => ('u2u8', ('ult_pan', ('u2u16', 'a'), ('u2u16', 'b')))
 *    ('uge_pan@8', 'a', 'b') => ('u2u8', ('uge_pan', ('u2u16', 'a'), ('u2u16', 'b')))
 *    ('b2f16', ('inot', 'a@16')) => ('bcsel_pan', 'a', 0.0, 1.0)
 *    ('b2f16', ('inot', 'a')) => ('bcsel_pan', ('i2i16', 'a'), 0.0, 1.0)
 *    ('b2f16', 'a@16') => ('bcsel_pan', 'a', 1.0, 0.0)
 *    ('b2f16', 'a') => ('bcsel_pan', ('i2i16', 'a'), 1.0, 0.0)
 *    ('b2f32', ('inot', 'a@32')) => ('bcsel_pan', 'a', 0.0, 1.0)
 *    ('b2f32', ('inot', 'a')) => ('bcsel_pan', ('i2i32', 'a'), 0.0, 1.0)
 *    ('b2f32', 'a@32') => ('bcsel_pan', 'a', 1.0, 0.0)
 *    ('b2f32', 'a') => ('bcsel_pan', ('i2i32', 'a'), 1.0, 0.0)
 *    ('b2i8', ('inot', 'a@8')) => ('bcsel_pan', 'a', 0, 1)
 *    ('b2i8', ('inot', 'a')) => ('bcsel_pan', ('i2i8', 'a'), 0, 1)
 *    ('b2i8', 'a@8') => ('bcsel_pan', 'a', 1, 0)
 *    ('b2i8', 'a') => ('bcsel_pan', ('i2i8', 'a'), 1, 0)
 *    ('b2i16', ('inot', 'a@16')) => ('bcsel_pan', 'a', 0, 1)
 *    ('b2i16', ('inot', 'a')) => ('bcsel_pan', ('i2i16', 'a'), 0, 1)
 *    ('b2i16', 'a@16') => ('bcsel_pan', 'a', 1, 0)
 *    ('b2i16', 'a') => ('bcsel_pan', ('i2i16', 'a'), 1, 0)
 *    ('b2i32', ('inot', 'a@32')) => ('bcsel_pan', 'a', 0, 1)
 *    ('b2i32', ('inot', 'a')) => ('bcsel_pan', ('i2i32', 'a'), 0, 1)
 *    ('b2i32', 'a@32') => ('bcsel_pan', 'a', 1, 0)
 *    ('b2i32', 'a') => ('bcsel_pan', ('i2i32', 'a'), 1, 0)
 *    ('iand', ('ishl(is_used_once)', ('iand', 'a', '#b'), '#c'), '#d') => ('lshift_and_pan', 'a', ('u2u8', 'c'), ('iand', ('ishl', 'b', 'c'), 'd'))
 *    ('iand', ('ishl(is_used_once)', 'a', 'b'), 'c') => ('lshift_and_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ishl', ('iand', 'a', '#b'), '#c') => ('lshift_and_pan', 'a', ('u2u8', 'c'), ('ishl', 'b', 'c'))
 *    ('ior', ('ishl(is_used_once)', ('ior', 'a', '#b'), '#c'), '#d') => ('lshift_or_pan', 'a', ('u2u8', 'c'), ('ior', ('ishl', 'b', 'c'), 'd'))
 *    ('ior', ('ishl(is_used_once)', 'a', 'b'), 'c') => ('lshift_or_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ishl', ('ior', 'a', '#b'), '#c') => ('lshift_or_pan', 'a', ('u2u8', 'c'), ('ishl', 'b', 'c'))
 *    ('ixor', ('ishl(is_used_once)', ('ixor', 'a', '#b'), '#c'), '#d') => ('lshift_xor_pan', 'a', ('u2u8', 'c'), ('ixor', ('ishl', 'b', 'c'), 'd'))
 *    ('ixor', ('ishl(is_used_once)', 'a', 'b'), 'c') => ('lshift_xor_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ishl', ('ixor', 'a', '#b'), '#c') => ('lshift_xor_pan', 'a', ('u2u8', 'c'), ('ishl', 'b', 'c'))
 *    ('iand', ('ushr(is_used_once)', ('iand', 'a', '#b'), '#c'), '#d') => ('rshift_and_pan', 'a', ('u2u8', 'c'), ('iand', ('ushr', 'b', 'c'), 'd'))
 *    ('iand', ('ushr(is_used_once)', 'a', 'b'), 'c') => ('rshift_and_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ushr', ('iand', 'a', '#b'), '#c') => ('rshift_and_pan', 'a', ('u2u8', 'c'), ('ushr', 'b', 'c'))
 *    ('ior', ('ushr(is_used_once)', ('ior', 'a', '#b'), '#c'), '#d') => ('rshift_or_pan', 'a', ('u2u8', 'c'), ('ior', ('ushr', 'b', 'c'), 'd'))
 *    ('ior', ('ushr(is_used_once)', 'a', 'b'), 'c') => ('rshift_or_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ushr', ('ior', 'a', '#b'), '#c') => ('rshift_or_pan', 'a', ('u2u8', 'c'), ('ushr', 'b', 'c'))
 *    ('ixor', ('ushr(is_used_once)', ('ixor', 'a', '#b'), '#c'), '#d') => ('rshift_xor_pan', 'a', ('u2u8', 'c'), ('ixor', ('ushr', 'b', 'c'), 'd'))
 *    ('ixor', ('ushr(is_used_once)', 'a', 'b'), 'c') => ('rshift_xor_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ushr', ('ixor', 'a', '#b'), '#c') => ('rshift_xor_pan', 'a', ('u2u8', 'c'), ('ushr', 'b', 'c'))
 *    ('iand', ('ishr(is_used_once)', ('iand', 'a', '#b'), '#c'), '#d') => ('arshift_and_pan', 'a', ('u2u8', 'c'), ('iand', ('ishr', 'b', 'c'), 'd'))
 *    ('iand', ('ishr(is_used_once)', 'a', 'b'), 'c') => ('arshift_and_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ishr', ('iand', 'a', '#b'), '#c') => ('arshift_and_pan', 'a', ('u2u8', 'c'), ('ishr', 'b', 'c'))
 *    ('ior', ('ishr(is_used_once)', ('ior', 'a', '#b'), '#c'), '#d') => ('arshift_or_pan', 'a', ('u2u8', 'c'), ('ior', ('ishr', 'b', 'c'), 'd'))
 *    ('ior', ('ishr(is_used_once)', 'a', 'b'), 'c') => ('arshift_or_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ishr', ('ior', 'a', '#b'), '#c') => ('arshift_or_pan', 'a', ('u2u8', 'c'), ('ishr', 'b', 'c'))
 *    ('ixor', ('ishr(is_used_once)', ('ixor', 'a', '#b'), '#c'), '#d') => ('arshift_xor_pan', 'a', ('u2u8', 'c'), ('ixor', ('ishr', 'b', 'c'), 'd'))
 *    ('ixor', ('ishr(is_used_once)', 'a', 'b'), 'c') => ('arshift_xor_pan', 'a', ('u2u8', 'b'), 'c')
 *    ('ishr', ('ixor', 'a', '#b'), '#c') => ('arshift_xor_pan', 'a', ('u2u8', 'c'), ('ishr', 'b', 'c'))
 *    ('iand', 'a', 'b') => ('lshift_and_pan', 'a', 0, 'b')
 *    ('ior', 'a', 'b') => ('lshift_or_pan', 'a', 0, 'b')
 *    ('ixor', 'a', 'b') => ('lshift_xor_pan', 'a', 0, 'b')
 *    ('inot', 'a') => ('lshift_xor_pan', 'a', 0, -1)
 *    ('ishl', 'a', 'b') => ('lshift_or_pan', 'a', ('u2u8', 'b'), 0)
 *    ('ushr', 'a', 'b') => ('rshift_or_pan', 'a', ('u2u8', 'b'), 0)
 *    ('ishr', 'a', 'b') => ('arshift_or_pan', 'a', ('u2u8', 'b'), 0)
 *    ('urol', 'a', 'b') => ('lrot_or_pan', 'a', ('u2u8', 'b'), 0)
 *    ('uror', 'a', 'b') => ('rrot_or_pan', 'a', ('u2u8', 'b'), 0)
 *    ('imul_high', 'a@32', 'b@32') => ('unpack_64_2x32_split_y', ('imul_2x32_64', 'a', 'b'))
 *    ('umul_high', 'a@32', 'b@32') => ('unpack_64_2x32_split_y', ('umul_2x32_64', 'a', 'b'))
 *    ('ldexp', 'a@16', 'b') => ('ldexp16_pan', 'a', ('i2i16', ('imin', ('imax', 'b', -127), 127)))
 */


static const nir_search_value_union bifrost_nir_lower_algebraic_late_values[] = {
   /* ('fmul', 'a', 2.0) => ('fadd', 'a', 'a') */
   { .variable = {
      { nir_search_value_variable, -1 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x4000000000000000ull /* 2.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmul,
      0, 1,
      { 0, 1 },
      -1,
   } },

   /* replace4_0 -> 0 in the cache */
   /* replace4_1 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      -1, 0,
      { 0, 0 },
      -1,
   } },

   /* ('fmin', ('fmax', 'a', -1.0), 1.0) => ('fsat_signed', 'a') */
   /* search5_0_0 -> 0 in the cache */
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0xbff0000000000000ull /* -1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmax,
      1, 1,
      { 0, 4 },
      -1,
   } },
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x3ff0000000000000ull /* 1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmin,
      0, 2,
      { 5, 6 },
      -1,
   } },

   /* replace5_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fsat_signed,
      -1, 0,
      { 0 },
      -1,
   } },

   /* ('fmax', ('fmin', 'a', 1.0), -1.0) => ('fsat_signed', 'a') */
   /* search6_0_0 -> 0 in the cache */
   /* search6_0_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmin,
      1, 1,
      { 0, 6 },
      -1,
   } },
   /* search6_1 -> 4 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmax,
      0, 2,
      { 9, 4 },
      -1,
   } },

   /* replace6_0 -> 0 in the cache */
   /* replace6 -> 8 in the cache */

   /* ('fmax', 'a', 0.0) => ('fclamp_pos', 'a') */
   /* search7_0 -> 0 in the cache */
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x0ull /* 0.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmax,
      0, 1,
      { 0, 11 },
      -1,
   } },

   /* replace7_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fclamp_pos,
      -1, 0,
      { 0 },
      -1,
   } },

   /* ('bcsel_pan', 'b@32', ('iadd', 'a@32', 1), 'a') => ('iadd', 'a', ('b2i32', 'b')) */
   { .variable = {
      { nir_search_value_variable, 32 },
      0, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 32 },
      1, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0x1ull /* 1 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_iadd,
      0, 1,
      { 15, 16 },
      -1,
   } },
   /* search8_2 -> 15 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel_pan,
      -1, 1,
      { 14, 17, 15 },
      -1,
   } },

   /* replace8_0 -> 15 in the cache */
   /* replace8_1_0 -> 14 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i32,
      -1, 0,
      { 14 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iadd,
      0, 1,
      { 15, 19 },
      -1,
   } },

   /* ('imin@8', 'a', 'b') => ('bcsel_pan', ('ilt_pan', 'a', 'b'), 'a', 'b') */
   { .variable = {
      { nir_search_value_variable, 8 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 8 },
      1, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_imin,
      0, 1,
      { 21, 22 },
      -1,
   } },

   /* replace9_0_0 -> 21 in the cache */
   /* replace9_0_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ilt_pan,
      -1, 0,
      { 21, 22 },
      -1,
   } },
   /* replace9_1 -> 21 in the cache */
   /* replace9_2 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 24, 21, 22 },
      -1,
   } },

   /* ('imax@8', 'a', 'b') => ('bcsel_pan', ('ilt_pan', 'a', 'b'), 'b', 'a') */
   /* search10_0 -> 21 in the cache */
   /* search10_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_imax,
      0, 1,
      { 21, 22 },
      -1,
   } },

   /* replace10_0_0 -> 21 in the cache */
   /* replace10_0_1 -> 22 in the cache */
   /* replace10_0 -> 24 in the cache */
   /* replace10_1 -> 22 in the cache */
   /* replace10_2 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 24, 22, 21 },
      -1,
   } },

   /* ('umin@8', 'a', 'b') => ('bcsel_pan', ('ult_pan', 'a', 'b'), 'a', 'b') */
   /* search11_0 -> 21 in the cache */
   /* search11_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_umin,
      0, 1,
      { 21, 22 },
      -1,
   } },

   /* replace11_0_0 -> 21 in the cache */
   /* replace11_0_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ult_pan,
      -1, 0,
      { 21, 22 },
      -1,
   } },
   /* replace11_1 -> 21 in the cache */
   /* replace11_2 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 29, 21, 22 },
      -1,
   } },

   /* ('umax@8', 'a', 'b') => ('bcsel_pan', ('ult_pan', 'a', 'b'), 'b', 'a') */
   /* search12_0 -> 21 in the cache */
   /* search12_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_umax,
      0, 1,
      { 21, 22 },
      -1,
   } },

   /* replace12_0_0 -> 21 in the cache */
   /* replace12_0_1 -> 22 in the cache */
   /* replace12_0 -> 29 in the cache */
   /* replace12_1 -> 22 in the cache */
   /* replace12_2 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 29, 22, 21 },
      -1,
   } },

   /* ('f2i8', 'a') => ('i2i8', ('f2i16', 'a')) */
   /* search13_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i8,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace13_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i16,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 0,
      { 34 },
      -1,
   } },

   /* ('f2u8', 'a') => ('u2u8', ('f2u16', 'a')) */
   /* search14_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u8,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace14_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u16,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u8,
      -1, 0,
      { 37 },
      -1,
   } },

   /* ('unpack_64_2x32', 'a') => ('vec2', ('unpack_64_2x32_split_x', 'a'), ('unpack_64_2x32_split_y', 'a')) */
   { .variable = {
      { nir_search_value_variable, 64 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_unpack_64_2x32,
      -1, 0,
      { 39 },
      -1,
   } },

   /* replace15_0_0 -> 39 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_unpack_64_2x32_split_x,
      -1, 0,
      { 39 },
      -1,
   } },
   /* replace15_1_0 -> 39 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_unpack_64_2x32_split_y,
      -1, 0,
      { 39 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_vec2,
      -1, 0,
      { 41, 42 },
      -1,
   } },

   /* ('i2f32', 'a@8') => ('i2f32', ('i2i32', 'a')) */
   /* search16_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f32,
      -1, 0,
      { 21 },
      -1,
   } },

   /* replace16_0_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 21 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f32,
      -1, 0,
      { 45 },
      -1,
   } },

   /* ('i2f32', 'a@16') => ('i2f32', ('i2i32', 'a')) */
   { .variable = {
      { nir_search_value_variable, 16 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f32,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace17_0_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 47 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f32,
      -1, 0,
      { 49 },
      -1,
   } },

   /* ('u2f32', 'a@8') => ('u2f32', ('u2u32', 'a')) */
   /* search18_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 21 },
      -1,
   } },

   /* replace18_0_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 21 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 52 },
      -1,
   } },

   /* ('u2f32', 'a@16') => ('u2f32', ('u2u32', 'a')) */
   /* search19_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace19_0_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 47 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 55 },
      -1,
   } },

   /* ('i2f16', 'a') => ('f2f16', ('i2f32', ('i2i32', 'a'))) */
   /* search20_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f16,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace20_0_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f32,
      -1, 0,
      { 58 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f16,
      -1, 0,
      { 59 },
      -1,
   } },

   /* ('u2f16', 'a') => ('f2f16', ('u2f32', ('u2u32', 'a'))) */
   /* search21_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f16,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace21_0_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 62 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f16,
      -1, 0,
      { 63 },
      -1,
   } },

   /* ('i2f16', 'a@32') => ('f2f16', ('i2f32', 'a')) */
   { .variable = {
      { nir_search_value_variable, 32 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f16,
      -1, 0,
      { 65 },
      -1,
   } },

   /* replace22_0_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f32,
      -1, 0,
      { 65 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f16,
      -1, 0,
      { 67 },
      -1,
   } },

   /* ('u2f16', 'a@32') => ('f2f16', ('u2f32', 'a')) */
   /* search23_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f16,
      -1, 0,
      { 65 },
      -1,
   } },

   /* replace23_0_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 65 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f16,
      -1, 0,
      { 70 },
      -1,
   } },

   /* ('f2i16', 'a@16') => ('f2i16', ('f2f32', 'a')) */
   /* search24_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i16,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace24_0_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f32,
      -1, 0,
      { 47 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i16,
      -1, 0,
      { 73 },
      -1,
   } },

   /* ('f2u16', 'a@16') => ('f2u16', ('f2f32', 'a')) */
   /* search25_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u16,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace25_0_0 -> 47 in the cache */
   /* replace25_0 -> 73 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u16,
      -1, 0,
      { 73 },
      -1,
   } },

   /* ('pack_half_2x16_split', 'a', 'b') => ('pack_32_2x16_split', ('f2f16', 'a'), ('f2f16', 'b')) */
   /* search26_0 -> 65 in the cache */
   { .variable = {
      { nir_search_value_variable, 32 },
      1, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_pack_half_2x16_split,
      -1, 0,
      { 65, 77 },
      -1,
   } },

   /* replace26_0_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f16,
      -1, 0,
      { 65 },
      -1,
   } },
   /* replace26_1_0 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f16,
      -1, 0,
      { 77 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_pack_32_2x16_split,
      -1, 0,
      { 79, 80 },
      -1,
   } },

   /* ('f2i32', 'a@16') => ('f2i32', ('f2f32', 'a')) */
   /* search27_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i32,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace27_0_0 -> 47 in the cache */
   /* replace27_0 -> 73 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i32,
      -1, 0,
      { 73 },
      -1,
   } },

   /* ('f2u32', 'a@16') => ('f2u32', ('f2f32', 'a')) */
   /* search28_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u32,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace28_0_0 -> 47 in the cache */
   /* replace28_0 -> 73 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u32,
      -1, 0,
      { 73 },
      -1,
   } },

   /* ('f2u16', 'a') => ('u2u16', ('f2u32', 'a')) */
   /* search29_0 -> 0 in the cache */
   /* search29 -> 37 in the cache */

   /* replace29_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u32,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u16,
      -1, 0,
      { 86 },
      -1,
   } },

   /* ('f2i16', 'a') => ('i2i16', ('f2i32', 'a')) */
   /* search30_0 -> 0 in the cache */
   /* search30 -> 34 in the cache */

   /* replace30_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i32,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 88 },
      -1,
   } },

   /* ('pack_uvec2_to_uint', 'a') => ('pack_32_2x16', ('u2u16', 'a')) */
   /* search31_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_pack_uvec2_to_uint,
      -1, 0,
      { 65 },
      -1,
   } },

   /* replace31_0_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u16,
      -1, 0,
      { 65 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_pack_32_2x16,
      -1, 0,
      { 91 },
      -1,
   } },

   /* ('pack_uvec4_to_uint', 'a') => ('pack_32_4x8', ('u2u8', 'a')) */
   /* search32_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_pack_uvec4_to_uint,
      -1, 0,
      { 65 },
      -1,
   } },

   /* replace32_0_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u8,
      -1, 0,
      { 65 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_pack_32_4x8,
      -1, 0,
      { 94 },
      -1,
   } },

   /* ('ffract', 'a') => ('fadd', 'a', ('fneg', ('ffloor', 'a'))) */
   /* search33_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ffract,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace33_0 -> 0 in the cache */
   /* replace33_1_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ffloor,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fneg,
      -1, 0,
      { 97 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      0, 1,
      { 0, 98 },
      -1,
   } },

   /* ('i2i8', 'a@8') => a */
   /* search34_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 0,
      { 21 },
      -1,
   } },

   /* replace34 -> 21 in the cache */

   /* ('i2i16', 'a@16') => a */
   /* search35_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace35 -> 47 in the cache */

   /* ('i2i32', 'a@32') => a */
   /* search36_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 65 },
      -1,
   } },

   /* replace36 -> 65 in the cache */

   /* ('ilt_pan@8', 'a', 'b') => ('i2i8', ('ilt_pan', ('i2i16', 'a'), ('i2i16', 'b'))) */
   /* search37_0 -> 21 in the cache */
   /* search37_1 -> 22 in the cache */
   /* search37 -> 24 in the cache */

   /* replace37_0_0_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 21 },
      -1,
   } },
   /* replace37_0_1_0 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 22 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ilt_pan,
      -1, 0,
      { 103, 104 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 0,
      { 105 },
      -1,
   } },

   /* ('ige_pan@8', 'a', 'b') => ('i2i8', ('ige_pan', ('i2i16', 'a'), ('i2i16', 'b'))) */
   /* search38_0 -> 21 in the cache */
   /* search38_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ige_pan,
      -1, 0,
      { 21, 22 },
      -1,
   } },

   /* replace38_0_0_0 -> 21 in the cache */
   /* replace38_0_0 -> 103 in the cache */
   /* replace38_0_1_0 -> 22 in the cache */
   /* replace38_0_1 -> 104 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ige_pan,
      -1, 0,
      { 103, 104 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 0,
      { 108 },
      -1,
   } },

   /* ('ieq_pan@8', 'a', 'b') => ('i2i8', ('ieq_pan', ('i2i16', 'a'), ('i2i16', 'b'))) */
   /* search39_0 -> 21 in the cache */
   /* search39_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ieq_pan,
      0, 1,
      { 21, 22 },
      -1,
   } },

   /* replace39_0_0_0 -> 21 in the cache */
   /* replace39_0_0 -> 103 in the cache */
   /* replace39_0_1_0 -> 22 in the cache */
   /* replace39_0_1 -> 104 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ieq_pan,
      0, 1,
      { 103, 104 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 1,
      { 111 },
      -1,
   } },

   /* ('ine_pan@8', 'a', 'b') => ('i2i8', ('ine_pan', ('i2i16', 'a'), ('i2i16', 'b'))) */
   /* search40_0 -> 21 in the cache */
   /* search40_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ine_pan,
      0, 1,
      { 21, 22 },
      -1,
   } },

   /* replace40_0_0_0 -> 21 in the cache */
   /* replace40_0_0 -> 103 in the cache */
   /* replace40_0_1_0 -> 22 in the cache */
   /* replace40_0_1 -> 104 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ine_pan,
      0, 1,
      { 103, 104 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 1,
      { 114 },
      -1,
   } },

   /* ('ult_pan@8', 'a', 'b') => ('u2u8', ('ult_pan', ('u2u16', 'a'), ('u2u16', 'b'))) */
   /* search41_0 -> 21 in the cache */
   /* search41_1 -> 22 in the cache */
   /* search41 -> 29 in the cache */

   /* replace41_0_0_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u16,
      -1, 0,
      { 21 },
      -1,
   } },
   /* replace41_0_1_0 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u16,
      -1, 0,
      { 22 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ult_pan,
      -1, 0,
      { 116, 117 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u8,
      -1, 0,
      { 118 },
      -1,
   } },

   /* ('uge_pan@8', 'a', 'b') => ('u2u8', ('uge_pan', ('u2u16', 'a'), ('u2u16', 'b'))) */
   /* search42_0 -> 21 in the cache */
   /* search42_1 -> 22 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uge_pan,
      -1, 0,
      { 21, 22 },
      -1,
   } },

   /* replace42_0_0_0 -> 21 in the cache */
   /* replace42_0_0 -> 116 in the cache */
   /* replace42_0_1_0 -> 22 in the cache */
   /* replace42_0_1 -> 117 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uge_pan,
      -1, 0,
      { 116, 117 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u8,
      -1, 0,
      { 121 },
      -1,
   } },

   /* ('b2f16', ('inot', 'a@16')) => ('bcsel_pan', 'a', 0.0, 1.0) */
   /* search43_0_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 47 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f16,
      -1, 0,
      { 123 },
      -1,
   } },

   /* replace43_0 -> 47 in the cache */
   { .constant = {
      { nir_search_value_constant, 16 },
      nir_type_float, { 0x0ull /* 0.0 */ },
   } },
   { .constant = {
      { nir_search_value_constant, 16 },
      nir_type_float, { 0x3ff0000000000000ull /* 1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 47, 125, 126 },
      -1,
   } },

   /* ('b2f16', ('inot', 'a')) => ('bcsel_pan', ('i2i16', 'a'), 0.0, 1.0) */
   /* search44_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 0 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f16,
      -1, 0,
      { 128 },
      -1,
   } },

   /* replace44_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 0 },
      -1,
   } },
   /* replace44_1 -> 125 in the cache */
   /* replace44_2 -> 126 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 130, 125, 126 },
      -1,
   } },

   /* ('b2f16', 'a@16') => ('bcsel_pan', 'a', 1.0, 0.0) */
   /* search45_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f16,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace45_0 -> 47 in the cache */
   /* replace45_1 -> 126 in the cache */
   /* replace45_2 -> 125 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 47, 126, 125 },
      -1,
   } },

   /* ('b2f16', 'a') => ('bcsel_pan', ('i2i16', 'a'), 1.0, 0.0) */
   /* search46_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f16,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace46_0_0 -> 0 in the cache */
   /* replace46_0 -> 130 in the cache */
   /* replace46_1 -> 126 in the cache */
   /* replace46_2 -> 125 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 130, 126, 125 },
      -1,
   } },

   /* ('b2f32', ('inot', 'a@32')) => ('bcsel_pan', 'a', 0.0, 1.0) */
   /* search47_0_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 65 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f32,
      -1, 0,
      { 136 },
      -1,
   } },

   /* replace47_0 -> 65 in the cache */
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_float, { 0x0ull /* 0.0 */ },
   } },
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_float, { 0x3ff0000000000000ull /* 1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 65, 138, 139 },
      -1,
   } },

   /* ('b2f32', ('inot', 'a')) => ('bcsel_pan', ('i2i32', 'a'), 0.0, 1.0) */
   /* search48_0_0 -> 0 in the cache */
   /* search48_0 -> 128 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f32,
      -1, 0,
      { 128 },
      -1,
   } },

   /* replace48_0_0 -> 0 in the cache */
   /* replace48_0 -> 58 in the cache */
   /* replace48_1 -> 138 in the cache */
   /* replace48_2 -> 139 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 58, 138, 139 },
      -1,
   } },

   /* ('b2f32', 'a@32') => ('bcsel_pan', 'a', 1.0, 0.0) */
   /* search49_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f32,
      -1, 0,
      { 65 },
      -1,
   } },

   /* replace49_0 -> 65 in the cache */
   /* replace49_1 -> 139 in the cache */
   /* replace49_2 -> 138 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 65, 139, 138 },
      -1,
   } },

   /* ('b2f32', 'a') => ('bcsel_pan', ('i2i32', 'a'), 1.0, 0.0) */
   /* search50_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2f32,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace50_0_0 -> 0 in the cache */
   /* replace50_0 -> 58 in the cache */
   /* replace50_1 -> 139 in the cache */
   /* replace50_2 -> 138 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 58, 139, 138 },
      -1,
   } },

   /* ('b2i8', ('inot', 'a@8')) => ('bcsel_pan', 'a', 0, 1) */
   /* search51_0_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 21 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i8,
      -1, 0,
      { 147 },
      -1,
   } },

   /* replace51_0 -> 21 in the cache */
   { .constant = {
      { nir_search_value_constant, 8 },
      nir_type_int, { 0x0ull /* 0 */ },
   } },
   { .constant = {
      { nir_search_value_constant, 8 },
      nir_type_int, { 0x1ull /* 1 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 21, 149, 150 },
      -1,
   } },

   /* ('b2i8', ('inot', 'a')) => ('bcsel_pan', ('i2i8', 'a'), 0, 1) */
   /* search52_0_0 -> 0 in the cache */
   /* search52_0 -> 128 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i8,
      -1, 0,
      { 128 },
      -1,
   } },

   /* replace52_0_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 0,
      { 0 },
      -1,
   } },
   /* replace52_1 -> 149 in the cache */
   /* replace52_2 -> 150 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 153, 149, 150 },
      -1,
   } },

   /* ('b2i8', 'a@8') => ('bcsel_pan', 'a', 1, 0) */
   /* search53_0 -> 21 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i8,
      -1, 0,
      { 21 },
      -1,
   } },

   /* replace53_0 -> 21 in the cache */
   /* replace53_1 -> 150 in the cache */
   /* replace53_2 -> 149 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 21, 150, 149 },
      -1,
   } },

   /* ('b2i8', 'a') => ('bcsel_pan', ('i2i8', 'a'), 1, 0) */
   /* search54_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i8,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace54_0_0 -> 0 in the cache */
   /* replace54_0 -> 153 in the cache */
   /* replace54_1 -> 150 in the cache */
   /* replace54_2 -> 149 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 153, 150, 149 },
      -1,
   } },

   /* ('b2i16', ('inot', 'a@16')) => ('bcsel_pan', 'a', 0, 1) */
   /* search55_0_0 -> 47 in the cache */
   /* search55_0 -> 123 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i16,
      -1, 0,
      { 123 },
      -1,
   } },

   /* replace55_0 -> 47 in the cache */
   { .constant = {
      { nir_search_value_constant, 16 },
      nir_type_int, { 0x0ull /* 0 */ },
   } },
   { .constant = {
      { nir_search_value_constant, 16 },
      nir_type_int, { 0x1ull /* 1 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 47, 160, 161 },
      -1,
   } },

   /* ('b2i16', ('inot', 'a')) => ('bcsel_pan', ('i2i16', 'a'), 0, 1) */
   /* search56_0_0 -> 0 in the cache */
   /* search56_0 -> 128 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i16,
      -1, 0,
      { 128 },
      -1,
   } },

   /* replace56_0_0 -> 0 in the cache */
   /* replace56_0 -> 130 in the cache */
   /* replace56_1 -> 160 in the cache */
   /* replace56_2 -> 161 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 130, 160, 161 },
      -1,
   } },

   /* ('b2i16', 'a@16') => ('bcsel_pan', 'a', 1, 0) */
   /* search57_0 -> 47 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i16,
      -1, 0,
      { 47 },
      -1,
   } },

   /* replace57_0 -> 47 in the cache */
   /* replace57_1 -> 161 in the cache */
   /* replace57_2 -> 160 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 47, 161, 160 },
      -1,
   } },

   /* ('b2i16', 'a') => ('bcsel_pan', ('i2i16', 'a'), 1, 0) */
   /* search58_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i16,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace58_0_0 -> 0 in the cache */
   /* replace58_0 -> 130 in the cache */
   /* replace58_1 -> 161 in the cache */
   /* replace58_2 -> 160 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 130, 161, 160 },
      -1,
   } },

   /* ('b2i32', ('inot', 'a@32')) => ('bcsel_pan', 'a', 0, 1) */
   /* search59_0_0 -> 65 in the cache */
   /* search59_0 -> 136 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i32,
      -1, 0,
      { 136 },
      -1,
   } },

   /* replace59_0 -> 65 in the cache */
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0x0ull /* 0 */ },
   } },
   /* replace59_2 -> 16 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 65, 170, 16 },
      -1,
   } },

   /* ('b2i32', ('inot', 'a')) => ('bcsel_pan', ('i2i32', 'a'), 0, 1) */
   /* search60_0_0 -> 0 in the cache */
   /* search60_0 -> 128 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i32,
      -1, 0,
      { 128 },
      -1,
   } },

   /* replace60_0_0 -> 0 in the cache */
   /* replace60_0 -> 58 in the cache */
   /* replace60_1 -> 170 in the cache */
   /* replace60_2 -> 16 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 58, 170, 16 },
      -1,
   } },

   /* ('b2i32', 'a@32') => ('bcsel_pan', 'a', 1, 0) */
   /* search61_0 -> 65 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i32,
      -1, 0,
      { 65 },
      -1,
   } },

   /* replace61_0 -> 65 in the cache */
   /* replace61_1 -> 16 in the cache */
   /* replace61_2 -> 170 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 65, 16, 170 },
      -1,
   } },

   /* ('b2i32', 'a') => ('bcsel_pan', ('i2i32', 'a'), 1, 0) */
   /* search62_0 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b2i32,
      -1, 0,
      { 0 },
      -1,
   } },

   /* replace62_0_0 -> 0 in the cache */
   /* replace62_0 -> 58 in the cache */
   /* replace62_1 -> 16 in the cache */
   /* replace62_2 -> 170 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_bcsel_pan,
      -1, 0,
      { 58, 16, 170 },
      -1,
   } },

   /* ('iand', ('ishl(is_used_once)', ('iand', 'a', '#b'), '#c'), '#d') => ('lshift_and_pan', 'a', ('u2u8', 'c'), ('iand', ('ishl', 'b', 'c'), 'd')) */
   { .variable = {
      { nir_search_value_variable, -4 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, -4 },
      1, /* b */
      true,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      1, 1,
      { 178, 179 },
      -1,
   } },
   { .variable = {
      { nir_search_value_variable, 32 },
      2, /* c */
      true,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 1,
      { 180, 181 },
      0,
   } },
   { .variable = {
      { nir_search_value_variable, -4 },
      3, /* d */
      true,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 2,
      { 182, 183 },
      -1,
   } },

   /* replace63_0 -> 178 in the cache */
   { .variable = {
      { nir_search_value_variable, 32 },
      2, /* c */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u8,
      -1, 0,
      { 185 },
      -1,
   } },
   { .variable = {
      { nir_search_value_variable, -4 },
      1, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   /* replace63_2_0_1 -> 185 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 187, 185 },
      -1,
   } },
   { .variable = {
      { nir_search_value_variable, -4 },
      3, /* d */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 188, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_and_pan,
      -1, 1,
      { 178, 186, 190 },
      -1,
   } },

   /* ('iand', ('ishl(is_used_once)', 'a', 'b'), 'c') => ('lshift_and_pan', 'a', ('u2u8', 'b'), 'c') */
   { .variable = {
      { nir_search_value_variable, -3 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   /* search64_0_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 192, 77 },
      0,
   } },
   { .variable = {
      { nir_search_value_variable, -3 },
      2, /* c */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 193, 194 },
      -1,
   } },

   /* replace64_0 -> 192 in the cache */
   /* replace64_1_0 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u8,
      -1, 0,
      { 77 },
      -1,
   } },
   /* replace64_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_and_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ishl', ('iand', 'a', '#b'), '#c') => ('lshift_and_pan', 'a', ('u2u8', 'c'), ('ishl', 'b', 'c')) */
   { .variable = {
      { nir_search_value_variable, -2 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, -2 },
      1, /* b */
      true,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 198, 199 },
      -1,
   } },
   /* search65_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 1,
      { 200, 181 },
      -1,
   } },

   /* replace65_0 -> 198 in the cache */
   /* replace65_1_0 -> 185 in the cache */
   /* replace65_1 -> 186 in the cache */
   { .variable = {
      { nir_search_value_variable, -2 },
      1, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   /* replace65_2_1 -> 185 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 202, 185 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_and_pan,
      -1, 0,
      { 198, 186, 203 },
      -1,
   } },

   /* ('ior', ('ishl(is_used_once)', ('ior', 'a', '#b'), '#c'), '#d') => ('lshift_or_pan', 'a', ('u2u8', 'c'), ('ior', ('ishl', 'b', 'c'), 'd')) */
   /* search66_0_0_0 -> 178 in the cache */
   /* search66_0_0_1 -> 179 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      1, 1,
      { 178, 179 },
      -1,
   } },
   /* search66_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 1,
      { 205, 181 },
      0,
   } },
   /* search66_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 2,
      { 206, 183 },
      -1,
   } },

   /* replace66_0 -> 178 in the cache */
   /* replace66_1_0 -> 185 in the cache */
   /* replace66_1 -> 186 in the cache */
   /* replace66_2_0_0 -> 187 in the cache */
   /* replace66_2_0_1 -> 185 in the cache */
   /* replace66_2_0 -> 188 in the cache */
   /* replace66_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 188, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_or_pan,
      -1, 1,
      { 178, 186, 208 },
      -1,
   } },

   /* ('ior', ('ishl(is_used_once)', 'a', 'b'), 'c') => ('lshift_or_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search67_0_0 -> 192 in the cache */
   /* search67_0_1 -> 77 in the cache */
   /* search67_0 -> 193 in the cache */
   /* search67_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 193, 194 },
      -1,
   } },

   /* replace67_0 -> 192 in the cache */
   /* replace67_1_0 -> 77 in the cache */
   /* replace67_1 -> 196 in the cache */
   /* replace67_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_or_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ishl', ('ior', 'a', '#b'), '#c') => ('lshift_or_pan', 'a', ('u2u8', 'c'), ('ishl', 'b', 'c')) */
   /* search68_0_0 -> 198 in the cache */
   /* search68_0_1 -> 199 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 198, 199 },
      -1,
   } },
   /* search68_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 1,
      { 212, 181 },
      -1,
   } },

   /* replace68_0 -> 198 in the cache */
   /* replace68_1_0 -> 185 in the cache */
   /* replace68_1 -> 186 in the cache */
   /* replace68_2_0 -> 202 in the cache */
   /* replace68_2_1 -> 185 in the cache */
   /* replace68_2 -> 203 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_or_pan,
      -1, 0,
      { 198, 186, 203 },
      -1,
   } },

   /* ('ixor', ('ishl(is_used_once)', ('ixor', 'a', '#b'), '#c'), '#d') => ('lshift_xor_pan', 'a', ('u2u8', 'c'), ('ixor', ('ishl', 'b', 'c'), 'd')) */
   /* search69_0_0_0 -> 178 in the cache */
   /* search69_0_0_1 -> 179 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      1, 1,
      { 178, 179 },
      -1,
   } },
   /* search69_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 1,
      { 215, 181 },
      0,
   } },
   /* search69_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 2,
      { 216, 183 },
      -1,
   } },

   /* replace69_0 -> 178 in the cache */
   /* replace69_1_0 -> 185 in the cache */
   /* replace69_1 -> 186 in the cache */
   /* replace69_2_0_0 -> 187 in the cache */
   /* replace69_2_0_1 -> 185 in the cache */
   /* replace69_2_0 -> 188 in the cache */
   /* replace69_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 188, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_xor_pan,
      -1, 1,
      { 178, 186, 218 },
      -1,
   } },

   /* ('ixor', ('ishl(is_used_once)', 'a', 'b'), 'c') => ('lshift_xor_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search70_0_0 -> 192 in the cache */
   /* search70_0_1 -> 77 in the cache */
   /* search70_0 -> 193 in the cache */
   /* search70_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 193, 194 },
      -1,
   } },

   /* replace70_0 -> 192 in the cache */
   /* replace70_1_0 -> 77 in the cache */
   /* replace70_1 -> 196 in the cache */
   /* replace70_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_xor_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ishl', ('ixor', 'a', '#b'), '#c') => ('lshift_xor_pan', 'a', ('u2u8', 'c'), ('ishl', 'b', 'c')) */
   /* search71_0_0 -> 198 in the cache */
   /* search71_0_1 -> 199 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 198, 199 },
      -1,
   } },
   /* search71_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 1,
      { 222, 181 },
      -1,
   } },

   /* replace71_0 -> 198 in the cache */
   /* replace71_1_0 -> 185 in the cache */
   /* replace71_1 -> 186 in the cache */
   /* replace71_2_0 -> 202 in the cache */
   /* replace71_2_1 -> 185 in the cache */
   /* replace71_2 -> 203 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_xor_pan,
      -1, 0,
      { 198, 186, 203 },
      -1,
   } },

   /* ('iand', ('ushr(is_used_once)', ('iand', 'a', '#b'), '#c'), '#d') => ('rshift_and_pan', 'a', ('u2u8', 'c'), ('iand', ('ushr', 'b', 'c'), 'd')) */
   /* search72_0_0_0 -> 178 in the cache */
   /* search72_0_0_1 -> 179 in the cache */
   /* search72_0_0 -> 180 in the cache */
   /* search72_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 1,
      { 180, 181 },
      0,
   } },
   /* search72_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 2,
      { 225, 183 },
      -1,
   } },

   /* replace72_0 -> 178 in the cache */
   /* replace72_1_0 -> 185 in the cache */
   /* replace72_1 -> 186 in the cache */
   /* replace72_2_0_0 -> 187 in the cache */
   /* replace72_2_0_1 -> 185 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 187, 185 },
      -1,
   } },
   /* replace72_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 227, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_and_pan,
      -1, 1,
      { 178, 186, 228 },
      -1,
   } },

   /* ('iand', ('ushr(is_used_once)', 'a', 'b'), 'c') => ('rshift_and_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search73_0_0 -> 192 in the cache */
   /* search73_0_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 192, 77 },
      0,
   } },
   /* search73_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 230, 194 },
      -1,
   } },

   /* replace73_0 -> 192 in the cache */
   /* replace73_1_0 -> 77 in the cache */
   /* replace73_1 -> 196 in the cache */
   /* replace73_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_and_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ushr', ('iand', 'a', '#b'), '#c') => ('rshift_and_pan', 'a', ('u2u8', 'c'), ('ushr', 'b', 'c')) */
   /* search74_0_0 -> 198 in the cache */
   /* search74_0_1 -> 199 in the cache */
   /* search74_0 -> 200 in the cache */
   /* search74_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 1,
      { 200, 181 },
      -1,
   } },

   /* replace74_0 -> 198 in the cache */
   /* replace74_1_0 -> 185 in the cache */
   /* replace74_1 -> 186 in the cache */
   /* replace74_2_0 -> 202 in the cache */
   /* replace74_2_1 -> 185 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 202, 185 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_and_pan,
      -1, 0,
      { 198, 186, 234 },
      -1,
   } },

   /* ('ior', ('ushr(is_used_once)', ('ior', 'a', '#b'), '#c'), '#d') => ('rshift_or_pan', 'a', ('u2u8', 'c'), ('ior', ('ushr', 'b', 'c'), 'd')) */
   /* search75_0_0_0 -> 178 in the cache */
   /* search75_0_0_1 -> 179 in the cache */
   /* search75_0_0 -> 205 in the cache */
   /* search75_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 1,
      { 205, 181 },
      0,
   } },
   /* search75_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 2,
      { 236, 183 },
      -1,
   } },

   /* replace75_0 -> 178 in the cache */
   /* replace75_1_0 -> 185 in the cache */
   /* replace75_1 -> 186 in the cache */
   /* replace75_2_0_0 -> 187 in the cache */
   /* replace75_2_0_1 -> 185 in the cache */
   /* replace75_2_0 -> 227 in the cache */
   /* replace75_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 227, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_or_pan,
      -1, 1,
      { 178, 186, 238 },
      -1,
   } },

   /* ('ior', ('ushr(is_used_once)', 'a', 'b'), 'c') => ('rshift_or_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search76_0_0 -> 192 in the cache */
   /* search76_0_1 -> 77 in the cache */
   /* search76_0 -> 230 in the cache */
   /* search76_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 230, 194 },
      -1,
   } },

   /* replace76_0 -> 192 in the cache */
   /* replace76_1_0 -> 77 in the cache */
   /* replace76_1 -> 196 in the cache */
   /* replace76_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_or_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ushr', ('ior', 'a', '#b'), '#c') => ('rshift_or_pan', 'a', ('u2u8', 'c'), ('ushr', 'b', 'c')) */
   /* search77_0_0 -> 198 in the cache */
   /* search77_0_1 -> 199 in the cache */
   /* search77_0 -> 212 in the cache */
   /* search77_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 1,
      { 212, 181 },
      -1,
   } },

   /* replace77_0 -> 198 in the cache */
   /* replace77_1_0 -> 185 in the cache */
   /* replace77_1 -> 186 in the cache */
   /* replace77_2_0 -> 202 in the cache */
   /* replace77_2_1 -> 185 in the cache */
   /* replace77_2 -> 234 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_or_pan,
      -1, 0,
      { 198, 186, 234 },
      -1,
   } },

   /* ('ixor', ('ushr(is_used_once)', ('ixor', 'a', '#b'), '#c'), '#d') => ('rshift_xor_pan', 'a', ('u2u8', 'c'), ('ixor', ('ushr', 'b', 'c'), 'd')) */
   /* search78_0_0_0 -> 178 in the cache */
   /* search78_0_0_1 -> 179 in the cache */
   /* search78_0_0 -> 215 in the cache */
   /* search78_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 1,
      { 215, 181 },
      0,
   } },
   /* search78_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 2,
      { 244, 183 },
      -1,
   } },

   /* replace78_0 -> 178 in the cache */
   /* replace78_1_0 -> 185 in the cache */
   /* replace78_1 -> 186 in the cache */
   /* replace78_2_0_0 -> 187 in the cache */
   /* replace78_2_0_1 -> 185 in the cache */
   /* replace78_2_0 -> 227 in the cache */
   /* replace78_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 227, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_xor_pan,
      -1, 1,
      { 178, 186, 246 },
      -1,
   } },

   /* ('ixor', ('ushr(is_used_once)', 'a', 'b'), 'c') => ('rshift_xor_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search79_0_0 -> 192 in the cache */
   /* search79_0_1 -> 77 in the cache */
   /* search79_0 -> 230 in the cache */
   /* search79_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 230, 194 },
      -1,
   } },

   /* replace79_0 -> 192 in the cache */
   /* replace79_1_0 -> 77 in the cache */
   /* replace79_1 -> 196 in the cache */
   /* replace79_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_xor_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ushr', ('ixor', 'a', '#b'), '#c') => ('rshift_xor_pan', 'a', ('u2u8', 'c'), ('ushr', 'b', 'c')) */
   /* search80_0_0 -> 198 in the cache */
   /* search80_0_1 -> 199 in the cache */
   /* search80_0 -> 222 in the cache */
   /* search80_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 1,
      { 222, 181 },
      -1,
   } },

   /* replace80_0 -> 198 in the cache */
   /* replace80_1_0 -> 185 in the cache */
   /* replace80_1 -> 186 in the cache */
   /* replace80_2_0 -> 202 in the cache */
   /* replace80_2_1 -> 185 in the cache */
   /* replace80_2 -> 234 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_xor_pan,
      -1, 0,
      { 198, 186, 234 },
      -1,
   } },

   /* ('iand', ('ishr(is_used_once)', ('iand', 'a', '#b'), '#c'), '#d') => ('arshift_and_pan', 'a', ('u2u8', 'c'), ('iand', ('ishr', 'b', 'c'), 'd')) */
   /* search81_0_0_0 -> 178 in the cache */
   /* search81_0_0_1 -> 179 in the cache */
   /* search81_0_0 -> 180 in the cache */
   /* search81_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 1,
      { 180, 181 },
      0,
   } },
   /* search81_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 2,
      { 252, 183 },
      -1,
   } },

   /* replace81_0 -> 178 in the cache */
   /* replace81_1_0 -> 185 in the cache */
   /* replace81_1 -> 186 in the cache */
   /* replace81_2_0_0 -> 187 in the cache */
   /* replace81_2_0_1 -> 185 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 187, 185 },
      -1,
   } },
   /* replace81_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 254, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_and_pan,
      -1, 1,
      { 178, 186, 255 },
      -1,
   } },

   /* ('iand', ('ishr(is_used_once)', 'a', 'b'), 'c') => ('arshift_and_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search82_0_0 -> 192 in the cache */
   /* search82_0_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 192, 77 },
      0,
   } },
   /* search82_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 257, 194 },
      -1,
   } },

   /* replace82_0 -> 192 in the cache */
   /* replace82_1_0 -> 77 in the cache */
   /* replace82_1 -> 196 in the cache */
   /* replace82_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_and_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ishr', ('iand', 'a', '#b'), '#c') => ('arshift_and_pan', 'a', ('u2u8', 'c'), ('ishr', 'b', 'c')) */
   /* search83_0_0 -> 198 in the cache */
   /* search83_0_1 -> 199 in the cache */
   /* search83_0 -> 200 in the cache */
   /* search83_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 1,
      { 200, 181 },
      -1,
   } },

   /* replace83_0 -> 198 in the cache */
   /* replace83_1_0 -> 185 in the cache */
   /* replace83_1 -> 186 in the cache */
   /* replace83_2_0 -> 202 in the cache */
   /* replace83_2_1 -> 185 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 202, 185 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_and_pan,
      -1, 0,
      { 198, 186, 261 },
      -1,
   } },

   /* ('ior', ('ishr(is_used_once)', ('ior', 'a', '#b'), '#c'), '#d') => ('arshift_or_pan', 'a', ('u2u8', 'c'), ('ior', ('ishr', 'b', 'c'), 'd')) */
   /* search84_0_0_0 -> 178 in the cache */
   /* search84_0_0_1 -> 179 in the cache */
   /* search84_0_0 -> 205 in the cache */
   /* search84_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 1,
      { 205, 181 },
      0,
   } },
   /* search84_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 2,
      { 263, 183 },
      -1,
   } },

   /* replace84_0 -> 178 in the cache */
   /* replace84_1_0 -> 185 in the cache */
   /* replace84_1 -> 186 in the cache */
   /* replace84_2_0_0 -> 187 in the cache */
   /* replace84_2_0_1 -> 185 in the cache */
   /* replace84_2_0 -> 254 in the cache */
   /* replace84_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 254, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_or_pan,
      -1, 1,
      { 178, 186, 265 },
      -1,
   } },

   /* ('ior', ('ishr(is_used_once)', 'a', 'b'), 'c') => ('arshift_or_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search85_0_0 -> 192 in the cache */
   /* search85_0_1 -> 77 in the cache */
   /* search85_0 -> 257 in the cache */
   /* search85_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 257, 194 },
      -1,
   } },

   /* replace85_0 -> 192 in the cache */
   /* replace85_1_0 -> 77 in the cache */
   /* replace85_1 -> 196 in the cache */
   /* replace85_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_or_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ishr', ('ior', 'a', '#b'), '#c') => ('arshift_or_pan', 'a', ('u2u8', 'c'), ('ishr', 'b', 'c')) */
   /* search86_0_0 -> 198 in the cache */
   /* search86_0_1 -> 199 in the cache */
   /* search86_0 -> 212 in the cache */
   /* search86_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 1,
      { 212, 181 },
      -1,
   } },

   /* replace86_0 -> 198 in the cache */
   /* replace86_1_0 -> 185 in the cache */
   /* replace86_1 -> 186 in the cache */
   /* replace86_2_0 -> 202 in the cache */
   /* replace86_2_1 -> 185 in the cache */
   /* replace86_2 -> 261 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_or_pan,
      -1, 0,
      { 198, 186, 261 },
      -1,
   } },

   /* ('ixor', ('ishr(is_used_once)', ('ixor', 'a', '#b'), '#c'), '#d') => ('arshift_xor_pan', 'a', ('u2u8', 'c'), ('ixor', ('ishr', 'b', 'c'), 'd')) */
   /* search87_0_0_0 -> 178 in the cache */
   /* search87_0_0_1 -> 179 in the cache */
   /* search87_0_0 -> 215 in the cache */
   /* search87_0_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 1,
      { 215, 181 },
      0,
   } },
   /* search87_1 -> 183 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 2,
      { 271, 183 },
      -1,
   } },

   /* replace87_0 -> 178 in the cache */
   /* replace87_1_0 -> 185 in the cache */
   /* replace87_1 -> 186 in the cache */
   /* replace87_2_0_0 -> 187 in the cache */
   /* replace87_2_0_1 -> 185 in the cache */
   /* replace87_2_0 -> 254 in the cache */
   /* replace87_2_1 -> 189 in the cache */
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 254, 189 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -4 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_xor_pan,
      -1, 1,
      { 178, 186, 273 },
      -1,
   } },

   /* ('ixor', ('ishr(is_used_once)', 'a', 'b'), 'c') => ('arshift_xor_pan', 'a', ('u2u8', 'b'), 'c') */
   /* search88_0_0 -> 192 in the cache */
   /* search88_0_1 -> 77 in the cache */
   /* search88_0 -> 257 in the cache */
   /* search88_1 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 257, 194 },
      -1,
   } },

   /* replace88_0 -> 192 in the cache */
   /* replace88_1_0 -> 77 in the cache */
   /* replace88_1 -> 196 in the cache */
   /* replace88_2 -> 194 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_xor_pan,
      -1, 0,
      { 192, 196, 194 },
      -1,
   } },

   /* ('ishr', ('ixor', 'a', '#b'), '#c') => ('arshift_xor_pan', 'a', ('u2u8', 'c'), ('ishr', 'b', 'c')) */
   /* search89_0_0 -> 198 in the cache */
   /* search89_0_1 -> 199 in the cache */
   /* search89_0 -> 222 in the cache */
   /* search89_1 -> 181 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 1,
      { 222, 181 },
      -1,
   } },

   /* replace89_0 -> 198 in the cache */
   /* replace89_1_0 -> 185 in the cache */
   /* replace89_1 -> 186 in the cache */
   /* replace89_2_0 -> 202 in the cache */
   /* replace89_2_1 -> 185 in the cache */
   /* replace89_2 -> 261 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_xor_pan,
      -1, 0,
      { 198, 186, 261 },
      -1,
   } },

   /* ('iand', 'a', 'b') => ('lshift_and_pan', 'a', 0, 'b') */
   /* search90_0 -> 198 in the cache */
   /* search90_1 -> 202 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 198, 202 },
      -1,
   } },

   /* replace90_0 -> 198 in the cache */
   /* replace90_1 -> 149 in the cache */
   /* replace90_2 -> 202 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_lshift_and_pan,
      -1, 0,
      { 198, 149, 202 },
      -1,
   } },

   /* ('ior', 'a', 'b') => ('lshift_or_pan', 'a', 0, 'b') */
   /* search91_0 -> 198 in the cache */
   /* search91_1 -> 202 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ior,
      0, 1,
      { 198, 202 },
      -1,
   } },

   /* replace91_0 -> 198 in the cache */
   /* replace91_1 -> 149 in the cache */
   /* replace91_2 -> 202 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_lshift_or_pan,
      -1, 0,
      { 198, 149, 202 },
      -1,
   } },

   /* ('ixor', 'a', 'b') => ('lshift_xor_pan', 'a', 0, 'b') */
   /* search92_0 -> 198 in the cache */
   /* search92_1 -> 202 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ixor,
      0, 1,
      { 198, 202 },
      -1,
   } },

   /* replace92_0 -> 198 in the cache */
   /* replace92_1 -> 149 in the cache */
   /* replace92_2 -> 202 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_lshift_xor_pan,
      -1, 0,
      { 198, 149, 202 },
      -1,
   } },

   /* ('inot', 'a') => ('lshift_xor_pan', 'a', 0, -1) */
   /* search93_0 -> 0 in the cache */
   /* search93 -> 128 in the cache */

   /* replace93_0 -> 0 in the cache */
   /* replace93_1 -> 149 in the cache */
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_int, { 0xffffffffffffffffull /* -1 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_lshift_xor_pan,
      -1, 0,
      { 0, 149, 285 },
      -1,
   } },

   /* ('ishl', 'a', 'b') => ('lshift_or_pan', 'a', ('u2u8', 'b'), 0) */
   /* search94_0 -> 0 in the cache */
   /* search94_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 0, 77 },
      -1,
   } },

   /* replace94_0 -> 0 in the cache */
   /* replace94_1_0 -> 77 in the cache */
   /* replace94_1 -> 196 in the cache */
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_int, { 0x0ull /* 0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lshift_or_pan,
      -1, 0,
      { 0, 196, 288 },
      -1,
   } },

   /* ('ushr', 'a', 'b') => ('rshift_or_pan', 'a', ('u2u8', 'b'), 0) */
   /* search95_0 -> 0 in the cache */
   /* search95_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 0, 77 },
      -1,
   } },

   /* replace95_0 -> 0 in the cache */
   /* replace95_1_0 -> 77 in the cache */
   /* replace95_1 -> 196 in the cache */
   /* replace95_2 -> 288 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rshift_or_pan,
      -1, 0,
      { 0, 196, 288 },
      -1,
   } },

   /* ('ishr', 'a', 'b') => ('arshift_or_pan', 'a', ('u2u8', 'b'), 0) */
   /* search96_0 -> 0 in the cache */
   /* search96_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 0, 77 },
      -1,
   } },

   /* replace96_0 -> 0 in the cache */
   /* replace96_1_0 -> 77 in the cache */
   /* replace96_1 -> 196 in the cache */
   /* replace96_2 -> 288 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_arshift_or_pan,
      -1, 0,
      { 0, 196, 288 },
      -1,
   } },

   /* ('urol', 'a', 'b') => ('lrot_or_pan', 'a', ('u2u8', 'b'), 0) */
   /* search97_0 -> 0 in the cache */
   /* search97_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_urol,
      -1, 0,
      { 0, 77 },
      -1,
   } },

   /* replace97_0 -> 0 in the cache */
   /* replace97_1_0 -> 77 in the cache */
   /* replace97_1 -> 196 in the cache */
   /* replace97_2 -> 288 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_lrot_or_pan,
      -1, 0,
      { 0, 196, 288 },
      -1,
   } },

   /* ('uror', 'a', 'b') => ('rrot_or_pan', 'a', ('u2u8', 'b'), 0) */
   /* search98_0 -> 0 in the cache */
   /* search98_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uror,
      -1, 0,
      { 0, 77 },
      -1,
   } },

   /* replace98_0 -> 0 in the cache */
   /* replace98_1_0 -> 77 in the cache */
   /* replace98_1 -> 196 in the cache */
   /* replace98_2 -> 288 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_rrot_or_pan,
      -1, 0,
      { 0, 196, 288 },
      -1,
   } },

   /* ('imul_high', 'a@32', 'b@32') => ('unpack_64_2x32_split_y', ('imul_2x32_64', 'a', 'b')) */
   /* search99_0 -> 65 in the cache */
   /* search99_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_imul_high,
      0, 1,
      { 65, 77 },
      -1,
   } },

   /* replace99_0_0 -> 65 in the cache */
   /* replace99_0_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_imul_2x32_64,
      0, 1,
      { 65, 77 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_unpack_64_2x32_split_y,
      -1, 1,
      { 299 },
      -1,
   } },

   /* ('umul_high', 'a@32', 'b@32') => ('unpack_64_2x32_split_y', ('umul_2x32_64', 'a', 'b')) */
   /* search100_0 -> 65 in the cache */
   /* search100_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_umul_high,
      0, 1,
      { 65, 77 },
      -1,
   } },

   /* replace100_0_0 -> 65 in the cache */
   /* replace100_0_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_umul_2x32_64,
      0, 1,
      { 65, 77 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_unpack_64_2x32_split_y,
      -1, 1,
      { 302 },
      -1,
   } },

   /* ('ldexp', 'a@16', 'b') => ('ldexp16_pan', 'a', ('i2i16', ('imin', ('imax', 'b', -127), 127))) */
   /* search101_0 -> 47 in the cache */
   /* search101_1 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ldexp,
      -1, 0,
      { 47, 77 },
      -1,
   } },

   /* replace101_0 -> 47 in the cache */
   /* replace101_1_0_0_0 -> 77 in the cache */
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0xffffffffffffff81ull /* -127 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_imax,
      1, 1,
      { 77, 305 },
      -1,
   } },
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0x7full /* 127 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_imin,
      0, 2,
      { 306, 307 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 2,
      { 308 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ldexp16_pan,
      -1, 2,
      { 47, 309 },
      -1,
   } },

};

UNUSED static const nir_search_expression_cond bifrost_nir_lower_algebraic_late_expression_cond[] = {
   is_used_once,
};


static const struct transform bifrost_nir_lower_algebraic_late_transforms[] = {
   { ~0, ~0, ~0 }, /* Sentinel */

   { 2, 3, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 12, 13, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 23, 25, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 26, 27, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 28, 30, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 31, 32, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 33, 35, 0 },
   { 72, 74, 1 },
   { 82, 83, 1 },
   { 34, 89, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 36, 38, 0 },
   { 75, 76, 1 },
   { 84, 85, 1 },
   { 37, 87, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 40, 43, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 44, 46, 1 },
   { 48, 50, 1 },
   { 57, 60, 1 },
   { 66, 68, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 51, 53, 1 },
   { 54, 56, 1 },
   { 61, 64, 1 },
   { 69, 71, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 78, 81, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 90, 92, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 93, 95, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 96, 99, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 100, 21, 0 },
   { 101, 47, 0 },
   { 102, 65, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 24, 106, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 107, 109, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 110, 112, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 113, 115, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 29, 119, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 120, 122, 1 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 132, 133, 0 },
   { 134, 135, 0 },
   { 143, 144, 0 },
   { 145, 146, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 128, 286, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 155, 156, 2 },
   { 157, 158, 2 },
   { 165, 166, 2 },
   { 167, 168, 2 },
   { 174, 175, 2 },
   { 176, 177, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 287, 289, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 290, 291, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 292, 293, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 294, 295, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 296, 297, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 298, 300, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 301, 303, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 304, 310, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 10, 8, 0 },
   { 12, 13, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 7, 8, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 18, 20, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 124, 127, 0 },
   { 129, 131, 0 },
   { 132, 133, 0 },
   { 134, 135, 0 },
   { 137, 140, 0 },
   { 141, 142, 0 },
   { 143, 144, 0 },
   { 145, 146, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 148, 151, 2 },
   { 152, 154, 2 },
   { 155, 156, 2 },
   { 157, 158, 2 },
   { 159, 162, 2 },
   { 163, 164, 2 },
   { 165, 166, 2 },
   { 167, 168, 2 },
   { 169, 171, 2 },
   { 172, 173, 2 },
   { 174, 175, 2 },
   { 176, 177, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 201, 204, 2 },
   { 287, 289, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 213, 214, 2 },
   { 287, 289, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 223, 224, 2 },
   { 287, 289, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 233, 235, 2 },
   { 290, 291, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 242, 243, 2 },
   { 290, 291, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 250, 251, 2 },
   { 290, 291, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 260, 262, 2 },
   { 292, 293, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 269, 270, 2 },
   { 292, 293, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 277, 278, 2 },
   { 292, 293, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 195, 197, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 231, 232, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 258, 259, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 195, 197, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 231, 232, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 258, 259, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 195, 197, 2 },
   { 231, 232, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 195, 197, 2 },
   { 258, 259, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 231, 232, 2 },
   { 258, 259, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 210, 211, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 240, 241, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 267, 268, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 210, 211, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 240, 241, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 267, 268, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 210, 211, 2 },
   { 240, 241, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 210, 211, 2 },
   { 267, 268, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 240, 241, 2 },
   { 267, 268, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 220, 221, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 248, 249, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 275, 276, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 220, 221, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 248, 249, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 275, 276, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 220, 221, 2 },
   { 248, 249, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 220, 221, 2 },
   { 275, 276, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 248, 249, 2 },
   { 275, 276, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 184, 191, 2 },
   { 195, 197, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 226, 229, 2 },
   { 231, 232, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 253, 256, 2 },
   { 258, 259, 2 },
   { 279, 280, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 207, 209, 2 },
   { 210, 211, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 237, 239, 2 },
   { 240, 241, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 264, 266, 2 },
   { 267, 268, 2 },
   { 281, 282, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 217, 219, 2 },
   { 220, 221, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 245, 247, 2 },
   { 248, 249, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 272, 274, 2 },
   { 275, 276, 2 },
   { 283, 284, 2 },
   { ~0, ~0, ~0 }, /* Sentinel */

};

static const struct per_op_table bifrost_nir_lower_algebraic_late_pass_op_table[nir_num_search_ops] = {
   [nir_op_fmul] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         2,
         2,
         2,
      },
   },
   [nir_op_fmin] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 3,
      .table = (const uint16_t []) {
      
         0,
         3,
         0,
         3,
         3,
         44,
         0,
         44,
         0,
      },
   },
   [nir_op_fmax] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 3,
      .table = (const uint16_t []) {
      
         0,
         4,
         0,
         4,
         4,
         43,
         0,
         43,
         0,
      },
   },
   [nir_op_bcsel_pan] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         0,
         45,
         45,
         0,
         0,
         45,
         45,
      },
   },
   [nir_op_iadd] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         5,
         5,
         5,
      },
   },
   [nir_op_imin] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         6,
      },
   },
   [nir_op_imax] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         7,
      },
   },
   [nir_op_umin] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         8,
      },
   },
   [nir_op_umax] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         9,
      },
   },
   [nir_search_op_f2i] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         10,
      },
   },
   [nir_search_op_f2u] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         11,
      },
   },
   [nir_op_unpack_64_2x32] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         12,
      },
   },
   [nir_search_op_i2f] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         13,
      },
   },
   [nir_search_op_u2f] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         14,
      },
   },
   [nir_op_pack_half_2x16_split] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         15,
      },
   },
   [nir_op_pack_uvec2_to_uint] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         16,
      },
   },
   [nir_op_pack_uvec4_to_uint] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         17,
      },
   },
   [nir_op_ffract] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         18,
      },
   },
   [nir_search_op_i2i] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         19,
      },
   },
   [nir_op_ilt_pan] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         20,
      },
   },
   [nir_op_ige_pan] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         21,
      },
   },
   [nir_op_ieq_pan] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         22,
      },
   },
   [nir_op_ine_pan] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         23,
      },
   },
   [nir_op_ult_pan] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         24,
      },
   },
   [nir_op_uge_pan] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         25,
      },
   },
   [nir_search_op_b2f] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         26,
         46,
      },
   },
   [nir_op_inot] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         27,
      },
   },
   [nir_search_op_b2i] = {
      .filter = (const uint16_t []) {
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         28,
         47,
      },
   },
   [nir_op_iand] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         3,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         5,
         2,
         2,
         6,
         3,
         3,
         7,
         4,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 8,
      .table = (const uint16_t []) {
      
         29,
         30,
         57,
         58,
         59,
         57,
         58,
         59,
         30,
         30,
         60,
         61,
         62,
         84,
         85,
         86,
         57,
         60,
         57,
         63,
         64,
         57,
         63,
         64,
         58,
         61,
         63,
         58,
         65,
         63,
         58,
         65,
         59,
         62,
         64,
         65,
         59,
         64,
         65,
         59,
         57,
         84,
         57,
         63,
         64,
         57,
         63,
         64,
         58,
         85,
         63,
         58,
         65,
         63,
         58,
         65,
         59,
         86,
         64,
         65,
         59,
         64,
         65,
         59,
      },
   },
   [nir_op_ishl] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         3,
         0,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         2,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         3,
         3,
         3,
         0,
         0,
         0,
         0,
         0,
         0,
         4,
         4,
         4,
         0,
         0,
         0,
         2,
         2,
         2,
         3,
         3,
         3,
         4,
         4,
         4,
      },
      
      .num_filtered_states = 5,
      .table = (const uint16_t []) {
      
         31,
         31,
         31,
         31,
         31,
         31,
         31,
         31,
         31,
         31,
         31,
         48,
         31,
         31,
         31,
         31,
         49,
         31,
         31,
         31,
         31,
         50,
         31,
         31,
         31,
      },
   },
   [nir_op_ior] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         3,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         5,
         2,
         3,
         6,
         3,
         4,
         7,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 8,
      .table = (const uint16_t []) {
      
         32,
         33,
         66,
         67,
         68,
         66,
         67,
         68,
         33,
         33,
         69,
         70,
         71,
         87,
         88,
         89,
         66,
         69,
         66,
         72,
         73,
         66,
         72,
         73,
         67,
         70,
         72,
         67,
         74,
         72,
         67,
         74,
         68,
         71,
         73,
         74,
         68,
         73,
         74,
         68,
         66,
         87,
         66,
         72,
         73,
         66,
         72,
         73,
         67,
         88,
         72,
         67,
         74,
         72,
         67,
         74,
         68,
         89,
         73,
         74,
         68,
         73,
         74,
         68,
      },
   },
   [nir_op_ixor] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         0,
         0,
         3,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         2,
         5,
         3,
         3,
         6,
         4,
         4,
         7,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
      },
      
      .num_filtered_states = 8,
      .table = (const uint16_t []) {
      
         34,
         35,
         75,
         76,
         77,
         75,
         76,
         77,
         35,
         35,
         78,
         79,
         80,
         90,
         91,
         92,
         75,
         78,
         75,
         81,
         82,
         75,
         81,
         82,
         76,
         79,
         81,
         76,
         83,
         81,
         76,
         83,
         77,
         80,
         82,
         83,
         77,
         82,
         83,
         77,
         75,
         90,
         75,
         81,
         82,
         75,
         81,
         82,
         76,
         91,
         81,
         76,
         83,
         81,
         76,
         83,
         77,
         92,
         82,
         83,
         77,
         82,
         83,
         77,
      },
   },
   [nir_op_ushr] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         3,
         0,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         2,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         3,
         3,
         3,
         0,
         0,
         0,
         0,
         0,
         0,
         4,
         4,
         4,
         0,
         0,
         0,
         2,
         2,
         2,
         3,
         3,
         3,
         4,
         4,
         4,
      },
      
      .num_filtered_states = 5,
      .table = (const uint16_t []) {
      
         36,
         36,
         36,
         36,
         36,
         36,
         36,
         36,
         36,
         36,
         36,
         51,
         36,
         36,
         36,
         36,
         52,
         36,
         36,
         36,
         36,
         53,
         36,
         36,
         36,
      },
   },
   [nir_op_ishr] = {
      .filter = (const uint16_t []) {
         0,
         1,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         0,
         0,
         3,
         0,
         4,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         0,
         2,
         2,
         2,
         0,
         0,
         0,
         0,
         0,
         0,
         3,
         3,
         3,
         0,
         0,
         0,
         0,
         0,
         0,
         4,
         4,
         4,
         0,
         0,
         0,
         2,
         2,
         2,
         3,
         3,
         3,
         4,
         4,
         4,
      },
      
      .num_filtered_states = 5,
      .table = (const uint16_t []) {
      
         37,
         37,
         37,
         37,
         37,
         37,
         37,
         37,
         37,
         37,
         37,
         54,
         37,
         37,
         37,
         37,
         55,
         37,
         37,
         37,
         37,
         56,
         37,
         37,
         37,
      },
   },
   [nir_op_urol] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         38,
      },
   },
   [nir_op_uror] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         39,
      },
   },
   [nir_op_imul_high] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         40,
      },
   },
   [nir_op_umul_high] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         41,
      },
   },
   [nir_op_ldexp] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         42,
      },
   },
};

/* Mapping from state index to offset in transforms (0 being no transforms) */
static const uint16_t bifrost_nir_lower_algebraic_late_transform_offsets[] = {
   0,
   0,
   1,
   0,
   3,
   0,
   5,
   7,
   9,
   11,
   13,
   18,
   23,
   25,
   30,
   35,
   37,
   39,
   41,
   43,
   47,
   49,
   51,
   53,
   55,
   57,
   59,
   64,
   66,
   73,
   75,
   77,
   79,
   81,
   83,
   85,
   87,
   89,
   91,
   93,
   95,
   97,
   99,
   101,
   104,
   106,
   108,
   117,
   130,
   133,
   136,
   139,
   142,
   145,
   148,
   151,
   154,
   157,
   160,
   163,
   166,
   169,
   172,
   175,
   179,
   183,
   187,
   190,
   193,
   196,
   199,
   202,
   205,
   209,
   213,
   217,
   220,
   223,
   226,
   229,
   232,
   235,
   239,
   243,
   247,
   251,
   255,
   259,
   263,
   267,
   271,
   275,
   279,
};

static const nir_algebraic_table bifrost_nir_lower_algebraic_late_table = {
   .transforms = bifrost_nir_lower_algebraic_late_transforms,
   .transform_offsets = bifrost_nir_lower_algebraic_late_transform_offsets,
   .pass_op_table = bifrost_nir_lower_algebraic_late_pass_op_table,
   .values = bifrost_nir_lower_algebraic_late_values,
   .expression_cond = bifrost_nir_lower_algebraic_late_expression_cond,
   .variable_cond = NULL,
};

bool
bifrost_nir_lower_algebraic_late(
   nir_shader *shader
   , unsigned  gpu_arch
   , bool  is_kraid
) {
   bool progress = false;
   bool condition_flags[3];
   const nir_shader_compiler_options *options = shader->options;
   const shader_info *info = &shader->info;
   (void) options;
   (void) info;

   STATIC_ASSERT(311 == ARRAY_SIZE(bifrost_nir_lower_algebraic_late_values));
   condition_flags[0] = true;
   condition_flags[1] = gpu_arch >= 11;
   condition_flags[2] = is_kraid;

   nir_foreach_function_impl(impl, shader) {
     progress |= nir_algebraic_impl(impl, condition_flags, &bifrost_nir_lower_algebraic_late_table);
   }

   return progress;
}

