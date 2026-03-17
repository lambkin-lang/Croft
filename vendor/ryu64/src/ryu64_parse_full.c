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

#include "ryu64_parse_internal.h"

#ifdef RYU64_ENABLE_PARSE_BIGINT
#include "ryu64_internal.h"

#ifdef RYU_ENABLE_BIGINT_PROFILE
void ryu_bigint_profile_note_len(size_t len);
#endif

typedef struct {
  int negative;
  int special; /* 0=none, 1=inf, 2=nan */
  int saw_any_digit;
  int saw_nonzero;
  int sig_is_u64;
  int truncated;
  int dropped_nonzero;
  uint64_t sig_u64;
  long long sig_digits;
  long long dropped_digits;
  long long exp10;
  size_t parsed_len;
  ryu_bigint sig;
} ryu_full_parsed;

typedef struct {
  uint64_t hi;
  uint64_t mid;
  uint64_t lo;
} ryu_u192;

typedef struct {
  ryu_bigint num;
  ryu_bigint den;
  ryu_bigint scratch0;
  ryu_bigint scratch1;
  ryu_bigint scratch2;
  ryu_bigint scratch3;
} ryu_parse_bigint_ws;

static ryu_parse_status ryu_convert_ratio_to_double(
    int negative,
    const ryu_bigint* num,
    const ryu_bigint* den,
    int exp2_adjust,
    ryu_parse_bigint_ws* ws,
    double* out_value);

static int ryu_u192_is_zero(const ryu_u192* v) {
  return v->hi == 0u && v->mid == 0u && v->lo == 0u;
}

static unsigned ryu_u192_bitlen(const ryu_u192* v) {
  if (v->hi != 0u) {
    return 129u + ryu_log2_u64(v->hi);
  }
  if (v->mid != 0u) {
    return 65u + ryu_log2_u64(v->mid);
  }
  if (v->lo != 0u) {
    return 1u + ryu_log2_u64(v->lo);
  }
  return 0u;
}

static uint64_t ryu_u192_shr_to_u64(const ryu_u192* v, unsigned shift) {
  if (shift == 0u) {
    return v->lo;
  }
  if (shift < 64u) {
    return (v->lo >> shift) | (v->mid << (64u - shift));
  }
  if (shift == 64u) {
    return v->mid;
  }
  if (shift < 128u) {
    return (v->mid >> (shift - 64u)) | (v->hi << (128u - shift));
  }
  if (shift == 128u) {
    return v->hi;
  }
  if (shift < 192u) {
    return v->hi >> (shift - 128u);
  }
  return 0u;
}

static int ryu_u192_get_bit(const ryu_u192* v, unsigned bit_index) {
  if (bit_index < 64u) {
    return (int)((v->lo >> bit_index) & UINT64_C(1));
  }
  if (bit_index < 128u) {
    return (int)((v->mid >> (bit_index - 64u)) & UINT64_C(1));
  }
  if (bit_index < 192u) {
    return (int)((v->hi >> (bit_index - 128u)) & UINT64_C(1));
  }
  return 0;
}

/*
 * Returns nonzero when any bit in [0, low_bits) is set.
 * low_bits == 0 inspects an empty range.
 */
static int ryu_u192_any_low_bits(const ryu_u192* v, unsigned low_bits) {
  if (low_bits == 0u) {
    return 0;
  }
  if (low_bits >= 192u) {
    return !ryu_u192_is_zero(v);
  }
  if (low_bits <= 64u) {
    uint64_t mask = (low_bits == 64u) ? UINT64_MAX : ((UINT64_C(1) << low_bits) - UINT64_C(1));
    return (v->lo & mask) != 0u;
  }
  if (low_bits <= 128u) {
    unsigned mid_bits = low_bits - 64u;
    uint64_t mask = (mid_bits == 64u) ? UINT64_MAX : ((UINT64_C(1) << mid_bits) - UINT64_C(1));
    return v->lo != 0u || (v->mid & mask) != 0u;
  }
  {
    unsigned hi_bits = low_bits - 128u;
    uint64_t mask = (hi_bits == 64u) ? UINT64_MAX : ((UINT64_C(1) << hi_bits) - UINT64_C(1));
    return v->lo != 0u || v->mid != 0u || (v->hi & mask) != 0u;
  }
}

static ryu_u192 ryu_mul_u64_u128_192(uint64_t m, const ryu_u128* p10) {
  ryu_u128 p0 = ryu_mul_u64_u64_128(m, p10->lo);
  ryu_u128 p1 = ryu_mul_u64_u64_128(m, p10->hi);
  ryu_u192 out;
  uint64_t mid = p0.hi + p1.lo;
  uint64_t carry = (mid < p0.hi) ? 1u : 0u;
  out.lo = p0.lo;
  out.mid = mid;
  out.hi = p1.hi + carry;
  return out;
}

/*
 * Converts a positive integer represented as u192 into nearest-even binary64.
 */
