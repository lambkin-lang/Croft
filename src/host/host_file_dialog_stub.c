#include "croft/host_file_dialog.h"

char* host_file_dialog_open_path(void)
{
    return 0;
}

char* host_file_dialog_save_path(const char* current_path)
{
    (void)current_path;
    return 0;
}

void host_file_dialog_free_path(char* path)
{
    (void)path;
}
