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

#include "ryu64_internal.h"

#include <string.h>

static const uint32_t bg_pow10_small[9] = {
    1u,
    10u,
    100u,
    1000u,
    10000u,
    100000u,
    1000000u,
    10000000u,
    100000000u,
};

static const uint32_t bg_pow5_small[14] = {
    1u,
    5u,
    25u,
    125u,
    625u,
    3125u,
    15625u,
    78125u,
    390625u,
    1953125u,
    9765625u,
    48828125u,
    244140625u,
    1220703125u,
};

#ifdef RYU_ENABLE_BIGINT_PROFILE
#define RYU_BIGINT_PROFILE_SCOPE_FORMAT 0u
#define RYU_BIGINT_PROFILE_SCOPE_PARSE 1u
#define RYU_BIGINT_PROFILE_SCOPE_COUNT 2u

static unsigned g_bigint_profile_active_scope = RYU_BIGINT_PROFILE_SCOPE_COUNT;
static unsigned g_bigint_profile_active_depth = 0u;
static size_t g_bigint_profile_current_peak = 0u;
static size_t g_bigint_profile_last_peak[RYU_BIGINT_PROFILE_SCOPE_COUNT];

void ryu_bigint_profile_begin(unsigned scope) {
  if (scope >= RYU_BIGINT_PROFILE_SCOPE_COUNT) {
    return;
  }
  if (g_bigint_profile_active_depth == 0u) {
    g_bigint_profile_active_scope = scope;
    g_bigint_profile_current_peak = 0u;
  }
  if (g_bigint_profile_active_scope == scope) {
    g_bigint_profile_active_depth += 1u;
  }
}

void ryu_bigint_profile_end(unsigned scope) {
  if (scope >= RYU_BIGINT_PROFILE_SCOPE_COUNT) {
    return;
  }
  if (g_bigint_profile_active_scope != scope || g_bigint_profile_active_depth == 0u) {
    return;
  }
  g_bigint_profile_active_depth -= 1u;
  if (g_bigint_profile_active_depth == 0u) {
    g_bigint_profile_last_peak[scope] = g_bigint_profile_current_peak;
    g_bigint_profile_active_scope = RYU_BIGINT_PROFILE_SCOPE_COUNT;
    g_bigint_profile_current_peak = 0u;
  }
}

size_t ryu_bigint_profile_last_peak(unsigned scope) {
  if (scope >= RYU_BIGINT_PROFILE_SCOPE_COUNT) {
    return 0u;
  }
  return g_bigint_profile_last_peak[scope];
}

void ryu_bigint_profile_note_len(size_t len) {
  if (g_bigint_profile_active_depth != 0u && len > g_bigint_profile_current_peak) {
    g_bigint_profile_current_peak = len;
  }
}

#define RYU_BIGINT_PROFILE_NOTE_LEN(v) ryu_bigint_profile_note_len((v))
#else
#define RYU_BIGINT_PROFILE_NOTE_LEN(v) ((void)(v))
#endif

static void ryu_bigint_normalize(ryu_bigint* v) {
  while (v->len > 1u && v->limb[v->len - 1u] == 0u) {
    v->len -= 1u;
  }
  RYU_BIGINT_PROFILE_NOTE_LEN(v->len);
}

void ryu_bigint_zero(ryu_bigint* v) {
  v->len = 1u;
  v->limb[0] = 0u;
  RYU_BIGINT_PROFILE_NOTE_LEN(v->len);
}

void ryu_bigint_from_u64(ryu_bigint* v, uint64_t x) {
  if (x == 0u) {
    ryu_bigint_zero(v);
    return;
  }
  v->len = 0u;
  while (x != 0u) {
    if (v->len >= RYU_BIGINT_MAX_LIMBS) {
      ryu_bigint_zero(v);
      return;
    }
    v->limb[v->len] = (uint32_t)(x % (uint64_t)RYU_BIGINT_BASE);
    v->len += 1u;
    x /= (uint64_t)RYU_BIGINT_BASE;
  }
  RYU_BIGINT_PROFILE_NOTE_LEN(v->len);
}

int ryu_bigint_is_zero(const ryu_bigint* v) {
  return v->len == 1u && v->limb[0] == 0u;
}

void ryu_bigint_copy(ryu_bigint* dst, const ryu_bigint* src) {
  dst->len = src->len;
  memcpy(dst->limb, src->limb, src->len * sizeof(uint32_t));
  RYU_BIGINT_PROFILE_NOTE_LEN(dst->len);
}

int ryu_bigint_cmp(const ryu_bigint* a, const ryu_bigint* b) {
  size_t i;
  if (a->len < b->len) {
    return -1;
  }
  if (a->len > b->len) {
    return 1;
  }
  i = a->len;
  while (i > 0u) {
    size_t idx = i - 1u;
    if (a->limb[idx] < b->limb[idx]) {
      return -1;
    }
    if (a->limb[idx] > b->limb[idx]) {
      return 1;
    }
    i -= 1u;
  }
  return 0;
}

int ryu_bigint_add(ryu_bigint* a, const ryu_bigint* b) {
  size_t max_len = (a->len > b->len) ? a->len : b->len;
  uint64_t carry = 0u;
  size_t i;
  if (max_len >= RYU_BIGINT_MAX_LIMBS) {
    return 0;
  }
  for (i = 0u; i < max_len || carry != 0u; ++i) {
    uint64_t sum;
    if (i >= RYU_BIGINT_MAX_LIMBS) {
      return 0;
    }
    if (i >= a->len) {
      a->limb[i] = 0u;
      a->len = i + 1u;
    }
    sum = (uint64_t)a->limb[i] + carry;
    if (i < b->len) {
      sum += (uint64_t)b->limb[i];
    }
    if (sum >= (uint64_t)RYU_BIGINT_BASE) {
      a->limb[i] = (uint32_t)(sum - (uint64_t)RYU_BIGINT_BASE);
      carry = 1u;
    } else {
      a->limb[i] = (uint32_t)sum;
      carry = 0u;
    }
  }
  if (a->len < max_len) {
    a->len = max_len;
  }
  ryu_bigint_normalize(a);
  return 1;
}