static ryu_parse_status ryu_fast_int_to_double(
    int negative,
    const ryu_u192* int_value,
    double* out_value) {
  unsigned nbits;
  int q2;
  uint64_t mant;
  int inexact = 0;
  uint64_t bits;

  if (ryu_u192_is_zero(int_value)) {
    bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
    *out_value = ryu_double_from_bits_local(bits);
    return RYU_PARSE_OK;
  }

  nbits = ryu_u192_bitlen(int_value);
  q2 = (int)nbits - 1;
  if (q2 > 1023) {
    return RYU_PARSE_OVERFLOW;
  }

  if (nbits <= 53u) {
    mant = ryu_u192_shr_to_u64(int_value, 0u) << (53u - nbits);
  } else {
    unsigned shift = nbits - 53u;
    int guard;
    int sticky;
    mant = ryu_u192_shr_to_u64(int_value, shift);
    guard = ryu_u192_get_bit(int_value, shift - 1u);
    sticky = ryu_u192_any_low_bits(int_value, shift - 1u);
    inexact = guard || sticky;
    if (guard && (sticky || ((mant & UINT64_C(1)) != 0u))) {
      mant += UINT64_C(1);
    }
    if (mant == (UINT64_C(1) << 53u)) {
      mant >>= 1u;
      q2 += 1;
      if (q2 > 1023) {
        return RYU_PARSE_OVERFLOW;
      }
    }
  }

  bits = ((uint64_t)(q2 + 1023) << 52u) | (mant & ((UINT64_C(1) << 52u) - UINT64_C(1)));
  if (negative) {
    bits |= UINT64_C(1) << 63u;
  }
  *out_value = ryu_double_from_bits_local(bits);
  return inexact ? RYU_PARSE_INEXACT : RYU_PARSE_OK;
}

/*
 * Eisel-Lemire fast path for decimal-to-double conversion.
 *
 * Converts w * 10^q to the nearest IEEE-754 binary64 using a single
 * 64x128-bit multiplication and a precomputed table of 128-bit
 * approximations of 5^q.
 *
 * Handles normal doubles only.  Returns OUT_OF_RANGE for subnormals
 * (biased exponent <= 0), letting the caller fall back to the existing
 * bigint path for those rare cases.
 *
 * Reference: Daniel Lemire, "Number Parsing at a Gigabyte per Second",
 *            Software: Practice and Experience 51(8), 2021.
 * Correctness guarantee (no fallback needed for <= 19-digit significands):
 *            Noble Mushtak and Daniel Lemire, "Fast Number Parsing Without
 *            Fallback", Software: Practice and Experience, 2023.
 */
static ryu_parse_status ryu_eisel_lemire_to_double(
    int negative,
    uint64_t w,
    int q,
    double* out_value) {
  ryu_u128 pow5;
  ryu_u128 hi_prod;
  ryu_u128 lo_prod;
  uint64_t z_lo;
  uint64_t z_mid;
  uint64_t z_hi;
  int upperbit;
  int shift;
  uint64_t mantissa;
  int round_bit;
  uint64_t trailing;
  int sticky;
  int lz;
  int power2;
  int biased_exp;
  uint64_t ieee_mantissa;
  uint64_t bits;

  if (w == 0u) {
    bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
    *out_value = ryu_double_from_bits_local(bits);
    return RYU_PARSE_OK;
  }

  if (q < RYU_EISEL_LEMIRE_MIN_EXP10 || q > RYU_EISEL_LEMIRE_MAX_EXP10) {
    return RYU_PARSE_OUT_OF_RANGE;
  }

  /* Normalize w: shift left so MSB is at bit 63. */
  lz = 63 - (int)ryu_log2_u64(w);
  w <<= (unsigned)lz;

  /* Table lookup: 128-bit approximation of 5^q, normalized with MSB at bit 127. */
  pow5 = ryu64_pow5_128[q + RYU_EISEL_LEMIRE_TABLE_OFFSET];

  /* 64x128 -> 192-bit multiply: z = w * pow5.
   * z_hi  = bits [191..128]
   * z_mid = bits [127..64]
   * z_lo  = bits [63..0]
   */
  hi_prod = ryu_mul_u64_u64_128(w, pow5.hi);
  lo_prod = ryu_mul_u64_u64_128(w, pow5.lo);

  z_lo = lo_prod.lo;
  z_mid = hi_prod.lo + lo_prod.hi;
  z_hi = hi_prod.hi + (z_mid < hi_prod.lo ? UINT64_C(1) : UINT64_C(0));

  /* Upperbit: is the product's MSB at bit 191 (vs 190)?
   * This determines whether the mantissa starts at bit 190 or 189. */
  upperbit = (int)(z_hi >> 63u);

  /* Extract 54 bits (53 mantissa + 1 round bit) from z_hi.
   * For upperbit=1: bits [63..10] = 54 bits.
   * For upperbit=0: bits [63..9]  = 55 bits, but MSB is 0, so 54 meaningful. */
  shift = 9 + upperbit;
  mantissa = z_hi >> (unsigned)shift;

  /* Round bit is the LSB of the 54-bit extraction. */
  round_bit = (int)(mantissa & UINT64_C(1));
  mantissa >>= 1u;

  /* Sticky: any bit below the round position. */
  trailing = z_hi & ((UINT64_C(1) << (unsigned)shift) - UINT64_C(1));
  sticky = (trailing | z_mid | z_lo) != 0u;

  /* Round-to-nearest-even. */
  if (round_bit && (sticky || (mantissa & UINT64_C(1)))) {
    mantissa += UINT64_C(1);
  }

  /* Handle mantissa overflow from rounding (53 bits -> 54 bits). */
  if (mantissa >= (UINT64_C(1) << 53u)) {
    mantissa >>= 1u;
    upperbit += 1;
  }

  /* Binary exponent.
   * floor(q * log2(10)) is approximated by (q * 217706) >> 16.
   * 217706 / 65536 = 3.32192993... which approximates log2(10) = 3.32192809...
   * The error over |q| <= 342 is < 0.001, so the floor is always correct.
   * Note: q * 217706 fits in 27 bits (max |q|=342), so 32-bit arithmetic suffices.
   * Using int avoids i64 sign-extension instructions that are unavailable in wasm MVP. */
  power2 = ((q * 217706) >> 16) + 63 - lz + upperbit;
  biased_exp = power2 + 1023;

  /* Overflow: value too large for double. */
  if (biased_exp >= 2047) {
    return RYU_PARSE_OVERFLOW;
  }

  /* Subnormal range: fall back to existing paths for correct handling.
   * Subnormals are rare (only near 5e-324 to 2.2e-308) and the bigint
   * path handles them correctly. */
  if (biased_exp <= 0) {
    return RYU_PARSE_OUT_OF_RANGE;
  }

  /* Assemble IEEE-754 double. */
  ieee_mantissa = mantissa & ((UINT64_C(1) << 52u) - UINT64_C(1));
  bits = ((uint64_t)biased_exp << 52u) | ieee_mantissa;
  if (negative) {
    bits |= UINT64_C(1) << 63u;
  }
  *out_value = ryu_double_from_bits_local(bits);

  if (round_bit || sticky) {
    return RYU_PARSE_INEXACT;
  }
  return RYU_PARSE_OK;
}

