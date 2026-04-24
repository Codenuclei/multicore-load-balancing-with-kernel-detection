# Multicore Load Balancing Scheduler with Kernel Detection v1.0

## Version History
- v1.0: Initial release with basic load balancing

A Windows console application that dynamically distributes computational tasks across multiple CPU cores using intelligent load balancing algorithms.

## Features

- **Kernel Detection**: Enumerates CPU cores, detects cache topology, monitors CPU usage
- **Load Balancing**: Supports Round Robin, Least Loaded, and Work Stealing algorithms
- **Real-time Visualization**: Console-based dashboard showing core utilization
- **Task Scheduling**: Submit computational tasks to optimal cores automatically

## Building

### Using MinGW (Recommended)
```bash
gcc -O2 -Wall -o scheduler.exe main.c src/kernel_detect.c src/scheduler.c src/ui.c -lkernel32 -ladvapi32
```

### Using Visual Studio
```cmd
cl /O2 /W3 /Fe:scheduler.exe main.c src\kernel_detect.c src\scheduler.c src\ui.c /link kernel32.lib advapi32.lib
```

### Using build.bat
```cmd
build.bat
```

## Running

### Interactive Mode (Default)
```cmd
scheduler.exe
```

### Batch/Benchmark Mode
```cmd
scheduler.exe -b -t 200 -i 5000000
```

Options:
- `-t N`   Number of tasks (default: 100)
- `-i N`   Iterations per task (default: 1000000)
- `-a N`   Algorithm (0=RR, 1=LeastLoaded, 2=WorkStealing)
- `-b`     Batch mode (run benchmark and exit)
- `-h`     Show help

## Menu Options

1. Submit Tasks - Submit computational tasks to scheduler
2. Change Algorithm - Switch between scheduling algorithms
3. Show Core Details - Display detailed core information
4. View Task Queue - Show queue status per core
5. Start Scheduler - Start the scheduler
6. Stop Scheduler - Stop the scheduler
7. Run Benchmark - Run performance benchmark
8. Settings - Configure settings
0. Exit - Exit the application

## Output Example

```
================================================================================
|           MULTICORE LOAD BALANCING SCHEDULER WITH KERNEL DETECTION           |
================================================================================
| CPU: Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz                        |
| Logical Cores: 8  | Physical Cores: 4  | Total Memory: 16384 MB   |
================================================================================

+========================= CORE UTILIZATION ==========================+
| Core 0: ######################################## 100.0% | Q: 5  | A: 1 |
| Core 1: #######################------------------- 45.0%  | Q: 2  | A: 0 |
...
```

## Project Structure

```
os/
├── include/
│   ├── kernel_detect.h
│   ├── scheduler.h
│   └── ui.h
├── src/
│   ├── kernel_detect.c
│   ├── scheduler.c
│   └── ui.c
├── main.c
├── Makefile
├── build.bat
└── README.md
```

## Requirements

- Windows 10/11
- MinGW-w64 or Visual Studio 2022
- 64-bit system (for best performance)