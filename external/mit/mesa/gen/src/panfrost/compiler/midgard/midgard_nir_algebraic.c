#include "midgard_nir.h"

#include "nir.h"
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

/* What follows is NIR algebraic transform code for the following 3
 * transforms:
 *    ('~fadd', ('fadd', 'a', 'b'), 'a') => ('fadd', ('fadd', 'a', 'a'), 'b')
 *    ('fsin', 'a') => ('fsin_mdg', ('fdiv', 'a', 3.141592653589793))
 *    ('fcos', 'a') => ('fcos_mdg', ('fdiv', 'a', 3.141592653589793))
 */


static const nir_search_value_union midgard_nir_lower_algebraic_early_values[] = {
   /* ('~fadd', ('fadd', 'a', 'b'), 'a') => ('fadd', ('fadd', 'a', 'a'), 'b') */
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
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_exact,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      1, 1,
      { 0, 1 },
      -1,
   } },
   /* search0_1 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_exact,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      0, 2,
      { 2, 0 },
      -1,
   } },

   /* replace0_0_0 -> 0 in the cache */
   /* replace0_0_1 -> 0 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      -1, 0,
      { 0, 0 },
      -1,
   } },
   /* replace0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      0, 1,
      { 4, 1 },
      -1,
   } },

   /* ('fsin', 'a') => ('fsin_mdg', ('fdiv', 'a', 3.141592653589793)) */
   { .variable = {
      { nir_search_value_variable, -1 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fsin,
      -1, 0,
      { 6 },
      -1,
   } },

   /* replace1_0_0 -> 6 in the cache */
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x400921fb54442d18ull /* 3.141592653589793 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fdiv,
      -1, 0,
      { 6, 8 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fsin_mdg,
      -1, 0,
      { 9 },
      -1,
   } },

   /* ('fcos', 'a') => ('fcos_mdg', ('fdiv', 'a', 3.141592653589793)) */
   /* search2_0 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fcos,
      -1, 0,
      { 6 },
      -1,
   } },

   /* replace2_0_0 -> 6 in the cache */
   /* replace2_0_1 -> 8 in the cache */
   /* replace2_0 -> 9 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fcos_mdg,
      -1, 0,
      { 9 },
      -1,
   } },

};



static const struct transform midgard_nir_lower_algebraic_early_transforms[] = {
   { ~0, ~0, ~0 }, /* Sentinel */

   { 7, 10, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 11, 12, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 3, 5, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

};

static const struct per_op_table midgard_nir_lower_algebraic_early_pass_op_table[nir_num_search_ops] = {
   [nir_op_fadd] = {
      .filter = (const uint16_t []) {
         0,
         0,
         1,
         0,
         0,
         1,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         2,
         5,
         5,
         5,
      },
   },
   [nir_op_fsin] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         3,
      },
   },
   [nir_op_fcos] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         4,
      },
   },
};

/* Mapping from state index to offset in transforms (0 being no transforms) */
static const uint16_t midgard_nir_lower_algebraic_early_transform_offsets[] = {
   0,
   0,
   0,
   1,
   3,
   5,
};

static const nir_algebraic_table midgard_nir_lower_algebraic_early_table = {
   .transforms = midgard_nir_lower_algebraic_early_transforms,
   .transform_offsets = midgard_nir_lower_algebraic_early_transform_offsets,
   .pass_op_table = midgard_nir_lower_algebraic_early_pass_op_table,
   .values = midgard_nir_lower_algebraic_early_values,
   .expression_cond = NULL,
   .variable_cond = NULL,
};

bool
midgard_nir_lower_algebraic_early(
   nir_shader *shader
) {
   bool progress = false;
   bool condition_flags[1];
   const nir_shader_compiler_options *options = shader->options;
   const shader_info *info = &shader->info;
   (void) options;
   (void) info;

   STATIC_ASSERT(13 == ARRAY_SIZE(midgard_nir_lower_algebraic_early_values));
   condition_flags[0] = true;

   nir_foreach_function_impl(impl, shader) {
     progress |= nir_algebraic_impl(impl, condition_flags, &midgard_nir_lower_algebraic_early_table);
   }

   return progress;
}


#include "nir.h"
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

