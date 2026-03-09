#include "croft/host_file_dialog.h"

#include <AppKit/AppKit.h>
#include <stdlib.h>
#include <string.h>

static char* croft_host_file_dialog_strdup(NSString* path)
{
    const char* utf8;
    size_t len;
    char* copy;

    if (!path) {
        return NULL;
    }

    utf8 = path.fileSystemRepresentation;
    if (!utf8) {
        return NULL;
    }

    len = strlen(utf8);
    copy = (char*)malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, utf8, len + 1u);
    return copy;
}

char* host_file_dialog_open_path(void)
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] != NSModalResponseOK) {
        return NULL;
    }
    return croft_host_file_dialog_strdup(panel.URL.path);
}

char* host_file_dialog_save_path(const char* current_path)
{
    NSSavePanel* panel = [NSSavePanel savePanel];

    if (current_path && current_path[0] != '\0') {
        NSString* path = [NSString stringWithUTF8String:current_path];
        if (path) {
            NSString* directory = [path stringByDeletingLastPathComponent];
            NSString* leaf = path.lastPathComponent;
            if (directory.length > 0u) {
                panel.directoryURL = [NSURL fileURLWithPath:directory];
            }
            if (leaf.length > 0u) {
                panel.nameFieldStringValue = leaf;
            }
        }
    }

    if ([panel runModal] != NSModalResponseOK) {
        return NULL;
    }
    return croft_host_file_dialog_strdup(panel.URL.path);
}

void host_file_dialog_free_path(char* path)
{
    free(path);
}
