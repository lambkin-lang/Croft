#ifndef CROFT_EDITOR_COMMANDS_H
#define CROFT_EDITOR_COMMANDS_H

#include "croft/editor_text_model.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* CROFT_EDITOR_COMMANDS_H */
