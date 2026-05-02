#include "wingui/spec_builder.h"
#include "wingui/wingui.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_with_message(const char* title_utf8) {
    const char* message = wingui_last_error_utf8();
    MessageBoxA(
        NULL,
        (message && message[0]) ? message : "Unknown error",
        title_utf8,
        MB_OK | MB_ICONERROR);
    return 1;
}

static char* copy_canonical_json_text(const char* json_utf8) {
    uint32_t required_size = 0;
    char* buffer;

    if (!wingui_spec_builder_copy_canonical_json(json_utf8, NULL, 0, &required_size) && required_size == 0) {
        return NULL;
    }

    buffer = (char*)malloc(required_size);
    if (!buffer) {
        return NULL;
    }

    if (!wingui_spec_builder_copy_canonical_json(json_utf8, buffer, required_size, &required_size)) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

static char* copy_normalized_json_text(const char* json_utf8) {
    uint32_t required_size = 0;
    char* buffer;

    if (!wingui_spec_builder_copy_normalized_json(json_utf8, NULL, 0, &required_size) && required_size == 0) {
        return NULL;
    }

    buffer = (char*)malloc(required_size);
    if (!buffer) {
        return NULL;
    }

    if (!wingui_spec_builder_copy_normalized_json(json_utf8, buffer, required_size, &required_size)) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

static char* copy_patch_json_text(
    const char* old_json_utf8,
    const char* new_json_utf8,
    int* out_requires_full_publish,
    uint32_t* out_patch_op_count) {
    uint32_t required_size = 0;
    int requires_full_publish = 0;
    uint32_t patch_op_count = 0;
    char* buffer;

    if (!wingui_spec_builder_copy_patch_json(
            old_json_utf8,
            new_json_utf8,
            NULL,
            0,
            &required_size,
            &requires_full_publish,
            &patch_op_count)) {
        if (requires_full_publish || required_size > 0) {
            /* Expected probe path: size is reported through out_required_size. */
        } else {
            return NULL;
        }
    }

    if (out_requires_full_publish) {
        *out_requires_full_publish = requires_full_publish;
    }
    if (out_patch_op_count) {
        *out_patch_op_count = patch_op_count;
    }

    if (requires_full_publish) {
        return NULL;
    }

    buffer = (char*)malloc(required_size);
    if (!buffer) {
        return NULL;
    }

    if (!wingui_spec_builder_copy_patch_json(
            old_json_utf8,
            new_json_utf8,
            buffer,
            required_size,
            &required_size,
            &requires_full_publish,
            &patch_op_count)) {
        free(buffer);
        return NULL;
    }

    if (out_requires_full_publish) {
        *out_requires_full_publish = requires_full_publish;
    }
    if (out_patch_op_count) {
        *out_patch_op_count = patch_op_count;
    }

    return buffer;
}

int main(void) {
    static const char* before_spec =
        "{"
        "\n  \"type\": \"window\","
        "\n  \"title\": \"Spec Builder Demo\","
        "\n  \"body\": {"
        "\n    \"type\": \"stack\","
        "\n    \"children\": ["
        "\n      { \"type\": \"heading\", \"text\": \"Spec Builder Demo\" },"
        "\n      { \"type\": \"text\", \"id\": \"summary\", \"text\": \"Before\" },"
        "\n      { \"type\": \"button\", \"id\": \"apply\", \"text\": \"Apply\", \"event\": \"apply\" }"
        "\n    ]"
        "\n  }"
        "\n}";

    static const char* after_spec =
        "{"
        "\n  \"type\": \"window\","
        "\n  \"title\": \"Spec Builder Demo\","
        "\n  \"body\": {"
        "\n    \"type\": \"stack\","
        "\n    \"children\": ["
        "\n      { \"type\": \"heading\", \"text\": \"Spec Builder Demo\" },"
        "\n      { \"type\": \"text\", \"id\": \"summary\", \"text\": \"After\" },"
        "\n      { \"type\": \"button\", \"id\": \"apply\", \"text\": \"Apply again\", \"event\": \"apply\" }"
        "\n    ]"
        "\n  }"
        "\n}";

    static const char* full_publish_spec =
        "{"
        "\n  \"type\": \"window\","
        "\n  \"title\": \"Spec Builder Demo\","
        "\n  \"body\": {"
        "\n    \"type\": \"stack\","
        "\n    \"children\": ["
        "\n      { \"type\": \"heading\", \"text\": \"Spec Builder Demo\" },"
        "\n      { \"type\": \"button\", \"id\": \"summary\", \"text\": \"Structural change\", \"event\": \"summary_click\" },"
        "\n      { \"type\": \"button\", \"id\": \"apply\", \"text\": \"Apply\", \"event\": \"apply\" }"
        "\n    ]"
        "\n  }"
        "\n}";

    char* canonical_before = NULL;
    char* normalized_before = NULL;
    char* patch_json = NULL;
    char* full_publish_patch_json = NULL;
    char message[12288];
    int requires_full_publish = 0;
    int full_publish_required = 0;
    uint32_t patch_op_count = 0;
    uint32_t full_publish_patch_op_count = 0;

    if (!wingui_spec_builder_validate_json(before_spec)) {
        return fail_with_message("Spec Builder C Demo");
    }

    canonical_before = copy_canonical_json_text(before_spec);
    if (!canonical_before) {
        return fail_with_message("Spec Builder C Demo");
    }

    normalized_before = copy_normalized_json_text(before_spec);
    if (!normalized_before) {
        free(canonical_before);
        return fail_with_message("Spec Builder C Demo");
    }

    patch_json = copy_patch_json_text(before_spec, after_spec, &requires_full_publish, &patch_op_count);
    if (!patch_json && !requires_full_publish) {
        free(canonical_before);
        free(normalized_before);
        return fail_with_message("Spec Builder C Demo");
    }

    full_publish_patch_json = copy_patch_json_text(
        before_spec,
        full_publish_spec,
        &full_publish_required,
        &full_publish_patch_op_count);
    if (!full_publish_patch_json && !full_publish_required) {
        free(canonical_before);
        free(normalized_before);
        free(patch_json);
        return fail_with_message("Spec Builder C Demo");
    }

    snprintf(
        message,
        sizeof(message),
        "validate: ok\n"
        "canonical: ok\n"
        "normalize: ok\n\n"
        "incremental patch ops: %u\n"
        "incremental requires full publish: %s\n"
        "structural patch ops: %u\n"
        "structural requires full publish: %s\n\n"
        "canonical before spec:\n%s\n\n"
        "normalized before spec:\n%s\n\n"
        "incremental patch json:\n%s\n\n"
        "structural patch result:\n%s",
        (unsigned int)patch_op_count,
        requires_full_publish ? "yes" : "no",
        (unsigned int)full_publish_patch_op_count,
        full_publish_required ? "yes" : "no",
        canonical_before,
        normalized_before,
        patch_json ? patch_json : "<full publish required>",
        full_publish_patch_json ? full_publish_patch_json : "<full publish required>");

    MessageBoxA(NULL, message, "Spec Builder C Demo", MB_OK | MB_ICONINFORMATION);

    free(canonical_before);
    free(normalized_before);
    free(patch_json);
    free(full_publish_patch_json);
    return 0;
}