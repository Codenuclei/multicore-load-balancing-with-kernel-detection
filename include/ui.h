#ifndef UI_H
#define UI_H

#include "kernel_detect.h"
#include "scheduler.h"
#include <windows.h>

#define MAX_CORE_DISPLAY 64

typedef struct {
    HANDLE console;
    SHORT width;
    SHORT height;
    BOOL color_support;
} CONSOLE_UI;

typedef struct {
    DWORD core_id;
    double usage;
    DWORD queue_count;
    DWORD active_count;
    char bar[51];
} CORE_DISPLAY;

CONSOLE_UI* ui_init(void);
void ui_free(CONSOLE_UI* ui);
void ui_clear(CONSOLE_UI* ui);
void ui_draw_header(CONSOLE_UI* ui, SYSTEM_INFO_EXT* sys_info);
void ui_draw_core(CONSOLE_UI* ui, CORE_DISPLAY* core);
void ui_draw_all_cores(CONSOLE_UI* ui, SYSTEM_INFO_EXT* sys_info, SCHEDULER* sched);
void ui_draw_stats(CONSOLE_UI* ui, SCHEDULER* sched);
void ui_draw_menu(CONSOLE_UI* ui);
void ui_draw_progress(CONSOLE_UI* ui, const char* label, DWORD current, DWORD total);
int ui_get_choice(CONSOLE_UI* ui);
void ui_draw_legend(CONSOLE_UI* ui);
void ui_set_color(CONSOLE_UI* ui, int color);
void ui_reset_color(CONSOLE_UI* ui);
void ui_draw_bar(CONSOLE_UI* ui, double percentage, int width);

#endif