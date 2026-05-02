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
    
    kernel_enum_processors(RelationProcessorCore, &buffer, &length);
    if (!buffer) return;
    
    DWORD logical_id = 0;
    DWORD physical_id = 0;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX ptr = buffer;
    DWORD offset = 0;
    
    while (offset < length) {
        if (ptr->Relationship == RelationProcessorCore) {
            for (WORD i = 0; i < ptr->Processor.GroupCount; i++) {
                KAFFINITY mask = ptr->Processor.GroupMask[i].Mask;
                for (int b = 0; b < sizeof(KAFFINITY) * 8; b++) {
                    if ((mask >> b) & 1) {
                        if (logical_id < sys_info->num_cores) {
                            sys_info->cores[logical_id].physical_core_id = physical_id;
                            sys_info->cores[logical_id].processor_group = ptr->Processor.GroupMask[i].Group;
                            logical_id++;
                        }
                    }
                }
            }
            physical_id++;
        }
        offset += ptr->Size;
        ptr = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)((PBYTE)ptr + ptr->Size);
    }
    
    sys_info->num_logical_cores = logical_id;
    sys_info->num_physical_cores = physical_id;
    
    if (buffer) free(buffer);
}

#include <intrin.h>

static void detect_cpu_brand(char* brand, size_t size) {
    int cpu_info[4];
    char brand_string[49] = {0};
    
    // Check if extended CPUID functions are supported
    __cpuid(cpu_info, 0x80000000);
    unsigned int nExIds = cpu_info[0];
    
    if (nExIds >= 0x80000004) {
        for (int i = 0; i < 3; ++i) {
            __cpuid(cpu_info, 0x80000002 + i);
            memcpy(brand_string + (i * 16), cpu_info, 16);
        }
        
        // Trim leading spaces
        char* start = brand_string;
        while (*start == ' ') start++;
        strncpy(brand, start, size - 1);
        brand[size - 1] = '\0';
    } else {
        // Fallback to Registry if CPUID brand string is not supported
        HKEY hKey;
        DWORD type = REG_SZ;
        DWORD cbData = (DWORD)size;
        
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 
                        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "ProcessorNameString", NULL, &type, 
                            (LPBYTE)brand, &cbData);
            RegCloseKey(hKey);
        }
    }
    
    if (strlen(brand) == 0) {
        strcpy(brand, "Generic x86-64 Processor");
    }
}

static void detect_os_info(SYSTEM_INFO_EXT* sys) {
    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleA("ntdll.dll");
    if (hMod) {
        RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW osvi = {0};
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (pRtlGetVersion(&osvi) == 0) {
                sprintf(sys->os_build, "%lu", osvi.dwBuildNumber);
                sys->is_win11 = (osvi.dwBuildNumber >= 22000);
                if (sys->is_win11) strcpy(sys->os_version, "Windows 11");
                else if (osvi.dwMajorVersion == 10) strcpy(sys->os_version, "Windows 10");
                else sprintf(sys->os_version, "Windows %lu.%lu", osvi.dwMajorVersion, osvi.dwMinorVersion);
            }
        }
    }
}

static void detect_gpu_brand(char* brand, size_t size) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\WinSAT", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type, cbData = (DWORD)size;
        RegQueryValueExA(hKey, "PrimaryAdapterString", NULL, &type, (LPBYTE)brand, &cbData);
        RegCloseKey(hKey);
    }
    
    if (strlen(brand) == 0) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD type, cbData = (DWORD)size;
            RegQueryValueExA(hKey, "DriverDesc", NULL, &type, (LPBYTE)brand, &cbData);
            RegCloseKey(hKey);
        }
    }
    
    if (strlen(brand) == 0) strcpy(brand, "Generic VGA / Integrated");
}

static void detect_motherboard(char* mb, size_t size) {
    HKEY hKey;
    char man[64] = {0}, prod[64] = {0};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type, cbData = 64;
        RegQueryValueExA(hKey, "BaseBoardManufacturer", NULL, &type, (LPBYTE)man, &cbData);
        cbData = 64;
        RegQueryValueExA(hKey, "BaseBoardProduct", NULL, &type, (LPBYTE)prod, &cbData);
        RegCloseKey(hKey);
    }
    if (strlen(man) > 0) sprintf(mb, "%.30s %.30s", man, prod);
    else strcpy(mb, "Unknown System Board");
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
    detect_os_info(sys_info);
    detect_gpu_brand(sys_info->gpu_brand, sizeof(sys_info->gpu_brand));
    detect_motherboard(sys_info->motherboard, sizeof(sys_info->motherboard));
    
    // Simple storage detection
    ULARGE_INTEGER free, total, totalFree;
    if (GetDiskFreeSpaceExA("C:\\", &free, &total, &totalFree)) {
        sprintf(sys_info->storage_info, "SSD/HDD (C: %llu GB Total)", total.QuadPart / (1024*1024*1024));
    }
    
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