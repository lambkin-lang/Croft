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

static ryu_parse_status ryu_tiny_mq_to_double(
    int negative,
    uint64_t m,
    int exp10,
    double* out_value) {
  ryu_u128 numer;
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

  if (exp10 >= 0) {
    numer = ryu_mul_u64_u64_128(m, ryu64_pow10_u64[(size_t)exp10]);
    denom = ryu_u128_from_u64(1u);
  } else {
    numer = ryu_u128_from_u64(m);
    denom = ryu_u128_from_u64(ryu64_pow10_u64[(size_t)(-exp10)]);
  }

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

ryu64_parse_result ryu64_from_decimal_tiny(const char* s, size_t n) {
  size_t i = 0u;
  size_t sign_pos;
  int negative = 0;
  size_t token_start;
  size_t end_pos;
  int saw_any_digit = 0;
  int saw_nonzero = 0;
  int too_many_sig = 0;
  uint64_t sig = 0u;
  unsigned sig_digits = 0u;
  long long exp10 = 0;

  if (s == NULL) {
    return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
  }

  while (i < n && ryu_ascii_isspace(s[i])) {
    i += 1u;
  }
  if (i == n) {
    return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
  }

  sign_pos = i;
  if (s[i] == '+' || s[i] == '-') {
    negative = (s[i] == '-');
    i += 1u;
  }
  token_start = i;

  if (i < n && ryu_ascii_lower(s[i]) == 'i') {
    size_t p = 0u;
    if (ryu_ascii_match_ci(s, n, i, "infinity", &p) || ryu_ascii_match_ci(s, n, i, "inf", &p)) {
      uint64_t bits = UINT64_C(0x7ff0000000000000);
      if (negative) {
        bits |= UINT64_C(1) << 63u;
      }
      return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), p);
    }
  }

  if (i < n && ryu_ascii_lower(s[i]) == 'n') {
    size_t p = 0u;
    if (ryu_ascii_match_ci(s, n, i, "nan", &p)) {
      uint64_t bits = UINT64_C(0x7ff8000000000000);
      if (negative) {
        bits |= UINT64_C(1) << 63u;
      }
      return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), p);
    }
  }

  while (i < n && ryu_ascii_isdigit(s[i])) {
    unsigned d = (unsigned)(s[i] - '0');
    saw_any_digit = 1;
    if (d != 0u || saw_nonzero) {
      saw_nonzero = 1;
      if (sig_digits < RYU_PARSE_TINY_MAX_SIG_DIGITS) {
        sig = (sig * UINT64_C(10)) + (uint64_t)d;
        sig_digits += 1u;
      } else {
        too_many_sig = 1;
      }
    }
    i += 1u;
  }

  if (i < n && s[i] == '.') {
    i += 1u;
    while (i < n && ryu_ascii_isdigit(s[i])) {
      unsigned d = (unsigned)(s[i] - '0');
      saw_any_digit = 1;
      if (d != 0u || saw_nonzero) {
        saw_nonzero = 1;
        if (sig_digits < RYU_PARSE_TINY_MAX_SIG_DIGITS) {
          sig = (sig * UINT64_C(10)) + (uint64_t)d;
          sig_digits += 1u;
        } else {
          too_many_sig = 1;
        }
        exp10 -= 1;
      } else {
        exp10 -= 1;
      }
      i += 1u;
    }
  }

  if (!saw_any_digit) {
    (void)sign_pos;
    (void)token_start;
    return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
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
      if (exp_part < 1000000) {
        exp_part = (exp_part * 10) + (int)(s[j] - '0');
      }
      j += 1u;
    }

    if (have_exp_digit) {
      if (exp_neg) {
        exp10 -= (long long)exp_part;
      } else {
        exp10 += (long long)exp_part;
      }
      end_pos = j;
    }
  }

  if (!saw_nonzero) {
    uint64_t bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
    return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), end_pos);
  }

  if (too_many_sig) {
    return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, end_pos);
  }

  if (exp10 < (long long)RYU_PARSE_TINY_MIN_EXP10 || exp10 > (long long)RYU_PARSE_TINY_MAX_EXP10) {
    return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, end_pos);
  }

  {
    double value = 0.0;
    ryu_parse_status st = ryu_tiny_mq_to_double(negative, sig, (int)exp10, &value);
    if (st == RYU_PARSE_OVERFLOW || st == RYU_PARSE_UNDERFLOW) {
      return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, end_pos);
    }
    if (st == RYU_PARSE_INVALID) {
      return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
    }
    return ryu_parse_result_make(st, value, end_pos);
  }
}
