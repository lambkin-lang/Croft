#pragma once

#include "croft/editor_typography.h"

#include <tgfx/core/Font.h>
#include <tgfx/core/TextBlob.h>
#include <tgfx/core/Typeface.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

namespace croft_tgfx_text_cache {

struct TextKey {
    uint32_t font_size_centipoints;
    uint8_t font_role;
    std::string text;

    bool operator==(const TextKey& other) const {
        return font_size_centipoints == other.font_size_centipoints
            && font_role == other.font_role
            && text == other.text;
    }
};

struct TextKeyHash {
    size_t operator()(const TextKey& key) const {
        size_t h1 = std::hash<uint32_t>{}(key.font_size_centipoints);
        size_t h2 = std::hash<uint8_t>{}(key.font_role);
        size_t h3 = std::hash<std::string>{}(key.text);
        size_t combined = h1 ^ (h2 + 0x9e3779b9u + (h1 << 6u) + (h1 >> 2u));
        return combined ^ (h3 + 0x9e3779b9u + (combined << 6u) + (combined >> 2u));
    }
};

struct Cache {
    std::shared_ptr<tgfx::Typeface> monospace_typeface = nullptr;
    std::shared_ptr<tgfx::Typeface> ui_typeface = nullptr;
    std::unordered_map<TextKey, std::shared_ptr<tgfx::TextBlob>, TextKeyHash> blobs = {};
    std::unordered_map<TextKey, float, TextKeyHash> widths = {};
};

enum {
    CROFT_TGFX_TEXT_BLOB_CACHE_LIMIT = 1024,
    CROFT_TGFX_TEXT_WIDTH_CACHE_LIMIT = 4096
};

inline uint32_t font_size_key(float font_size) {
    return static_cast<uint32_t>(std::lround(font_size * 100.0f));
}

inline const std::shared_ptr<tgfx::Typeface>& resolve_typeface(Cache* cache, uint8_t font_role) {
    static std::shared_ptr<tgfx::Typeface> empty = nullptr;
    std::shared_ptr<tgfx::Typeface>* slot = nullptr;

    if (!cache) {
        return empty;
    }

    slot = font_role == CROFT_TEXT_FONT_ROLE_UI
        ? &cache->ui_typeface
        : &cache->monospace_typeface;
    if (!*slot) {
        if (font_role == CROFT_TEXT_FONT_ROLE_UI) {
            *slot = tgfx::Typeface::MakeFromName("", "");
        } else {
            *slot = tgfx::Typeface::MakeFromName(CROFT_EDITOR_MONOSPACE_FONT_FAMILY, "");
        }
        if (!*slot && font_role != CROFT_TEXT_FONT_ROLE_MONOSPACE) {
            *slot = resolve_typeface(cache, CROFT_TEXT_FONT_ROLE_MONOSPACE);
        }
        if (!*slot) {
            *slot = tgfx::Typeface::MakeFromName("", "");
        }
    }
    return *slot;
}

inline tgfx::Font make_font(Cache* cache, float font_size, uint8_t font_role) {
    return tgfx::Font(resolve_typeface(cache, font_role), font_size);
}

inline void copy_probe_name(char* dest, size_t capacity, const std::string& value) {
    if (!dest || capacity == 0u) {
        return;
    }
    std::snprintf(dest, capacity, "%s", value.c_str());
}

inline void reset(Cache* cache) {
    if (!cache) {
        return;
    }

    cache->monospace_typeface.reset();
    cache->ui_typeface.reset();
    cache->blobs.clear();
    cache->widths.clear();
}

inline std::shared_ptr<tgfx::TextBlob> get_text_blob(Cache* cache,
                                                     const char* text,
                                                     uint32_t len,
                                                     float font_size,
                                                     uint8_t font_role);
inline float measure_text(Cache* cache,
                          const char* text,
                          uint32_t len,
                          float font_size,
                          uint8_t font_role);

inline std::shared_ptr<tgfx::TextBlob> get_text_blob(Cache* cache,
                                                     const char* text,
                                                     uint32_t len,
                                                     float font_size,
                                                     uint8_t font_role) {
    TextKey key;
    auto found = cache ? cache->blobs.end() : decltype(cache->blobs.end()){};
    tgfx::Font font = make_font(cache, font_size, font_role);

    if (!cache || !text || len == 0) {
        return nullptr;
    }

    key.font_size_centipoints = font_size_key(font_size);
    key.font_role = font_role;
    key.text.assign(text, static_cast<size_t>(len));
    found = cache->blobs.find(key);
    if (found != cache->blobs.end()) {
        return found->second;
    }

    if (cache->blobs.size() >= CROFT_TGFX_TEXT_BLOB_CACHE_LIMIT) {
        cache->blobs.clear();
    }

    {
        auto blob = tgfx::TextBlob::MakeFrom(key.text, font);
        if (blob) {
            cache->blobs.emplace(std::move(key), blob);
        }
        return blob;
    }
}

inline float measure_text(Cache* cache, const char* text, uint32_t len, float font_size) {
    return measure_text(cache, text, len, font_size, CROFT_TEXT_FONT_ROLE_MONOSPACE);
}

inline std::shared_ptr<tgfx::TextBlob> get_text_blob(Cache* cache,
                                                     const char* text,
                                                     uint32_t len,
                                                     float font_size) {
    return get_text_blob(cache, text, len, font_size, CROFT_TEXT_FONT_ROLE_MONOSPACE);
}

inline float measure_text(Cache* cache,
                          const char* text,
                          uint32_t len,
                          float font_size,
                          uint8_t font_role) {
    TextKey key;
    auto found = cache ? cache->widths.end() : decltype(cache->widths.end()){};
    tgfx::Font font = make_font(cache, font_size, font_role);
    float total_advance = 0.0f;
    uint32_t i = 0;

    if (!text || len == 0) {
        return 0.0f;
    }

    if (cache) {
        key.font_size_centipoints = font_size_key(font_size);
        key.font_role = font_role;
        key.text.assign(text, static_cast<size_t>(len));
        found = cache->widths.find(key);
        if (found != cache->widths.end()) {
            return found->second;
        }
    }

    while (i < len) {
        uint8_t c = static_cast<uint8_t>(text[i]);
        uint32_t codepoint = 0;
        int bytes = 1;
        if ((c & 0x80) == 0) {
            codepoint = c;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < len) {
                codepoint = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
                bytes = 2;
            } else {
                break;
            }
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 < len) {
                codepoint = ((c & 0x0F) << 12) |
                            ((text[i + 1] & 0x3F) << 6) |
                            (text[i + 2] & 0x3F);
                bytes = 3;
            } else {
                break;
            }
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 < len) {
                codepoint = ((c & 0x07) << 18) |
                            ((text[i + 1] & 0x3F) << 12) |
                            ((text[i + 2] & 0x3F) << 6) |
                            (text[i + 3] & 0x3F);
                bytes = 4;
            } else {
                break;
            }
        }

        total_advance += font.getAdvance(font.getGlyphID(codepoint));
        i += static_cast<uint32_t>(bytes);
    }

    if (cache) {
        if (cache->widths.size() >= CROFT_TGFX_TEXT_WIDTH_CACHE_LIMIT) {
            cache->widths.clear();
        }
        cache->widths.emplace(std::move(key), total_advance);
    }
    return total_advance;
}

