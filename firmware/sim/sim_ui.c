#include "ui.c"
void sim_tab(int tab) { hide_result(NULL); ui_settings_close(); show_tab((tab_t)tab); }
void sim_code_text(const char *t, const char *err) { lv_textarea_set_text(s_code_input, t); lv_label_set_text(s_code_error, err); }
void sim_settings_from_home(void) { on_settings(NULL); }
