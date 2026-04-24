#define _CRT_SECURE_NO_WARNINGS
#include "kernel_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double g_last_idle[64] = {0};
static double g_last_total[64] = {0};
static BOOL g_initialized = FALSE;

static ULONGLONG FileTimeToULONGLong(FILETIME* ft) {
    return (((ULONGLONG)ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
}

void kernel_enum_processors(LOGICAL_PROCESSOR_RELATIONSHIP relation_type, 
                          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* buffer, 
                          DWORD* ReturnLength) {
    DWORD length = 0;
    DWORD error = ERROR_INSUFFICIENT_BUFFER;
    
    GetLogicalProcessorInformationEx(relation_type, NULL, &length);
    
    if (length == 0) return;
    
    *buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)malloc(length);
    if (*buffer) {
        if (!GetLogicalProcessorInformationEx(relation_type, *buffer, ReturnLength)) {
            free(*buffer);
            *buffer = NULL;
        }
    }
}

static void detect_cache_info(CORE_INFO* core, PCACHE_DESCRIPTOR Cache) {
    switch (Cache->LineSize) {
        case 32:
            strcpy(core->cache_level, "L1");
            break;
        case 64:
            strcpy(core->cache_level, "L2");
            break;
        case 128:
            strcpy(core->cache_level, "L3");
            break;
        default:
            strcpy(core->cache_level, "Unknown");
    }
    core->cache_size = Cache->Size;
}

static void detect_processor_info(SYSTEM_INFO_EXT* sys_info) {
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer = NULL;
    DWORD length = 0;
    DWORD i;
    
    kernel_enum_processors(RelationAll, &buffer, &length);
    
    if (!buffer) return;
    
    DWORD num_cores = 0;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX ptr = buffer;
    DWORD offset = 0;
    
    while (offset < length) {
        switch (ptr->Relationship) {
            case RelationProcessorCore:
                num_cores++;
                break;
            case RelationCache:
                break;
        }
        
        if (ptr->Size == 0) break;
        offset += ptr->Size;
        ptr = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((PBYTE)ptr + ptr->Size);
    }
    
    sys_info->num_cores = num_cores;
    sys_info->num_logical_cores = num_cores;
    sys_info->num_physical_cores = num_cores;
    
    if (buffer) free(buffer);
}

static void detect_cpu_brand(char* brand, size_t size) {
    HKEY hKey;
    DWORD type = REG_SZ;
    DWORD cbData = (DWORD)size;
    
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                    "HARDWARE\\DESCRIPTION\\CPU\\0", 
                    0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "ProcessorNameString", NULL, &type, 
                        (LPBYTE)brand, &cbData);
        RegCloseKey(hKey);
    }
    
    if (strlen(brand) == 0) {
        strcpy(brand, "Unknown CPU");
    }
}

SYSTEM_INFO_EXT* kernel_init(void) {
    SYSTEM_INFO_EXT* sys_info = (SYSTEM_INFO_EXT*)calloc(1, sizeof(SYSTEM_INFO_EXT));
    if (!sys_info) return NULL;
    
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    
    sys_info->num_cores = si.dwNumberOfProcessors;
    sys_info->num_logical_cores = si.dwNumberOfProcessors;
    sys_info->num_physical_cores = si.dwNumberOfProcessors / 2;
    if (sys_info->num_physical_cores == 0) sys_info->num_physical_cores = 1;
    
    MEMORYSTATUSEX mem_stat;
    mem_stat.dwLength = sizeof(mem_stat);
    if (GlobalMemoryStatusEx(&mem_stat)) {
        sys_info->total_memory = mem_stat.ullTotalPhys;
    }
    
    detect_cpu_brand(sys_info->cpu_brand, sizeof(sys_info->cpu_brand));
    
    sys_info->cores = (CORE_INFO*)calloc(sys_info->num_cores, sizeof(CORE_INFO));
    if (sys_info->cores) {
        for (DWORD i = 0; i < sys_info->num_cores; i++) {
            sys_info->cores[i].core_id = i;
            sys_info->cores[i].processor_group = 0;
            sys_info->cores[i].mask = (DWORD64)(1ULL << i);
            sys_info->cores[i].cpu_usage = 0.0;
        }
    }
    
    detect_processor_info(sys_info);
    
    g_initialized = TRUE;
    
    return sys_info;
}

void kernel_free(SYSTEM_INFO_EXT* sys_info) {
    if (sys_info) {
        if (sys_info->cores) {
            free(sys_info->cores);
        }
        free(sys_info);
    }
    g_initialized = FALSE;
}

DWORD kernel_get_core_count(SYSTEM_INFO_EXT* sys_info) {
    if (!sys_info) return 0;
    return sys_info->num_cores;
}

void kernel_update_cpu_usage(SYSTEM_INFO_EXT* sys_info) {
    if (!sys_info || !sys_info->cores) return;
    
    FILETIME idle_time, kernel_time, user_time;
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        return;
    }
    
    static FILETIME last_idle, last_kernel, last_user;
    static BOOL first_call = TRUE;
    
    if (first_call) {
        last_idle = idle_time;
        last_kernel = kernel_time;
        last_user = user_time;
        first_call = FALSE;
        return;
    }
    
    ULONGLONG idle = FileTimeToULONGLong(&idle_time) - FileTimeToULONGLong(&last_idle);
    ULONGLONG kernel = FileTimeToULONGLong(&kernel_time) - FileTimeToULONGLong(&last_kernel);
    ULONGLONG user = FileTimeToULONGLong(&user_time) - FileTimeToULONGLong(&last_user);
    
    ULONGLONG total = kernel + user;
    
    last_idle = idle_time;
    last_kernel = kernel_time;
    last_user = user_time;
    
    if (total > 0) {
        double usage = 100.0 * (double)(total - idle) / (double)total;
        if (usage < 0) usage = 0;
        if (usage > 100) usage = 100;
        
        for (DWORD i = 0; i < sys_info->num_cores; i++) {
            sys_info->cores[i].cpu_usage = usage;
        }
    }
}

BOOL kernel_set_affinity(DWORD core_id, HANDLE thread) {
    if (!thread) thread = GetCurrentThread();
    
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (core_id >= si.dwNumberOfProcessors) {
        return FALSE;
    }
    
    DWORD64 mask = (DWORD64)(1ULL << core_id);
    return SetThreadAffinityMask(thread, mask) != 0;
}

DWORD64 kernel_get_affinity_mask(DWORD core_id, DWORD num_cores) {
    if (core_id >= num_cores) return 0;
    return (DWORD64)(1ULL << core_id);
}