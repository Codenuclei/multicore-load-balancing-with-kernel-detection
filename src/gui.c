#include <winsock2.h>
#include <windows.h>
#include "kernel_detect.h"
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wincrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

#define HTTP_PORT 8080
#define MAX_CORES 64

static SCHEDULER*      g_scheduler  = NULL;
static SYSTEM_INFO_EXT* g_sysinfo   = NULL;
static WSADATA          g_wsa;
static int              g_active_port = 8080;

/* ── HTTP header templates ─────────────────────────────────────────────── */
static const char HTML_HDR[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: %lu\r\n"
    "Connection: close\r\n\r\n";

static const char JSON_HDR[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Content-Length: %lu\r\n"
    "Connection: close\r\n\r\n";

/* ── WebSocket helpers ─────────────────────────────────────────────────── */
static void ws_accept_key(const char* key, char* out) {
    char combined[300];
    sprintf(combined, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    HCRYPTPROV hProv; HCRYPTHASH hHash;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return;
    if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
        CryptHashData(hHash, (BYTE*)combined, (DWORD)strlen(combined), 0);
        BYTE hash[20]; DWORD hlen = 20;
        CryptGetHashParam(hHash, HP_HASHVAL, hash, &hlen, 0);
        DWORD olen = 128;
        CryptBinaryToStringA(hash, 20, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &olen);
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
}

static void ws_send(SOCKET s, const char* msg) {
    size_t len = strlen(msg);
    BYTE frame[10]; int flen = 0;
    frame[0] = 0x81;
    if (len <= 125) { frame[1] = (BYTE)len; flen = 2; }
    else            { frame[1] = 126; frame[2] = (BYTE)(len>>8); frame[3] = (BYTE)(len&0xFF); flen = 4; }
    send(s, (char*)frame, flen, 0);
    send(s, msg, (int)len, 0);
}

/* ── Per-connection handler (runs in its own thread) ───────────────────── */
static DWORD WINAPI handle_client(LPVOID p) {
    SOCKET cli = (SOCKET)p;
    char buf[4096];

    /* The HTML body is the included string literal from gui_html.c */
    static const char html_body[] =
#include "gui_html.c"
    ;

    int n = recv(cli, buf, sizeof(buf)-1, 0);
    if (n <= 0) { closesocket(cli); return 0; }
    buf[n] = 0;

    /* ── Serve HTML page ───────────────────────────────────────────────── */
    if (strstr(buf,"GET / ") || strstr(buf,"GET / HTTP")) {
        char hdr[512];
        sprintf(hdr, HTML_HDR, (unsigned long)strlen(html_body));
        send(cli, hdr, (int)strlen(hdr), 0);
        send(cli, html_body, (int)strlen(html_body), 0);

    /* ── WebSocket upgrade → realtime push loop ────────────────────────── */
    } else if (strstr(buf,"Upgrade: websocket")) {
        char* ks = strstr(buf,"Sec-WebSocket-Key: ");
        if (ks) {
            ks += 19;
            char* ke = strstr(ks,"\r\n");
            if (ke) {
                char key[128]={0}, accept[128]={0};
                memcpy(key, ks, ke-ks);
                ws_accept_key(key, accept);

                char shake[512];
                sprintf(shake,
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
                send(cli, shake, (int)strlen(shake), 0);

                unsigned long nb = 1;
                ioctlsocket(cli, FIONBIO, &nb);

                DWORD last_done = 0, last_tick = GetTickCount();
                while (g_scheduler && g_scheduler->running) {
                    Sleep(50); /* 20 Hz push */

                    DWORD now = GetTickCount();
                    DWORD done = g_scheduler->completed_tasks;
                    double dt = (double)(now - last_tick);
                    double tps = (dt > 0) ? (done - last_done) * 1000.0 / dt : 0.0;
                    last_done = done; last_tick = now;

                    /* Build per-core stats + efficiency */
                    DWORD num = g_sysinfo ? g_sysinfo->num_cores : 1;
                    double loads[MAX_CORES], avg = 0, var = 0;
                    char st[8192];
                    int pos = sprintf(st,
                        "{\"total\":%lu,\"completed\":%lu,\"throughput\":%.1f,\"cores\":[",
                        g_scheduler->total_tasks, done, tps);

                    for (DWORD i = 0; i < num; i++) {
                        EnterCriticalSection(&g_scheduler->queues[i].cs);
                        DWORD q = g_scheduler->queues[i].count;
                        DWORD a = g_scheduler->queues[i].active_count;
                        int   u = (int)g_scheduler->queues[i].usage;
                        LeaveCriticalSection(&g_scheduler->queues[i].cs);

                        loads[i] = (double)q;
                        avg += loads[i];
                        pos += sprintf(st+pos,
                            "{\"usage\":%d,\"queue\":%lu,\"active\":%lu}%c",
                            u, q, a, i < num-1 ? ',' : ']');
                    }

                    avg /= num;
                    for (DWORD i = 0; i < num; i++)
                        var += (loads[i]-avg)*(loads[i]-avg);
                    var /= num;
                    double eff = (var < 0.01) ? 100.0 : 100.0/(1.0+var*0.05);
                    if (eff > 100) eff = 100;

                    sprintf(st+pos, ",\"efficiency\":%.1f}", eff);
                    ws_send(cli, st);

                    /* Check client disconnect */
                    char probe; int r = recv(cli, &probe, 1, MSG_PEEK);
                    if (r == 0) break;
                    if (r == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) break;
                }
            }
        }

    /* ── REST: GET /api/info ───────────────────────────────────────────── */
    } else if (strstr(buf,"GET /api/info")) {
        char info[2048], hdr[256];
        int len = sprintf(info,
            "{\"cpu\":\"%s\",\"gpu\":\"%s\",\"os\":\"%s\","
            "\"build\":\"%s\",\"mb\":\"%s\",\"storage\":\"%s\","
            "\"cores\":%lu,\"memory\":%llu,\"win11\":%d}",
            g_sysinfo->cpu_brand, g_sysinfo->gpu_brand,
            g_sysinfo->os_version, g_sysinfo->os_build,
            g_sysinfo->motherboard, g_sysinfo->storage_info,
            g_sysinfo->num_cores,
            g_sysinfo->total_memory / (1024*1024),
            g_sysinfo->is_win11);
        sprintf(hdr, JSON_HDR, (unsigned long)len);
        send(cli, hdr, (int)strlen(hdr), 0);
        send(cli, info, len, 0);

    /* ── REST: GET /api/bench?count=N&iters=M ─────────────────────────── */
    } else if (strstr(buf,"GET /api/bench?")) {
        char* cp = strstr(buf,"count="), *ip = strstr(buf,"iters=");
        int cnt = 10, iters = 1000000;
        if (cp) sscanf(cp+6, "%d", &cnt);
        if (ip) sscanf(ip+6, "%d", &iters);
        for (int i = 0; i < cnt && i < 1000; i++) {
            TASK* t = scheduler_create_task(default_work_func,
                                            (void*)(DWORD_PTR)iters, 1);
            if (t) scheduler_submit_task(g_scheduler, t);
        }
        const char* rsp = "{\"ok\":1}";
        char hdr[256]; sprintf(hdr, JSON_HDR, (unsigned long)strlen(rsp));
        send(cli, hdr, (int)strlen(hdr), 0);
        send(cli, rsp, (int)strlen(rsp), 0);

    /* ── REST: GET /api/algo?a=N ──────────────────────────────────────── */
    } else if (strstr(buf,"GET /api/algo?")) {
        char* ap = strstr(buf,"a=");
        if (ap) {
            int a = atoi(ap+2);
            if (a >= 0 && a <= 2)
                scheduler_set_algorithm(g_scheduler, (SCHED_ALGORITHM)a);
        }
        const char* rsp = "{\"ok\":1}";
        char hdr[256]; sprintf(hdr, JSON_HDR, (unsigned long)strlen(rsp));
        send(cli, hdr, (int)strlen(hdr), 0);
        send(cli, rsp, (int)strlen(rsp), 0);

    /* ── REST: GET /api/stop ──────────────────────────────────────────── */
    } else if (strstr(buf,"GET /api/stop")) {
        scheduler_stop(g_scheduler);
        const char* rsp = "{\"ok\":1}";
        char hdr[256]; sprintf(hdr, JSON_HDR, (unsigned long)strlen(rsp));
        send(cli, hdr, (int)strlen(hdr), 0);
        send(cli, rsp, (int)strlen(rsp), 0);

    } else {
        const char* nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send(cli, nf, (int)strlen(nf), 0);
    }

    closesocket(cli);
    return 0;
}

/* ── Listener thread ───────────────────────────────────────────────────── */
static DWORD WINAPI http_server(LPVOID p) {
    (void)p;
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(HTTP_PORT);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        addr.sin_port = htons(8081);
        if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(srv); return 1;
        }
        g_active_port = 8081;
    } else {
        g_active_port = 8080;
    }

    printf("\n[GUI] Listening on http://localhost:%d\n", g_active_port);
    fflush(stdout);
    listen(srv, SOMAXCONN);

    while (1) {
        SOCKET cli = accept(srv, NULL, NULL);
        if (cli != INVALID_SOCKET)
            CreateThread(NULL, 0, handle_client, (LPVOID)cli, 0, NULL);
    }
    return 0;
}

/* ── Public entry point ────────────────────────────────────────────────── */
void start_gui_server(SCHEDULER* sched, SYSTEM_INFO_EXT* sys) {
    g_scheduler = sched;
    g_sysinfo   = sys;
    WSAStartup(MAKEWORD(2,2), &g_wsa);
    CreateThread(NULL, 0, http_server, NULL, 0, NULL);
    Sleep(400); /* let listener bind */
    char url[128];
    sprintf(url, "http://localhost:%d", g_active_port);
    printf("[GUI] Opening %s\n", url); fflush(stdout);
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOW);
}