/*
 * Fixed-width fast path:
 * - significand fits in uint64_t
 * - exponent range is bounded so power-of-ten factors fit in uint128/u192 math
 * This keeps conversion in constant-size integer math and avoids bigint.
 */
static ryu_parse_status ryu_fast_mq_to_double(
    int negative,
    uint64_t m,
    int exp10,
    double* out_value) {
  ryu_u128 numer = ryu_u128_from_u64(0u);
  ryu_u128 denom;
  unsigned bn;
  unsigned bd;
  int q2;
  ryu_u128 numer_norm;
  ryu_u128 denom_norm;
  ryu_u128 rem;
  uint64_t frac = 0u;
  uint64_t mant;
  int guard = 0;
  int round_bit = 0;
  int sticky = 0;
  uint64_t bits;

  if (m == 0u) {
    bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
    *out_value = ryu_double_from_bits_local(bits);
    return RYU_PARSE_OK;
  }

  if (exp10 > RYU_PARSE_FAST_MAX_POS_EXP10 || exp10 < -RYU_PARSE_FAST_MAX_NEG_EXP10) {
    return RYU_PARSE_OUT_OF_RANGE;
  }

  if (exp10 >= 0) {
    ryu_u192 int_value = ryu_mul_u64_u128_192(m, &ryu64_pow10_u128[(size_t)exp10]);
    return ryu_fast_int_to_double(negative, &int_value, out_value);
  }
  numer = ryu_u128_from_u64(m);
  denom = ryu64_pow10_u128[(size_t)(-exp10)];

  bn = ryu_u128_bitlen(&numer);
  bd = ryu_u128_bitlen(&denom);
  if (bn == 0u || bd == 0u) {
    return RYU_PARSE_INVALID;
  }

  q2 = (int)bn - (int)bd;
  if (q2 >= 0) {
    ryu_u128 d_shift = ryu_u128_shl(&denom, (unsigned)q2);
    if (ryu_u128_cmp(&numer, &d_shift) < 0) {
      q2 -= 1;
    }
  } else {
    ryu_u128 n_shift = ryu_u128_shl(&numer, (unsigned)(-q2));
    if (ryu_u128_cmp(&n_shift, &denom) < 0) {
      q2 -= 1;
    }
  }

  if (q2 >= 0) {
    denom_norm = ryu_u128_shl(&denom, (unsigned)q2);
    numer_norm = numer;
  } else {
    denom_norm = denom;
    numer_norm = ryu_u128_shl(&numer, (unsigned)(-q2));
  }

  if (ryu_u128_cmp(&numer_norm, &denom_norm) < 0) {
    return RYU_PARSE_INVALID;
  }

  rem = ryu_u128_sub(&numer_norm, &denom_norm);

  {
    unsigned i;
    for (i = 0u; i < 52u; ++i) {
      rem = ryu_u128_shl1(&rem);
      frac <<= 1u;
      if (ryu_u128_cmp(&rem, &denom_norm) >= 0) {
        rem = ryu_u128_sub(&rem, &denom_norm);
        frac |= UINT64_C(1);
      }
    }
  }

  rem = ryu_u128_shl1(&rem);
  if (ryu_u128_cmp(&rem, &denom_norm) >= 0) {
    rem = ryu_u128_sub(&rem, &denom_norm);
    guard = 1;
  }

  rem = ryu_u128_shl1(&rem);
  if (ryu_u128_cmp(&rem, &denom_norm) >= 0) {
    rem = ryu_u128_sub(&rem, &denom_norm);
    round_bit = 1;
  }

  sticky = !ryu_u128_is_zero(&rem);

  mant = (UINT64_C(1) << 52u) | frac;
  if (guard && (round_bit || sticky || ((mant & UINT64_C(1)) != 0u))) {
    mant += UINT64_C(1);
  }

  if (mant == (UINT64_C(1) << 53u)) {
    mant >>= 1u;
    q2 += 1;
  }

  if (q2 > 1023) {
    return RYU_PARSE_OVERFLOW;
  }
  if (q2 < -1022) {
    return RYU_PARSE_UNDERFLOW;
  }

  bits = ((uint64_t)(q2 + 1023) << 52u) | (mant & ((UINT64_C(1) << 52u) - UINT64_C(1)));
  if (negative) {
    bits |= UINT64_C(1) << 63u;
  }
  *out_value = ryu_double_from_bits_local(bits);

  if (guard || round_bit || sticky) {
    return RYU_PARSE_INEXACT;
  }
  return RYU_PARSE_OK;
}

static ryu64_parse_result ryu_parse_overflow_result(int negative, size_t parsed_len) {
  uint64_t bits = UINT64_C(0x7ff0000000000000);
  if (negative) {
    bits |= UINT64_C(1) << 63u;
  }
  return ryu_parse_result_make(RYU_PARSE_OVERFLOW, ryu_double_from_bits_local(bits), parsed_len);
}