int ryu_bigint_add_small(ryu_bigint* a, uint32_t b) {
  uint64_t carry = (uint64_t)b;
  size_t i = 0u;
  while (carry != 0u) {
    uint64_t sum;
    if (i >= RYU_BIGINT_MAX_LIMBS) {
      return 0;
    }
    if (i >= a->len) {
      a->limb[i] = 0u;
      a->len = i + 1u;
    }
    sum = (uint64_t)a->limb[i] + carry;
    a->limb[i] = (uint32_t)(sum % (uint64_t)RYU_BIGINT_BASE);
    carry = sum / (uint64_t)RYU_BIGINT_BASE;
    i += 1u;
  }
  RYU_BIGINT_PROFILE_NOTE_LEN(a->len);
  return 1;
}

int ryu_bigint_sub(ryu_bigint* a, const ryu_bigint* b) {
  size_t i;
  uint64_t borrow = 0u;
  if (ryu_bigint_cmp(a, b) < 0) {
    return 0;
  }
  for (i = 0u; i < a->len; ++i) {
    uint64_t av = (uint64_t)a->limb[i];
    uint64_t bv = (i < b->len) ? (uint64_t)b->limb[i] : 0u;
    uint64_t sub = bv + borrow;
    if (av >= sub) {
      a->limb[i] = (uint32_t)(av - sub);
      borrow = 0u;
    } else {
      a->limb[i] = (uint32_t)(av + (uint64_t)RYU_BIGINT_BASE - sub);
      borrow = 1u;
    }
  }
  if (borrow != 0u) {
    return 0;
  }
  ryu_bigint_normalize(a);
  return 1;
}

int ryu_bigint_sub_small(ryu_bigint* a, uint32_t b) {
  uint64_t borrow = (uint64_t)b;
  size_t i = 0u;
  while (borrow != 0u) {
    uint64_t cur;
    if (i >= a->len) {
      return 0;
    }
    cur = (uint64_t)a->limb[i];
    if (cur >= borrow) {
      a->limb[i] = (uint32_t)(cur - borrow);
      borrow = 0u;
    } else {
      a->limb[i] = (uint32_t)(cur + (uint64_t)RYU_BIGINT_BASE - borrow);
      borrow = 1u;
    }
    i += 1u;
  }
  ryu_bigint_normalize(a);
  return 1;
}

int ryu_bigint_mul_small(ryu_bigint* a, uint32_t m) {
  uint64_t carry = 0u;
  size_t i;
  if (m == 0u) {
    ryu_bigint_zero(a);
    return 1;
  }
  if (m == 1u || ryu_bigint_is_zero(a)) {
    RYU_BIGINT_PROFILE_NOTE_LEN(a->len);
    return 1;
  }
  for (i = 0u; i < a->len; ++i) {
    uint64_t prod = ((uint64_t)a->limb[i] * (uint64_t)m) + carry;
    a->limb[i] = (uint32_t)(prod % (uint64_t)RYU_BIGINT_BASE);
    carry = prod / (uint64_t)RYU_BIGINT_BASE;
  }
  while (carry != 0u) {
    if (a->len >= RYU_BIGINT_MAX_LIMBS) {
      return 0;
    }
    a->limb[a->len] = (uint32_t)(carry % (uint64_t)RYU_BIGINT_BASE);
    a->len += 1u;
    carry /= (uint64_t)RYU_BIGINT_BASE;
  }
  RYU_BIGINT_PROFILE_NOTE_LEN(a->len);
  return 1;
}

int ryu_bigint_mul_pow5(ryu_bigint* a, unsigned p) {
  while (p != 0u) {
    unsigned chunk = (p > 13u) ? 13u : p;
    if (!ryu_bigint_mul_small(a, bg_pow5_small[chunk])) {
      return 0;
    }
    p -= chunk;
  }
  return 1;
}

int ryu_bigint_mul_pow10(ryu_bigint* a, unsigned p) {
  unsigned q = p / 9u;
  unsigned r = p % 9u;
  if (ryu_bigint_is_zero(a)) {
    return 1;
  }
  if (r != 0u && !ryu_bigint_mul_small(a, bg_pow10_small[r])) {
    return 0;
  }
  if (q == 0u) {
    RYU_BIGINT_PROFILE_NOTE_LEN(a->len);
    return 1;
  }
  if (a->len + (size_t)q > RYU_BIGINT_MAX_LIMBS) {
    return 0;
  }
  memmove(a->limb + q, a->limb, a->len * sizeof(uint32_t));
  memset(a->limb, 0, (size_t)q * sizeof(uint32_t));
  a->len += (size_t)q;
  RYU_BIGINT_PROFILE_NOTE_LEN(a->len);
  return 1;
}

int ryu_bigint_shl_bits(ryu_bigint* a, unsigned bits) {
  while (bits >= 29u) {
    if (!ryu_bigint_mul_small(a, (uint32_t)(1u << 29u))) {
      return 0;
    }
    bits -= 29u;
  }
  if (bits != 0u) {
    if (!ryu_bigint_mul_small(a, (uint32_t)(1u << bits))) {
      return 0;
    }
  }
  return 1;
}

static int ryu_bigint_div_small(ryu_bigint* a, uint32_t div, uint32_t* rem_out) {
  uint64_t carry = 0u;
  size_t idx;

  if (a->len == 0u || div < 2u) {
    return 0;
  }
  for (idx = a->len; idx > 0u; --idx) {
    uint64_t cur = (carry * (uint64_t)RYU_BIGINT_BASE) + (uint64_t)a->limb[idx - 1u];
    a->limb[idx - 1u] = (uint32_t)(cur / (uint64_t)div);
    carry = cur % (uint64_t)div;
  }
  ryu_bigint_normalize(a);
  if (rem_out != NULL) {
    *rem_out = (uint32_t)carry;
  }
  return 1;
}

