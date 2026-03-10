#include "tests/generated/world_meta_command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIT_WORLD_META_MANIFEST_PATH
#error "WIT_WORLD_META_MANIFEST_PATH must be defined"
#endif

static int expect_contains(const char *haystack, const char *needle)
{
    if (strstr(haystack, needle)) {
        return 1;
    }
    fprintf(stderr, "missing manifest fragment: %s\n", needle);
    return 0;
}

static char *read_file(const char *path)
{
    FILE *f;
    long size;
    char *buf;
    size_t nread;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open %s\n", path);
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

    buf = (char *)malloc((size_t)size + 1u);
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

int main(void)
{
    SapWitCliEnvironmentCommand env_command = {0};
    SapWitCliRunCommand run_command = {0};
    char *manifest = read_file(WIT_WORLD_META_MANIFEST_PATH);
    int ok = 1;

    if (!manifest) {
        fprintf(stderr, "unable to read manifest %s\n", WIT_WORLD_META_MANIFEST_PATH);
        return 1;
    }

    env_command.case_tag = SAP_WIT_CLI_ENVIRONMENT_COMMAND_GET_ENVIRONMENT;
    run_command.case_tag = SAP_WIT_CLI_RUN_COMMAND_START;

    ok &= expect_contains(manifest, "# Columns: kind\twit-name\tnormalized\tc-name\tmacro-or-function\tmetadata");
    ok &= expect_contains(manifest, "world\tdemo:cli@0.1.0/command\tcommand\t<none>\t<none>\timported=0; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world\tdemo:cli@0.1.0/imports\timports\t<none>\t<none>\timported=1; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world-include\tdemo:cli@0.1.0/command::imports\timports\t<none>\t<none>\timported=0; target-kind=world; target=demo:cli@0.1.0/imports; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world-import\tdemo:cli@0.1.0/imports::environment\tenvironment\t<none>\t<none>\timported=1; target-kind=interface; target=demo:cli@0.1.0/environment; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "world-export\tdemo:cli@0.1.0/command::run\trun\t<none>\t<none>\timported=0; target-kind=interface; target=demo:cli@0.1.0/run; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "interface\tdemo:cli@0.1.0/environment\tenvironment\t<none>\t<none>\timported=1; attrs=@since(version = 0.1.0)");
    ok &= expect_contains(manifest, "interface\tdemo:cli@0.1.0/run\trun\t<none>\t<none>\timported=1; attrs=@since(version = 0.1.0)");

    free(manifest);
    (void)env_command;
    (void)run_command;
    return ok ? 0 : 1;
}
