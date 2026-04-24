#define _CRT_SECURE_NO_WARNINGS
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD g_task_counter = 0;
static CRITICAL_SECTION g_task_counter_cs;
static BOOL g_cs_initialized = FALSE;

static DWORD WINAPI core_worker_thread(LPVOID param) {
    CORE_QUEUE* queue = (CORE_QUEUE*)param;
    TASK* task;
    
    while (1) {
        EnterCriticalSection(&queue->cs);
        
        if (queue->count == 0) {
            LeaveCriticalSection(&queue->cs);
            Sleep(10);
            continue;
        }
        
        task = queue->head;
        if (!task) {
            LeaveCriticalSection(&queue->cs);
            Sleep(10);
            continue;
        }
        
        queue->head = task->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
        queue->count--;
        queue->active_count++;
        
        LeaveCriticalSection(&queue->cs);
        
        if (task->work_func) {
            DWORD start = GetTickCount();
            task->status = 1;
            task->start_time = start;
            
            task->work_func(task->arg);
            
            task->end_time = GetTickCount();
            task->actual_time = task->end_time - start;
            task->status = 2;
        }
        
        EnterCriticalSection(&queue->cs);
        queue->active_count--;
        LeaveCriticalSection(&queue->cs);
    }
    
    return 0;
}

static DWORD WINAPI rebalance_thread(LPVOID param) {
    SCHEDULER* sched = (SCHEDULER*)param;
    
    while (sched->running) {
        Sleep(1000);
        
        if (!sched->running) break;
        
        EnterCriticalSection(&sched->global_cs);
        
        for (DWORD i = 0; i < sched->num_cores; i++) {
            EnterCriticalSection(&sched->queues[i].cs);
            DWORD load = sched->queues[i].count + sched->queues[i].active_count;
            LeaveCriticalSection(&sched->queues[i].cs);
        }
        
        LeaveCriticalSection(&sched->global_cs);
    }
    
    return 0;
}

SCHEDULER* scheduler_init(DWORD num_cores) {
    if (!g_cs_initialized) {
        InitializeCriticalSection(&g_task_counter_cs);
        g_cs_initialized = TRUE;
    }
    
    SCHEDULER* sched = (SCHEDULER*)calloc(1, sizeof(SCHEDULER));
    if (!sched) return NULL;
    
    sched->num_cores = num_cores;
    sched->algorithm = SCHED_ROUND_ROBIN;
    sched->running = FALSE;
    
    sched->queues = (CORE_QUEUE*)calloc(num_cores, sizeof(CORE_QUEUE));
    if (!sched->queues) {
        free(sched);
        return NULL;
    }
    
    InitializeCriticalSection(&sched->global_cs);
    
    for (DWORD i = 0; i < num_cores; i++) {
        sched->queues[i].core_id = i;
        sched->queues[i].head = NULL;
        sched->queues[i].tail = NULL;
        sched->queues[i].count = 0;
        sched->queues[i].active_count = 0;
        InitializeCriticalSection(&sched->queues[i].cs);
        sched->queues[i].thread_pool = CreateThread(NULL, 0, core_worker_thread, 
                                               &sched->queues[i], 0, NULL);
    }
    
    return sched;
}

void scheduler_free(SCHEDULER* sched) {
    if (!sched) return;
    
    sched->running = FALSE;
    Sleep(100);
    
    for (DWORD i = 0; i < sched->num_cores; i++) {
        if (sched->queues[i].thread_pool) {
            TerminateThread(sched->queues[i].thread_pool, 0);
            CloseHandle(sched->queues[i].thread_pool);
        }
        DeleteCriticalSection(&sched->queues[i].cs);
    }
    
    if (sched->monitor_thread) {
        TerminateThread(sched->monitor_thread, 0);
        CloseHandle(sched->monitor_thread);
    }
    
    DeleteCriticalSection(&sched->global_cs);
    
    if (sched->queues) {
        for (DWORD i = 0; i < sched->num_cores; i++) {
            TASK* task = sched->queues[i].head;
            while (task) {
                TASK* next = task->next;
                free(task);
                task = next;
            }
        }
        free(sched->queues);
    }
    
    free(sched);
}

TASK* scheduler_create_task(void* (*work_func)(void*), void* arg, DWORD priority) {
    TASK* task = (TASK*)calloc(1, sizeof(TASK));
    if (!task) return NULL;
    
    EnterCriticalSection(&g_task_counter_cs);
    task->task_id = ++g_task_counter;
    LeaveCriticalSection(&g_task_counter_cs);
    
    task->work_func = work_func;
    task->arg = arg;
    task->priority = priority;
    task->status = 0;
    task->core_assigned = (DWORD)-1;
    task->next = NULL;
    
    return task;
}