/* What follows is NIR algebraic transform code for the following 53
 * transforms:
 *    ('fsub', 'a', 'b') => ('fadd', 'a', ('fneg', 'b'))
 *    ('b32csel', 'a', 'b@32', 0) => ('iand', 'a', 'b')
 *    ('b32csel', 'a', 0, 'b@32') => ('iand', ('inot', 'a'), 'b')
 *    ('~fmin', ('fmax', 'a', -1.0), 1.0) => ('fsat_signed', 'a')
 *    ('~fmax', ('fmin', 'a', 1.0), -1.0) => ('fsat_signed', 'a')
 *    ('fmax', 'a', 0.0) => ('fclamp_pos', 'a')
 *    ('ishl', 'a@16', 'b') => ('u2u16', ('ishl', ('u2u32', 'a'), 'b'))
 *    ('ishr', 'a@16', 'b') => ('i2i16', ('ishr', ('i2i32', 'a'), 'b'))
 *    ('ushr', 'a@16', 'b') => ('u2u16', ('ushr', ('u2u32', 'a'), 'b'))
 *    ('ishl', 'a@8', 'b') => ('u2u8', ('u2u16', ('ishl', ('u2u32', ('u2u16', 'a')), 'b')))
 *    ('ishr', 'a@8', 'b') => ('i2i8', ('i2i16', ('ishr', ('i2i32', ('i2i16', 'a')), 'b')))
 *    ('ushr', 'a@8', 'b') => ('u2u8', ('u2u16', ('ushr', ('u2u32', ('u2u16', 'a')), 'b')))
 *    ('fmul', 'a', 2.0) => ('fadd', 'a', 'a')
 *    ('ufind_msb', 'a@8') => ('isub', 31, ('uclz', ('u2u32', 'a')))
 *    ('ufind_msb', 'a@16') => ('isub', 31, ('uclz', ('u2u32', 'a')))
 *    ('ufind_msb', 'a@32') => ('isub', 31, ('uclz', 'a'))
 *    ('u2u8', 'a@32') => ('u2u8', ('u2u16', 'a'))
 *    ('u2u8', 'a@64') => ('u2u8', ('u2u16', ('u2u32', 'a')))
 *    ('u2u16', 'a@64') => ('u2u16', ('u2u32', 'a'))
 *    ('u2u32', 'a@8') => ('u2u32', ('u2u16', 'a'))
 *    ('u2u64', 'a@8') => ('u2u64', ('u2u32', ('u2u16', 'a')))
 *    ('u2u64', 'a@16') => ('u2u64', ('u2u32', 'a'))
 *    ('i2i8', 'a@32') => ('i2i8', ('i2i16', 'a'))
 *    ('i2i8', 'a@64') => ('i2i8', ('i2i16', ('i2i32', 'a')))
 *    ('i2i16', 'a@64') => ('i2i16', ('i2i32', 'a'))
 *    ('i2i32', 'a@8') => ('i2i32', ('i2i16', 'a'))
 *    ('i2i64', 'a@8') => ('i2i64', ('i2i32', ('i2i16', 'a')))
 *    ('i2i64', 'a@16') => ('i2i64', ('i2i32', 'a'))
 *    ('f2f16', 'a@64') => ('f2f16', ('f2f32', 'a'))
 *    ('f2f64', 'a@16') => ('f2f64', ('f2f32', 'a'))
 *    ('i2f16', 'a@64') => ('f2f16', ('f2f32', ('i2f64', 'a')))
 *    ('i2f32', 'a@8') => ('i2f32', ('i2i32', ('i2i16', 'a')))
 *    ('i2f64', 'a@8') => ('i2f64', ('i2i64', ('i2i32', ('i2i16', 'a'))))
 *    ('i2f64', 'a@16') => ('i2f64', ('i2i64', ('i2i32', 'a')))
 *    ('u2f16', 'a@64') => ('f2f16', ('f2f32', ('u2f64', 'a')))
 *    ('u2f32', 'a@8') => ('u2f32', ('u2u32', ('u2u16', 'a')))
 *    ('u2f64', 'a@8') => ('u2f64', ('u2u64', ('u2u32', ('u2u16', 'a'))))
 *    ('u2f64', 'a@16') => ('u2f64', ('u2u64', ('u2u32', 'a')))
 *    ('f2i8', 'a@32') => ('i2i8', ('i2i16', ('f2i32', 'a')))
 *    ('f2i8', 'a@64') => ('i2i8', ('i2i16', ('i2i32', ('f2i64', 'a'))))
 *    ('f2i16', 'a@64') => ('i2i16', ('i2i32', ('f2i64', 'a')))
 *    ('f2i64', 'a@16') => ('f2i64', ('f2f64', ('f2f32', 'a')))
 *    ('f2u8', 'a@32') => ('u2u8', ('u2u16', ('f2u32', 'a')))
 *    ('f2u8', 'a@64') => ('u2u8', ('u2u16', ('u2u32', ('f2u64', 'a'))))
 *    ('f2u16', 'a@64') => ('u2u16', ('u2u32', ('f2u64', 'a')))
 *    ('f2u64', 'a@16') => ('f2u64', ('f2f64', ('f2f32', 'a')))
 *    ('fge', 'a', '#b') => ('inot', ('flt', 'a', 'b'))
 *    ('fge32', 'a', '#b') => ('inot', ('flt32', 'a', 'b'))
 *    ('ige32', 'a', '#b') => ('inot', ('ilt32', 'a', 'b'))
 *    ('uge32', 'a', '#b') => ('inot', ('ult32', 'a', 'b'))
 *    ('flt32', '#a', 'b') => ('inot', ('fge32', 'a', 'b'))
 *    ('ilt32', '#a', 'b') => ('inot', ('ige32', 'a', 'b'))
 *    ('ult32', '#a', 'b') => ('inot', ('uge32', 'a', 'b'))
 */