static ryu64_parse_result ryu_parse_underflow_result(int negative, size_t parsed_len) {
  uint64_t bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
  return ryu_parse_result_make(RYU_PARSE_UNDERFLOW, ryu_double_from_bits_local(bits), parsed_len);
}

static ryu64_parse_result ryu_parse_zero_result(int negative, size_t parsed_len) {
  uint64_t bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
  return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), parsed_len);
}

static long long ryu_add_ll_saturate(long long a, long long b) {
  if (b > 0 && a > (long long)INT64_MAX - b) {
    return (long long)INT64_MAX;
  }
  if (b < 0 && a < (long long)INT64_MIN - b) {
    return (long long)INT64_MIN;
  }
  return a + b;
}

static uint64_t ryu_bits_from_double_local(double x) {
  uint64_t bits;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

static int ryu_status_is_value_or_range(ryu_parse_status st) {
  return st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT || st == RYU_PARSE_OVERFLOW || st == RYU_PARSE_UNDERFLOW;
}

static uint64_t ryu_status_to_bits(ryu_parse_status st, double value, int negative) {
  if (st == RYU_PARSE_OVERFLOW) {
    uint64_t bits = UINT64_C(0x7ff0000000000000);
    if (negative) {
      bits |= UINT64_C(1) << 63u;
    }
    return bits;
  }
  if (st == RYU_PARSE_UNDERFLOW) {
    return negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
  }
  return ryu_bits_from_double_local(value);
}

static ryu64_parse_result ryu_result_from_convert_status(
    ryu_parse_status st,
    int negative,
    size_t parsed_len,
    double value) {
  if (st == RYU_PARSE_OVERFLOW) {
    return ryu_parse_overflow_result(negative, parsed_len);
  }
  if (st == RYU_PARSE_UNDERFLOW) {
    return ryu_parse_underflow_result(negative, parsed_len);
  }
  if (st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT) {
    return ryu_parse_result_make(st, value, parsed_len);
  }
  return ryu_parse_result_make(st, 0.0, parsed_len);
}

static ryu_parse_status ryu_convert_decimal_bigint_to_double(
    int negative,
    const ryu_bigint* sig,
    uint32_t sig_increment,
    long long exp10,
    ryu_parse_bigint_ws* ws,
    double* out_value) {
  /*
   * ws->num/ws->den are scratch and are overwritten on every call.
   * Callers may invoke this repeatedly with the same workspace (e.g. lower and
   * upper truncated-interval probes). Each call always reconstructs state from
   * the provided source significand.
   */
  ryu_bigint* num = &ws->num;
  ryu_bigint* den = &ws->den;
  long long sig_digits;
  long long dec_exp10;
  int exp2_adjust = 0;

  if (num != sig) {
    ryu_bigint_copy(num, sig);
  }
  if (sig_increment != 0u && !ryu_bigint_add_small(num, sig_increment)) {
    return RYU_PARSE_OUT_OF_RANGE;
  }

  sig_digits = (long long)ryu_bigint_decimal_len(num);
  if (sig_digits <= 0) {
    return RYU_PARSE_INVALID;
  }

  dec_exp10 = ryu_add_ll_saturate(sig_digits - 1, exp10);
  if (dec_exp10 > 309) {
    return RYU_PARSE_OVERFLOW;
  }
  if (dec_exp10 < -325) {
    return RYU_PARSE_UNDERFLOW;
  }

  while (exp10 < 0 && num->len != 0u && (num->limb[0] % 10u) == 0u) {
    if (!ryu_bigint_div_small_exact(num, 10u)) {
      break;
    }
    exp10 += 1;
  }

  ryu_bigint_from_u64(den, UINT64_C(1));

  if (exp10 >= 0) {
    if (exp10 > 1000000) {
      return RYU_PARSE_OVERFLOW;
    }
    if (!ryu_bigint_mul_pow5(num, (unsigned)exp10)) {
      return RYU_PARSE_OVERFLOW;
    }
    exp2_adjust = (int)exp10;
  } else {
    long long abs_exp10 = -exp10;
    long long den_pow5;
    long long den_pow2;
    if (abs_exp10 > 1000000) {
      return RYU_PARSE_UNDERFLOW;
    }
    den_pow5 = abs_exp10;
    den_pow2 = abs_exp10;
    while (den_pow5 > 0 && num->len != 0u && (num->limb[0] % 5u) == 0u) {
      if (!ryu_bigint_div_small_exact(num, 5u)) {
        break;
      }
      den_pow5 -= 1;
    }
    while (den_pow2 > 0 && num->len != 0u && (num->limb[0] % 2u) == 0u) {
      if (!ryu_bigint_div_small_exact(num, 2u)) {
        break;
      }
      den_pow2 -= 1;
    }
    if (!ryu_bigint_mul_pow5(den, (unsigned)den_pow5)) {
      if (dec_exp10 < -325) {
        return RYU_PARSE_UNDERFLOW;
      }
      return RYU_PARSE_OUT_OF_RANGE;
    }
    exp2_adjust -= (int)den_pow2;
  }

  return ryu_convert_ratio_to_double(negative, num, den, exp2_adjust, ws, out_value);
}

static ryu64_parse_result ryu_resolve_truncated_decimal(const ryu_full_parsed* p, ryu_parse_bigint_ws* ws) {
  long long kept_digits;
  long long exp10_adj;
  ryu_parse_status st_lo;
  double lo_value = 0.0;

  kept_digits = p->sig_digits - p->dropped_digits;
  if (kept_digits <= 0) {
    return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, p->parsed_len);
  }
  exp10_adj = ryu_add_ll_saturate(p->exp10, p->dropped_digits);

  st_lo = ryu_convert_decimal_bigint_to_double(p->negative, &p->sig, 0u, exp10_adj, ws, &lo_value);
  if (!p->dropped_nonzero) {
    return ryu_result_from_convert_status(st_lo, p->negative, p->parsed_len, lo_value);
  }
  if ((st_lo == RYU_PARSE_OK || st_lo == RYU_PARSE_INEXACT) && exp10_adj <= -350) {
    return ryu_parse_result_make(RYU_PARSE_INEXACT, lo_value, p->parsed_len);
  }

  {
    ryu_parse_status st_hi;
    double hi_value = 0.0;
    uint64_t lo_bits;
    uint64_t hi_bits;
    ryu_parse_status merged_status;

    st_hi = ryu_convert_decimal_bigint_to_double(p->negative, &p->sig, 1u, exp10_adj, ws, &hi_value);

    if (!ryu_status_is_value_or_range(st_lo) || !ryu_status_is_value_or_range(st_hi)) {
      return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, p->parsed_len);
    }

    lo_bits = ryu_status_to_bits(st_lo, lo_value, p->negative);
    hi_bits = ryu_status_to_bits(st_hi, hi_value, p->negative);
    if (lo_bits != hi_bits) {
      return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, p->parsed_len);
    }

    if (st_lo == RYU_PARSE_OVERFLOW || st_hi == RYU_PARSE_OVERFLOW) {
      return ryu_parse_overflow_result(p->negative, p->parsed_len);
    }
    if (st_lo == RYU_PARSE_UNDERFLOW || st_hi == RYU_PARSE_UNDERFLOW) {
      return ryu_parse_underflow_result(p->negative, p->parsed_len);
    }

    merged_status = (st_lo == RYU_PARSE_INEXACT || st_hi == RYU_PARSE_INEXACT) ? RYU_PARSE_INEXACT : RYU_PARSE_OK;
    return ryu_parse_result_make(merged_status, lo_value, p->parsed_len);
  }
}

