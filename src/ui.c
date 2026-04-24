#define _CRT_SECURE_NO_WARNINGS
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLOR_RESET     7
#define COLOR_RED       12
#define COLOR_GREEN     10
#define COLOR_YELLOW    14
#define COLOR_CYAN      11
#define COLOR_MAGENTA   13

static WORD get_console_color(int color) {
    switch (color) {
        case COLOR_RED:    return FOREGROUND_RED;
        case COLOR_GREEN:  return FOREGROUND_GREEN;
        case COLOR_YELLOW: return FOREGROUND_RED | FOREGROUND_GREEN;
        case COLOR_CYAN:   return FOREGROUND_INTENSITY | FOREGROUND_BLUE | FOREGROUND_GREEN;
        case COLOR_MAGENTA: return FOREGROUND_RED | FOREGROUND_BLUE;
        default:          return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

CONSOLE_UI* ui_init(void) {
    CONSOLE_UI* ui = (CONSOLE_UI*)calloc(1, sizeof(CONSOLE_UI));
    if (!ui) return NULL;
    
    ui->console = GetStdHandle(STD_OUTPUT_HANDLE);
    ui->width = 80;
    ui->height = 25;
    ui->color_support = TRUE;
    
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(ui->console, &csbi)) {
        ui->width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        ui->height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    
    SetConsoleTitleA("Multicore Load Balancing Scheduler");
    
    return ui;
}

void ui_free(CONSOLE_UI* ui) {
    if (ui) free(ui);
}

void ui_clear(CONSOLE_UI* ui) {
    if (!ui) return;
    
    COORD coord = {0, 0};
    DWORD written;
    
    FillConsoleOutputCharacterA(ui->console, ' ', ui->width * ui->height, coord, &written);
    FillConsoleOutputAttribute(ui->console, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
                              ui->width * ui->height, coord, &written);
    
    SetConsoleCursorPosition(ui->console, coord);
}

void ui_set_color(CONSOLE_UI* ui, int color) {
    if (!ui) return;
    SetConsoleTextAttribute(ui->console, get_console_color(color));
}

void ui_reset_color(CONSOLE_UI* ui) {
    if (!ui) return;
    SetConsoleTextAttribute(ui->console, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void ui_draw_bar(CONSOLE_UI* ui, double percentage, int width) {
    if (!ui || width <= 0) return;
    
    int filled = (int)((percentage / 100.0) * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    
    int i;
    for (i = 0; i < filled; i++) {
        putchar('#');
    }
    for (i = filled; i < width; i++) {
        putchar('-');
    }
}

void ui_draw_header(CONSOLE_UI* ui, SYSTEM_INFO_EXT* sys_info) {
    if (!ui || !sys_info) return;
    
    printf("\n");
    ui_set_color(ui, COLOR_CYAN);
    printf("================================================================================\n");
    printf("|           MULTICORE LOAD BALANCING SCHEDULER WITH KERNEL DETECTION           |\n");
    printf("================================================================================\n");
    ui_reset_color(ui);
    
    printf("| CPU: %-60s |\n", sys_info->cpu_brand);
    printf("| Logical Cores: %-2lu | Physical Cores: %-2lu | Total Memory: %llu MB |\n",
           sys_info->num_logical_cores, sys_info->num_physical_cores,
           sys_info->total_memory / (1024 * 1024));
    printf("================================================================================\n\n");
}

void ui_draw_core(CONSOLE_UI* ui, CORE_DISPLAY* core) {
    if (!ui || !core) return;
    
    ui_set_color(ui, COLOR_YELLOW);
    printf("| Core %2lu: ", core->core_id);
    ui_reset_color(ui);
    
    ui_set_color(ui, COLOR_GREEN);
    for (int i = 0; i < 40; i++) {
        if (i < (int)(core->usage * 40 / 100)) {
            putchar('#');
        } else {
            putchar('-');
        }
    }
    ui_reset_color(ui);
    
    printf(" %5.1f%% | Q:%2lu | A:%2lu |\n", 
           core->usage, core->queue_count, core->active_count);
}

void ui_draw_all_cores(CONSOLE_UI* ui, SYSTEM_INFO_EXT* sys_info, SCHEDULER* sched) {
    if (!ui || !sys_info) return;
    
    ui_set_color(ui, COLOR_CYAN);
    printf("+========================= CORE UTILIZATION ==========================+\n");
    ui_reset_color(ui);
    
    DWORD num_cores = sys_info->num_cores;
    if (num_cores > MAX_CORE_DISPLAY) num_cores = MAX_CORE_DISPLAY;
    
    for (DWORD i = 0; i < num_cores; i++) {
        CORE_DISPLAY core;
        memset(&core, 0, sizeof(core));
        
        core.core_id = i;
        core.usage = sys_info->cores[i].cpu_usage;
        
        if (sched) {
            EnterCriticalSection(&sched->queues[i].cs);
            core.queue_count = sched->queues[i].count;
            core.active_count = sched->queues[i].active_count;
            LeaveCriticalSection(&sched->queues[i].cs);
        }
        
        ui_draw_core(ui, &core);
    }
    
    printf("+================================================================-----+\n\n");
}

void ui_draw_stats(CONSOLE_UI* ui, SCHEDULER* sched) {
    if (!ui || !sched) return;
    
    ui_set_color(ui, COLOR_MAGENTA);
    printf("+========================= SCHEDULER STATISTICS ==========================+\n");
    ui_reset_color(ui);
    
    const char* algo_name = "Unknown";
    switch (sched->algorithm) {
        case SCHED_ROUND_ROBIN: algo_name = "Round Robin"; break;
        case SCHED_LEAST_LOADED: algo_name = "Least Loaded"; break;
        case SCHED_WORK_STEALING: algo_name = "Work Stealing"; break;
    }
    
    printf("| Algorithm: %-20s | Total Tasks: %-5lu | Completed: %-5lu |\n",
           algo_name, sched->total_tasks, sched->completed_tasks);
    printf("| Status: %-23s | Avg Wait: %-8.2f | Failed: %-5lu |\n",
           sched->running ? "Running" : "Stopped",
           sched->avg_wait_time, sched->failed_tasks);
    printf("+================================================================-----+\n\n");
}

void ui_draw_menu(CONSOLE_UI* ui) {
    if (!ui) return;
    
    ui_set_color(ui, COLOR_CYAN);
    printf("+=========================== MENU ==============================+\n");
    ui_reset_color(ui);
    
    ui_set_color(ui, COLOR_GREEN);
    printf("| [1] Submit Tasks        | [2] Change Algorithm        |\n");
    printf("| [3] Show Core Details   | [4] View Task Queue         |\n");
    printf("| [5] Start Scheduler    | [6] Stop Scheduler          |\n");
    printf("| [7] Run Benchmark     | [8] Settings               |\n");
    ui_reset_color(ui);
    
    printf("| [0] Exit                                                   |\n");
    printf("+=============================================================+\n");
    printf("\nEnter choice: ");
}

void ui_draw_progress(CONSOLE_UI* ui, const char* label, DWORD current, DWORD total) {
    if (!ui || !label) return;
    
    double percentage = (total > 0) ? (double)current * 100.0 / total : 0;
    
    printf("\r%s: [", label);
    
    int bar_width = 40;
    int filled = (int)(percentage * bar_width / 100);
    
    int i;
    for (i = 0; i < filled; i++) putchar('#');
    for (i = filled; i < bar_width; i++) putchar('-');
    
    printf("] %5.1f%% (%lu/%lu)", percentage, current, total);
    fflush(stdout);
}

int ui_get_choice(CONSOLE_UI* ui) {
    int choice = -1;
    char input[16];
    
    if (fgets(input, sizeof(input), stdin)) {
        choice = atoi(input);
    }
    
    return choice;
}

void ui_draw_legend(CONSOLE_UI* ui) {
    if (!ui) return;
    
    printf("\nLegend:\n");
    printf("  [###] = CPU Usage (>50%% = high)\n");
    printf("  Q     = Queue count (pending tasks)\n");
    printf("  A     = Active count (running tasks)\n");
    printf("\n");
}

void ui_draw_welcome(CONSOLE_UI* ui, SYSTEM_INFO_EXT* sys_info) {
    if (!ui || !sys_info) return;
    
    ui_clear(ui);
    ui_draw_header(ui, sys_info);
}