inline int32_t probe_font(Cache* cache,
                          float font_size,
                          const char* sample,
                          uint32_t len,
                          croft_editor_font_probe* out_probe) {
    const auto& typeface = resolve_typeface(cache, CROFT_TEXT_FONT_ROLE_MONOSPACE);
    tgfx::Font font;
    tgfx::FontMetrics metrics;

    if (!out_probe || !typeface) {
        return -1;
    }

    std::memset(out_probe, 0, sizeof(*out_probe));
    out_probe->point_size = font_size;
    copy_probe_name(out_probe->requested_family,
                    sizeof(out_probe->requested_family),
                    CROFT_EDITOR_MONOSPACE_FONT_FAMILY);
    copy_probe_name(out_probe->requested_style,
                    sizeof(out_probe->requested_style),
                    "");
    copy_probe_name(out_probe->resolved_family,
                    sizeof(out_probe->resolved_family),
                    typeface->fontFamily());
    copy_probe_name(out_probe->resolved_style,
                    sizeof(out_probe->resolved_style),
                    typeface->fontStyle());

    font = make_font(cache, font_size, CROFT_TEXT_FONT_ROLE_MONOSPACE);
    metrics = font.getMetrics();
    out_probe->line_height = std::fmax(0.0f, metrics.descent - metrics.ascent + metrics.leading);
    if (sample && len > 0u) {
        out_probe->sample_width = measure_text(cache, sample, len, font_size);
    }
    return 0;
}

}  // namespace croft_tgfx_text_cache
