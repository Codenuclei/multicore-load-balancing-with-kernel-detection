#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winsock.h>

#include "kernel_detect.h"
#include "scheduler.h"
#include "gui.h"
#include "ui.h"

#define VERSION "1.0.0"
#define DEFAULT_TASK_COUNT 100
#define DEFAULT_ITERATIONS 1000000
#define HTTP_PORT 8080

typedef struct {
    DWORD start_time;
    DWORD tasks_submitted;
    DWORD tasks_completed;
    BOOL running;
} BENCHMARK_DATA;

static BENCHMARK_DATA g_benchmark;

static DWORD WINAPI benchmark_submit_thread(LPVOID param);
static void run_benchmark(CONSOLE_UI* ui, SCHEDULER* sched, DWORD num_tasks, DWORD iterations);
static void print_usage(const char* prog_name);

int main(int argc, char* argv[]) {
    printf("Multicore Load Balancing Scheduler v%s\n", VERSION);
    printf("Initializing...\n\n");
    
    SYSTEM_INFO_EXT* sys_info = kernel_init();
    if (!sys_info) {
        fprintf(stderr, "Error: Failed to initialize kernel detection\n");
        return 1;
    }
    
    DWORD num_cores = kernel_get_core_count(sys_info);
    printf("Detected %lu logical cores\n", num_cores);
    
    SCHEDULER* sched = scheduler_init(num_cores);
    if (!sched) {
        fprintf(stderr, "Error: Failed to initialize scheduler\n");
        kernel_free(sys_info);
        return 1;
    }
    sched->sys_info = sys_info;
    
    CONSOLE_UI* ui = ui_init();
    if (!ui) {
        fprintf(stderr, "Error: Failed to initialize UI\n");
        scheduler_free(sched);
        kernel_free(sys_info);
        return 1;
    }
    
    BOOL interactive = TRUE;
    BOOL gui_mode = FALSE;
    DWORD benchmark_tasks = DEFAULT_TASK_COUNT;
    DWORD benchmark_iterations = DEFAULT_ITERATIONS;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            benchmark_tasks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            benchmark_iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            int algo = atoi(argv[++i]);
            scheduler_set_algorithm(sched, (SCHED_ALGORITHM)algo);
        } else if (strcmp(argv[i], "-b") == 0) {
            interactive = FALSE;
        } else if (strcmp(argv[i], "-g") == 0) {
            gui_mode = TRUE;
            interactive = FALSE;
            printf("Starting GUI server on http://localhost:%d\n", HTTP_PORT);
            fflush(stdout);
            scheduler_start(sched);
            start_gui_server(sched, sys_info);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            ui_free(ui);
            scheduler_free(sched);
            kernel_free(sys_info);
            return 0;
        } else {
            print_usage(argv[0]);
            ui_free(ui);
            scheduler_free(sched);
            kernel_free(sys_info);
            return 1;
        }
    }
    
    if (gui_mode) {
        printf("\nGUI started! Open http://localhost:%d in your browser\n", HTTP_PORT);
        printf("Press Enter to exit...\n");
        fflush(stdout);
        getchar();
        // g_server_running = FALSE; // Removed as it is now managed by gui server thread
        ui_free(ui);
        scheduler_free(sched);
        kernel_free(sys_info);
        return 0;
    }
    
    if (!interactive) {
        printf("\n=== Running Benchmark ===\n");
        printf("Tasks: %lu, Iterations per task: %lu\n", benchmark_tasks, benchmark_iterations);
        
        run_benchmark(ui, sched, benchmark_tasks, benchmark_iterations);
        
        printf("\n=== Benchmark Complete ===\n");
        printf("Submitted: %lu tasks\n", g_benchmark.tasks_submitted);
        printf("Total time: %lu ms\n", GetTickCount() - g_benchmark.start_time);
        
        ui_free(ui);
        scheduler_free(sched);
        kernel_free(sys_info);
        
        return 0;
    }
    
    ui_draw_header(ui, sys_info);
    ui_draw_all_cores(ui, sys_info, sched);
    ui_draw_menu(ui);
    
    int choice = ui_get_choice(ui);
    
    while (choice != 0) {
        switch (choice) {
            case 1: {
                printf("\nSubmitting %d tasks...\n", DEFAULT_TASK_COUNT);
                DWORD iter = 5000000;
                for (DWORD i = 0; i < DEFAULT_TASK_COUNT; i++) {
                    TASK* task = scheduler_create_task(default_work_func, (void*)(DWORD_PTR)iter, 1);
                    if (task) {
                        scheduler_submit_task(sched, task);
                    }
                }
                printf("Submitted %d tasks\n", DEFAULT_TASK_COUNT);
                break;
            }
            
            case 2: {
                printf("\nSelect Algorithm (0=RR, 1=LeastLoaded, 2=WorkStealing): ");
                int algo = -1;
                scanf("%d", &algo);
                while (getchar() != '\n');
                if (algo >= 0 && algo <= 2) {
                    scheduler_set_algorithm(sched, (SCHED_ALGORITHM)algo);
                    printf("Algorithm changed\n");
                }
                break;
            }
            
            case 3: {
                ui_draw_all_cores(ui, sys_info, sched);
                ui_draw_legend(ui);
                break;
            }
            
            case 4: {
                printf("\n=== Task Queue Status ===\n");
                for (DWORD i = 0; i < num_cores; i++) {
                    EnterCriticalSection(&sched->queues[i].cs);
                    printf("Core %lu: Queue=%lu, Active=%lu\n", 
                           i, sched->queues[i].count, sched->queues[i].active_count);
                    LeaveCriticalSection(&sched->queues[i].cs);
                }
                break;
            }
            
            case 5: {
                printf("\nStarting scheduler...\n");
                scheduler_start(sched);
                printf("Scheduler started\n");
                break;
            }
            
            case 6: {
                printf("\nStopping scheduler...\n");
                scheduler_stop(sched);
                printf("Scheduler stopped\n");
                break;
            }
            
            case 7: {
                printf("\nRunning benchmark...\n");
                run_benchmark(ui, sched, benchmark_tasks, benchmark_iterations);
                break;
            }
            
            case 8: {
                printf("\n=== Settings ===\n");
                printf("Algorithm: %d\n", scheduler_get_algorithm(sched));
                printf("Task iterations: %lu\n", benchmark_iterations);
                printf("Would you change iterations? (y/n): ");
                int c = getchar();
                if (c == 'y' || c == 'Y') {
                    printf("Enter new value: ");
                    scanf("%lu", &benchmark_iterations);
                    while (getchar() != '\n');
                }
                break;
            }
            
            case 9: {
                printf("\nStarting GUI server...\n");
                start_gui_server(sched, sys_info);
                printf("GUI started at http://localhost:%d\n", HTTP_PORT);
                break;
            }
            
            default:
                printf("Invalid choice\n");
                break;
        }
        
        printf("\n");
        ui_draw_header(ui, sys_info);
        ui_draw_all_cores(ui, sys_info, sched);
        ui_draw_stats(ui, sched);
        ui_draw_menu(ui);
        
        choice = ui_get_choice(ui);
    }
    
    printf("\nExiting...\n");
    
    scheduler_stop(sched);
    Sleep(100);
    
    ui_free(ui);
    scheduler_free(sched);
    kernel_free(sys_info);
    
    return 0;
}