void scheduler_free_task(TASK* task) {
    if (task) free(task);
}

BOOL scheduler_submit_task(SCHEDULER* sched, TASK* task) {
    if (!sched || !task) return FALSE;
    
    DWORD core_id;
    
    switch (sched->algorithm) {
        case SCHED_ROUND_ROBIN:
            core_id = (sched->total_tasks + 1) % sched->num_cores;
            break;
            
        case SCHED_LEAST_LOADED:
            core_id = scheduler_get_best_core(sched);
            break;
            
        case SCHED_WORK_STEALING:
            core_id = scheduler_get_best_core(sched);
            break;
            
        default:
            core_id = 0;
    }
    
    task->core_assigned = core_id;
    
    EnterCriticalSection(&sched->queues[core_id].cs);
    
    task->next = NULL;
    if (sched->queues[core_id].tail) {
        sched->queues[core_id].tail->next = task;
        sched->queues[core_id].tail = task;
    } else {
        sched->queues[core_id].head = task;
        sched->queues[core_id].tail = task;
    }
    sched->queues[core_id].count++;
    
    LeaveCriticalSection(&sched->queues[core_id].cs);
    
    EnterCriticalSection(&sched->global_cs);
    sched->total_tasks++;
    LeaveCriticalSection(&sched->global_cs);
    
    return TRUE;
}

BOOL scheduler_cancel_task(SCHEDULER* sched, DWORD task_id) {
    if (!sched) return FALSE;
    
    for (DWORD i = 0; i < sched->num_cores; i++) {
        EnterCriticalSection(&sched->queues[i].cs);
        
        TASK* prev = NULL;
        TASK* curr = sched->queues[i].head;
        
        while (curr) {
            if (curr->task_id == task_id && curr->status == 0) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    sched->queues[i].head = curr->next;
                }
                
                if (curr == sched->queues[i].tail) {
                    sched->queues[i].tail = prev;
                }
                
                sched->queues[i].count--;
                LeaveCriticalSection(&sched->queues[i].cs);
                
                free(curr);
                return TRUE;
            }
            prev = curr;
            curr = curr->next;
        }
        
        LeaveCriticalSection(&sched->queues[i].cs);
    }
    
    return FALSE;
}

DWORD scheduler_get_best_core(SCHEDULER* sched) {
    DWORD best_core = 0;
    DWORD min_load = (DWORD)-1;
    
    for (DWORD i = 0; i < sched->num_cores; i++) {
        EnterCriticalSection(&sched->queues[i].cs);
        DWORD load = sched->queues[i].count + sched->queues[i].active_count;
        LeaveCriticalSection(&sched->queues[i].cs);
        
        if (load < min_load) {
            min_load = load;
            best_core = i;
        }
    }
    
    return best_core;
}

void scheduler_set_algorithm(SCHEDULER* sched, SCHED_ALGORITHM algo) {
    if (sched) {
        sched->algorithm = algo;
    }
}

SCHED_ALGORITHM scheduler_get_algorithm(SCHEDULER* sched) {
    return sched ? sched->algorithm : SCHED_ROUND_ROBIN;
}

void scheduler_run_task(SCHEDULER* sched, TASK* task) {
    if (!sched || !task) return;
    
    BOOL success = scheduler_submit_task(sched, task);
    if (success) {
        while (task->status != 2) {
            Sleep(10);
        }
    }
}

void scheduler_start(SCHEDULER* sched) {
    if (!sched || sched->running) return;
    
    sched->running = TRUE;
    sched->monitor_thread = CreateThread(NULL, 0, rebalance_thread, sched, 0, NULL);
}

void scheduler_stop(SCHEDULER* sched) {
    if (!sched) return;
    sched->running = FALSE;
}

void scheduler_get_stats(SCHEDULER* sched, DWORD* total, DWORD* completed, DWORD* failed) {
    if (!sched) return;
    
    if (total) *total = sched->total_tasks;
    if (completed) *completed = sched->completed_tasks;
    if (failed) *failed = sched->failed_tasks;
}

CORE_QUEUE* scheduler_get_queue(SCHEDULER* sched, DWORD core_id) {
    if (!sched || core_id >= sched->num_cores) return NULL;
    return &sched->queues[core_id];
}

void* default_work_func(void* arg) {
    if (!arg) return NULL;
    
    DWORD iterations = *(DWORD*)arg;
    volatile double result = 0;
    
    for (DWORD i = 0; i < iterations; i++) {
        result += (double)i * 0.0001;
    }
    return NULL;
}