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
        while (queue->count == 0 && queue->sched->running) {
            LeaveCriticalSection(&queue->cs);
            task = scheduler_steal_task(queue->sched, queue->core_id);
            EnterCriticalSection(&queue->cs);
            if (task) {
                LeaveCriticalSection(&queue->cs);
                goto execute;
            }
            SleepConditionVariableCS(&queue->cv, &queue->cs, 100); // Wait for task or timeout
            if (!queue->sched->running) {
                LeaveCriticalSection(&queue->cs);
                return 0;
            }
        }
        
        if (queue->count == 0) {
            LeaveCriticalSection(&queue->cs);
            continue;
        }
        
        task = queue->head;
        if (!task) {
            LeaveCriticalSection(&queue->cs);
            continue;
        }
        
        queue->head = task->next;
        if (queue->head == NULL) {
            queue->tail = NULL;
        }
        InterlockedDecrement((LONG*)&queue->count);
        LeaveCriticalSection(&queue->cs);
        
    execute:
        if (task) {
            InterlockedIncrement((LONG*)&queue->active_count);
            if (task->work_func) {
                DWORD start = GetTickCount();
                task->status = 1;
                task->start_time = start;
                
                task->work_func(task->arg);
                
                task->end_time = GetTickCount();
                task->actual_time = task->end_time - start;
                task->status = 2;
            }
            InterlockedDecrement((LONG*)&queue->active_count);
            InterlockedIncrement((LONG*)&queue->sched->completed_tasks);
        }
    }
    
    return 0;
}

static DWORD WINAPI rebalance_thread(LPVOID param) {
    SCHEDULER* sched = (SCHEDULER*)param;
    const DWORD OVERLOAD_LIMIT = 5; // Simple threshold: >5 tasks in queue
    
    while (sched->running) {
        Sleep(100); // Check every 100ms for faster rebalancing
        
        if (!sched->running) break;
        
        // Update smoothed usage metrics
        for (DWORD i = 0; i < sched->num_cores; i++) {
            double target = (sched->queues[i].active_count > 0 || sched->queues[i].count > 0) ? 100.0 : 0.0;
            sched->queues[i].usage = sched->queues[i].usage * 0.7 + target * 0.3;
        }
        
        DWORD busiest_core = (DWORD)-1;
        DWORD idlest_core = (DWORD)-1;
        DWORD max_load = 0;
        DWORD min_load = (DWORD)-1;
        
        // Phase 1: Identify Imbalance
        for (DWORD i = 0; i < sched->num_cores; i++) {
            // Use lock-free read for heuristic
            DWORD load = sched->queues[i].count;
            
            if (load > max_load) {
                max_load = load;
                busiest_core = i;
            }
            if (load < min_load) {
                min_load = load;
                idlest_core = i;
            }
        }
        
        // Phase 2: Push Migration
        // If the busiest core is overloaded and the idlest is significantly lighter
        if (busiest_core != (DWORD)-1 && idlest_core != (DWORD)-1 && 
            max_load > OVERLOAD_LIMIT && (max_load - min_load) > 2) {
            
            if (TryEnterCriticalSection(&sched->queues[busiest_core].cs)) {
                if (TryEnterCriticalSection(&sched->queues[idlest_core].cs)) {
                    // Migrate one task from busiest to idlest
                    if (sched->queues[busiest_core].count > 0) {
                        TASK* task = sched->queues[busiest_core].head;
                        if (task) {
                            sched->queues[busiest_core].head = task->next;
                            if (sched->queues[busiest_core].head == NULL) {
                                sched->queues[busiest_core].tail = NULL;
                            }
                            InterlockedDecrement((LONG*)&sched->queues[busiest_core].count);
                            
                            // Add to idlest
                            task->next = NULL;
                            if (sched->queues[idlest_core].tail) {
                                sched->queues[idlest_core].tail->next = task;
                                sched->queues[idlest_core].tail = task;
                            } else {
                                sched->queues[idlest_core].head = task;
                                sched->queues[idlest_core].tail = task;
                            }
                            InterlockedIncrement((LONG*)&sched->queues[idlest_core].count);
                            task->core_assigned = idlest_core;
                            WakeConditionVariable(&sched->queues[idlest_core].cv);
                        }
                    }
                    LeaveCriticalSection(&sched->queues[idlest_core].cs);
                }
                LeaveCriticalSection(&sched->queues[busiest_core].cs);
            }
        }
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
        sched->queues[i].usage = 0.0;
        sched->queues[i].sched = sched;
        InitializeCriticalSection(&sched->queues[i].cs);
        InitializeConditionVariable(&sched->queues[i].cv);
        sched->queues[i].thread_pool = CreateThread(NULL, 0, core_worker_thread, 
                                               &sched->queues[i], 0, NULL);
        
        // Kernel Awareness: Pin the thread to its specific logical core
        DWORD_PTR mask = (DWORD_PTR)1 << i;
        SetThreadAffinityMask(sched->queues[i].thread_pool, mask);
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
    
    task->task_id = InterlockedIncrement((LONG*)&g_task_counter);
    
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
        case SCHED_ROUND_ROBIN: {
            static volatile LONG rr_counter = 0;
            core_id = InterlockedIncrement(&rr_counter) % sched->num_cores;
            break;
        }
            
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
    InterlockedIncrement((LONG*)&sched->queues[core_id].count);
    
    WakeConditionVariable(&sched->queues[core_id].cv);
    LeaveCriticalSection(&sched->queues[core_id].cs);
    
    InterlockedIncrement((LONG*)&sched->total_tasks);
    
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
        // Use a lock-free heuristic first
        DWORD load = sched->queues[i].count + sched->queues[i].active_count;
        
        if (load < min_load) {
            min_load = load;
            best_core = i;
        }
    }
    
    return best_core;
}
 
