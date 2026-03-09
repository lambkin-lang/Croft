#ifndef CROFT_HOST_FILE_DIALOG_H
#define CROFT_HOST_FILE_DIALOG_H

#ifdef __cplusplus
extern "C" {
#endif

char* host_file_dialog_open_path(void);
char* host_file_dialog_save_path(const char* current_path);
void host_file_dialog_free_path(char* path);

#ifdef __cplusplus
}
#endif

#endif /* CROFT_HOST_FILE_DIALOG_H */