#ifdef RYU_ENABLE_POW5_STRIDE_CACHE
static int ryu_bigint_mul_u64(ryu_bigint* a, uint64_t m) {
  uint32_t lo;
  uint64_t hi64;
  uint32_t hi;
  ryu_bigint hi_term;

  if (m == 0u) {
    ryu_bigint_zero(a);
    return 1;
  }
  if (m < (uint64_t)RYU_BIGINT_BASE) {
    return ryu_bigint_mul_small(a, (uint32_t)m);
  }

  lo = (uint32_t)(m % (uint64_t)RYU_BIGINT_BASE);
  hi64 = m / (uint64_t)RYU_BIGINT_BASE;
  if (hi64 > (uint64_t)UINT32_MAX) {
    return 0;
  }
  hi = (uint32_t)hi64;
  if (hi == 0u) {
    return ryu_bigint_mul_small(a, lo);
  }

  ryu_bigint_copy(&hi_term, a);
  if (!ryu_bigint_mul_small(a, lo)) {
    return 0;
  }
  if (!ryu_bigint_mul_small(&hi_term, hi)) {
    return 0;
  }
  if (!ryu_bigint_is_zero(&hi_term)) {
    if (hi_term.len + 1u > RYU_BIGINT_MAX_LIMBS) {
      return 0;
    }
    memmove(hi_term.limb + 1u, hi_term.limb, hi_term.len * sizeof(uint32_t));
    hi_term.limb[0] = 0u;
    hi_term.len += 1u;
    RYU_BIGINT_PROFILE_NOTE_LEN(hi_term.len);
    if (!ryu_bigint_add(a, &hi_term)) {
      return 0;
    }
  }
  return 1;
}
#endif

#ifdef RYU_ENABLE_POW5_STRIDE_CACHE
#ifndef RYU_POW5_STRIDE
#define RYU_POW5_STRIDE 16u
#endif
#if (RYU_POW5_STRIDE == 0u)
#error "RYU_POW5_STRIDE must be > 0"
#endif
#define RYU_POW5_MAX_EXP 1074u
#define RYU_POW5_ANCHOR_COUNT ((RYU_POW5_MAX_EXP / RYU_POW5_STRIDE) + 1u)

static ryu_bigint g_pow5_anchor[RYU_POW5_ANCHOR_COUNT];
static int g_pow5_anchor_state = 0; /* 0=uninitialized, 1=initializing, 2=ready */

static int ryu_init_pow5_anchor_cache(void) {
  unsigned i;
  int expected;

  if (__atomic_load_n(&g_pow5_anchor_state, __ATOMIC_ACQUIRE) == 2) {
    return 1;
  }

  expected = 0;
  if (__atomic_compare_exchange_n(
          &g_pow5_anchor_state,
          &expected,
          1,
          0,
          __ATOMIC_ACQ_REL,
          __ATOMIC_ACQUIRE)) {
    ryu_bigint_from_u64(&g_pow5_anchor[0], 1u);
    for (i = 1u; i < RYU_POW5_ANCHOR_COUNT; ++i) {
      ryu_bigint_copy(&g_pow5_anchor[i], &g_pow5_anchor[i - 1u]);
      if (!ryu_bigint_mul_pow5(&g_pow5_anchor[i], RYU_POW5_STRIDE)) {
        __atomic_store_n(&g_pow5_anchor_state, 0, __ATOMIC_RELEASE);
        return 0;
      }
    }
    __atomic_store_n(&g_pow5_anchor_state, 2, __ATOMIC_RELEASE);
    return 1;
  }

  while (__atomic_load_n(&g_pow5_anchor_state, __ATOMIC_ACQUIRE) == 1) {
    /* spin until the initializing thread publishes state=2 or resets to 0 */
  }
  if (__atomic_load_n(&g_pow5_anchor_state, __ATOMIC_ACQUIRE) == 2) {
    return 1;
  }
  return ryu_init_pow5_anchor_cache();
}

static int ryu_bigint_from_pow5_stride(unsigned p, ryu_bigint* out) {
  unsigned idx;
  unsigned rem;
  if (p > RYU_POW5_MAX_EXP) {
    return 0;
  }
  if (!ryu_init_pow5_anchor_cache()) {
    return 0;
  }
  idx = p / RYU_POW5_STRIDE;
  rem = p % RYU_POW5_STRIDE;
  ryu_bigint_copy(out, &g_pow5_anchor[idx]);
  if (rem != 0u && !ryu_bigint_mul_pow5(out, rem)) {
    return 0;
  }
  return 1;
}
#endif

int ryu_bigint_div_small_exact(ryu_bigint* a, uint32_t div) {
  uint32_t rem = 0u;
  if (!ryu_bigint_div_small(a, div, &rem)) {
    return 0;
  }
  return rem == 0u;
}

int ryu_bigint_to_u64(const ryu_bigint* a, uint64_t* out) {
  uint64_t acc = 0u;
  size_t i = a->len;
  while (i > 0u) {
    size_t idx = i - 1u;
    uint64_t next;
    if (acc > UINT64_MAX / (uint64_t)RYU_BIGINT_BASE) {
      return 0;
    }
    next = (acc * (uint64_t)RYU_BIGINT_BASE) + (uint64_t)a->limb[idx];
    if (next < acc) {
      return 0;
    }
    acc = next;
    i -= 1u;
  }
  *out = acc;
  return 1;
}

