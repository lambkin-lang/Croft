#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CROFT_WASI_VENDOR_REPORT_TXT_PATH
#error "CROFT_WASI_VENDOR_REPORT_TXT_PATH must be defined"
#endif

#ifndef CROFT_WASI_VENDOR_REPORT_JSON_PATH
#error "CROFT_WASI_VENDOR_REPORT_JSON_PATH must be defined"
#endif

static char* read_file(const char* path)
{
    FILE* f;
    long size;
    char* buf;
    size_t nread;

    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char*)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

static int expect_contains(const char* label, const char* haystack, const char* needle)
{
    if (haystack && needle && strstr(haystack, needle)) {
        return 1;
    }
    fprintf(stderr, "%s: missing '%s'\n", label, needle ? needle : "<null>");
    return 0;
}

int main(void)
{
    char* report_txt = read_file(CROFT_WASI_VENDOR_REPORT_TXT_PATH);
    char* report_json = read_file(CROFT_WASI_VENDOR_REPORT_JSON_PATH);
    int ok = 1;

    if (!report_txt) {
        fprintf(stderr, "unable to read %s\n", CROFT_WASI_VENDOR_REPORT_TXT_PATH);
        return 1;
    }
    if (!report_json) {
        fprintf(stderr, "unable to read %s\n", CROFT_WASI_VENDOR_REPORT_JSON_PATH);
        free(report_txt);
        return 1;
    }

    ok &= expect_contains("txt header", report_txt, "# Croft WASI vendor drift report");
    ok &= expect_contains("txt overlay package", report_txt, "Overlay packages (1): cli");
    ok &= expect_contains("txt cli summary", report_txt, "- cli: vendor=");

    ok &= expect_contains("json vendor packages", report_json, "\"vendor_packages\": [\"cli\"");
    ok &= expect_contains("json overlay packages", report_json, "\"overlay_packages\": [\"cli\"]");
    ok &= expect_contains("json cli package", report_json, "\"name\": \"cli\"");
    ok &= expect_contains("json overlay count", report_json, "\"overlay_wit_count\": 6");
    ok &= expect_contains("json external root field", report_json, "\"external_root\":");

    free(report_txt);
    free(report_json);
    return ok ? 0 : 1;
}
