#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <netcli.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: scan <ip|host> [--fast|--full] [timeout_ms]\n");
        printf("       scan <ip|host> <start> [end] [timeout_ms]\n");
        printf("Examples:\n");
        printf("  scan 192.168.76.1\n");
        printf("  scan 192.168.76.1 --fast\n");
        printf("  scan google.com --full 120\n");
        printf("  scan google.com 1 1024 300\n");
        sys_exit(1);
        return 1;
    }

    uint8_t target_ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], target_ip, &from_dns, "scan") != 0) {
        sys_exit(1);
        return 1;
    }

    uint32_t timeout_ms = 350;
    bool range_mode = false;
    uint16_t start_port = 0;
    uint16_t end_port = 0;
    uint32_t total_ports = 0;
    const char* profile_name = "default";

    bool preset_fast = false;
    bool preset_full = false;
    int argi = 2;

    if (argc > argi && argv[argi][0] == '-' && argv[argi][1] == '-') {
        if (strcmp(argv[argi], "--help") == 0) {
            printf("Usage: scan <ip|host> [--fast|--full] [timeout_ms]\n");
            printf("       scan <ip|host> <start> [end] [timeout_ms]\n");
            sys_exit(0);
            return 0;
        } else if (strcmp(argv[argi], "--fast") == 0) {
            preset_fast = true;
            profile_name = "fast";
            timeout_ms = 120;
            argi++;
        } else if (strcmp(argv[argi], "--full") == 0) {
            preset_full = true;
            profile_name = "full";
            range_mode = true;
            start_port = 1;
            end_port = 1024;
            total_ports = 1024u;
            timeout_ms = 120;
            argi++;
        } else {
            printf("scan: unknown option '%s'\n", argv[argi]);
            sys_exit(1);
            return 1;
        }
    }

    if (preset_fast || preset_full) {
        if (argc > argi + 1) {
            printf("scan: too many arguments for preset mode\n");
            sys_exit(1);
            return 1;
        }
        if (argc == argi + 1) {
            long t = atoi(argv[argi]);
            if (t < 50 || t > 5000) {
                printf("scan: timeout_ms must be 50..5000\n");
                sys_exit(1);
                return 1;
            }
            timeout_ms = (uint32_t)t;
        }
    } else {
        if (argc >= 3) {
            long s = atoi(argv[2]);
            if (s <= 0 || s > 65535) {
                printf("scan: start port must be 1..65535\n");
                sys_exit(1);
                return 1;
            }
            long e = s;
            if (argc >= 4) {
                e = atoi(argv[3]);
                if (e <= 0 || e > 65535) {
                    printf("scan: end port must be 1..65535\n");
                    sys_exit(1);
                    return 1;
                }
            }
            if (s > e) {
                printf("scan: start must be <= end\n");
                sys_exit(1);
                return 1;
            }
            if ((e - s + 1) > 2048) {
                printf("scan: range too large (max 2048 ports)\n");
                sys_exit(1);
                return 1;
            }
            start_port = (uint16_t)s;
            end_port = (uint16_t)e;
            range_mode = true;
            total_ports = (uint32_t)(end_port - start_port + 1u);
        }

        if ((range_mode && argc >= 5) || (!range_mode && argc >= 3)) {
            int timeout_arg_index = range_mode ? 4 : 2;
            long t = atoi(argv[timeout_arg_index]);
            if (t < 50 || t > 5000) {
                printf("scan: timeout_ms must be 50..5000\n");
                sys_exit(1);
                return 1;
            }
            timeout_ms = (uint32_t)t;
        }
    }

    if (range_mode && total_ports == 0) {
        total_ports = (uint32_t)(end_port - start_port + 1u);
    }

    printf("SCAN ");
    if (from_dns) {
        printf("%s (", argv[1]);
        netcli_print_ip4(target_ip);
        printf(")");
    } else {
        netcli_print_ip4(target_ip);
    }
    if (range_mode) {
        printf(" ports %u-%u", (unsigned)start_port, (unsigned)end_port);
    } else {
        printf(" common ports");
    }
    printf(", timeout=%u ms", (unsigned)timeout_ms);
    if (preset_fast || preset_full) {
        printf(", profile=%s", profile_name);
    }
    printf("\n");

    if (range_mode) {
        uint32_t worst_case_s = (uint32_t)(((uint64_t)total_ports * (uint64_t)timeout_ms + 999u) / 1000u);
        printf("Scan estimate: up to ~%u s in worst case\n", (unsigned)worst_case_s);
    }

    int open_count = 0;
    int closed_count = 0;
    int timeout_count = 0;
    int scanned = 0;

    static const uint16_t common_ports[] = {
        21, 22, 23, 25, 53, 80, 110, 143, 443, 465, 587, 993, 995, 3306, 3389, 5432, 6379, 8080, 8443
    };

    if (!range_mode) {
        for (size_t i = 0; i < sizeof(common_ports) / sizeof(common_ports[0]); i++) {
            uint16_t port = common_ports[i];
            bool ok = false;
            bool open = false;
            uint32_t rtt_ms = 0;
            if (netcli_run_tcp_probe_once(target_ip, port, timeout_ms, &ok, &open, &rtt_ms) < 0) {
                printf("scan: syscall error\n");
                sys_exit(1);
                return 1;
            }
            scanned++;
            if (!ok) {
                timeout_count++;
                continue;
            }
            if (open) {
                open_count++;
                const char* svc = netcli_tcp_service_name(port);
                if (svc) {
                    printf("  %u/tcp open  (%s)  %u ms\n", (unsigned)port, svc, (unsigned)rtt_ms);
                } else {
                    printf("  %u/tcp open  %u ms\n", (unsigned)port, (unsigned)rtt_ms);
                }
            } else {
                closed_count++;
            }
        }
    } else {
        bool show_closed = (end_port - start_port) <= 64;
        uint32_t progress_step = (total_ports >= 512u) ? 64u : 32u;
        for (uint32_t p = start_port; p <= end_port; p++) {
            uint16_t port = (uint16_t)p;
            bool ok = false;
            bool open = false;
            uint32_t rtt_ms = 0;
            if (netcli_run_tcp_probe_once(target_ip, port, timeout_ms, &ok, &open, &rtt_ms) < 0) {
                printf("scan: syscall error\n");
                sys_exit(1);
                return 1;
            }
            scanned++;

            if (!ok) {
                timeout_count++;
                if (show_closed) {
                    printf("  %u/tcp timeout\n", (unsigned)port);
                }
            } else if (open) {
                open_count++;
                const char* svc = netcli_tcp_service_name(port);
                if (svc) {
                    printf("  %u/tcp open  (%s)  %u ms\n", (unsigned)port, svc, (unsigned)rtt_ms);
                } else {
                    printf("  %u/tcp open  %u ms\n", (unsigned)port, (unsigned)rtt_ms);
                }
            } else {
                closed_count++;
                if (show_closed) {
                    printf("  %u/tcp closed  %u ms\n", (unsigned)port, (unsigned)rtt_ms);
                }
            }

            if (!show_closed && (scanned % progress_step == 0 || scanned == (int)total_ports)) {
                printf("[scan] progress: %d/%u\n", scanned, (unsigned)total_ports);
            }
        }
    }

    printf("Scan summary: scanned=%d open=%d closed=%d timeout=%d\n",
           scanned, open_count, closed_count, timeout_count);

    sys_exit(0);
    return 0;
}