static int ryu_bigint_mul10_add_digit(ryu_bigint* a, unsigned digit) {
  uint64_t carry = (uint64_t)digit;
  size_t i;

  if (digit > 9u || a->len == 0u) {
    return 0;
  }

  if (a->len == RYU_BIGINT_MAX_LIMBS) {
    uint64_t probe = carry;
    for (i = 0u; i < a->len; ++i) {
      uint64_t v = (uint64_t)a->limb[i] * UINT64_C(10) + probe;
      probe = v / (uint64_t)RYU_BIGINT_BASE;
    }
    if (probe != 0u) {
      return 0;
    }
  }

  for (i = 0u; i < a->len; ++i) {
    uint64_t v = (uint64_t)a->limb[i] * UINT64_C(10) + carry;
    a->limb[i] = (uint32_t)(v % (uint64_t)RYU_BIGINT_BASE);
    carry = v / (uint64_t)RYU_BIGINT_BASE;
  }

  while (carry != 0u) {
    if (a->len >= RYU_BIGINT_MAX_LIMBS) {
      return 0;
    }
    a->limb[a->len] = (uint32_t)(carry % (uint64_t)RYU_BIGINT_BASE);
    a->len += 1u;
    carry /= (uint64_t)RYU_BIGINT_BASE;
  }

#if defined(RYU_ENABLE_BIGINT_PROFILE)
  ryu_bigint_profile_note_len(a->len);
#endif
  return 1;
}

static int ryu_parse_append_digit(ryu_full_parsed* p, unsigned digit) {

  if (p->truncated) {
    if (p->dropped_digits < (long long)INT64_MAX) {
      p->dropped_digits += 1;
    }
    if (digit != 0u) {
      p->dropped_nonzero = 1;
    }
    return 1;
  }
  if (p->sig_is_u64) {
    if (p->sig_u64 <= (UINT64_MAX - (uint64_t)digit) / UINT64_C(10)) {
      p->sig_u64 = p->sig_u64 * UINT64_C(10) + (uint64_t)digit;
      return 1;
    }
    p->sig_is_u64 = 0;
    ryu_bigint_from_u64(&p->sig, p->sig_u64);
  }
  if (!ryu_bigint_mul10_add_digit(&p->sig, digit)) {
    p->truncated = 1;
    p->dropped_digits = 1;
    p->dropped_nonzero = (digit != 0u);
  }
  return 1;
}

