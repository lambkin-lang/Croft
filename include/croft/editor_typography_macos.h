#ifndef CROFT_EDITOR_TYPOGRAPHY_MACOS_H
#define CROFT_EDITOR_TYPOGRAPHY_MACOS_H

#include "croft/editor_typography.h"

#ifdef __OBJC__

#import <AppKit/AppKit.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static inline void croft_editor_font_probe_copy_cstr(char* dest, size_t capacity, const char* value) {
    if (!dest || capacity == 0u) {
        return;
    }

    if (!value) {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, capacity, "%s", value);
}

static inline void croft_editor_font_probe_copy_nsstring(char* dest, size_t capacity, NSString* value) {
    croft_editor_font_probe_copy_cstr(dest,
                                      capacity,
                                      value ? value.UTF8String : "");
}

static inline void croft_editor_font_probe_clear(croft_editor_font_probe* probe) {
    if (!probe) {
        return;
    }
    memset(probe, 0, sizeof(*probe));
}

static inline NSFont* croft_editor_mac_monospace_font(CGFloat size) {
    NSFont* font = [NSFont fontWithName:@CROFT_EDITOR_MONOSPACE_FONT_REGULAR size:size];

    if (!font) {
        font = [NSFont fontWithName:@CROFT_EDITOR_MONOSPACE_FONT_FAMILY size:size];
    }
    if (!font) {
        font = [NSFont userFixedPitchFontOfSize:size];
    }
    if (!font) {
        font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    }
    if (!font) {
        font = [NSFont systemFontOfSize:size];
    }
    return font;
}

static inline NSFont* croft_editor_mac_monospace_bold_font(CGFloat size) {
    NSFont* font = [NSFont fontWithName:@CROFT_EDITOR_MONOSPACE_FONT_BOLD size:size];

    if (!font) {
        font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightSemibold];
    }
    if (!font) {
        font = croft_editor_mac_monospace_font(size);
    }
    return font;
}

static inline NSFont* croft_editor_mac_ui_font(CGFloat size) {
    NSFont* font = [NSFont systemFontOfSize:size];

    if (!font) {
        font = [NSFont messageFontOfSize:size];
    }
    if (!font) {
        font = croft_editor_mac_monospace_font(size);
    }
    return font;
}

static inline int32_t croft_editor_mac_probe_font(NSFont* font,
                                                  CGFloat point_size,
                                                  const char* requested_style,
                                                  const char* sample_utf8,
                                                  size_t sample_len,
                                                  croft_editor_font_probe* probe_out) {
    NSString* sample = nil;
    NSDictionary* attrs = nil;
    NSSize measured = NSZeroSize;

    if (!font || !probe_out) {
        return -1;
    }

    croft_editor_font_probe_clear(probe_out);
    probe_out->point_size = (float)point_size;
    croft_editor_font_probe_copy_cstr(probe_out->requested_family,
                                      sizeof(probe_out->requested_family),
                                      CROFT_EDITOR_MONOSPACE_FONT_FAMILY);
    croft_editor_font_probe_copy_cstr(probe_out->requested_style,
                                      sizeof(probe_out->requested_style),
                                      requested_style ? requested_style : "");
    croft_editor_font_probe_copy_nsstring(probe_out->resolved_family,
                                          sizeof(probe_out->resolved_family),
                                          font.familyName);
    croft_editor_font_probe_copy_nsstring(probe_out->resolved_style,
                                          sizeof(probe_out->resolved_style),
                                          font.fontName);
    probe_out->ascender = (float)ceil(font.ascender);
    probe_out->descender = (float)ceil(-font.descender);
    if (probe_out->descender < 0.0f) {
        probe_out->descender = 0.0f;
    }
    probe_out->leading = (float)ceil(font.leading);
    if (probe_out->leading < 0.0f) {
        probe_out->leading = 0.0f;
    }
    probe_out->line_height = (float)ceil(font.ascender - font.descender + font.leading);
    if (probe_out->line_height <= 0.0f) {
        probe_out->line_height = (float)point_size;
    }

    if (!sample_utf8 || sample_len == 0u) {
        return 0;
    }

    sample = [[NSString alloc] initWithBytes:sample_utf8
                                      length:sample_len
                                    encoding:NSUTF8StringEncoding];
    if (!sample) {
        return -1;
    }

    attrs = @{ NSFontAttributeName: font };
    measured = [sample sizeWithAttributes:attrs];
    probe_out->sample_width = (float)ceil(measured.width);
    return 0;
}

#endif

#endif
