#pragma once

void wingui_set_last_error_string_internal(const char* text);
void wingui_set_last_error_hresult_internal(const char* prefix, long hr);
void wingui_clear_last_error_internal();