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

#ifndef RYU64_PARSE_INTERNAL_H
#define RYU64_PARSE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ryu64.h"

#define RYU_PARSE_TINY_MAX_SIG_DIGITS 19
#define RYU_PARSE_TINY_MIN_EXP10 (-19)
#define RYU_PARSE_TINY_MAX_EXP10 19
#define RYU_PARSE_FAST_MAX_NEG_EXP10 38
#define RYU_PARSE_FAST_MAX_POS_EXP10 38

#ifdef RYU64_ENABLE_PARSE_BIGINT
#define RYU_EISEL_LEMIRE_MIN_EXP10 (-342)
#define RYU_EISEL_LEMIRE_MAX_EXP10 308
#define RYU_EISEL_LEMIRE_TABLE_OFFSET 342
#endif

typedef struct {
  uint64_t hi;
  uint64_t lo;
} ryu_u128;

extern const uint64_t ryu64_pow10_u64[20];
extern const ryu_u128 ryu64_pow10_u128[39];

#ifdef RYU64_ENABLE_PARSE_BIGINT
extern const ryu_u128 ryu64_pow5_128[651];
#endif

static inline ryu_u128 ryu_u128_from_u64(uint64_t x) {
  ryu_u128 v;
  v.hi = 0u;
  v.lo = x;
  return v;
}

static inline int ryu_u128_is_zero(const ryu_u128* v) {
  return v->hi == 0u && v->lo == 0u;
}

static inline int ryu_u128_cmp(const ryu_u128* a, const ryu_u128* b) {
  if (a->hi < b->hi) {
    return -1;
  }
  if (a->hi > b->hi) {
    return 1;
  }
  if (a->lo < b->lo) {
    return -1;
  }
  if (a->lo > b->lo) {
    return 1;
  }
  return 0;
}

static inline ryu_u128 ryu_u128_sub(const ryu_u128* a, const ryu_u128* b) {
  ryu_u128 out;
  out.hi = a->hi - b->hi - (a->lo < b->lo ? 1u : 0u);
  out.lo = a->lo - b->lo;
  return out;
}

static inline ryu_u128 ryu_u128_shl(const ryu_u128* a, unsigned shift) {
  ryu_u128 out;
  if (shift == 0u) {
    return *a;
  }
  if (shift >= 128u) {
    out.hi = 0u;
    out.lo = 0u;
    return out;
  }
  if (shift >= 64u) {
    out.hi = a->lo << (shift - 64u);
    out.lo = 0u;
    return out;
  }
  out.hi = (a->hi << shift) | (a->lo >> (64u - shift));
  out.lo = a->lo << shift;
  return out;
}

static inline ryu_u128 ryu_u128_shl1(const ryu_u128* a) {
  ryu_u128 out;
  out.hi = (a->hi << 1u) | (a->lo >> 63u);
  out.lo = a->lo << 1u;
  return out;
}

static inline unsigned ryu_log2_u64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return 63u - (unsigned)__builtin_clzll(x);
#else
  unsigned p = 0u;
  while (x > 1u) {
    x >>= 1u;
    p += 1u;
  }
  return p;
#endif
}

static inline unsigned ryu_u128_bitlen(const ryu_u128* v) {
  if (v->hi != 0u) {
    return 65u + ryu_log2_u64(v->hi);
  }
  if (v->lo != 0u) {
    return 1u + ryu_log2_u64(v->lo);
  }
  return 0u;
}

static inline ryu_u128 ryu_mul_u64_u64_128(uint64_t a, uint64_t b) {
  ryu_u128 out;
#ifdef __SIZEOF_INT128__
  __uint128_t p = ((__uint128_t)a) * ((__uint128_t)b);
  out.lo = (uint64_t)p;
  out.hi = (uint64_t)(p >> 64u);
#else
  uint64_t a_lo = a & UINT64_C(0xffffffff);
  uint64_t a_hi = a >> 32u;
  uint64_t b_lo = b & UINT64_C(0xffffffff);
  uint64_t b_hi = b >> 32u;

  uint64_t p0 = a_lo * b_lo;
  uint64_t p1 = a_lo * b_hi;
  uint64_t p2 = a_hi * b_lo;
  uint64_t p3 = a_hi * b_hi;

  uint64_t mid = (p0 >> 32u) + (p1 & UINT64_C(0xffffffff)) + (p2 & UINT64_C(0xffffffff));
  out.hi = p3 + (p1 >> 32u) + (p2 >> 32u) + (mid >> 32u);
  out.lo = (p0 & UINT64_C(0xffffffff)) | (mid << 32u);
#endif
  return out;
}

static inline char ryu_ascii_lower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static inline int ryu_ascii_isdigit(char c) {
  return c >= '0' && c <= '9';
}

static inline int ryu_ascii_isspace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static inline int ryu_ascii_match_ci(
    const char* s,
    size_t n,
    size_t pos,
    const char* lit,
    size_t* out_end) {
  size_t i = 0u;
  while (lit[i] != '\0') {
    if (pos + i >= n) {
      return 0;
    }
    if (ryu_ascii_lower(s[pos + i]) != lit[i]) {
      return 0;
    }
    i += 1u;
  }
  *out_end = pos + i;
  return 1;
}

static inline double ryu_double_from_bits_local(uint64_t bits) {
  double x;
  memcpy(&x, &bits, sizeof(x));
  return x;
}

static inline ryu64_parse_result ryu_parse_result_make(ryu_parse_status status, double value, size_t parsed_len) {
  ryu64_parse_result out;
  out.status = status;
  out.value = value;
  out.parsed_len = parsed_len;
  return out;
}

#endif