static const nir_search_value_union midgard_nir_lower_algebraic_late_values[] = {
   /* ('fsub', 'a', 'b') => ('fadd', 'a', ('fneg', 'b')) */
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
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fsub,
      -1, 0,
      { 0, 1 },
      -1,
   } },

   /* replace3_0 -> 0 in the cache */
   /* replace3_1_0 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fneg,
      -1, 0,
      { 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, -2 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      0, 1,
      { 0, 3 },
      -1,
   } },

   /* ('b32csel', 'a', 'b@32', 0) => ('iand', 'a', 'b') */
   { .variable = {
      { nir_search_value_variable, 32 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .variable = {
      { nir_search_value_variable, 32 },
      1, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0x0ull /* 0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b32csel,
      -1, 0,
      { 5, 6, 7 },
      -1,
   } },

   /* replace4_0 -> 5 in the cache */
   /* replace4_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 5, 6 },
      -1,
   } },

   /* ('b32csel', 'a', 0, 'b@32') => ('iand', ('inot', 'a'), 'b') */
   /* search5_0 -> 5 in the cache */
   /* search5_1 -> 7 in the cache */
   /* search5_2 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_b32csel,
      -1, 0,
      { 5, 7, 6 },
      -1,
   } },

   /* replace5_0_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 5 },
      -1,
   } },
   /* replace5_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_iand,
      0, 1,
      { 11, 6 },
      -1,
   } },

   /* ('~fmin', ('fmax', 'a', -1.0), 1.0) => ('fsat_signed', 'a') */
   { .variable = {
      { nir_search_value_variable, -1 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0xbff0000000000000ull /* -1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_exact,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmax,
      1, 1,
      { 13, 14 },
      -1,
   } },
   { .constant = {
      { nir_search_value_constant, -1 },
      nir_type_float, { 0x3ff0000000000000ull /* 1.0 */ },
   } },
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_exact,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmin,
      0, 2,
      { 15, 16 },
      -1,
   } },

   /* replace6_0 -> 13 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fsat_signed,
      -1, 0,
      { 13 },
      -1,
   } },

   /* ('~fmax', ('fmin', 'a', 1.0), -1.0) => ('fsat_signed', 'a') */
   /* search7_0_0 -> 13 in the cache */
   /* search7_0_1 -> 16 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_exact,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmin,
      1, 1,
      { 13, 16 },
      -1,
   } },
   /* search7_1 -> 14 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_exact,
      nir_fp_fast_math,
      true,
      -1,
      nir_op_fmax,
      0, 2,
      { 19, 14 },
      -1,
   } },

   /* replace7_0 -> 13 in the cache */
   /* replace7 -> 18 in the cache */

   /* ('fmax', 'a', 0.0) => ('fclamp_pos', 'a') */
   /* search8_0 -> 13 in the cache */
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
      { 13, 21 },
      -1,
   } },

   /* replace8_0 -> 13 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fclamp_pos,
      -1, 0,
      { 13 },
      -1,
   } },

   /* ('ishl', 'a@16', 'b') => ('u2u16', ('ishl', ('u2u32', 'a'), 'b')) */
   { .variable = {
      { nir_search_value_variable, 16 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   /* search9_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 24, 6 },
      -1,
   } },

   /* replace9_0_0_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 24 },
      -1,
   } },
   /* replace9_0_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 26, 6 },
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
      { 27 },
      -1,
   } },

   /* ('ishr', 'a@16', 'b') => ('i2i16', ('ishr', ('i2i32', 'a'), 'b')) */
   /* search10_0 -> 24 in the cache */
   /* search10_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 24, 6 },
      -1,
   } },

   /* replace10_0_0_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 24 },
      -1,
   } },
   /* replace10_0_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 30, 6 },
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
      { 31 },
      -1,
   } },

   /* ('ushr', 'a@16', 'b') => ('u2u16', ('ushr', ('u2u32', 'a'), 'b')) */
   /* search11_0 -> 24 in the cache */
   /* search11_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 24, 6 },
      -1,
   } },

   /* replace11_0_0_0 -> 24 in the cache */
   /* replace11_0_0 -> 26 in the cache */
   /* replace11_0_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 26, 6 },
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
      { 34 },
      -1,
   } },

   /* ('ishl', 'a@8', 'b') => ('u2u8', ('u2u16', ('ishl', ('u2u32', ('u2u16', 'a')), 'b'))) */
   { .variable = {
      { nir_search_value_variable, 8 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   /* search12_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 36, 6 },
      -1,
   } },

   /* replace12_0_0_0_0_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u16,
      -1, 0,
      { 36 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 38 },
      -1,
   } },
   /* replace12_0_0_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishl,
      -1, 0,
      { 39, 6 },
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
      { 40 },
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
      { 41 },
      -1,
   } },

   /* ('ishr', 'a@8', 'b') => ('i2i8', ('i2i16', ('ishr', ('i2i32', ('i2i16', 'a')), 'b'))) */
   /* search13_0 -> 36 in the cache */
   /* search13_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 36, 6 },
      -1,
   } },

   /* replace13_0_0_0_0_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 36 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 44 },
      -1,
   } },
   /* replace13_0_0_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ishr,
      -1, 0,
      { 45, 6 },
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
      { 46 },
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
      { 47 },
      -1,
   } },

   /* ('ushr', 'a@8', 'b') => ('u2u8', ('u2u16', ('ushr', ('u2u32', ('u2u16', 'a')), 'b'))) */
   /* search14_0 -> 36 in the cache */
   /* search14_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 36, 6 },
      -1,
   } },

   /* replace14_0_0_0_0_0 -> 36 in the cache */
   /* replace14_0_0_0_0 -> 38 in the cache */
   /* replace14_0_0_0 -> 39 in the cache */
   /* replace14_0_0_1 -> 6 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ushr,
      -1, 0,
      { 39, 6 },
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
      { 50 },
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
      { 51 },
      -1,
   } },

   /* ('fmul', 'a', 2.0) => ('fadd', 'a', 'a') */
   /* search15_0 -> 13 in the cache */
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
      { 13, 53 },
      -1,
   } },

   /* replace15_0 -> 13 in the cache */
   /* replace15_1 -> 13 in the cache */
   { .expression = {
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fadd,
      -1, 0,
      { 13, 13 },
      -1,
   } },

   /* ('ufind_msb', 'a@8') => ('isub', 31, ('uclz', ('u2u32', 'a'))) */
   /* search16_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ufind_msb,
      -1, 0,
      { 36 },
      -1,
   } },

   { .constant = {
      { nir_search_value_constant, 32 },
      nir_type_int, { 0x1full /* 31 */ },
   } },
   /* replace16_1_0_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 36 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uclz,
      -1, 0,
      { 58 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_isub,
      -1, 0,
      { 57, 59 },
      -1,
   } },

   /* ('ufind_msb', 'a@16') => ('isub', 31, ('uclz', ('u2u32', 'a'))) */
   /* search17_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ufind_msb,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace17_0 -> 57 in the cache */
   /* replace17_1_0_0 -> 24 in the cache */
   /* replace17_1_0 -> 26 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uclz,
      -1, 0,
      { 26 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_isub,
      -1, 0,
      { 57, 62 },
      -1,
   } },

   /* ('ufind_msb', 'a@32') => ('isub', 31, ('uclz', 'a')) */
   /* search18_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ufind_msb,
      -1, 0,
      { 5 },
      -1,
   } },

   /* replace18_0 -> 57 in the cache */
   /* replace18_1_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uclz,
      -1, 0,
      { 5 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_isub,
      -1, 0,
      { 57, 65 },
      -1,
   } },

   /* ('u2u8', 'a@32') => ('u2u8', ('u2u16', 'a')) */
   /* search19_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u8,
      -1, 0,
      { 5 },
      -1,
   } },

   /* replace19_0_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u16,
      -1, 0,
      { 5 },
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
      { 68 },
      -1,
   } },

   /* ('u2u8', 'a@64') => ('u2u8', ('u2u16', ('u2u32', 'a'))) */
   { .variable = {
      { nir_search_value_variable, 64 },
      0, /* a */
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
      { 70 },
      -1,
   } },

   /* replace20_0_0_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 70 },
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
      { 72 },
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
      { 73 },
      -1,
   } },

   /* ('u2u16', 'a@64') => ('u2u16', ('u2u32', 'a')) */
   /* search21_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u16,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace21_0_0 -> 70 in the cache */
   /* replace21_0 -> 72 in the cache */
   /* replace21 -> 73 in the cache */

   /* ('u2u32', 'a@8') => ('u2u32', ('u2u16', 'a')) */
   /* search22_0 -> 36 in the cache */
   /* search22 -> 58 in the cache */

   /* replace22_0_0 -> 36 in the cache */
   /* replace22_0 -> 38 in the cache */
   /* replace22 -> 39 in the cache */

   /* ('u2u64', 'a@8') => ('u2u64', ('u2u32', ('u2u16', 'a'))) */
   /* search23_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u64,
      -1, 0,
      { 36 },
      -1,
   } },

   /* replace23_0_0_0 -> 36 in the cache */
   /* replace23_0_0 -> 38 in the cache */
   /* replace23_0 -> 39 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u64,
      -1, 0,
      { 39 },
      -1,
   } },

   /* ('u2u64', 'a@16') => ('u2u64', ('u2u32', 'a')) */
   /* search24_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u64,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace24_0_0 -> 24 in the cache */
   /* replace24_0 -> 26 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u64,
      -1, 0,
      { 26 },
      -1,
   } },

   /* ('i2i8', 'a@32') => ('i2i8', ('i2i16', 'a')) */
   /* search25_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 0,
      { 5 },
      -1,
   } },

   /* replace25_0_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 5 },
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
      { 81 },
      -1,
   } },

   /* ('i2i8', 'a@64') => ('i2i8', ('i2i16', ('i2i32', 'a'))) */
   /* search26_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i8,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace26_0_0_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 70 },
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
      { 84 },
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
      { 85 },
      -1,
   } },

   /* ('i2i16', 'a@64') => ('i2i16', ('i2i32', 'a')) */
   /* search27_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i16,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace27_0_0 -> 70 in the cache */
   /* replace27_0 -> 84 in the cache */
   /* replace27 -> 85 in the cache */

   /* ('i2i32', 'a@8') => ('i2i32', ('i2i16', 'a')) */
   /* search28_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 36 },
      -1,
   } },

   /* replace28_0_0 -> 36 in the cache */
   /* replace28_0 -> 44 in the cache */
   /* replace28 -> 45 in the cache */

   /* ('i2i64', 'a@8') => ('i2i64', ('i2i32', ('i2i16', 'a'))) */
   /* search29_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i64,
      -1, 0,
      { 36 },
      -1,
   } },

   /* replace29_0_0_0 -> 36 in the cache */
   /* replace29_0_0 -> 44 in the cache */
   /* replace29_0 -> 45 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i64,
      -1, 0,
      { 45 },
      -1,
   } },

   /* ('i2i64', 'a@16') => ('i2i64', ('i2i32', 'a')) */
   /* search30_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i64,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace30_0_0 -> 24 in the cache */
   /* replace30_0 -> 30 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i64,
      -1, 0,
      { 30 },
      -1,
   } },

   /* ('f2f16', 'a@64') => ('f2f16', ('f2f32', 'a')) */
   /* search31_0 -> 70 in the cache */
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

   /* replace31_0_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f32,
      -1, 0,
      { 70 },
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
      { 94 },
      -1,
   } },

   /* ('f2f64', 'a@16') => ('f2f64', ('f2f32', 'a')) */
   /* search32_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f64,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace32_0_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f32,
      -1, 0,
      { 24 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f64,
      -1, 0,
      { 97 },
      -1,
   } },

   /* ('i2f16', 'a@64') => ('f2f16', ('f2f32', ('i2f64', 'a'))) */
   /* search33_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f16,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace33_0_0_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f64,
      -1, 0,
      { 70 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f32,
      -1, 0,
      { 100 },
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
      { 101 },
      -1,
   } },

   /* ('i2f32', 'a@8') => ('i2f32', ('i2i32', ('i2i16', 'a'))) */
   /* search34_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f32,
      -1, 0,
      { 36 },
      -1,
   } },

   /* replace34_0_0_0 -> 36 in the cache */
   /* replace34_0_0 -> 44 in the cache */
   /* replace34_0 -> 45 in the cache */
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

   /* ('i2f64', 'a@8') => ('i2f64', ('i2i64', ('i2i32', ('i2i16', 'a')))) */
   /* search35_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f64,
      -1, 0,
      { 36 },
      -1,
   } },

   /* replace35_0_0_0_0 -> 36 in the cache */
   /* replace35_0_0_0 -> 44 in the cache */
   /* replace35_0_0 -> 45 in the cache */
   /* replace35_0 -> 90 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f64,
      -1, 0,
      { 90 },
      -1,
   } },

   /* ('i2f64', 'a@16') => ('i2f64', ('i2i64', ('i2i32', 'a'))) */
   /* search36_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f64,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace36_0_0_0 -> 24 in the cache */
   /* replace36_0_0 -> 30 in the cache */
   /* replace36_0 -> 92 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2f64,
      -1, 0,
      { 92 },
      -1,
   } },

   /* ('u2f16', 'a@64') => ('f2f16', ('f2f32', ('u2f64', 'a'))) */
   /* search37_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f16,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace37_0_0_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f64,
      -1, 0,
      { 70 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2f32,
      -1, 0,
      { 110 },
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
      { 111 },
      -1,
   } },

   /* ('u2f32', 'a@8') => ('u2f32', ('u2u32', ('u2u16', 'a'))) */
   /* search38_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 36 },
      -1,
   } },

   /* replace38_0_0_0 -> 36 in the cache */
   /* replace38_0_0 -> 38 in the cache */
   /* replace38_0 -> 39 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f32,
      -1, 0,
      { 39 },
      -1,
   } },

   /* ('u2f64', 'a@8') => ('u2f64', ('u2u64', ('u2u32', ('u2u16', 'a')))) */
   /* search39_0 -> 36 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f64,
      -1, 0,
      { 36 },
      -1,
   } },

   /* replace39_0_0_0_0 -> 36 in the cache */
   /* replace39_0_0_0 -> 38 in the cache */
   /* replace39_0_0 -> 39 in the cache */
   /* replace39_0 -> 77 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f64,
      -1, 0,
      { 77 },
      -1,
   } },

   /* ('u2f64', 'a@16') => ('u2f64', ('u2u64', ('u2u32', 'a'))) */
   /* search40_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f64,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace40_0_0_0 -> 24 in the cache */
   /* replace40_0_0 -> 26 in the cache */
   /* replace40_0 -> 79 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2f64,
      -1, 0,
      { 79 },
      -1,
   } },

   /* ('f2i8', 'a@32') => ('i2i8', ('i2i16', ('f2i32', 'a'))) */
   /* search41_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i8,
      -1, 0,
      { 5 },
      -1,
   } },

   /* replace41_0_0_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i32,
      -1, 0,
      { 5 },
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
      { 120 },
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
      { 121 },
      -1,
   } },

   /* ('f2i8', 'a@64') => ('i2i8', ('i2i16', ('i2i32', ('f2i64', 'a')))) */
   /* search42_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i8,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace42_0_0_0_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i64,
      -1, 0,
      { 70 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_i2i32,
      -1, 0,
      { 124 },
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
      { 125 },
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
      { 126 },
      -1,
   } },

   /* ('f2i16', 'a@64') => ('i2i16', ('i2i32', ('f2i64', 'a'))) */
   /* search43_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i16,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace43_0_0_0 -> 70 in the cache */
   /* replace43_0_0 -> 124 in the cache */
   /* replace43_0 -> 125 in the cache */
   /* replace43 -> 126 in the cache */

   /* ('f2i64', 'a@16') => ('f2i64', ('f2f64', ('f2f32', 'a'))) */
   /* search44_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i64,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace44_0_0_0 -> 24 in the cache */
   /* replace44_0_0 -> 97 in the cache */
   /* replace44_0 -> 98 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2i64,
      -1, 0,
      { 98 },
      -1,
   } },

   /* ('f2u8', 'a@32') => ('u2u8', ('u2u16', ('f2u32', 'a'))) */
   /* search45_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u8,
      -1, 0,
      { 5 },
      -1,
   } },

   /* replace45_0_0_0 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u32,
      -1, 0,
      { 5 },
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
      { 132 },
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
      { 133 },
      -1,
   } },

   /* ('f2u8', 'a@64') => ('u2u8', ('u2u16', ('u2u32', ('f2u64', 'a')))) */
   /* search46_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 8 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u8,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace46_0_0_0_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u64,
      -1, 0,
      { 70 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_u2u32,
      -1, 0,
      { 136 },
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
      { 137 },
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
      { 138 },
      -1,
   } },

   /* ('f2u16', 'a@64') => ('u2u16', ('u2u32', ('f2u64', 'a'))) */
   /* search47_0 -> 70 in the cache */
   { .expression = {
      { nir_search_value_expression, 16 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u16,
      -1, 0,
      { 70 },
      -1,
   } },

   /* replace47_0_0_0 -> 70 in the cache */
   /* replace47_0_0 -> 136 in the cache */
   /* replace47_0 -> 137 in the cache */
   /* replace47 -> 138 in the cache */

   /* ('f2u64', 'a@16') => ('f2u64', ('f2f64', ('f2f32', 'a'))) */
   /* search48_0 -> 24 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u64,
      -1, 0,
      { 24 },
      -1,
   } },

   /* replace48_0_0_0 -> 24 in the cache */
   /* replace48_0_0 -> 97 in the cache */
   /* replace48_0 -> 98 in the cache */
   { .expression = {
      { nir_search_value_expression, 64 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_f2u64,
      -1, 0,
      { 98 },
      -1,
   } },

   /* ('fge', 'a', '#b') => ('inot', ('flt', 'a', 'b')) */
   /* search49_0 -> 0 in the cache */
   { .variable = {
      { nir_search_value_variable, -2 },
      1, /* b */
      true,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fge,
      -1, 0,
      { 0, 143 },
      -1,
   } },

   /* replace49_0_0 -> 0 in the cache */
   /* replace49_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_flt,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 145 },
      -1,
   } },

   /* ('fge32', 'a', '#b') => ('inot', ('flt32', 'a', 'b')) */
   /* search50_0 -> 0 in the cache */
   /* search50_1 -> 143 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fge32,
      -1, 0,
      { 0, 143 },
      -1,
   } },

   /* replace50_0_0 -> 0 in the cache */
   /* replace50_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_flt32,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 148 },
      -1,
   } },

   /* ('ige32', 'a', '#b') => ('inot', ('ilt32', 'a', 'b')) */
   /* search51_0 -> 0 in the cache */
   /* search51_1 -> 143 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ige32,
      -1, 0,
      { 0, 143 },
      -1,
   } },

   /* replace51_0_0 -> 0 in the cache */
   /* replace51_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ilt32,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 151 },
      -1,
   } },

   /* ('uge32', 'a', '#b') => ('inot', ('ult32', 'a', 'b')) */
   /* search52_0 -> 0 in the cache */
   /* search52_1 -> 143 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uge32,
      -1, 0,
      { 0, 143 },
      -1,
   } },

   /* replace52_0_0 -> 0 in the cache */
   /* replace52_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ult32,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 154 },
      -1,
   } },

   /* ('flt32', '#a', 'b') => ('inot', ('fge32', 'a', 'b')) */
   { .variable = {
      { nir_search_value_variable, -2 },
      0, /* a */
      true,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
   /* search53_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_flt32,
      -1, 0,
      { 156, 1 },
      -1,
   } },

   /* replace53_0_0 -> 0 in the cache */
   /* replace53_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_fge32,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 158 },
      -1,
   } },

   /* ('ilt32', '#a', 'b') => ('inot', ('ige32', 'a', 'b')) */
   /* search54_0 -> 156 in the cache */
   /* search54_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ilt32,
      -1, 0,
      { 156, 1 },
      -1,
   } },

   /* replace54_0_0 -> 0 in the cache */
   /* replace54_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ige32,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 161 },
      -1,
   } },

   /* ('ult32', '#a', 'b') => ('inot', ('uge32', 'a', 'b')) */
   /* search55_0 -> 156 in the cache */
   /* search55_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_ult32,
      -1, 0,
      { 156, 1 },
      -1,
   } },

   /* replace55_0_0 -> 0 in the cache */
   /* replace55_0_1 -> 1 in the cache */
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_uge32,
      -1, 0,
      { 0, 1 },
      -1,
   } },
   { .expression = {
      { nir_search_value_expression, 32 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 164 },
      -1,
   } },

};



static const struct transform midgard_nir_lower_algebraic_late_transforms[] = {
   { ~0, ~0, ~0 }, /* Sentinel */

   { 2, 4, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 8, 9, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 10, 12, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 8, 9, 0 },
   { 10, 12, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 22, 23, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 25, 28, 0 },
   { 37, 42, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 29, 32, 0 },
   { 43, 48, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 33, 35, 0 },
   { 49, 52, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 54, 55, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 56, 60, 0 },
   { 61, 63, 0 },
   { 64, 66, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 67, 69, 0 },
   { 71, 74, 0 },
   { 75, 73, 0 },
   { 58, 39, 0 },
   { 76, 77, 0 },
   { 78, 79, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 80, 82, 0 },
   { 83, 86, 0 },
   { 87, 85, 0 },
   { 88, 45, 0 },
   { 89, 90, 0 },
   { 91, 92, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 93, 95, 0 },
   { 96, 98, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 99, 102, 0 },
   { 103, 104, 0 },
   { 105, 106, 0 },
   { 107, 108, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 109, 112, 0 },
   { 113, 114, 0 },
   { 115, 116, 0 },
   { 117, 118, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 119, 122, 0 },
   { 123, 127, 0 },
   { 128, 126, 0 },
   { 129, 130, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 131, 134, 0 },
   { 135, 139, 0 },
   { 140, 138, 0 },
   { 141, 142, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 144, 146, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 147, 149, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 150, 152, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 153, 155, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 157, 159, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 160, 162, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 163, 165, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 20, 18, 0 },
   { 22, 23, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 17, 18, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

};

static const struct per_op_table midgard_nir_lower_algebraic_late_pass_op_table[nir_num_search_ops] = {
   [nir_op_fsub] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         2,
      },
   },
   [nir_op_b32csel] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         3,
         4,
         5,
         0,
         3,
         4,
         5,
      },
   },
   [nir_op_fmin] = {
      .filter = (const uint16_t []) {
         0,
         1,
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
         2,
         0,
      },
      
      .num_filtered_states = 3,
      .table = (const uint16_t []) {
      
         0,
         6,
         0,
         6,
         6,
         28,
         0,
         28,
         0,
      },
   },
   [nir_op_fmax] = {
      .filter = (const uint16_t []) {
         0,
         1,
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
         2,
      },
      
      .num_filtered_states = 3,
      .table = (const uint16_t []) {
      
         0,
         7,
         0,
         7,
         7,
         27,
         0,
         27,
         0,
      },
   },
   [nir_op_ishl] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         8,
      },
   },
   [nir_op_ishr] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         9,
      },
   },
   [nir_op_ushr] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         10,
      },
   },
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         11,
         11,
         11,
      },
   },
   [nir_op_ufind_msb] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         12,
      },
   },
   [nir_search_op_u2u] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         13,
      },
   },
   [nir_search_op_i2i] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         14,
      },
   },
   [nir_search_op_f2f] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         15,
      },
   },
   [nir_search_op_i2f] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         16,
      },
   },
   [nir_search_op_u2f] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         17,
      },
   },
   [nir_search_op_f2i] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         18,
      },
   },
   [nir_search_op_f2u] = {
      .filter = NULL,
      
      .num_filtered_states = 1,
      .table = (const uint16_t []) {
      
         19,
      },
   },
   [nir_op_fge] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         20,
         0,
         20,
      },
   },
   [nir_op_fge32] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         21,
         0,
         21,
      },
   },
   [nir_op_ige32] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         22,
         0,
         22,
      },
   },
   [nir_op_uge32] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         23,
         0,
         23,
      },
   },
   [nir_op_flt32] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         0,
         24,
         24,
      },
   },
   [nir_op_ilt32] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         0,
         25,
         25,
      },
   },
   [nir_op_ult32] = {
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
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         0,
         26,
         26,
      },
   },
};

