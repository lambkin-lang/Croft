/*
 * MIT License
 *
 * Copyright (c) 2026 lambkin-lang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef RYU64_INTERNAL_H
#define RYU64_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ryu64.h"

#define RYU_BIGINT_BASE 1000000000u
#define RYU_BIGINT_BASE_DIGITS 9u
#ifndef RYU_BIGINT_MAX_LIMBS
#ifdef __wasm32__
#define RYU_BIGINT_MAX_LIMBS 256u
#else
#define RYU_BIGINT_MAX_LIMBS 512u
#endif
#endif
#if (RYU_BIGINT_MAX_LIMBS < 96u)
#error "RYU_BIGINT_MAX_LIMBS is too small for required binary64 formatting coverage"
#endif
#define RYU_LOCAL_BUF_CAP 4096u
#define RYU_MAX_PRINTF_PRECISION 1000

typedef struct {
  uint32_t limb[RYU_BIGINT_MAX_LIMBS];
  size_t len;
} ryu_bigint;

typedef struct {
  int sign;
  int is_nan;
  int is_inf;
  int is_zero;
  uint64_t bits;
  uint64_t mantissa;
  int exp_raw;
  int exp2;
} ryu_fp64;

typedef struct {
  ryu_bigint digits; /* X in value = X / 10^scale */
  unsigned scale;
} ryu_decimal_exact;

typedef struct {
  ryu_bigint low;
  ryu_bigint high;
  unsigned scale;
  int low_closed;
  int high_closed;
} ryu_decimal_interval;

void ryu_bigint_zero(ryu_bigint* v);
void ryu_bigint_from_u64(ryu_bigint* v, uint64_t x);
int ryu_bigint_is_zero(const ryu_bigint* v);
void ryu_bigint_copy(ryu_bigint* dst, const ryu_bigint* src);
int ryu_bigint_cmp(const ryu_bigint* a, const ryu_bigint* b);
int ryu_bigint_add(ryu_bigint* a, const ryu_bigint* b);
int ryu_bigint_add_small(ryu_bigint* a, uint32_t b);
int ryu_bigint_sub(ryu_bigint* a, const ryu_bigint* b);
int ryu_bigint_sub_small(ryu_bigint* a, uint32_t b);
int ryu_bigint_mul_small(ryu_bigint* a, uint32_t m);
int ryu_bigint_mul_pow5(ryu_bigint* a, unsigned p);
int ryu_bigint_mul_pow10(ryu_bigint* a, unsigned p);
int ryu_bigint_shl_bits(ryu_bigint* a, unsigned bits);
int ryu_bigint_div_small_exact(ryu_bigint* a, uint32_t div);
int ryu_bigint_to_u64(const ryu_bigint* a, uint64_t* out);
unsigned ryu_bigint_decimal_len(const ryu_bigint* a);
int ryu_bigint_to_decimal(const ryu_bigint* a, char* out, size_t out_cap, size_t* out_len);
int ryu_bigint_div_pow10_floor(
    const ryu_bigint* n,
    unsigned pow10,
    ryu_bigint* q,
    int* remainder_is_zero);

uint64_t ryu_u64_from_double(double x);
double ryu_double_from_u64(uint64_t bits);
void ryu_decode_fp64(double x, ryu_fp64* out);

int ryu_exact_decimal_from_bits(uint64_t abs_bits, ryu_decimal_exact* out);
int ryu_decimal_interval_from_bits(uint64_t abs_bits, ryu_decimal_interval* out);

int ryu_choose_shortest_digits(
    uint64_t abs_bits,
    uint64_t* out_significand,
    int* out_k,
    int* out_digits);

int ryu_format_significand_exponent(
    char* out,
    size_t out_cap,
    int negative,
    uint64_t significand,
    int k,
    int prefer_scientific,
    size_t* out_len);

int ryu_format_scientific_fixed_sig(
    char* out,
    size_t out_cap,
    int negative,
    uint64_t significand,
    unsigned sig_digits,
    int exp10,
    int uppercase,
    size_t* out_len);

int ryu_write_special(
    char* out,
    size_t out_cap,
    int negative,
    int is_inf,
    int uppercase,
    int always_sign,
    int space_sign,
    size_t* out_len);

int ryu_write_sign(char* out, size_t out_cap, int negative, int always_sign, int space_sign, size_t* pos);
ryu_status ryu_copy_literal_signed(char* out, size_t out_cap, const char* lit, int negative, size_t* out_len);

int ryu_round_exact_to_significant(
    const ryu_decimal_exact* exact,
    unsigned sig_digits,
    ryu_bigint* out_rounded,
    int* out_exp10,
    int rounding_mode,
    int negative);

int ryu_round_exact_to_fractional(
    const ryu_decimal_exact* exact,
    unsigned frac_digits,
    ryu_bigint* out_rounded,
    int rounding_mode,
    int negative);

int ryu_emit_fixed_from_scaled(
    char* out,
    size_t out_cap,
    int negative,
    const ryu_bigint* scaled,
    unsigned frac_digits,
    int alternate_form,
    int always_sign,
    int space_sign,
    size_t* out_len);

int ryu_trim_trailing_fraction_zeros(char* buf, size_t* len);

#endif