unsigned ryu_bigint_decimal_len(const ryu_bigint* a) {
  uint32_t top;
  if (ryu_bigint_is_zero(a)) {
    return 1u;
  }
  top = a->limb[a->len - 1u];
  if (top >= 100000000u) {
    return 9u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  if (top >= 10000000u) {
    return 8u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  if (top >= 1000000u) {
    return 7u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  if (top >= 100000u) {
    return 6u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  if (top >= 10000u) {
    return 5u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  if (top >= 1000u) {
    return 4u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  if (top >= 100u) {
    return 3u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  if (top >= 10u) {
    return 2u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
  }
  return 1u + (unsigned)((a->len - 1u) * (size_t)RYU_BIGINT_BASE_DIGITS);
}

static size_t ryu_write_u32_no_pad(char* out, size_t cap, uint32_t v) {
  char rev[10];
  size_t n = 0u;
  if (cap == 0u) {
    return 0u;
  }
  if (v == 0u) {
    out[0] = '0';
    return 1u;
  }
  while (v != 0u && n < sizeof(rev)) {
    rev[n] = (char)('0' + (v % 10u));
    v /= 10u;
    n += 1u;
  }
  if (n > cap) {
    return 0u;
  }
  {
    size_t i;
    for (i = 0u; i < n; ++i) {
      out[i] = rev[n - 1u - i];
    }
  }
  return n;
}

static void ryu_write_u32_pad9(char* out, uint32_t v) {
  int i;
  for (i = 8; i >= 0; --i) {
    out[(size_t)i] = (char)('0' + (v % 10u));
    v /= 10u;
  }
}

int ryu_bigint_to_decimal(const ryu_bigint* a, char* out, size_t out_cap, size_t* out_len) {
  size_t pos = 0u;
  size_t i;
  size_t n_top;
  if (out_cap == 0u) {
    return 0;
  }
  if (ryu_bigint_is_zero(a)) {
    if (out_cap < 2u) {
      return 0;
    }
    out[0] = '0';
    out[1] = '\0';
    if (out_len != NULL) {
      *out_len = 1u;
    }
    return 1;
  }
  n_top = ryu_write_u32_no_pad(out, out_cap - 1u, a->limb[a->len - 1u]);
  if (n_top == 0u) {
    return 0;
  }
  pos += n_top;
  i = a->len - 1u;
  while (i > 0u) {
    if (pos + 9u >= out_cap) {
      return 0;
    }
    ryu_write_u32_pad9(out + pos, a->limb[i - 1u]);
    pos += 9u;
    i -= 1u;
  }
  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return 1;
}

int ryu_bigint_div_pow10_floor(
    const ryu_bigint* n,
    unsigned pow10,
    ryu_bigint* q,
    int* remainder_is_zero) {
  unsigned q_digits = pow10 / 9u;
  unsigned r = pow10 % 9u;
  int rem_nonzero = 0;
  size_t i;

  if (q_digits >= n->len) {
    ryu_bigint_zero(q);
    if (remainder_is_zero != NULL) {
      *remainder_is_zero = ryu_bigint_is_zero(n);
    }
    return 1;
  }

  q->len = n->len - (size_t)q_digits;
  RYU_BIGINT_PROFILE_NOTE_LEN(q->len);
  for (i = 0u; i < q->len; ++i) {
    q->limb[i] = n->limb[i + (size_t)q_digits];
  }

  for (i = 0u; i < (size_t)q_digits; ++i) {
    if (n->limb[i] != 0u) {
      rem_nonzero = 1;
      break;
    }
  }

  if (r != 0u) {
    uint32_t div = bg_pow10_small[r];
    uint64_t carry = 0u;
    i = q->len;
    while (i > 0u) {
      size_t idx = i - 1u;
      uint64_t cur = (carry * (uint64_t)RYU_BIGINT_BASE) + (uint64_t)q->limb[idx];
      q->limb[idx] = (uint32_t)(cur / (uint64_t)div);
      carry = cur % (uint64_t)div;
      i -= 1u;
    }
    if (carry != 0u) {
      rem_nonzero = 1;
    }
  }

  ryu_bigint_normalize(q);
  if (remainder_is_zero != NULL) {
    *remainder_is_zero = !rem_nonzero;
  }
  return 1;
}

uint64_t ryu_u64_from_double(double x) {
  uint64_t bits;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

double ryu_double_from_u64(uint64_t bits) {
  double x;
  memcpy(&x, &bits, sizeof(x));
  return x;
}

void ryu_decode_fp64(double x, ryu_fp64* out) {
  uint64_t bits = ryu_u64_from_double(x);
  int exp_raw = (int)((bits >> 52u) & 0x7ffu);
  uint64_t frac = bits & ((UINT64_C(1) << 52u) - UINT64_C(1));

  out->sign = (int)(bits >> 63u);
  out->bits = bits;
  out->exp_raw = exp_raw;
  out->mantissa = 0u;
  out->is_nan = 0;
  out->is_inf = 0;
  out->is_zero = 0;
  out->exp2 = 0;

  if (exp_raw == 0x7ff) {
    if (frac == 0u) {
      out->is_inf = 1;
    } else {
      out->is_nan = 1;
    }
    return;
  }

  if (exp_raw == 0) {
    if (frac == 0u) {
      out->is_zero = 1;
      out->exp2 = -1074;
      return;
    }
    out->mantissa = frac;
    out->exp2 = -1074;
    return;
  }

  out->mantissa = (UINT64_C(1) << 52u) | frac;
  out->exp2 = exp_raw - 1075;
}

static int ryu_rational_from_bits(uint64_t bits, ryu_bigint* num, unsigned* den_exp) {
  uint64_t exp_raw = (bits >> 52u) & 0x7ffu;
  uint64_t frac = bits & ((UINT64_C(1) << 52u) - UINT64_C(1));
  uint64_t m;
  int e2;
  if (exp_raw == 0x7ffu) {
    return 0;
  }
  if (exp_raw == 0u) {
    if (frac == 0u) {
      ryu_bigint_zero(num);
      *den_exp = 0u;
      return 1;
    }
    m = frac;
    e2 = -1074;
  } else {
    m = (UINT64_C(1) << 52u) | frac;
    e2 = (int)exp_raw - 1075;
  }
  ryu_bigint_from_u64(num, m);
  if (e2 >= 0) {
    if (!ryu_bigint_shl_bits(num, (unsigned)e2)) {
      return 0;
    }
    *den_exp = 0u;
  } else {
    *den_exp = (unsigned)(-e2);
  }
  return 1;
}

static int ryu_midpoint(
    const ryu_bigint* a_num,
    unsigned a_den,
    const ryu_bigint* b_num,
    unsigned b_den,
    ryu_bigint* out_num,
    unsigned* out_den) {
  unsigned k = (a_den > b_den) ? a_den : b_den;
  ryu_bigint tmp;

  ryu_bigint_copy(out_num, a_num);
  ryu_bigint_copy(&tmp, b_num);

  if (k > a_den && !ryu_bigint_shl_bits(out_num, k - a_den)) {
    return 0;
  }
  if (k > b_den && !ryu_bigint_shl_bits(&tmp, k - b_den)) {
    return 0;
  }
  if (!ryu_bigint_add(out_num, &tmp)) {
    return 0;
  }
  *out_den = k + 1u;
  return 1;
}

int ryu_exact_decimal_from_bits(uint64_t abs_bits, ryu_decimal_exact* out) {
  ryu_fp64 fp;
  double x;
  if ((abs_bits >> 63u) != 0u) {
    return 0;
  }
  x = ryu_double_from_u64(abs_bits);
  ryu_decode_fp64(x, &fp);
  if (fp.is_nan || fp.is_inf) {
    return 0;
  }
  if (fp.is_zero) {
    ryu_bigint_zero(&out->digits);
    out->scale = 0u;
    return 1;
  }

  ryu_bigint_from_u64(&out->digits, fp.mantissa);
  if (fp.exp2 >= 0) {
    if (!ryu_bigint_shl_bits(&out->digits, (unsigned)fp.exp2)) {
      return 0;
    }
    out->scale = 0u;
  } else {
    unsigned d = (unsigned)(-fp.exp2);
#ifdef RYU_ENABLE_POW5_STRIDE_CACHE
    if (!ryu_bigint_from_pow5_stride(d, &out->digits)) {
      return 0;
    }
    if (!ryu_bigint_mul_u64(&out->digits, fp.mantissa)) {
      return 0;
    }
#else
    if (!ryu_bigint_mul_pow5(&out->digits, d)) {
      return 0;
    }
#endif
    out->scale = d;
  }
  return 1;
}

int ryu_decimal_interval_from_bits(uint64_t abs_bits, ryu_decimal_interval* out) {
  ryu_bigint x_num;
  ryu_bigint neighbor_num;
  unsigned x_den;
  unsigned neighbor_den;
  unsigned low_den;
  unsigned high_den;
  unsigned q;
  uint64_t exp_raw;
  uint64_t frac;
  uint64_t m;
  int even;

  if (abs_bits == 0u) {
    return 0;
  }
  if (abs_bits >= UINT64_C(0x7ff0000000000000)) {
    return 0;
  }
  if (abs_bits == UINT64_C(0x7fefffffffffffff)) {
    return 0;
  }

  exp_raw = (abs_bits >> 52u) & 0x7ffu;
  frac = abs_bits & ((UINT64_C(1) << 52u) - UINT64_C(1));
  if (exp_raw == 0u) {
    m = frac;
  } else {
    m = (UINT64_C(1) << 52u) | frac;
  }
  even = ((m & UINT64_C(1)) == UINT64_C(0));

  if (!ryu_rational_from_bits(abs_bits, &x_num, &x_den)) {
    return 0;
  }
  if (!ryu_rational_from_bits(abs_bits - UINT64_C(1), &neighbor_num, &neighbor_den)) {
    return 0;
  }

  if (!ryu_midpoint(&neighbor_num, neighbor_den, &x_num, x_den, &out->low, &low_den)) {
    return 0;
  }
  if (!ryu_rational_from_bits(abs_bits + UINT64_C(1), &neighbor_num, &neighbor_den)) {
    return 0;
  }
  if (!ryu_midpoint(&x_num, x_den, &neighbor_num, neighbor_den, &out->high, &high_den)) {
    return 0;
  }

  q = (low_den > high_den) ? low_den : high_den;

  if (!ryu_bigint_mul_pow5(&out->low, low_den)) {
    return 0;
  }
  if (q > low_den && !ryu_bigint_mul_pow10(&out->low, q - low_den)) {
    return 0;
  }

  if (!ryu_bigint_mul_pow5(&out->high, high_den)) {
    return 0;
  }
  if (q > high_den && !ryu_bigint_mul_pow10(&out->high, q - high_den)) {
    return 0;
  }

  out->scale = q;
  out->low_closed = even;
  out->high_closed = even;
  return 1;
}

int ryu_choose_shortest_digits(
    uint64_t abs_bits,
    uint64_t* out_significand,
    int* out_k,
    int* out_digits) {
  ryu_decimal_interval interval;
  int t;

  if (abs_bits == 0u) {
    *out_significand = 0u;
    *out_k = 0;
    *out_digits = 1;
    return 1;
  }

  if (abs_bits == UINT64_C(0x7fefffffffffffff)) {
    *out_significand = UINT64_C(17976931348623157);
    *out_k = 292;
    *out_digits = 17;
    return 1;
  }

  if (!ryu_decimal_interval_from_bits(abs_bits, &interval)) {
    return 0;
  }

  t = (int)ryu_bigint_decimal_len(&interval.high);
  for (; t >= 0; --t) {
    ryu_bigint n_min;
    ryu_bigint n_max;
    int low_rem_zero = 0;
    int high_rem_zero = 0;

    if (!ryu_bigint_div_pow10_floor(&interval.low, (unsigned)t, &n_min, &low_rem_zero)) {
      return 0;
    }
    if (!ryu_bigint_div_pow10_floor(&interval.high, (unsigned)t, &n_max, &high_rem_zero)) {
      return 0;
    }

    if (!low_rem_zero) {
      if (!ryu_bigint_add_small(&n_min, 1u)) {
        return 0;
      }
    }
    if (!interval.low_closed && low_rem_zero) {
      if (!ryu_bigint_add_small(&n_min, 1u)) {
        return 0;
      }
    }

    if (!interval.high_closed && high_rem_zero) {
      if (ryu_bigint_is_zero(&n_max)) {
        continue;
      }
      if (!ryu_bigint_sub_small(&n_max, 1u)) {
        continue;
      }
    }

    if (ryu_bigint_is_zero(&n_max)) {
      continue;
    }
    if (ryu_bigint_is_zero(&n_min)) {
      if (!ryu_bigint_add_small(&n_min, 1u)) {
        return 0;
      }
    }

    if (ryu_bigint_cmp(&n_min, &n_max) <= 0) {
      uint64_t sig;
      if (!ryu_bigint_to_u64(&n_min, &sig)) {
        return 0;
      }
      *out_significand = sig;
      *out_k = t - (int)interval.scale;
      *out_digits = (int)ryu_bigint_decimal_len(&n_min);
      return 1;
    }
  }

  return 0;
}

static size_t ryu_u64_to_dec(uint64_t x, char* out) {
  char rev[32];
  size_t n = 0u;
  if (x == 0u) {
    out[0] = '0';
    return 1u;
  }
  while (x != 0u) {
    rev[n] = (char)('0' + (x % UINT64_C(10)));
    x /= UINT64_C(10);
    n += 1u;
  }
  {
    size_t i;
    for (i = 0u; i < n; ++i) {
      out[i] = rev[n - 1u - i];
    }
  }
  return n;
}

static size_t ryu_i32_to_dec(int x, char* out) {
  uint32_t u = (x < 0) ? (uint32_t)(-x) : (uint32_t)x;
  return ryu_u64_to_dec((uint64_t)u, out);
}

static int ryu_bigint_to_decimal_fast(
    const ryu_bigint* v,
    char* out,
    size_t out_cap,
    size_t* out_len) {
  uint64_t small = 0u;
  if (ryu_bigint_to_u64(v, &small)) {
    size_t n;
    if (out_cap == 0u) {
      return 0;
    }
    n = ryu_u64_to_dec(small, out);
    if (n + 1u > out_cap) {
      return 0;
    }
    out[n] = '\0';
    if (out_len != NULL) {
      *out_len = n;
    }
    return 1;
  }
  return ryu_bigint_to_decimal(v, out, out_cap, out_len);
}

int ryu_write_sign(
    char* out,
    size_t out_cap,
    int negative,
    int always_sign,
    int space_sign,
    size_t* pos) {
  if (negative) {
    if (*pos >= out_cap) {
      return 0;
    }
    out[*pos] = '-';
    *pos += 1u;
    return 1;
  }
  if (always_sign) {
    if (*pos >= out_cap) {
      return 0;
    }
    out[*pos] = '+';
    *pos += 1u;
    return 1;
  }
  if (space_sign) {
    if (*pos >= out_cap) {
      return 0;
    }
    out[*pos] = ' ';
    *pos += 1u;
  }
  return 1;
}

ryu_status ryu_copy_literal_signed(
    char* out,
    size_t out_cap,
    const char* lit,
    int negative,
    size_t* out_len) {
  size_t pos = 0u;
  size_t lit_len = strlen(lit);
  if (negative) {
    if (out_cap < lit_len + 2u) {
      return RYU_BUFFER_TOO_SMALL;
    }
    out[pos++] = '-';
  } else if (out_cap < lit_len + 1u) {
    return RYU_BUFFER_TOO_SMALL;
  }
  memcpy(out + pos, lit, lit_len);
  pos += lit_len;
  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return RYU_OK;
}

int ryu_write_special(
    char* out,
    size_t out_cap,
    int negative,
    int is_inf,
    int uppercase,
    int always_sign,
    int space_sign,
    size_t* out_len) {
  const char* lit = is_inf ? (uppercase ? "INF" : "inf") : (uppercase ? "NAN" : "nan");
  size_t lit_len = 3u;
  size_t pos = 0u;

  if (!ryu_write_sign(out, out_cap, negative, always_sign, space_sign, &pos)) {
    return 0;
  }
  if (pos + lit_len >= out_cap) {
    return 0;
  }
  memcpy(out + pos, lit, lit_len);
  pos += lit_len;
  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return 1;
}

int ryu_format_significand_exponent(
    char* out,
    size_t out_cap,
    int negative,
    uint64_t significand,
    int k,
    int prefer_scientific,
    size_t* out_len) {
  char digits[32];
  size_t dlen;
  char plain[RYU_LOCAL_BUF_CAP];
  char sci[128];
  size_t ppos = 0u;
  size_t spos = 0u;
  int exp10;
  size_t i;

  dlen = ryu_u64_to_dec(significand, digits);

  if (!ryu_write_sign(plain, sizeof(plain), negative, 0, 0, &ppos)) {
    return 0;
  }
  if (k >= 0) {
    if (ppos + dlen + (size_t)k >= sizeof(plain)) {
      return 0;
    }
    memcpy(plain + ppos, digits, dlen);
    ppos += dlen;
    for (i = 0u; i < (size_t)k; ++i) {
      plain[ppos + i] = '0';
    }
    ppos += (size_t)k;
  } else {
    int split = (int)dlen + k;
    if (split > 0) {
      if (ppos + dlen + 1u >= sizeof(plain)) {
        return 0;
      }
      memcpy(plain + ppos, digits, (size_t)split);
      ppos += (size_t)split;
      plain[ppos++] = '.';
      memcpy(plain + ppos, digits + (size_t)split, dlen - (size_t)split);
      ppos += dlen - (size_t)split;
    } else {
      size_t zeros = (size_t)(-split);
      if (ppos + 2u + zeros + dlen >= sizeof(plain)) {
        return 0;
      }
      plain[ppos++] = '0';
      plain[ppos++] = '.';
      for (i = 0u; i < zeros; ++i) {
        plain[ppos + i] = '0';
      }
      ppos += zeros;
      memcpy(plain + ppos, digits, dlen);
      ppos += dlen;
    }
  }
  plain[ppos] = '\0';

  if (!ryu_write_sign(sci, sizeof(sci), negative, 0, 0, &spos)) {
    return 0;
  }
  if (spos + dlen + 8u >= sizeof(sci)) {
    return 0;
  }
  sci[spos++] = digits[0];
  if (dlen > 1u) {
    sci[spos++] = '.';
    memcpy(sci + spos, digits + 1u, dlen - 1u);
    spos += dlen - 1u;
  }
  exp10 = k + (int)dlen - 1;
  sci[spos++] = 'e';
  if (exp10 < 0) {
    sci[spos++] = '-';
    exp10 = -exp10;
  }
  spos += ryu_i32_to_dec(exp10, sci + spos);
  sci[spos] = '\0';

  if (prefer_scientific || spos < ppos) {
    if (spos + 1u > out_cap) {
      return 0;
    }
    memcpy(out, sci, spos + 1u);
    if (out_len != NULL) {
      *out_len = spos;
    }
  } else {
    if (ppos + 1u > out_cap) {
      return 0;
    }
    memcpy(out, plain, ppos + 1u);
    if (out_len != NULL) {
      *out_len = ppos;
    }
  }
  return 1;
}

int ryu_format_scientific_fixed_sig(
    char* out,
    size_t out_cap,
    int negative,
    uint64_t significand,
    unsigned sig_digits,
    int exp10,
    int uppercase,
    size_t* out_len) {
  char digits[32];
  size_t dlen = ryu_u64_to_dec(significand, digits);
  size_t pos = 0u;
  char e_char = uppercase ? 'E' : 'e';
  int e = exp10;

  if (sig_digits == 0u) {
    return 0;
  }

  if (!ryu_write_sign(out, out_cap, negative, 0, 0, &pos)) {
    return 0;
  }

  if (dlen < (size_t)sig_digits) {
    unsigned pad = sig_digits - (unsigned)dlen;
    while (pad != 0u) {
      if (dlen + 1u >= sizeof(digits)) {
        return 0;
      }
      digits[dlen++] = '0';
      e -= 1;
      pad -= 1u;
    }
  }

  if (pos + dlen + 8u >= out_cap) {
    return 0;
  }

  out[pos++] = digits[0];
  if (sig_digits > 1u) {
    out[pos++] = '.';
    memcpy(out + pos, digits + 1u, (size_t)sig_digits - 1u);
    pos += (size_t)sig_digits - 1u;
  }

  out[pos++] = e_char;
  if (e < 0) {
    out[pos++] = '-';
    e = -e;
  }
  pos += ryu_i32_to_dec(e, out + pos);
  if (pos >= out_cap) {
    return 0;
  }
  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return 1;
}

static int ryu_round_integer_div_pow10(
    const ryu_bigint* value,
    unsigned drop,
    ryu_bigint* rounded,
    int rounding_mode,
    int negative) {
  unsigned q_digits = drop / 9u;
  unsigned r = drop % 9u;
  uint32_t round_digit = 0u;
  int tail_nonzero = 0;
  size_t i;

  if (drop == 0u) {
    ryu_bigint_copy(rounded, value);
    return 1;
  }

  if (q_digits >= value->len) {
    ryu_bigint_zero(rounded);
    if (q_digits > value->len || r != 0u) {
      return 1;
    }
    {
      uint32_t top = value->limb[value->len - 1u];
      if (top < 100000000u) {
        return 1;
      }
      round_digit = top / 100000000u;
      if ((top % 100000000u) != 0u) {
        tail_nonzero = 1;
      }
      for (i = 0u; i + 1u < value->len; ++i) {
        if (value->limb[i] != 0u) {
          tail_nonzero = 1;
          break;
        }
      }
    }
    {
      int should_round_up;
      switch (rounding_mode) {
      case RYU_ROUND_TOWARD_ZERO:
        should_round_up = 0; break;
      case RYU_ROUND_TOWARD_POS:
        should_round_up = !negative && (round_digit > 0u || tail_nonzero); break;
      case RYU_ROUND_TOWARD_NEG:
        should_round_up = negative && (round_digit > 0u || tail_nonzero); break;
      default: /* NEAREST_EVEN */
        should_round_up = round_digit > 5u || (round_digit == 5u && tail_nonzero);
        break;
      }
      if (should_round_up) {
        if (!ryu_bigint_add_small(rounded, 1u)) {
          return 0;
        }
      }
    }
    return 1;
  }

  rounded->len = value->len - (size_t)q_digits;
  RYU_BIGINT_PROFILE_NOTE_LEN(rounded->len);
  for (i = 0u; i < rounded->len; ++i) {
    rounded->limb[i] = value->limb[i + (size_t)q_digits];
  }

  for (i = 0u; i < (size_t)q_digits; ++i) {
    if (value->limb[i] != 0u) {
      tail_nonzero = 1;
      break;
    }
  }

  if (r == 0u) {
    uint32_t dropped_top = value->limb[(size_t)q_digits - 1u];
    round_digit = dropped_top / 100000000u;
    if ((dropped_top % 100000000u) != 0u) {
      tail_nonzero = 1;
    }
  } else {
    uint32_t div = bg_pow10_small[r];
    uint32_t rem = 0u;
    uint32_t low_mask = bg_pow10_small[r - 1u];
    if (!ryu_bigint_div_small(rounded, div, &rem)) {
      return 0;
    }
    round_digit = rem / low_mask;
    if ((rem % low_mask) != 0u) {
      tail_nonzero = 1;
    }
  }

  ryu_bigint_normalize(rounded);

  {
    int should_round_up;
    switch (rounding_mode) {
    case RYU_ROUND_TOWARD_ZERO:
      should_round_up = 0; break;
    case RYU_ROUND_TOWARD_POS:
      should_round_up = !negative && (round_digit > 0u || tail_nonzero); break;
    case RYU_ROUND_TOWARD_NEG:
      should_round_up = negative && (round_digit > 0u || tail_nonzero); break;
    default: /* NEAREST_EVEN */
      should_round_up = round_digit > 5u ||
          (round_digit == 5u && (tail_nonzero || ((rounded->limb[0] & 1u) != 0u)));
      break;
    }
    if (should_round_up) {
      if (!ryu_bigint_add_small(rounded, 1u)) {
        return 0;
      }
    }
  }
  return 1;
}

int ryu_round_exact_to_significant(
    const ryu_decimal_exact* exact,
    unsigned sig_digits,
    ryu_bigint* out_rounded,
    int* out_exp10,
    int rounding_mode,
    int negative) {
  unsigned len;
  int exp10;
  if (sig_digits == 0u) {
    return 0;
  }
  if (ryu_bigint_is_zero(&exact->digits)) {
    ryu_bigint_zero(out_rounded);
    *out_exp10 = 0;
    return 1;
  }

  len = ryu_bigint_decimal_len(&exact->digits);
  exp10 = (int)len - (int)exact->scale - 1;

  if (len > sig_digits) {
    unsigned drop = len - sig_digits;
    if (!ryu_round_integer_div_pow10(&exact->digits, drop, out_rounded,
                                      rounding_mode, negative)) {
      return 0;
    }
  } else {
    ryu_bigint_copy(out_rounded, &exact->digits);
    if (!ryu_bigint_mul_pow10(out_rounded, sig_digits - len)) {
      return 0;
    }
  }

  while (ryu_bigint_decimal_len(out_rounded) > sig_digits) {
    ryu_bigint tmp;
    int rem_zero = 0;
    if (!ryu_bigint_div_pow10_floor(out_rounded, 1u, &tmp, &rem_zero)) {
      return 0;
    }
    ryu_bigint_copy(out_rounded, &tmp);
    exp10 += 1;
  }

  *out_exp10 = exp10;
  return 1;
}

int ryu_round_exact_to_fractional(
    const ryu_decimal_exact* exact,
    unsigned frac_digits,
    ryu_bigint* out_rounded,
    int rounding_mode,
    int negative) {
  if (exact->scale <= frac_digits) {
    ryu_bigint_copy(out_rounded, &exact->digits);
    return ryu_bigint_mul_pow10(out_rounded, frac_digits - exact->scale);
  }
  return ryu_round_integer_div_pow10(&exact->digits, exact->scale - frac_digits,
                                      out_rounded, rounding_mode, negative);
}

int ryu_emit_fixed_from_scaled(
    char* out,
    size_t out_cap,
    int negative,
    const ryu_bigint* scaled,
    unsigned frac_digits,
    int alternate_form,
    int always_sign,
    int space_sign,
    size_t* out_len) {
  char dec[RYU_LOCAL_BUF_CAP];
  size_t len = 0u;
  size_t pos = 0u;

  if (!ryu_bigint_to_decimal_fast(scaled, dec, sizeof(dec), &len)) {
    return 0;
  }

  if (!ryu_write_sign(out, out_cap, negative, always_sign, space_sign, &pos)) {
    return 0;
  }

  if (frac_digits == 0u) {
    if (pos + len + 2u > out_cap) {
      return 0;
    }
    memcpy(out + pos, dec, len);
    pos += len;
    if (alternate_form) {
      out[pos++] = '.';
    }
    out[pos] = '\0';
    if (out_len != NULL) {
      *out_len = pos;
    }
    return 1;
  }

  if (len <= (size_t)frac_digits) {
    size_t zeros = (size_t)frac_digits - len;
    if (pos + 2u + zeros + len + 1u > out_cap) {
      return 0;
    }
    out[pos++] = '0';
    out[pos++] = '.';
    while (zeros != 0u) {
      out[pos++] = '0';
      zeros -= 1u;
    }
    memcpy(out + pos, dec, len);
    pos += len;
  } else {
    size_t int_len = len - (size_t)frac_digits;
    if (pos + len + 2u > out_cap) {
      return 0;
    }
    memcpy(out + pos, dec, int_len);
    pos += int_len;
    out[pos++] = '.';
    memcpy(out + pos, dec + int_len, (size_t)frac_digits);
    pos += (size_t)frac_digits;
  }

  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return 1;
}

int ryu_trim_trailing_fraction_zeros(char* buf, size_t* len) {
  size_t l = *len;
  size_t e_pos = l;
  size_t dot_pos = l;
  size_t trim_end;
  size_t i;

  for (i = 0u; i < l; ++i) {
    if (buf[i] == 'e' || buf[i] == 'E') {
      e_pos = i;
      break;
    }
  }
  for (i = 0u; i < e_pos; ++i) {
    if (buf[i] == '.') {
      dot_pos = i;
      break;
    }
  }
  if (dot_pos == l) {
    return 1;
  }

  trim_end = e_pos;
  while (trim_end > dot_pos + 1u && buf[trim_end - 1u] == '0') {
    trim_end -= 1u;
  }
  if (trim_end == dot_pos + 1u) {
    trim_end -= 1u;
  }

  if (e_pos < l) {
    size_t tail = l - e_pos;
    memmove(buf + trim_end, buf + e_pos, tail + 1u);
    l = trim_end + tail;
  } else {
    buf[trim_end] = '\0';
    l = trim_end;
  }

  *len = l;
  return 1;
}