static ryu_parse_status ryu_parse_full_decimal_lex(
    const char* s,
    size_t n,
    ryu_full_parsed* out) {
  size_t i = 0u;
  size_t end_pos;

  out->negative = 0;
  out->special = 0;
  out->saw_any_digit = 0;
  out->saw_nonzero = 0;
  out->sig_is_u64 = 1;
  out->truncated = 0;
  out->dropped_nonzero = 0;
  out->sig_u64 = 0u;
  out->sig_digits = 0;
  out->dropped_digits = 0;
  out->exp10 = 0;
  out->parsed_len = 0u;
  ryu_bigint_zero(&out->sig);

  while (i < n && ryu_ascii_isspace(s[i])) {
    i += 1u;
  }
  if (i == n) {
    return RYU_PARSE_INVALID;
  }

  if (s[i] == '+' || s[i] == '-') {
    out->negative = (s[i] == '-');
    i += 1u;
  }

  if (i < n && ryu_ascii_lower(s[i]) == 'i') {
    size_t p = 0u;
    if (ryu_ascii_match_ci(s, n, i, "infinity", &p) || ryu_ascii_match_ci(s, n, i, "inf", &p)) {
      out->special = 1;
      out->parsed_len = p;
      return RYU_PARSE_OK;
    }
  }

  if (i < n && ryu_ascii_lower(s[i]) == 'n') {
    size_t p = 0u;
    if (ryu_ascii_match_ci(s, n, i, "nan", &p)) {
      if (p < n && s[p] == '(') {
        size_t j = p + 1u;
        while (j < n) {
          char c = s[j];
          int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
          if (!ok) {
            break;
          }
          j += 1u;
        }
        if (j < n && s[j] == ')') {
          p = j + 1u;
        }
      }
      out->special = 2;
      out->parsed_len = p;
      return RYU_PARSE_OK;
    }
  }

  while (i < n && ryu_ascii_isdigit(s[i])) {
    unsigned d = (unsigned)(s[i] - '0');
    out->saw_any_digit = 1;
    if (d != 0u || out->saw_nonzero) {
      out->saw_nonzero = 1;
      out->sig_digits += 1;
      ryu_parse_append_digit(out, d);
    }
    i += 1u;
  }

  if (i < n && s[i] == '.') {
    i += 1u;
    while (i < n && ryu_ascii_isdigit(s[i])) {
      unsigned d = (unsigned)(s[i] - '0');
      out->saw_any_digit = 1;
      out->exp10 -= 1;
      if (d != 0u || out->saw_nonzero) {
        out->saw_nonzero = 1;
        out->sig_digits += 1;
        ryu_parse_append_digit(out, d);
      }
      i += 1u;
    }
  }

  if (!out->saw_any_digit) {
    return RYU_PARSE_INVALID;
  }

  end_pos = i;
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    size_t j = i + 1u;
    int exp_neg = 0;
    int have_exp_digit = 0;
    int exp_part = 0;

    if (j < n && (s[j] == '+' || s[j] == '-')) {
      exp_neg = (s[j] == '-');
      j += 1u;
    }

    while (j < n && ryu_ascii_isdigit(s[j])) {
      have_exp_digit = 1;
      if (exp_part < 100000000) {
        exp_part = exp_part * 10 + (int)(s[j] - '0');
      }
      j += 1u;
    }

    if (have_exp_digit) {
      if (exp_neg) {
        out->exp10 -= (long long)exp_part;
      } else {
        out->exp10 += (long long)exp_part;
      }
      end_pos = j;
    }
  }

  out->parsed_len = end_pos;
  if (!out->saw_nonzero) {
    out->sig_is_u64 = 1;
    out->sig_u64 = 0u;
    ryu_bigint_zero(&out->sig);
  }
  return RYU_PARSE_OK;
}

/*
 * Computes floor(log2(num / den)) for positive bigints.
 * Uses limb-length bounds and incremental step probing to avoid
 * repeatedly shifting from the original operands for each probe.
 */
static int ryu_floor_log2_ratio(
    const ryu_bigint* num,
    const ryu_bigint* den,
    ryu_bigint* cur,
    ryu_bigint* cand,
    int* out_q) {
  const int kMaxPosQ = 32767;
  const int kMaxNegQ = 32768;
  int cmp0 = ryu_bigint_cmp(num, den);
  if (cmp0 >= 0) {
    int q = 0;
    int step = 1;
    int q_bound;

    {
      long long len_diff = (long long)num->len - (long long)den->len;
      long long bound_ll = len_diff * 30ll + 29ll;
      if (bound_ll < 1ll) {
        q_bound = 1;
      } else if (bound_ll > (long long)kMaxPosQ) {
        q_bound = kMaxPosQ;
      } else {
        q_bound = (int)bound_ll;
      }
    }

    ryu_bigint_copy(cur, den);
    while (step <= (q_bound / 2)) {
      step <<= 1;
    }

    while (step > 0) {
      if (q <= (q_bound - step)) {
        ryu_bigint_copy(cand, cur);
        if (ryu_bigint_shl_bits(cand, (unsigned)step) && ryu_bigint_cmp(num, cand) >= 0) {
          q += step;
          ryu_bigint_copy(cur, cand);
        }
      }
      step >>= 1;
    }

    *out_q = q;
    return 1;
  }

  {
    int t = 0;
    int step = 1;
    int t_bound;

    {
      long long len_diff = (long long)den->len - (long long)num->len;
      long long bound_ll = len_diff * 30ll + 30ll;
      if (bound_ll < 1ll) {
        t_bound = 1;
      } else if (bound_ll > (long long)kMaxNegQ) {
        t_bound = kMaxNegQ;
      } else {
        t_bound = (int)bound_ll;
      }
    }

    ryu_bigint_copy(cur, num);
    while (step <= (t_bound / 2)) {
      step <<= 1;
    }

    while (step > 0) {
      if (t <= (t_bound - step)) {
        ryu_bigint_copy(cand, cur);
        if (ryu_bigint_shl_bits(cand, (unsigned)step) && ryu_bigint_cmp(cand, den) < 0) {
          t += step;
          ryu_bigint_copy(cur, cand);
        }
      }
      step >>= 1;
    }

    /*
     * If the bounded search reaches the hard floor, clamp to -32768.
     * Downstream conversion logic treats this as an extreme-ratio signal and
     * maps it through normal overflow/underflow handling.
     */
    if (t == kMaxNegQ) {
      *out_q = -kMaxNegQ;
    } else {
      *out_q = -(t + 1);
    }
  }
  return 1;
}

