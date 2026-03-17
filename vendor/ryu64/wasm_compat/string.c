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

#include "string.h"

/*
 * Minimal freestanding implementations used by no-libc/wasm builds.
 * These routines are intentionally simple and correctness-oriented.
 */
void* memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  size_t i;
  for (i = 0u; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  const unsigned char* s = (const unsigned char*)src;
  size_t i;
  if (d == s || n == 0u) {
    return dst;
  }
  if (d < s) {
    for (i = 0u; i < n; ++i) {
      d[i] = s[i];
    }
  } else {
    for (i = n; i > 0u; --i) {
      d[i - 1u] = s[i - 1u];
    }
  }
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = (const unsigned char*)a;
  const unsigned char* y = (const unsigned char*)b;
  size_t i;
  for (i = 0u; i < n; ++i) {
    if (x[i] != y[i]) {
      /* C only requires negative/zero/positive, not exact byte delta. */
      return (x[i] < y[i]) ? -1 : 1;
    }
  }
  return 0;
}

void* memset(void* dst, int c, size_t n) {
  unsigned char* d = (unsigned char*)dst;
  unsigned char v = (unsigned char)c;
  size_t i;
  for (i = 0u; i < n; ++i) {
    d[i] = v;
  }
  return dst;
}

size_t strlen(const char* s) {
  size_t n = 0u;
  while (s[n] != '\0') {
    n += 1u;
  }
  return n;
}
