#ifndef CROFT_EDITOR_TYPOGRAPHY_H
#define CROFT_EDITOR_TYPOGRAPHY_H

#include <stdint.h>

#define CROFT_EDITOR_MONOSPACE_FONT_FAMILY "Menlo"
#define CROFT_EDITOR_MONOSPACE_FONT_REGULAR "Menlo-Regular"
#define CROFT_EDITOR_MONOSPACE_FONT_BOLD "Menlo-Bold"
#define CROFT_EDITOR_FONT_PROBE_SAMPLE "0O1lI []{}() +-*/ _=<>|"

enum {
    CROFT_EDITOR_FONT_PROBE_NAME_CAPACITY = 64
};

typedef struct croft_editor_font_probe {
    float point_size;
    float sample_width;
    float line_height;
    char requested_family[CROFT_EDITOR_FONT_PROBE_NAME_CAPACITY];
    char requested_style[CROFT_EDITOR_FONT_PROBE_NAME_CAPACITY];
    char resolved_family[CROFT_EDITOR_FONT_PROBE_NAME_CAPACITY];
    char resolved_style[CROFT_EDITOR_FONT_PROBE_NAME_CAPACITY];
} croft_editor_font_probe;

#endif