static DWORD WINAPI benchmark_submit_thread(LPVOID param) {
    SCHEDULER* sched = (SCHEDULER*)param;
    DWORD iterations = DEFAULT_ITERATIONS;
    
    while (g_benchmark.running && g_benchmark.tasks_submitted < DEFAULT_TASK_COUNT) {
        TASK* task = scheduler_create_task(default_work_func, (void*)(DWORD_PTR)iterations, 1);
        if (task) {
            scheduler_submit_task(sched, task);
            g_benchmark.tasks_submitted++;
        }
    }
    
    return 0;
}

static void run_benchmark(CONSOLE_UI* ui, SCHEDULER* sched, DWORD num_tasks, DWORD iterations) {
    g_benchmark.start_time = GetTickCount();
    g_benchmark.tasks_submitted = 0;
    g_benchmark.tasks_completed = 0;
    g_benchmark.running = TRUE;
    
    DWORD start = GetTickCount();
    
    for (DWORD i = 0; i < num_tasks; i++) {
        TASK* task = scheduler_create_task(default_work_func, (void*)(DWORD_PTR)iterations, 1);
        if (task) {
            scheduler_submit_task(sched, task);
            g_benchmark.tasks_submitted++;
            
            if (i % 10 == 0) {
                ui_draw_progress(ui, "Submitting", i + 1, num_tasks);
            }
        }
    }
    
    printf("\n\nAll %lu tasks submitted\n", num_tasks);
    printf("Waiting for completion...\n");
    
    DWORD last_completed = 0;
    while (g_benchmark.tasks_completed < num_tasks) {
        DWORD completed = 0;
        for (DWORD i = 0; i < sched->num_cores; i++) {
            EnterCriticalSection(&sched->queues[i].cs);
            completed += (sched->queues[i].count + sched->queues[i].active_count);
            LeaveCriticalSection(&sched->queues[i].cs);
        }
        
        DWORD submitted = g_benchmark.tasks_submitted;
        if (completed != last_completed) {
            ui_draw_progress(ui, "Processing", submitted - completed, submitted);
            last_completed = completed;
        }
        
        if (completed == 0 && submitted >= num_tasks) {
            break;
        }
        
        Sleep(100);
    }
    
    DWORD elapsed = GetTickCount() - start;
    
    printf("\n\n=== Benchmark Results ===\n");
    printf("Total tasks submitted: %lu\n", g_benchmark.tasks_submitted);
    printf("Total time: %lu ms\n", elapsed);
    printf("Throughput: %.2f tasks/sec\n", 
           (elapsed > 0) ? (double)g_benchmark.tasks_submitted * 1000.0 / elapsed : 0);
    
    g_benchmark.running = FALSE;
}

static void print_usage(const char* prog_name) {
    printf("\nUsage: %s [options]\n", prog_name);
    printf("\nOptions:\n");
    printf("  -t N    Number of tasks (default: %d)\n", DEFAULT_TASK_COUNT);
    printf("  -i N    Iterations per task (default: %d)\n", DEFAULT_ITERATIONS);
    printf("  -a N    Algorithm (0=RR, 1=LeastLoaded, 2=WorkStealing)\n");
    printf("  -b      Batch mode (run benchmark and exit)\n");
    printf("  -g      GUI mode (start web server and open browser)\n");
    printf("  -h      Show this help\n");
    printf("\nExamples:\n");
    printf("  %s                      Interactive mode\n", prog_name);
    printf("  %s -b -t 200 -i 5000000  Benchmark with 200 tasks\n", prog_name);
    printf("  %s -g                   GUI mode with web interface\n", prog_name);
    printf("\n");
}