/* Mapping from state index to offset in transforms (0 being no transforms) */
static const uint16_t midgard_nir_lower_algebraic_late_transform_offsets[] = {
   0,
   0,
   1,
   3,
   5,
   7,
   0,
   10,
   12,
   15,
   18,
   21,
   23,
   27,
   34,
   41,
   44,
   49,
   54,
   59,
   64,
   66,
   68,
   70,
   72,
   74,
   76,
   78,
   81,
};

static const nir_algebraic_table midgard_nir_lower_algebraic_late_table = {
   .transforms = midgard_nir_lower_algebraic_late_transforms,
   .transform_offsets = midgard_nir_lower_algebraic_late_transform_offsets,
   .pass_op_table = midgard_nir_lower_algebraic_late_pass_op_table,
   .values = midgard_nir_lower_algebraic_late_values,
   .expression_cond = NULL,
   .variable_cond = NULL,
};

bool
midgard_nir_lower_algebraic_late(
   nir_shader *shader
) {
   bool progress = false;
   bool condition_flags[1];
   const nir_shader_compiler_options *options = shader->options;
   const shader_info *info = &shader->info;
   (void) options;
   (void) info;

   STATIC_ASSERT(166 == ARRAY_SIZE(midgard_nir_lower_algebraic_late_values));
   condition_flags[0] = true;

   nir_foreach_function_impl(impl, shader) {
     progress |= nir_algebraic_impl(impl, condition_flags, &midgard_nir_lower_algebraic_late_table);
   }

   return progress;
}


