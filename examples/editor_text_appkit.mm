#include "croft/editor_document.h"
#include "croft/editor_document_fs.h"
#include "croft/host_editor_appkit.h"
#include "croft/editor_typography_macos.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

static int env_flag_enabled(const char* value) {
    return value && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

static void print_font_probe_summary(void) {
    croft_editor_font_probe probe = {0};

    if (croft_editor_mac_probe_font(croft_editor_mac_monospace_font(15.0),
                                    15.0,
                                    CROFT_EDITOR_MONOSPACE_FONT_REGULAR,
                                    CROFT_EDITOR_FONT_PROBE_SAMPLE,
                                    std::strlen(CROFT_EDITOR_FONT_PROBE_SAMPLE),
                                    &probe) != 0) {
        return;
    }

    std::printf("editor-font-probe variant=appkit backend=textkit role=text requested_family=%s requested_style=%s resolved_family=%s resolved_style=%s point_size=%.1f sample_width=%.3f font_line_height=%.3f editor_line_height=%.3f\n",
                probe.requested_family,
                probe.requested_style,
                probe.resolved_family,
                probe.resolved_style,
                probe.point_size,
                probe.sample_width,
                probe.line_height,
                probe.line_height);
}

int main(int argc, char** argv) {
    const char* target_file = argc > 1 ? argv[1] : NULL;
    const char* auto_close_env = std::getenv("CROFT_EDITOR_AUTO_CLOSE_MS");
    const char* font_probe_env = std::getenv("CROFT_EDITOR_FONT_PROBE");
    const char* fallback =
        "Big analysis, small binaries.\n"
        "\n"
        "This AppKit/TextKit editor keeps Sapling as the document model,\n"
        "but drops tgfx and the scene renderer entirely.\n";
    croft_editor_document* document;
    croft_editor_appkit_options options;

    document = croft_editor_document_open(argc > 0 ? argv[0] : NULL,
                                          target_file,
                                          (const uint8_t*)fallback,
                                          std::strlen(fallback));
    if (!document) {
        return 1;
    }

    options.window_title = "Croft AppKit Text Editor";
    options.auto_close_millis = auto_close_env ? (int32_t)std::atoi(auto_close_env) : 0;
    if (env_flag_enabled(font_probe_env)) {
        print_font_probe_summary();
    }

    {
        int rc = croft_editor_appkit_run(document, &options);
        croft_editor_document_destroy(document);
        return rc == 0 ? 0 : 1;
    }
}
