#ifndef CROFT_EDITOR_TEXT_MODEL_H
#define CROFT_EDITOR_TEXT_MODEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CROFT_EDITOR_WORD_SEPARATORS "`~!@#$%^&*()-=+[{]}\\|;:'\",.<>/?"

enum {
    CROFT_EDITOR_OK = 0,
    CROFT_EDITOR_ERR_INVALID = -1,
    CROFT_EDITOR_ERR_OOM = -2
};

typedef struct {
    uint32_t line_number;
    uint32_t column;
} croft_editor_position;

typedef struct {
    uint32_t start_line_number;
    uint32_t start_column;
    uint32_t end_line_number;
    uint32_t end_column;
} croft_editor_range;

typedef struct {
    uint32_t selection_start_line_number;
    uint32_t selection_start_column;
    uint32_t position_line_number;
    uint32_t position_column;
} croft_editor_selection;

typedef enum {
    CROFT_EDITOR_SELECTION_LTR = 0,
    CROFT_EDITOR_SELECTION_RTL = 1
} croft_editor_selection_direction;

typedef struct {
    char* utf8;
    uint32_t utf8_len;
    uint32_t* codepoint_to_byte_offsets;
    uint32_t codepoint_count;
    uint32_t* line_start_offsets;
    uint32_t line_count;
} croft_editor_text_model;

void croft_editor_text_model_init(croft_editor_text_model* model);
void croft_editor_text_model_dispose(croft_editor_text_model* model);

int32_t croft_editor_text_model_set_text(croft_editor_text_model* model,
                                         const char* utf8,
                                         size_t utf8_len);

const char* croft_editor_text_model_text(const croft_editor_text_model* model);
uint32_t croft_editor_text_model_length(const croft_editor_text_model* model);
uint32_t croft_editor_text_model_codepoint_length(const croft_editor_text_model* model);
uint32_t croft_editor_text_model_line_count(const croft_editor_text_model* model);

uint32_t croft_editor_text_model_line_start_offset(const croft_editor_text_model* model,
                                                   uint32_t line_number);
uint32_t croft_editor_text_model_line_end_offset(const croft_editor_text_model* model,
                                                 uint32_t line_number);
uint32_t croft_editor_text_model_line_length(const croft_editor_text_model* model,
                                             uint32_t line_number);
const char* croft_editor_text_model_line_utf8(const croft_editor_text_model* model,
                                              uint32_t line_number,
                                              uint32_t* out_len_bytes);

uint32_t croft_editor_text_model_get_offset_at(const croft_editor_text_model* model,
                                               uint32_t line_number,
                                               uint32_t column);
uint32_t croft_editor_text_model_get_offset_for_position(const croft_editor_text_model* model,
                                                         croft_editor_position position);
croft_editor_position croft_editor_text_model_get_position_at(const croft_editor_text_model* model,
                                                              uint32_t offset);
uint32_t croft_editor_text_model_byte_offset_at(const croft_editor_text_model* model,
                                                uint32_t offset);
int32_t croft_editor_text_model_codepoint_at_offset(const croft_editor_text_model* model,
                                                    uint32_t offset,
                                                    uint32_t* out_codepoint);

int croft_editor_text_model_get_word_range_at(const croft_editor_text_model* model,
                                              croft_editor_position position,
                                              const char* extra_word_chars,
                                              croft_editor_range* out_range);

croft_editor_position croft_editor_position_create(uint32_t line_number, uint32_t column);
int croft_editor_position_equals(croft_editor_position a, croft_editor_position b);
int croft_editor_position_compare(croft_editor_position a, croft_editor_position b);

croft_editor_range croft_editor_range_create(croft_editor_position start,
                                             croft_editor_position end);
int croft_editor_range_is_empty(croft_editor_range range);
int croft_editor_range_contains_position(croft_editor_range range, croft_editor_position position);

croft_editor_selection croft_editor_selection_create(croft_editor_position selection_start,
                                                     croft_editor_position position);
croft_editor_selection croft_editor_selection_from_offsets(const croft_editor_text_model* model,
                                                           uint32_t anchor_offset,
                                                           uint32_t active_offset);
croft_editor_selection_direction croft_editor_selection_direction_of(croft_editor_selection selection);
croft_editor_range croft_editor_selection_normalized_range(croft_editor_selection selection);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_TEXT_MODEL_H */