TASK* scheduler_steal_task(SCHEDULER* sched, DWORD thief_core_id) {
    if (!sched || !sched->sys_info) return NULL;
    
    SYSTEM_INFO_EXT* sys = sched->sys_info;
    DWORD my_physical = sys->cores[thief_core_id].physical_core_id;
    DWORD my_group = sys->cores[thief_core_id].processor_group;
    
    DWORD victim_core = (DWORD)-1;
    DWORD max_load = 0;
    
    // Tier 1: Logical Siblings (same physical core)
    for (DWORD i = 0; i < sched->num_cores; i++) {
        if (i == thief_core_id) continue;
        if (sys->cores[i].physical_core_id == my_physical) {
            if (sched->queues[i].count > 0) {
                victim_core = i;
                goto perform_steal;
            }
        }
    }
    
    // Tier 2: Nearby Cores (same processor group)
    for (DWORD i = 0; i < sched->num_cores; i++) {
        if (i == thief_core_id || sys->cores[i].physical_core_id == my_physical) continue;
        if (sys->cores[i].processor_group == my_group) {
            if (sched->queues[i].count > max_load) {
                max_load = sched->queues[i].count;
                victim_core = i;
            }
        }
    }
    
    if (victim_core == (DWORD)-1) {
        // Tier 3: Global Search
        for (DWORD i = 0; i < sched->num_cores; i++) {
            if (i == thief_core_id) continue;
            if (sched->queues[i].count > max_load) {
                max_load = sched->queues[i].count;
                victim_core = i;
            }
        }
    }
    
perform_steal:
    if (victim_core != (DWORD)-1) {
        if (TryEnterCriticalSection(&sched->queues[victim_core].cs)) {
            if (sched->queues[victim_core].count > 0) {
                TASK* task = sched->queues[victim_core].head;
                if (task) {
                    sched->queues[victim_core].head = task->next;
                    if (sched->queues[victim_core].head == NULL) {
                        sched->queues[victim_core].tail = NULL;
                    }
                    InterlockedDecrement((LONG*)&sched->queues[victim_core].count);
                    LeaveCriticalSection(&sched->queues[victim_core].cs);
                    
                    task->core_assigned = thief_core_id;
                    task->next = NULL;
                    
                    return task;
                }
            }
            LeaveCriticalSection(&sched->queues[victim_core].cs);
        }
    }
    
    return NULL;
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
    DWORD iterations = (DWORD)(DWORD_PTR)arg;
    volatile double result = 0;
    
    for (DWORD i = 0; i < iterations; i++) {
        result += (double)i * 0.0001;
    }
    return NULL;
}