static int ryu_round_rational_shift_to_u64(
    const ryu_bigint* num,
    const ryu_bigint* den,
    int shift,
    ryu_parse_bigint_ws* ws,
    uint64_t* out,
    int* out_inexact) {
  ryu_bigint* scaled_num = &ws->scratch0;
  ryu_bigint* scaled_den = &ws->scratch1;
  ryu_bigint* tmp = &ws->scratch2;
  ryu_bigint* rem2 = &ws->scratch3;
  uint64_t value = 0u;
  int q;
  int cmp_half;
  int exact;

  ryu_bigint_copy(scaled_num, num);
  ryu_bigint_copy(scaled_den, den);
  if (shift >= 0) {
    if (!ryu_bigint_shl_bits(scaled_num, (unsigned)shift)) {
      return 0;
    }
  } else {
    if (!ryu_bigint_shl_bits(scaled_den, (unsigned)(-shift))) {
      return 0;
    }
  }

  if (!ryu_floor_log2_ratio(scaled_num, scaled_den, tmp, rem2, &q)) {
    return 0;
  }

  if (q >= 64) {
    return 0;
  }

  if (q >= 0) {
    int bit;
    for (bit = q; bit >= 0; --bit) {
      ryu_bigint_copy(tmp, scaled_den);
      if (!ryu_bigint_shl_bits(tmp, (unsigned)bit)) {
        continue;
      }
      if (ryu_bigint_cmp(scaled_num, tmp) >= 0) {
        if (!ryu_bigint_sub(scaled_num, tmp)) {
          return 0;
        }
        value |= (UINT64_C(1) << (unsigned)bit);
      }
    }
  }

  exact = ryu_bigint_is_zero(scaled_num);
  ryu_bigint_copy(rem2, scaled_num);
  if (!ryu_bigint_mul_small(rem2, 2u)) {
    return 0;
  }
  cmp_half = ryu_bigint_cmp(rem2, scaled_den);
  if (cmp_half > 0 || (cmp_half == 0 && (value & UINT64_C(1)) != 0u)) {
    if (value == UINT64_MAX) {
      return 0;
    }
    value += UINT64_C(1);
  }

  *out = value;
  *out_inexact = (!exact) || (cmp_half == 0);
  return 1;
}

