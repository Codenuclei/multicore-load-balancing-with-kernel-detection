<div align="center">

```
██╗  ██╗██████╗  ██████╗ ███╗   ██╗ ██████╗ ███████╗
██║ ██╔╝██╔══██╗██╔═══██╗████╗  ██║██╔═══██╗██╔════╝
█████╔╝ ██████╔╝██║   ██║██╔██╗ ██║██║   ██║███████╗
██╔═██╗ ██╔══██╗██║   ██║██║╚██╗██║██║   ██║╚════██║
██║  ██╗██║  ██║╚██████╔╝██║ ╚████║╚██████╔╝███████║
╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝ ╚══════╝
                                          SCHEDULER
```

**Kernel-Aware Multicore Load Balancing Scheduler**

*A Win32-native system-level scheduler that dynamically distributes workloads across CPU cores with real-time hardware telemetry and a browser-based control dashboard.*

---

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue?style=flat-square&logo=windows)
![Language](https://img.shields.io/badge/language-C99-orange?style=flat-square&logo=c)
![Build](https://img.shields.io/badge/build-MinGW%20%7C%20MSVC-green?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-purple?style=flat-square)
![Version](https://img.shields.io/badge/version-1.2.0-yellow?style=flat-square)
![Cores](https://img.shields.io/badge/cores-up%20to%2064-red?style=flat-square)

[**Features**](#-features) · [**Architecture**](#-architecture) · [**Quick Start**](#-quick-start) · [**Dashboard**](#-dashboard) · [**API**](#-api-reference) · [**Contributing**](#-contributing)

</div>

---

## Overview

**KRONOS** is a production-grade, kernel-aware multicore task scheduler built entirely on the Win32 API. It detects your CPU topology, GPU, motherboard, and OS build — then intelligently distributes computational tasks across all logical cores using three switchable load-balancing policies.

The system exposes a real-time browser dashboard over a native HTTP/WebSocket server with no external runtime dependencies. All telemetry is pushed at **20 Hz** via WebSocket frames.

```
┌─────────────────────────────────────────────────────────────┐
│  KRONOS SCHEDULER  ·  Kernel-Aware  ·  Win32 Native  ·  v1.2│
├────────────────┬────────────────────┬───────────────────────┤
│  12 Logical    │  Push Migration    │  Affinity Pinned       │
│  Units Active  │  OVERLOAD ≥ 5 q   │  Per Physical Core     │
├────────────────┴────────────────────┴───────────────────────┤
│  ████████████████████████░░░░░░  UNIT_00  78%               │
│  ████████████░░░░░░░░░░░░░░░░░░  UNIT_01  42%               │
│  ████████████████████████████░░  UNIT_02  94%               │
└─────────────────────────────────────────────────────────────┘
```

---

## ✨ Features

### Scheduling Engine
| Feature | Detail |
|---|---|
| **Work Stealing** | Idle cores pull tasks from overloaded queues — zero-wait under load |
| **Push Migration** | Monitor thread proactively rebalances when queue depth > 5 |
| **Round Robin** | Classic equal-distribution for predictable workloads |
| **Least Loaded** | Routes each task to the core with the shortest queue |
| **Runtime Switching** | Change algorithm live via dashboard or API — no restart required |

### Kernel Awareness
| Feature | Detail |
|---|---|
| **CPUID Detection** | Reads CPU brand string directly via x86 `__cpuid` intrinsic |
| **Topology Enumeration** | Uses `GetLogicalProcessorInformation` for physical/logical core mapping |
| **Affinity Pinning** | Each core thread pinned via `SetThreadAffinityMask` — no OS migration |
| **Cache Detection** | L1/L2/L3 sizes read from `CACHE_DESCRIPTOR` |
| **Win32 Registry** | GPU and motherboard detected via WMI/Registry queries |
| **Windows 11 Detection** | `RtlGetVersion` → build ≥ 22000 identifies Win11 |

### Real-Time Dashboard
- 📡 **WebSocket push** at 20 Hz — no polling, no lag
- 🎯 **Per-core utilization** with smoothed exponential moving average
- 📊 **Efficiency metric** — variance-based load balance score (0–100%)
- 🖥️ **System Manifest** — live CPU, GPU, motherboard, OS, RAM, storage
- ⚡ **One-click injection** — submit 10 / 100 / 1000 tasks from the UI
- 🔄 **Auto-reconnect** — dashboard reconnects if engine restarts

---

## 🏗 Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        main.c                                 │
│   CLI parser → kernel_detect → scheduler_init → gui_start    │
└────────────┬─────────────────┬────────────────┬──────────────┘
             │                 │                │
    ┌────────▼───────┐  ┌──────▼──────┐  ┌─────▼──────────────┐
    │ kernel_detect.c│  │ scheduler.c │  │      gui.c          │
    │                │  │             │  │                     │
    │ CPUID brand    │  │ CORE_QUEUE[]│  │ Winsock2 HTTP       │
    │ LogicalProc    │  │ Worker thds │  │ WebSocket upgrade   │
    │ WMI GPU/MB     │  │ Push migrat │  │ SHA-1 handshake     │
    │ Win11 detect   │  │ Work steal  │  │ 20Hz push loop      │
    └────────────────┘  └──────┬──────┘  └─────────┬──────────┘
                               │                   │
                    ┌──────────▼───────┐  ┌────────▼──────────┐
                    │  SetThreadAffin  │  │   gui_html.c      │
                    │  CriticalSection │  │                   │
                    │  CONDITION_VAR   │  │  Embedded HTML    │
                    └──────────────────┘  │  CSS + JS SPA     │
                                          │  WebSocket client │
                                          └───────────────────┘
```

### Threading Model

```
main thread
│
├─ scheduler monitor thread      (rebalance every 100ms)
│
├─ UNIT_00 worker thread ──┐
├─ UNIT_01 worker thread   ├── SleepConditionVariableCS
├─ UNIT_02 worker thread   │   WakeConditionVariable
│  ...                     │
└─ UNIT_N  worker thread ──┘
│
└─ gui server thread
    └─ per-client handler thread (one per WebSocket connection)
```

---

## 🚀 Quick Start

### Prerequisites
- Windows 10 or 11 (x64)
- [MinGW-w64](https://www.mingw-w64.org/) **or** Visual Studio 2019+
- Any modern browser (Chrome, Edge, Firefox)

### Build

**MinGW (recommended)**
```bash
gcc -O2 -D_CRT_SECURE_NO_WARNINGS -Iinclude -o scheduler.exe \
    main.c src/kernel_detect.c src/scheduler.c src/ui.c src/gui.c \
    -lkernel32 -ladvapi32 -lws2_32 -lcrypt32
```

**Using the batch script**
```cmd
build.bat
```

**Using Make**
```bash
make
```

### Run

```bash
# Launch with browser dashboard (recommended)
scheduler.exe -g

# Console-only benchmark mode
scheduler.exe -b -t 20 -i 1000000

# Help
scheduler.exe --help
```

The dashboard opens automatically at **http://localhost:8080**

---

## 🖥 Dashboard

The browser dashboard connects via WebSocket and displays live telemetry pushed directly from the scheduling engine.

### Layout

```
┌──── Header ─────────────────────────────────────────────────┐
│ KRONOS SCHEDULER  [● Affinity Locked▌]  [UNITS 12] [UP 00:04]│
│                                    [+10] [INJECT 100] [1K]   │
├──── Core Topology ──────────────────────┬── System Manifest ─┤
│                                         │                    │
│  UNIT_00  78%  ████████████░░░░ Q:3 A:2│  Processor  Intel  │
│  UNIT_01  12%  ██░░░░░░░░░░░░░░ Q:0 A:1│  Graphics   AMD RX │
│  UNIT_02  94%  ███████████████░ Q:8 A:1│  Motherboard  ASUS │
│  ...                                    │  OS  Win 11 (26200)│
│                                         │  RAM  15557 MB     │
├──── Performance ────────────────────────┤  Storage  SSD 329GB│
│  Tasks Managed  Completed  TPS  Eff%   ├─── Live Stats ─────┤
│  10,482         10,300     924  97%     │  CORES UPTIME  TPS │
├──── Control Nexus ──────────────────────┤  12    00:04   924 │
│ [INJECT 100▶] [500 TASKS] [BURST 1K]  │                    │
│ [ROUND ROBIN] [LEAST LOADED] [WORK ST] │                    │
│ [HALT ENGINE ⏻]                        │                    │
└─────────────────────────────────────────┴────────────────────┘
```

### Keyboard-free operation
All controls are accessible from the dashboard — no terminal interaction needed once the engine is running.

---

## 📡 API Reference

The engine exposes a lightweight HTTP API alongside the WebSocket endpoint.

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/` | Serves the dashboard SPA |
| `WS` | `/ws` | Real-time telemetry stream (20 Hz JSON frames) |
| `GET` | `/api/info` | Static system hardware info (CPU, GPU, MB, RAM, OS) |
| `GET` | `/api/bench?count=N&iters=M` | Inject N tasks each doing M iterations |
| `GET` | `/api/algo?a=N` | Switch scheduling policy: `0`=RR `1`=LL `2`=WS |
| `GET` | `/api/stop` | Graceful engine shutdown |

### WebSocket Frame Format

```json
{
  "total": 10482,
  "completed": 10300,
  "throughput": 924.3,
  "efficiency": 97.1,
  "cores": [
    { "usage": 78, "queue": 3, "active": 2 },
    { "usage": 12, "queue": 0, "active": 1 }
  ]
}
```

### `/api/info` Response

```json
{
  "cores": 12,
  "cpu": "12th Gen Intel Core i7-12700H",
  "gpu": "AMD Radeon Graphics",
  "mb": "LENOVO LNVNB161216",
  "os": "Windows 11",
  "build": "26200",
  "memory": "15557",
  "storage": "SSD/HDD (C: 329 GB Total)"
}
```

---

## 🧠 Scheduling Algorithms

### Work Stealing (default)
```
Idle core → scan all queues → steal from max-queue core
```
Best for heterogeneous workloads. Achieves highest throughput when task sizes vary.

### Push Migration
```
Monitor (100ms) → find overloaded core (queue > 5) → push to idle core
```
Runs as a background thread. Complements work stealing for bursty injection patterns.

### Least Loaded
```
Submit task → argmin(queue[i]) → enqueue → signal
```
O(N) scan on submit. Optimal for uniform task sizes with low contention.

### Round Robin
```
Submit task → core_id = (counter++) % num_cores → enqueue → signal
```
Zero overhead scheduling. Predictable, fair distribution. Best for benchmarking.

---

## 📁 Project Structure

```
kronos-scheduler/
├── main.c                  # Entry point, CLI, engine orchestration
├── include/
│   ├── scheduler.h         # Core data structures (TASK, CORE_QUEUE, SCHEDULER)
│   ├── kernel_detect.h     # Hardware detection interface
│   ├── gui.h               # GUI server declarations
│   └── ui.h                # Console UI interface
├── src/
│   ├── scheduler.c         # Load balancing engine, worker threads
│   ├── kernel_detect.c     # CPUID, WMI, Win32 hardware enumeration
│   ├── gui.c               # HTTP/WebSocket server, telemetry push
│   ├── gui_html.c          # Embedded dashboard (HTML/CSS/JS string literal)
│   └── ui.c                # Console fallback display
├── build.bat               # MSVC + MinGW + TDM-GCC build script
├── run.bat                 # Quick launch helper
├── Makefile                # GNU Make build
└── SPEC.md                 # Technical specification & CSE316 compliance
```

---

## ⚙️ Configuration

Key constants in `include/scheduler.h`:

```c
#define MAX_CORES        64      // Maximum logical cores supported
#define MAX_TASKS        10000   // Maximum queued tasks per core
#define OVERLOAD_LIMIT   5       // Push migration trigger threshold
#define MONITOR_INTERVAL 100     // Rebalance check interval (ms)
```

---

## 🤝 Contributing

Contributions are welcome. Please follow these steps:

1. Fork the repository
2. Create a feature branch: `git checkout -b feat/your-feature`
3. Commit with [Conventional Commits](https://www.conventionalcommits.org/): `feat:`, `fix:`, `docs:`, `build:`
4. Open a pull request with a clear description

### Development Build (debug symbols)
```bash
gcc -g -O0 -D_CRT_SECURE_NO_WARNINGS -Iinclude -o scheduler_dbg.exe \
    main.c src/kernel_detect.c src/scheduler.c src/ui.c src/gui.c \
    -lkernel32 -ladvapi32 -lws2_32 -lcrypt32
```

---

## 📋 Specification

See [SPEC.md](./SPEC.md) for:
- Full CSE316 compliance matrix
- Performance benchmarks
- Kernel protection strategy
- Hardware detection methodology

---

## 📄 License

MIT License © 2026 — see [LICENSE](./LICENSE) for details.

---

<div align="center">

Built with Win32 API · No external dependencies · Runs on bare metal

**[⬆ Back to top](#)**

</div>