#include "nir.h"
#include "nir_builder.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

/* What follows is NIR algebraic transform code for the following 2
 * transforms:
 *    ('inot', ('inot', 'a')) => a
 *    ('b32csel', ('inot', 'a'), 'b', 'c') => ('b32csel', 'a', 'c', 'b')
 */


static const nir_search_value_union midgard_nir_cancel_inot_values[] = {
   /* ('inot', ('inot', 'a')) => a */
   { .variable = {
      { nir_search_value_variable, -1 },
      0, /* a */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
   } },
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
      { nir_search_value_expression, -1 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_inot,
      -1, 0,
      { 1 },
      -1,
   } },

   /* replace56 -> 0 in the cache */

   /* ('b32csel', ('inot', 'a'), 'b', 'c') => ('b32csel', 'a', 'c', 'b') */
   { .variable = {
      { nir_search_value_variable, 32 },
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
      nir_op_inot,
      -1, 0,
      { 3 },
      -1,
   } },
   { .variable = {
      { nir_search_value_variable, -3 },
      1, /* b */
      false,
      -1,
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
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
      nir_op_b32csel,
      -1, 0,
      { 4, 5, 6 },
      -1,
   } },

   /* replace57_0 -> 3 in the cache */
   /* replace57_1 -> 6 in the cache */
   /* replace57_2 -> 5 in the cache */
   { .expression = {
      { nir_search_value_expression, -3 },
      nir_fp_fast_math,
      nir_fp_fast_math,
      false,
      -1,
      nir_op_b32csel,
      -1, 0,
      { 3, 6, 5 },
      -1,
   } },

};



