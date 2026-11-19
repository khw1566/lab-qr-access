#include "ui_settings.c"
void sim_settings_page(int p, const char *input)
{
    ui_settings_open();
    switch (p) {
    case PAGE_PIN: lv_textarea_set_text(s_pin_input, input ? input : ""); break;
    case PAGE_MENU: menu_open(); break;
    case PAGE_WIFI: wifi_page_open(); break;
    case PAGE_SCAN: {
        static const wifi_ap_t aps[] = {
            {"UOU-Lab", -48, true}, {"UOU-WiFi", -55, true}, {"Lab-Guest", -61, false},
            {"KT_GiGA_5G_3F", -70, true}, {"iptime-302", -77, true}, {"U+Net8A21", -83, true}};
        int n = sizeof(aps) / sizeof(aps[0]);
        memcpy(s_aps, aps, sizeof(aps));
        page_show(PAGE_SCAN);
        ap_list_fill(n);
        break; }
    case PAGE_PASS:
        strcpy(s_aps[3].ssid, "KT_GiGA_5G_3F"); s_aps[3].secured = true;
        lv_label_set_text(s_pass_title, "KT_GiGA_5G_3F 비밀번호");
        lv_textarea_set_text(s_pass_input, input ? input : "");
        page_show(PAGE_PASS); break;
    case PAGE_DOOR: door_page_open(); break;
    case PAGE_PINSET: pinset_page_open(); if (input) lv_textarea_set_text(s_pinset_input, input); break;
    case PAGE_VOICE: voice_page_open(); break;
    case PAGE_DISPLAY: display_page_open(); break;
    }
}
