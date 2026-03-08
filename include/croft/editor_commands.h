#ifndef CROFT_EDITOR_COMMANDS_H
#define CROFT_EDITOR_COMMANDS_H

#include "croft/editor_text_model.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tab_size;
    int insert_spaces;
} croft_editor_tab_settings;

typedef struct {
    uint32_t replace_start_offset;
    uint32_t replace_end_offset;
    uint32_t next_anchor_offset;
    uint32_t next_active_offset;
    char* replacement_utf8;
    size_t replacement_utf8_len;
} croft_editor_tab_edit;

void croft_editor_tab_settings_default(croft_editor_tab_settings* settings);
void croft_editor_tab_edit_dispose(croft_editor_tab_edit* edit);

void croft_editor_command_move_left(const croft_editor_text_model* model,
                                    uint32_t* anchor_offset,
                                    uint32_t* active_offset,
                                    uint32_t* preferred_column,
                                    int selecting);

void croft_editor_command_move_right(const croft_editor_text_model* model,
                                     uint32_t* anchor_offset,
                                     uint32_t* active_offset,
                                     uint32_t* preferred_column,
                                     int selecting);

void croft_editor_command_move_up(const croft_editor_text_model* model,
                                  uint32_t* anchor_offset,
                                  uint32_t* active_offset,
                                  uint32_t* preferred_column,
                                  int selecting);

void croft_editor_command_move_down(const croft_editor_text_model* model,
                                    uint32_t* anchor_offset,
                                    uint32_t* active_offset,
                                    uint32_t* preferred_column,
                                    int selecting);

void croft_editor_command_move_home(const croft_editor_text_model* model,
                                    uint32_t* anchor_offset,
                                    uint32_t* active_offset,
                                    uint32_t* preferred_column,
                                    int selecting);

void croft_editor_command_move_end(const croft_editor_text_model* model,
                                   uint32_t* anchor_offset,
                                   uint32_t* active_offset,
                                   uint32_t* preferred_column,
                                   int selecting);

void croft_editor_command_move_word_left(const croft_editor_text_model* model,
                                         uint32_t* anchor_offset,
                                         uint32_t* active_offset,
                                         uint32_t* preferred_column,
                                         int selecting);

void croft_editor_command_move_word_right(const croft_editor_text_model* model,
                                          uint32_t* anchor_offset,
                                          uint32_t* active_offset,
                                          uint32_t* preferred_column,
                                          int selecting);

void croft_editor_command_move_word_part_left(const croft_editor_text_model* model,
                                              uint32_t* anchor_offset,
                                              uint32_t* active_offset,
                                              uint32_t* preferred_column,
                                              int selecting);

void croft_editor_command_move_word_part_right(const croft_editor_text_model* model,
                                               uint32_t* anchor_offset,
                                               uint32_t* active_offset,
                                               uint32_t* preferred_column,
                                               int selecting);

int croft_editor_command_delete_left_range(const croft_editor_text_model* model,
                                           uint32_t anchor_offset,
                                           uint32_t active_offset,
                                           uint32_t* out_start_offset,
                                           uint32_t* out_end_offset);

int croft_editor_command_delete_right_range(const croft_editor_text_model* model,
                                            uint32_t anchor_offset,
                                            uint32_t active_offset,
                                            uint32_t* out_start_offset,
                                            uint32_t* out_end_offset);

int croft_editor_command_delete_word_left_range(const croft_editor_text_model* model,
                                                uint32_t anchor_offset,
                                                uint32_t active_offset,
                                                uint32_t* out_start_offset,
                                                uint32_t* out_end_offset);

int croft_editor_command_delete_word_right_range(const croft_editor_text_model* model,
                                                 uint32_t anchor_offset,
                                                 uint32_t active_offset,
                                                 uint32_t* out_start_offset,
                                                 uint32_t* out_end_offset);

int croft_editor_command_delete_word_part_left_range(const croft_editor_text_model* model,
                                                     uint32_t anchor_offset,
                                                     uint32_t active_offset,
                                                     uint32_t* out_start_offset,
                                                     uint32_t* out_end_offset);

int croft_editor_command_delete_word_part_right_range(const croft_editor_text_model* model,
                                                      uint32_t anchor_offset,
                                                      uint32_t active_offset,
                                                      uint32_t* out_start_offset,
                                                      uint32_t* out_end_offset);

int croft_editor_command_build_tab_edit(const croft_editor_text_model* model,
                                        uint32_t anchor_offset,
                                        uint32_t active_offset,
                                        const croft_editor_tab_settings* settings,
                                        int outdent,
                                        croft_editor_tab_edit* out_edit);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_COMMANDS_H */