static const struct transform midgard_nir_cancel_inot_transforms[] = {
   { ~0, ~0, ~0 }, /* Sentinel */

   { 2, 0, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

   { 7, 8, 0 },
   { ~0, ~0, ~0 }, /* Sentinel */

};

static const struct per_op_table midgard_nir_cancel_inot_pass_op_table[nir_num_search_ops] = {
   [nir_op_inot] = {
      .filter = (const uint16_t []) {
         0,
         0,
         1,
         1,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         2,
         3,
      },
   },
   [nir_op_b32csel] = {
      .filter = (const uint16_t []) {
         0,
         0,
         1,
         1,
         0,
      },
      
      .num_filtered_states = 2,
      .table = (const uint16_t []) {
      
         0,
         0,
         0,
         0,
         4,
         4,
         4,
         4,
      },
   },
};

/* Mapping from state index to offset in transforms (0 being no transforms) */
static const uint16_t midgard_nir_cancel_inot_transform_offsets[] = {
   0,
   0,
   0,
   1,
   3,
};

static const nir_algebraic_table midgard_nir_cancel_inot_table = {
   .transforms = midgard_nir_cancel_inot_transforms,
   .transform_offsets = midgard_nir_cancel_inot_transform_offsets,
   .pass_op_table = midgard_nir_cancel_inot_pass_op_table,
   .values = midgard_nir_cancel_inot_values,
   .expression_cond = NULL,
   .variable_cond = NULL,
};

bool
midgard_nir_cancel_inot(
   nir_shader *shader
) {
   bool progress = false;
   bool condition_flags[1];
   const nir_shader_compiler_options *options = shader->options;
   const shader_info *info = &shader->info;
   (void) options;
   (void) info;

   STATIC_ASSERT(9 == ARRAY_SIZE(midgard_nir_cancel_inot_values));
   condition_flags[0] = true;

   nir_foreach_function_impl(impl, shader) {
     progress |= nir_algebraic_impl(impl, condition_flags, &midgard_nir_cancel_inot_table);
   }

   return progress;
}

