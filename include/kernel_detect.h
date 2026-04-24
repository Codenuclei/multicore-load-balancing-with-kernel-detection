#ifndef KERNEL_DETECT_H
#define KERNEL_DETECT_H

#include <windows.h>

typedef struct {
    DWORD core_id;
    DWORD processor_group;
    DWORD64 mask;
    double cpu_usage;
    DWORD cache_size;
    char cache_level[16];
} CORE_INFO;

typedef struct {
    CORE_INFO* cores;
    DWORD num_cores;
    DWORD num_physical_cores;
    DWORD num_logical_cores;
    DWORDLONG total_memory;
    char cpu_brand[128];
} SYSTEM_INFO_EXT;

SYSTEM_INFO_EXT* kernel_init(void);
void kernel_free(SYSTEM_INFO_EXT* sys_info);
DWORD kernel_get_core_count(SYSTEM_INFO_EXT* sys_info);
void kernel_update_cpu_usage(SYSTEM_INFO_EXT* sys_info);
BOOL kernel_set_affinity(DWORD core_id, HANDLE thread);
DWORD64 kernel_get_affinity_mask(DWORD core_id, DWORD num_cores);
void kernel_enum_processors(LOGICAL_PROCESSOR_RELATIONSHIP relation_type, 
                          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* buffer, 
                          DWORD* ReturnLength);

#endif