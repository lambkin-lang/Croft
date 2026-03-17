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

#ifndef RYU64_H
#define RYU64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  RYU_OK = 0,
  RYU_BUFFER_TOO_SMALL = 1,
  RYU_UNSUPPORTED = 2,
  RYU_INVALID = 3
} ryu_status;

typedef enum {
  RYU_PARSE_OK = 0,
  RYU_PARSE_INVALID = 1,
  RYU_PARSE_OUT_OF_RANGE = 2,
  RYU_PARSE_INEXACT = 3,
  RYU_PARSE_UNSUPPORTED = 4,
  RYU_PARSE_OVERFLOW = 5,
  RYU_PARSE_UNDERFLOW = 6
} ryu_parse_status;

typedef struct {
  ryu_parse_status status;
  double value;
  size_t parsed_len;
} ryu64_parse_result;

/*
 * Shortest round-trip formatter.
 * Preserves the input sign bit for signed zero, infinities, and NaN.
 */
ryu_status ryu64_to_shortest(char* out, size_t out_cap, double x, size_t* out_len);

typedef enum {
  RYU_FMT_F = 0,
  RYU_FMT_E = 1,
  RYU_FMT_G = 2,
  RYU_FMT_A = 3
} ryu_fmt_kind;

typedef enum {
  RYU_ROUND_NEAREST_EVEN = 0,  /* default, backward compatible */
  RYU_ROUND_TOWARD_ZERO  = 1,
  RYU_ROUND_TOWARD_POS   = 2,
  RYU_ROUND_TOWARD_NEG   = 3
} ryu_rounding_mode;

typedef struct {
  ryu_fmt_kind kind;
  int precision;      /* -1 = default semantics per kind */
  int uppercase;      /* 0 => e/g/inf/nan, 1 => E/G/INF/NAN */
  int alternate_form; /* '#' */
  int always_sign;    /* '+' */
  int space_sign;     /* ' ' */
  int rounding_mode;  /* ryu_rounding_mode, 0 = nearest-even */
} ryu_printf_spec;

/*
 * Printf-style conversion for binary64.
 * NaN formatting is unsigned ("nan"/"NAN"): sign bit and '+'/' ' flags are
 * ignored for NaN tokens.
 * This is an intentional formatting-layer portability choice because C `printf`
 * NaN sign rendering is implementation-defined.
 */
ryu_status ryu64_to_printf(
    char* out,
    size_t out_cap,
    double x,
    const ryu_printf_spec* spec,
    size_t* out_len);

/*
 * Tiny decimal parser contract:
 * - C-locale ASCII syntax only.
 * - No heap, no locale, no libc parser calls.
 * - Nonzero values are accepted when they have <= 19 significant digits and
 *   effective base-10 exponent in [-19, 19].
 * - Special tokens:
 *   - recognizes inf/infinity (case-insensitive)
 *   - recognizes bare nan (case-insensitive)
 *   - does not consume nan payload syntax; for "nan(payload)" it consumes only
 *     "nan" (or signed prefix + "nan"), leaving "(payload)" as unparsed suffix.
 * - Subnormal-scale decimal texts (for example "5e-324") are outside this
 *   bounded contract and return RYU_PARSE_OUT_OF_RANGE.
 * - Inputs outside the bounded contract return RYU_PARSE_OUT_OF_RANGE.
 */
ryu64_parse_result ryu64_from_decimal_tiny(const char* s, size_t n);

/*
 * Full parser entry point (libc-free).
 * - When built with RYU64_ENABLE_PARSE_BIGINT, supports wide decimal ranges
 *   using bigint-backed conversion.
 * - Conversion tracks decimal powers as 5^k plus a binary exponent adjustment
 *   to keep intermediate rationals compact.
 * - For extremely long mantissas or ambiguous truncated intervals, the parser
 *   may return RYU_PARSE_OUT_OF_RANGE.
 * - NaN payload syntax "nan(...)" is consumed when payload characters are
 *   ASCII [0-9A-Za-z_], but payload bits are not preserved in output; parser
 *   returns canonical quiet NaN (with sign if requested).
 * - Build macro RYU_BIGINT_MAX_LIMBS (defaults: 512 native, 256 wasm32)
 *   controls bigint capacity, accepted long-mantissa coverage, and parser
 *   stack footprint.
 * - Without that macro, falls back to tiny parser coverage and may return
 *   RYU_PARSE_UNSUPPORTED for wider ranges.
 */
ryu64_parse_result ryu64_from_decimal_full(const char* s, size_t n);

#ifdef __cplusplus
}
#endif

#endif
