#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "kernel_detect.h"
#include <windows.h>

typedef enum {
    SCHED_ROUND_ROBIN,
    SCHED_LEAST_LOADED,
    SCHED_WORK_STEALING
} SCHED_ALGORITHM;

typedef struct TASK {
    DWORD task_id;
    DWORD priority;
    DWORD estimated_time;
    ULONGLONG actual_time;
    void* (*work_func)(void*);
    void* arg;
    DWORD status;
    DWORD core_assigned;
    ULONGLONG start_time;
    ULONGLONG end_time;
    struct TASK* next;
} TASK;

typedef struct SCHEDULER SCHEDULER;

typedef struct CORE_QUEUE {
    DWORD core_id;
    TASK* head;
    TASK* tail;
    volatile LONG count;           // use interlocked APIs
    volatile LONG active_count;    // use interlocked APIs
    double usage;
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
    HANDLE thread_pool;            // worker thread handle
    SCHEDULER* sched;
} CORE_QUEUE;

typedef struct SCHEDULER {
    CORE_QUEUE* queues;
    DWORD num_cores;
    SCHED_ALGORITHM algorithm;
    volatile LONG total_tasks;
    volatile LONG completed_tasks;
    volatile LONG failed_tasks;
    double avg_wait_time;
    double avg_turnaround;
    BOOL running;
    HANDLE monitor_thread;
    CRITICAL_SECTION global_cs;
    SYSTEM_INFO_EXT* sys_info;
    HANDLE start_event;            // event to coordinate worker thread start
} SCHEDULER;

SCHEDULER* scheduler_init(DWORD num_cores);
void scheduler_free(SCHEDULER* sched);
BOOL scheduler_submit_task(SCHEDULER* sched, TASK* task);
BOOL scheduler_cancel_task(SCHEDULER* sched, DWORD task_id);
TASK* scheduler_create_task(void* (*work_func)(void*), void* arg, DWORD priority);
void scheduler_free_task(TASK* task);
void scheduler_set_algorithm(SCHEDULER* sched, SCHED_ALGORITHM algo);
SCHED_ALGORITHM scheduler_get_algorithm(SCHEDULER* sched);
DWORD scheduler_get_best_core(SCHEDULER* sched);
void scheduler_run_task(SCHEDULER* sched, TASK* task);
TASK* scheduler_steal_task(SCHEDULER* sched, DWORD thief_core_id);
void scheduler_start(SCHEDULER* sched);
void scheduler_stop(SCHEDULER* sched);
void scheduler_get_stats(SCHEDULER* sched, DWORD* total, DWORD* completed, DWORD* failed);
CORE_QUEUE* scheduler_get_queue(SCHEDULER* sched, DWORD core_id);
void* default_work_func(void* arg);

#endif
