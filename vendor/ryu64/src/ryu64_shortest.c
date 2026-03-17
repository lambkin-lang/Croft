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

ryu_status ryu64_to_shortest(char* out, size_t out_cap, double x, size_t* out_len) {
  ryu_fp64 fp;
  uint64_t abs_bits;
  uint64_t sig;
  int k;
  int digits;
  size_t len = 0u;

  if (out == NULL || out_cap == 0u) {
    return RYU_BUFFER_TOO_SMALL;
  }

  ryu_decode_fp64(x, &fp);
  abs_bits = fp.bits & UINT64_C(0x7fffffffffffffff);

  if (fp.is_nan) {
    if (!ryu_write_special(out, out_cap, fp.sign, 0, 0, 0, 0, &len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    if (out_len != NULL) {
      *out_len = len;
    }
    return RYU_OK;
  }

  if (fp.is_inf) {
    if (!ryu_write_special(out, out_cap, fp.sign, 1, 0, 0, 0, &len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    if (out_len != NULL) {
      *out_len = len;
    }
    return RYU_OK;
  }

  if (fp.is_zero) {
    return ryu_copy_literal_signed(out, out_cap, "0", fp.sign, out_len);
  }

  if (!ryu_choose_shortest_digits(abs_bits, &sig, &k, &digits)) {
    return RYU_UNSUPPORTED;
  }
  (void)digits;

  if (!ryu_format_significand_exponent(out, out_cap, fp.sign, sig, k, 0, &len)) {
    return RYU_BUFFER_TOO_SMALL;
  }

  if (out_len != NULL) {
    *out_len = len;
  }
  return RYU_OK;
}
