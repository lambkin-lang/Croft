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

static size_t ryu_u32_to_dec(uint32_t v, char* out) {
  char rev[16];
  size_t n = 0u;
  if (v == 0u) {
    out[0] = '0';
    return 1u;
  }
  while (v != 0u) {
    rev[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  {
    size_t i;
    for (i = 0u; i < n; ++i) {
      out[i] = rev[n - 1u - i];
    }
  }
  return n;
}

static size_t ryu_u64_to_dec(uint64_t v, char* out) {
  char rev[32];
  size_t n = 0u;
  if (v == 0u) {
    out[0] = '0';
    return 1u;
  }
  while (v != 0u) {
    rev[n++] = (char)('0' + (v % UINT64_C(10)));
    v /= UINT64_C(10);
  }
  {
    size_t i;
    for (i = 0u; i < n; ++i) {
      out[i] = rev[n - 1u - i];
    }
  }
  return n;
}

static int ryu_bigint_to_decimal_fast(
    const ryu_bigint* value,
    char* out,
    size_t out_cap,
    size_t* out_len) {
  uint64_t small = 0u;
  if (ryu_bigint_to_u64(value, &small)) {
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
  return ryu_bigint_to_decimal(value, out, out_cap, out_len);
}

static int ryu_append_exponent(
    char* out,
    size_t out_cap,
    size_t* pos,
    int exp10,
    int uppercase) {
  char tmp[16];
  size_t n;
  uint32_t abs_exp;
  if (*pos + 4u >= out_cap) {
    return 0;
  }
  out[(*pos)++] = uppercase ? 'E' : 'e';
  if (exp10 < 0) {
    out[(*pos)++] = '-';
    abs_exp = (uint32_t)(-exp10);
  } else {
    out[(*pos)++] = '+';
    abs_exp = (uint32_t)exp10;
  }

  n = ryu_u32_to_dec(abs_exp, tmp);
  if (n == 1u) {
    out[(*pos)++] = '0';
  }
  if (*pos + n >= out_cap) {
    return 0;
  }
  memcpy(out + *pos, tmp, n);
  *pos += n;
  return 1;
}

static int ryu_emit_scientific_from_rounded(
    char* out,
    size_t out_cap,
    int negative,
    const ryu_bigint* rounded,
    unsigned precision,
    int exp10,
    int uppercase,
    int alternate_form,
    int always_sign,
    int space_sign,
    size_t* out_len) {
  char dec[RYU_LOCAL_BUF_CAP];
  size_t len = 0u;
  size_t pos = 0u;
  unsigned frac = precision;
  size_t copy_tail;

  if (!ryu_bigint_to_decimal_fast(rounded, dec, sizeof(dec), &len)) {
    return 0;
  }
  if (len == 0u) {
    return 0;
  }

  if (!ryu_write_sign(out, out_cap, negative, always_sign, space_sign, &pos)) {
    return 0;
  }

  if (pos + 2u >= out_cap) {
    return 0;
  }

  out[pos++] = dec[0];
  if (frac > 0u || alternate_form) {
    out[pos++] = '.';
    copy_tail = (len > 1u) ? (len - 1u) : 0u;
    if (copy_tail > (size_t)frac) {
      copy_tail = (size_t)frac;
    }
    if (pos + copy_tail + (size_t)frac + 8u >= out_cap) {
      return 0;
    }
    if (copy_tail != 0u) {
      memcpy(out + pos, dec + 1u, copy_tail);
      pos += copy_tail;
      frac -= (unsigned)copy_tail;
    }
    while (frac != 0u) {
      out[pos++] = '0';
      frac -= 1u;
    }
  }

  if (!ryu_append_exponent(out, out_cap, &pos, exp10, uppercase)) {
    return 0;
  }

  if (pos >= out_cap) {
    return 0;
  }
  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return 1;
}

static ryu_status ryu_printf_e(
    char* out,
    size_t out_cap,
    const ryu_decimal_exact* exact,
    int negative,
    int precision,
    int uppercase,
    int alternate_form,
    int always_sign,
    int space_sign,
    int rounding_mode,
    size_t* out_len) {
  ryu_bigint rounded;
  int exp10 = 0;

  if (precision < 0) {
    precision = 6;
  }

  if (ryu_bigint_is_zero(&exact->digits)) {
    ryu_bigint_from_u64(&rounded, 0u);
    if (!ryu_emit_scientific_from_rounded(
            out,
            out_cap,
            negative,
            &rounded,
            (unsigned)precision,
            0,
            uppercase,
            alternate_form,
            always_sign,
            space_sign,
            out_len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    return RYU_OK;
  }

  if (!ryu_round_exact_to_significant(exact, (unsigned)(precision + 1), &rounded, &exp10,
                                       rounding_mode, negative)) {
    return RYU_UNSUPPORTED;
  }

  if (!ryu_emit_scientific_from_rounded(
          out,
          out_cap,
          negative,
          &rounded,
          (unsigned)precision,
          exp10,
          uppercase,
          alternate_form,
          always_sign,
          space_sign,
          out_len)) {
    return RYU_BUFFER_TOO_SMALL;
  }

  return RYU_OK;
}

static ryu_status ryu_printf_f(
    char* out,
    size_t out_cap,
    const ryu_decimal_exact* exact,
    int negative,
    int precision,
    int alternate_form,
    int always_sign,
    int space_sign,
    int rounding_mode,
    size_t* out_len) {
  ryu_bigint rounded;

  if (precision < 0) {
    precision = 6;
  }

  if (!ryu_round_exact_to_fractional(exact, (unsigned)precision, &rounded,
                                      rounding_mode, negative)) {
    return RYU_UNSUPPORTED;
  }

  if (!ryu_emit_fixed_from_scaled(
          out,
          out_cap,
          negative,
          &rounded,
          (unsigned)precision,
          alternate_form,
          always_sign,
          space_sign,
          out_len)) {
    return RYU_BUFFER_TOO_SMALL;
  }
  return RYU_OK;
}

static ryu_status ryu_printf_g(
    char* out,
    size_t out_cap,
    const ryu_decimal_exact* exact,
    int negative,
    int precision,
    int uppercase,
    int alternate_form,
    int always_sign,
    int space_sign,
    int rounding_mode,
    size_t* out_len) {
  ryu_bigint rounded_sig;
  int exp10 = 0;
  int use_scientific;
  int f_precision;
  ryu_status st;

  if (precision < 0) {
    precision = 6;
  }
  if (precision == 0) {
    precision = 1;
  }

  if (ryu_bigint_is_zero(&exact->digits)) {
    ryu_bigint_from_u64(&rounded_sig, 0u);
    exp10 = 0;
  } else {
    if (!ryu_round_exact_to_significant(exact, (unsigned)precision, &rounded_sig, &exp10,
                                         rounding_mode, negative)) {
      return RYU_UNSUPPORTED;
    }
  }

  use_scientific = (exp10 < -4 || exp10 >= precision);
  if (use_scientific) {
    if (!ryu_emit_scientific_from_rounded(
            out,
            out_cap,
            negative,
            &rounded_sig,
            (unsigned)(precision - 1),
            exp10,
            uppercase,
            alternate_form,
            always_sign,
            space_sign,
            out_len)) {
      st = RYU_BUFFER_TOO_SMALL;
    } else {
      st = RYU_OK;
    }
  } else {
    f_precision = precision - (exp10 + 1);
    if (f_precision < 0) {
      f_precision = 0;
    }
    if (!ryu_emit_fixed_from_scaled(
            out,
            out_cap,
            negative,
            &rounded_sig,
            (unsigned)f_precision,
            alternate_form,
            always_sign,
            space_sign,
            out_len)) {
      st = RYU_BUFFER_TOO_SMALL;
    } else {
      st = RYU_OK;
    }
  }
  if (st != RYU_OK) {
    return st;
  }

  if (!alternate_form) {
    if (!ryu_trim_trailing_fraction_zeros(out, out_len)) {
      return RYU_UNSUPPORTED;
    }
  }
  return RYU_OK;
}

/*
 * Hex float formatter (%a/%A).
 * Emits [-]0xh.hhhhp±d directly from IEEE-754 binary64 bits.
 * No bigint or decimal conversion required.
 *
 * Default precision: minimum hex digits for exact representation (trailing
 * zero hex digits trimmed, matching C11 §7.21.6.1 ¶8 for FLT_RADIX = 2).
 * Explicit precision: round mantissa to that many hex digits with
 * round-to-nearest, ties-to-even.
 */
static ryu_status ryu_printf_a(
    char* out,
    size_t out_cap,
    uint64_t abs_bits,
    int sign,
    int precision,
    int uppercase,
    int alternate_form,
    int always_sign,
    int space_sign,
    int rounding_mode,
    size_t* out_len) {
  const char* hex = uppercase
      ? "0123456789ABCDEF"
      : "0123456789abcdef";
  size_t pos = 0u;
  uint64_t mant = abs_bits & UINT64_C(0x000FFFFFFFFFFFFF);
  int bexp = (int)(abs_bits >> 52);
  int leading;
  int exponent;

  if (bexp == 0) {
    leading = 0;
    exponent = (mant == 0u) ? 0 : -1022;
  } else {
    leading = 1;
    exponent = bexp - 1023;
  }

  /* Determine effective precision. */
  int prec;
  if (precision < 0) {
    /* Default: minimum hex digits for exact representation. */
    if (mant == 0u) {
      prec = 0;
    } else {
      prec = 13;
      {
        uint64_t m = mant;
        while (prec > 0 && (m & UINT64_C(0xF)) == 0u) {
          m >>= 4;
          prec--;
        }
      }
    }
  } else {
    prec = precision;
  }

  /* Round mantissa when fewer than 13 hex digits requested. */
  uint64_t frac = mant;
  if (prec < 13 && mant != 0u) {
    int shift = (13 - prec) * 4;
    uint64_t kept = mant >> shift;
    uint64_t discarded = mant & ((UINT64_C(1) << shift) - 1u);
    uint64_t half = UINT64_C(1) << (shift - 1);
    int round_bit;
    if (prec > 0) {
      round_bit = (int)(kept & 1u);
    } else {
      round_bit = leading & 1;
    }
    {
      int should_round_up;
      switch (rounding_mode) {
      case RYU_ROUND_TOWARD_ZERO:
        should_round_up = 0; break;
      case RYU_ROUND_TOWARD_POS:
        should_round_up = !sign && discarded > 0u; break;
      case RYU_ROUND_TOWARD_NEG:
        should_round_up = sign && discarded > 0u; break;
      default: /* NEAREST_EVEN */
        should_round_up = discarded > half || (discarded == half && round_bit);
        break;
      }
      if (should_round_up) {
        kept++;
        if ((prec == 0) || (kept >= (UINT64_C(1) << (prec * 4)))) {
          leading++;
          kept = 0u;
        }
      }
    }
    frac = kept;
  }

  /* --- Emit output --- */

  if (!ryu_write_sign(out, out_cap, sign, always_sign, space_sign, &pos)) {
    return RYU_BUFFER_TOO_SMALL;
  }

  /* "0x" / "0X" prefix */
  if (pos + 2u >= out_cap) {
    return RYU_BUFFER_TOO_SMALL;
  }
  out[pos++] = '0';
  out[pos++] = uppercase ? 'X' : 'x';

  /* Leading hex digit */
  if (pos >= out_cap) {
    return RYU_BUFFER_TOO_SMALL;
  }
  out[pos++] = hex[leading & 0xF];

  /* Decimal point + fraction */
  if (prec > 0 || alternate_form) {
    if (pos >= out_cap) {
      return RYU_BUFFER_TOO_SMALL;
    }
    out[pos++] = '.';
  }

  if (prec > 0) {
    int emit = (prec <= 13) ? prec : 13;
    if (pos + (size_t)emit >= out_cap) {
      return RYU_BUFFER_TOO_SMALL;
    }
    {
      int i;
      for (i = emit - 1; i >= 0; i--) {
        out[pos++] = hex[(frac >> (i * 4)) & 0xFu];
      }
    }
    if (prec > 13) {
      size_t pad = (size_t)(prec - 13);
      if (pos + pad >= out_cap) {
        return RYU_BUFFER_TOO_SMALL;
      }
      {
        size_t i;
        for (i = 0u; i < pad; i++) {
          out[pos++] = '0';
        }
      }
    }
  }

  /* Exponent: 'p'/'P' ± decimal */
  if (pos >= out_cap) {
    return RYU_BUFFER_TOO_SMALL;
  }
  out[pos++] = uppercase ? 'P' : 'p';

  if (pos >= out_cap) {
    return RYU_BUFFER_TOO_SMALL;
  }
  {
    uint32_t abs_exp;
    char tmp[16];
    size_t n;
    if (exponent < 0) {
      out[pos++] = '-';
      abs_exp = (uint32_t)(-exponent);
    } else {
      out[pos++] = '+';
      abs_exp = (uint32_t)exponent;
    }
    n = ryu_u32_to_dec(abs_exp, tmp);
    if (pos + n >= out_cap) {
      return RYU_BUFFER_TOO_SMALL;
    }
    memcpy(out + pos, tmp, n);
    pos += n;
  }

  if (pos >= out_cap) {
    return RYU_BUFFER_TOO_SMALL;
  }
  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return RYU_OK;
}

ryu_status ryu64_to_printf(
    char* out,
    size_t out_cap,
    double x,
    const ryu_printf_spec* spec,
    size_t* out_len) {
  ryu_fp64 fp;
  uint64_t abs_bits;
  ryu_decimal_exact exact;

  if (out == NULL || out_cap == 0u) {
    return RYU_BUFFER_TOO_SMALL;
  }
  if (spec == NULL) {
    return RYU_INVALID;
  }
  if (spec->precision < -1) {
    return RYU_INVALID;
  }
  if (spec->precision > RYU_MAX_PRINTF_PRECISION) {
    return RYU_UNSUPPORTED;
  }

  ryu_decode_fp64(x, &fp);
  abs_bits = fp.bits & UINT64_C(0x7fffffffffffffff);

  if (fp.is_nan) {
    /*
     * Printf-family NaN policy:
     * emit unsigned nan/NAN and ignore sign flags for deterministic behavior.
     */
    if (!ryu_write_special(
            out,
            out_cap,
            0,
            0,
            spec->uppercase,
            0,
            0,
            out_len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    return RYU_OK;
  }

  if (fp.is_inf) {
    if (!ryu_write_special(
            out,
            out_cap,
            fp.sign,
            1,
            spec->uppercase,
            spec->always_sign,
            spec->space_sign,
            out_len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    return RYU_OK;
  }

  if (spec->kind == RYU_FMT_A) {
    return ryu_printf_a(
        out, out_cap, abs_bits, fp.sign,
        spec->precision, spec->uppercase, spec->alternate_form,
        spec->always_sign, spec->space_sign, spec->rounding_mode,
        out_len);
  }

  if (!ryu_exact_decimal_from_bits(abs_bits, &exact)) {
    return RYU_UNSUPPORTED;
  }

  switch (spec->kind) {
    case RYU_FMT_F:
      return ryu_printf_f(
          out,
          out_cap,
          &exact,
          fp.sign,
          spec->precision,
          spec->alternate_form,
          spec->always_sign,
          spec->space_sign,
          spec->rounding_mode,
          out_len);
    case RYU_FMT_E:
      return ryu_printf_e(
          out,
          out_cap,
          &exact,
          fp.sign,
          spec->precision,
          spec->uppercase,
          spec->alternate_form,
          spec->always_sign,
          spec->space_sign,
          spec->rounding_mode,
          out_len);
    case RYU_FMT_G:
      return ryu_printf_g(
          out,
          out_cap,
          &exact,
          fp.sign,
          spec->precision,
          spec->uppercase,
          spec->alternate_form,
          spec->always_sign,
          spec->space_sign,
          spec->rounding_mode,
          out_len);
    default:
      return RYU_INVALID;
  }
}