static ryu_parse_status ryu_convert_ratio_to_double(
    int negative,
    const ryu_bigint* num,
    const ryu_bigint* den,
    int exp2_adjust,
    ryu_parse_bigint_ws* ws,
    double* out_value) {
  int q_ratio;
  long long q_total_ll;
  int q_total;

  if (!ryu_floor_log2_ratio(num, den, &ws->scratch2, &ws->scratch3, &q_ratio)) {
    return RYU_PARSE_OUT_OF_RANGE;
  }

  q_total_ll = (long long)q_ratio + (long long)exp2_adjust;
  if (q_total_ll > (long long)INT32_MAX || q_total_ll < (long long)INT32_MIN) {
    return q_total_ll > 0 ? RYU_PARSE_OVERFLOW : RYU_PARSE_UNDERFLOW;
  }
  q_total = (int)q_total_ll;

  if (q_total > 1023) {
    return RYU_PARSE_OVERFLOW;
  }

  if (q_total < -1022) {
    uint64_t sub = 0u;
    int inexact = 0;
    uint64_t bits;
    int shift = 1074 + exp2_adjust;
    if (!ryu_round_rational_shift_to_u64(num, den, shift, ws, &sub, &inexact)) {
      return RYU_PARSE_OUT_OF_RANGE;
    }
    if (sub == 0u) {
      bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
      *out_value = ryu_double_from_bits_local(bits);
      return RYU_PARSE_UNDERFLOW;
    }
    if (sub >= (UINT64_C(1) << 52u)) {
      bits = (UINT64_C(1) << 52u) | (sub - (UINT64_C(1) << 52u));
    } else {
      bits = sub;
    }
    if (negative) {
      bits |= UINT64_C(1) << 63u;
    }
    *out_value = ryu_double_from_bits_local(bits);
    return inexact ? RYU_PARSE_INEXACT : RYU_PARSE_OK;
  }

  {
    ryu_bigint* den_norm = &ws->scratch0;
    ryu_bigint* rem = &ws->scratch1;
    const ryu_bigint* den_ref;
    uint64_t frac = 0u;
    uint64_t mant;
    int guard = 0;
    int round_bit = 0;
    int sticky;
    uint64_t bits;
    int i;

    if (q_ratio >= 0) {
      ryu_bigint_copy(den_norm, den);
      if (!ryu_bigint_shl_bits(den_norm, (unsigned)q_ratio)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      if (ryu_bigint_cmp(num, den_norm) < 0) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      ryu_bigint_copy(rem, num);
      if (!ryu_bigint_sub(rem, den_norm)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      den_ref = den_norm;
    } else {
      ryu_bigint_copy(rem, num);
      if (!ryu_bigint_shl_bits(rem, (unsigned)(-q_ratio))) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      if (ryu_bigint_cmp(rem, den) < 0) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      if (!ryu_bigint_sub(rem, den)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      den_ref = den;
    }

    for (i = 0; i < 52; ++i) {
      if (!ryu_bigint_mul_small(rem, 2u)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      frac <<= 1u;
      if (ryu_bigint_cmp(rem, den_ref) >= 0) {
        if (!ryu_bigint_sub(rem, den_ref)) {
          return RYU_PARSE_OUT_OF_RANGE;
        }
        frac |= UINT64_C(1);
      }
    }

    if (!ryu_bigint_mul_small(rem, 2u)) {
      return RYU_PARSE_OUT_OF_RANGE;
    }
    if (ryu_bigint_cmp(rem, den_ref) >= 0) {
      if (!ryu_bigint_sub(rem, den_ref)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      guard = 1;
    }

    if (!ryu_bigint_mul_small(rem, 2u)) {
      return RYU_PARSE_OUT_OF_RANGE;
    }
    if (ryu_bigint_cmp(rem, den_ref) >= 0) {
      if (!ryu_bigint_sub(rem, den_ref)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      round_bit = 1;
    }

    sticky = !ryu_bigint_is_zero(rem);

    mant = (UINT64_C(1) << 52u) | frac;
    if (guard && (round_bit || sticky || ((mant & UINT64_C(1)) != 0u))) {
      mant += UINT64_C(1);
    }
    if (mant == (UINT64_C(1) << 53u)) {
      mant >>= 1u;
      q_total += 1;
    }

    if (q_total > 1023) {
      return RYU_PARSE_OVERFLOW;
    }

    bits = ((uint64_t)(q_total + 1023) << 52u) | (mant & ((UINT64_C(1) << 52u) - UINT64_C(1)));
    if (negative) {
      bits |= UINT64_C(1) << 63u;
    }
    *out_value = ryu_double_from_bits_local(bits);

    return (guard || round_bit || sticky) ? RYU_PARSE_INEXACT : RYU_PARSE_OK;
  }
}

ryu64_parse_result ryu64_from_decimal_full(const char* s, size_t n) {
  ryu_parse_bigint_ws ws;
  ryu_bigint sig_storage;
  const ryu_bigint* sig_ref;
  ryu_full_parsed p;
  ryu_parse_status st;
  long long dec_exp10;
  double value = 0.0;

  if (s == NULL) {
    return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
  }

  st = ryu_parse_full_decimal_lex(s, n, &p);
  if (st != RYU_PARSE_OK) {
    return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
  }

  if (p.special == 1) {
    uint64_t bits = UINT64_C(0x7ff0000000000000);
    if (p.negative) {
      bits |= UINT64_C(1) << 63u;
    }
    return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), p.parsed_len);
  }
  if (p.special == 2) {
    {
      uint64_t bits = UINT64_C(0x7ff8000000000000);
      if (p.negative) {
        bits |= UINT64_C(1) << 63u;
      }
      return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), p.parsed_len);
    }
  }

  if (!p.saw_nonzero) {
    return ryu_parse_zero_result(p.negative, p.parsed_len);
  }

  /* Eisel-Lemire fast path: handles <= 19-digit significands with
   * exponents in [-342, 308] using a single 64x128 multiply + table lookup.
   * Falls back (returns OUT_OF_RANGE) for subnormals and edge cases. */
  if (!p.truncated &&
      p.sig_digits > 0 &&
      p.sig_digits <= (long long)RYU_PARSE_TINY_MAX_SIG_DIGITS &&
      p.exp10 >= (long long)RYU_EISEL_LEMIRE_MIN_EXP10 &&
      p.exp10 <= (long long)RYU_EISEL_LEMIRE_MAX_EXP10) {
    uint64_t m = 0u;
    if (p.sig_is_u64) {
      m = p.sig_u64;
    } else if (!ryu_bigint_to_u64(&p.sig, &m)) {
      m = 0u;
    }
    if (m != 0u) {
      st = ryu_eisel_lemire_to_double(p.negative, m, (int)p.exp10, &value);
    } else {
      st = RYU_PARSE_INVALID;
    }
    if (st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT) {
      return ryu_parse_result_make(st, value, p.parsed_len);
    }
    if (st == RYU_PARSE_OVERFLOW) {
      return ryu_parse_overflow_result(p.negative, p.parsed_len);
    }
    if (st == RYU_PARSE_UNDERFLOW) {
      return ryu_parse_underflow_result(p.negative, p.parsed_len);
    }
    /* OUT_OF_RANGE: fall through to existing fast path or bigint path. */
  }

  /* Original fixed-width fast path: handles exp10 in [-38, 38]. */
  if (!p.truncated &&
      p.sig_digits > 0 &&
      p.sig_digits <= (long long)RYU_PARSE_TINY_MAX_SIG_DIGITS &&
      p.exp10 >= -(long long)RYU_PARSE_FAST_MAX_NEG_EXP10 &&
      p.exp10 <= (long long)RYU_PARSE_FAST_MAX_POS_EXP10) {
    uint64_t m = 0u;
    if (p.sig_is_u64) {
      m = p.sig_u64;
    } else if (!ryu_bigint_to_u64(&p.sig, &m)) {
      m = 0u;
    }
    if (m != 0u) {
      st = ryu_fast_mq_to_double(p.negative, m, (int)p.exp10, &value);
    } else {
      st = RYU_PARSE_INVALID;
    }
    if (st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT) {
      return ryu_parse_result_make(st, value, p.parsed_len);
    }
    if (st == RYU_PARSE_OVERFLOW) {
      return ryu_parse_overflow_result(p.negative, p.parsed_len);
    }
    if (st == RYU_PARSE_UNDERFLOW) {
      return ryu_parse_underflow_result(p.negative, p.parsed_len);
    }
  }

  dec_exp10 = ryu_add_ll_saturate(p.sig_digits - 1, p.exp10);
  if (dec_exp10 > 309) {
    return ryu_parse_overflow_result(p.negative, p.parsed_len);
  }
  if (dec_exp10 < -325) {
    return ryu_parse_underflow_result(p.negative, p.parsed_len);
  }

  if (p.truncated) {
    return ryu_resolve_truncated_decimal(&p, &ws);
  }

  if (p.sig_is_u64) {
    ryu_bigint_from_u64(&sig_storage, p.sig_u64);
    sig_ref = &sig_storage;
  } else {
    sig_ref = &p.sig;
  }

  st = ryu_convert_decimal_bigint_to_double(p.negative, sig_ref, 0u, p.exp10, &ws, &value);
  return ryu_result_from_convert_status(st, p.negative, p.parsed_len, value);
}

#else

ryu64_parse_result ryu64_from_decimal_full(const char* s, size_t n) {
  ryu64_parse_result tiny = ryu64_from_decimal_tiny(s, n);
  if (tiny.status == RYU_PARSE_OUT_OF_RANGE) {
    tiny.status = RYU_PARSE_UNSUPPORTED;
  }
  return tiny;
}

#endif
