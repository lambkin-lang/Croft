#include "croft/editor_document.h"
#include "croft/host_editor_appkit.h"

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    const char* target_file = argc > 1 ? argv[1] : NULL;
    const char* auto_close_env = std::getenv("CROFT_EDITOR_AUTO_CLOSE_MS");
    const char* fallback =
        "Big analysis, small binaries.\n"
        "\n"
        "This AppKit/TextKit editor keeps Sapling as the document model,\n"
        "but drops tgfx and the scene renderer entirely.\n";
    croft_editor_document* document;
    croft_editor_appkit_options options;

    document = croft_editor_document_create(argc > 0 ? argv[0] : NULL,
                                            target_file,
                                            (const uint8_t*)fallback,
                                            std::strlen(fallback));
    if (!document) {
        return 1;
    }

    options.window_title = "Croft AppKit Text Editor";
    options.auto_close_millis = auto_close_env ? (int32_t)std::atoi(auto_close_env) : 0;

    {
        int rc = croft_editor_appkit_run(document, &options);
        croft_editor_document_destroy(document);
        return rc == 0 ? 0 : 1;
    }
}
