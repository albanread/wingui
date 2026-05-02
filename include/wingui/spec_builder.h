#pragma once

#include "wingui/wingui.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * spec_builder.h
 *
 * Public authoring-side boundary for Wingui's portable declarative UI story.
 *
 * Build with spec_builder. Run with spec_bind.
 */

WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_validate_json(
	const char* json_utf8);

WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_copy_canonical_json(
	const char* json_utf8,
	char* buffer_utf8,
	uint32_t buffer_size,
	uint32_t* out_required_size);

WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_copy_normalized_json(
	const char* json_utf8,
	char* buffer_utf8,
	uint32_t buffer_size,
	uint32_t* out_required_size);

WINGUI_API int32_t WINGUI_CALL wingui_spec_builder_copy_patch_json(
	const char* old_json_utf8,
	const char* new_json_utf8,
	char* buffer_utf8,
	uint32_t buffer_size,
	uint32_t* out_required_size,
	int32_t* out_requires_full_publish,
	uint32_t* out_patch_op_count);

#ifdef __cplusplus
}
#endif