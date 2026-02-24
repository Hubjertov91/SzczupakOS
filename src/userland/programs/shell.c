#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <netcli.h>

#define MAX_CMD_LEN 512
#define MAX_ARGS 32
#define MAX_PATH 256
#define MAX_HISTORY 16

static char cwd[MAX_PATH] = "/";
static char line_buf[MAX_CMD_LEN];
static char* argv[MAX_ARGS];
static int argc;
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_next = 0;

static long try_exec_command(const char* cmd, int arg_count, char** args);
static int cmd_net(int argc, char** argv);
static int cmd_netstat(int argc, char** argv);
static int cmd_dns(int argc, char** argv);
static int cmd_nslookup(int argc, char** argv);
static int cmd_ping(int argc, char** argv);
static int cmd_traceroute(int argc, char** argv);
static int cmd_tcping(int argc, char** argv);
static int cmd_scan(int argc, char** argv);
static int cmd_netcheck(int argc, char** argv);

int cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Built-in commands:\n");
    printf("  help        - Show this help\n");
    printf("  echo        - Print arguments\n");
    printf("  history     - Show command history\n");
    printf("  dir         - List current directory\n");
    printf("  ls          - Alias for dir\n");
    printf("  pwd         - Print working directory\n");
    printf("  cd          - Change directory\n");
    printf("  run         - Run external program\n");
    printf("  clear       - Clear screen\n");
    printf("  sysinfo     - Show system info\n");
    printf("  net         - Show network config\n");
    printf("  netstat     - Show network statistics\n");
    printf("  dns         - Resolve host\n");
    printf("  nslookup    - Resolve host (alias)\n");
    printf("  ping        - ICMP echo\n");
    printf("  traceroute  - Route trace\n");
    printf("  tcping      - TCP probe\n");
    printf("  scan        - TCP port scan\n");
    printf("  netcheck    - Network diagnostics\n");
    printf("  exit        - Exit shell\n");
    printf("  quit        - Exit shell (alias)\n");
    printf("\n");
    printf("Aliases: ifconfig, trace, portscan, diagnet\n");
    printf("\n");
    printf("External programs:\n");
    printf("  Type ELF name or path, e.g. /LS.ELF\n");
    return 0;
}

int cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

int cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("%s\n", cwd);
    return 0;
}

static int normalize_path(const char* input, char* out, size_t out_size) {
    if (!input || !out || out_size < 2) return -1;

    char combined[MAX_PATH];
    if (input[0] == '/') {
        if ((size_t)strlen(input) >= sizeof(combined)) return -1;
        strcpy(combined, input);
    } else {
        size_t cwd_len = (size_t)strlen(cwd);
        size_t input_len = (size_t)strlen(input);
        size_t need_slash = (cwd_len > 0 && cwd[cwd_len - 1] != '/') ? 1 : 0;
        if (cwd_len + need_slash + input_len >= sizeof(combined)) return -1;
        strcpy(combined, cwd);
        if (need_slash) strcat(combined, "/");
        strcat(combined, input);
    }

    size_t out_len = 1;
    out[0] = '/';
    out[1] = '\0';

    size_t i = 0;
    while (combined[i] != '\0') {
        while (combined[i] == '/') i++;
        if (combined[i] == '\0') break;

        char segment[MAX_PATH];
        size_t seg_len = 0;
        while (combined[i] != '\0' && combined[i] != '/') {
            if (seg_len + 1 >= sizeof(segment)) return -1;
            segment[seg_len++] = combined[i++];
        }
        segment[seg_len] = '\0';

        if (seg_len == 1 && segment[0] == '.') {
            continue;
        }

        if (seg_len == 2 && segment[0] == '.' && segment[1] == '.') {
            if (out_len > 1) {
                size_t p = out_len;
                while (p > 1 && out[p - 1] == '/') p--;
                while (p > 1 && out[p - 1] != '/') p--;
                out_len = (p > 1) ? (p - 1) : 1;
                out[out_len] = '\0';
            }
            continue;
        }

        if (out_len > 1) {
            if (out_len + 1 >= out_size) return -1;
            out[out_len++] = '/';
        }
        if (out_len + seg_len >= out_size) return -1;

        for (size_t j = 0; j < seg_len; j++) {
            out[out_len++] = segment[j];
        }
        out[out_len] = '\0';
    }

    if (out_len == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    return 0;
}

static void history_add(const char* line) {
    if (!line || !line[0]) return;

    if (history_count > 0) {
        int last = (history_next + MAX_HISTORY - 1) % MAX_HISTORY;
        if (strcmp(history[last], line) == 0) return;
    }

    size_t len = (size_t)strlen(line);
    if (len >= MAX_CMD_LEN) len = MAX_CMD_LEN - 1;

    memcpy(history[history_next], line, len);
    history[history_next][len] = '\0';

    history_next = (history_next + 1) % MAX_HISTORY;
    if (history_count < MAX_HISTORY) history_count++;
}

int cmd_history(int argc, char** argv) {
    (void)argc; (void)argv;

    if (history_count == 0) {
        printf("(no history)\n");
        return 0;
    }

    int start = (history_count < MAX_HISTORY) ? 0 : history_next;
    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % MAX_HISTORY;
        printf("%d  %s\n", i + 1, history[idx]);
    }
    return 0;
}

int cmd_cd(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cd <path>\n");
        return 1;
    }

    char normalized[MAX_PATH];
    if (normalize_path(argv[1], normalized, sizeof(normalized)) != 0) {
        printf("Path too long\n");
        return 1;
    }

    strcpy(cwd, normalized);
    return 0;
}

int cmd_dir(int argc, char** argv) {
    (void)argc; (void)argv;

    char output[2048];
    long n = sys_listdir(cwd, output, sizeof(output) - 1);
    if (n < 0) {
        printf("dir: failed to list %s\n", cwd);
        return 1;
    }
    if (n > 0) {
        sys_write(output, n);
    }
    return 0;
}

int cmd_run(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: run <program> [args...]\n");
        return 1;
    }

    long pid = try_exec_command(argv[1], argc - 2, &argv[2]);
    if (pid < 0) {
        printf("Command not found: %s\n", argv[1]);
        return 1;
    }
    return 0;
}

int cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    sys_clear();
    printf("SzczupakOS Shell\n");
    printf("Type 'help' for available commands\n\n");
    return 0;
}

int cmd_sysinfo(int argc, char** argv) {
    (void)argc; (void)argv;
    struct sysinfo info;
    if (sys_sysinfo(&info) < 0) {
        printf("Error getting system info\n");
        return 1;
    }
    
    printf("=== System Information ===\n");
    printf("Uptime: %lu seconds\n", info.uptime);
    printf("Total Memory: %lu bytes\n", info.total_memory);
    printf("Free Memory: %lu bytes\n", info.free_memory);
    printf("Running Processes: %u\n", info.nr_processes);
    return 0;
}

int cmd_exit(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Goodbye!\n");
    sys_exit(0);
    return 0;
}

static int cmd_net(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct net_info info;
    if (sys_net_info(&info) < 0) {
        printf("net: unavailable\n");
        return 1;
    }

    printf("=== Network ===\n");
    printf("Link: %s\n", info.link_up ? "up" : "down");
    printf("DHCP: %s\n", info.configured ? "configured" : "not configured");
    printf("MAC: ");
    netcli_print_mac(info.mac);
    printf("\n");
    printf("IP: ");
    netcli_print_ip4(info.ip);
    printf("\n");
    printf("MASK: ");
    netcli_print_ip4(info.netmask);
    printf("\n");
    printf("GW: ");
    netcli_print_ip4(info.gateway);
    printf("\n");
    printf("DNS: ");
    netcli_print_ip4(info.dns);
    printf("\n");
    printf("Lease: %u s\n", (unsigned)info.lease_time_seconds);
    return 0;
}

static int cmd_netstat(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct net_stats st;
    if (sys_net_stats(&st) < 0) {
        printf("netstat: unavailable\n");
        return 1;
    }

    printf("=== Net Stats ===\n");
    printf("Frames RX/TX: %lu / %lu\n", st.rx_frames, st.tx_frames);
    printf("IPv4 RX/TX:   %lu / %lu\n", st.rx_ipv4, st.tx_ipv4);
    printf("ARP RX/TX:    %lu / %lu\n", st.rx_arp, st.tx_arp);
    printf("ICMP RX/TX:   %lu / %lu\n", st.rx_icmp, st.tx_icmp);
    printf("UDP RX/TX:    %lu / %lu\n", st.rx_udp, st.tx_udp);
    printf("TCP RX:       %lu\n", st.rx_tcp);
    printf("DHCP RX:      %lu\n", st.rx_dhcp);
    printf("DNS RX:       %lu\n", st.rx_dns);
    printf("ARP cache:    %u entries, hit/miss=%lu/%lu\n",
           (unsigned)st.arp_cache_entries, st.arp_cache_hits, st.arp_cache_misses);
    printf("DNS cache:    %u entries, hit/miss=%lu/%lu\n",
           (unsigned)st.dns_cache_entries, st.dns_cache_hits, st.dns_cache_misses);
    printf("DNS queries:  %lu, timeouts=%lu\n", st.dns_queries, st.dns_timeouts);
    return 0;
}

static int cmd_dns(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: dns <hostname>\n");
        return 1;
    }

    uint8_t ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], ip, &from_dns, "dns") != 0) {
        return 1;
    }

    printf("%s -> ", argv[1]);
    netcli_print_ip4(ip);
    if (!from_dns) {
        printf(" (literal)");
    }
    printf("\n");
    return 0;
}

static int cmd_nslookup(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: nslookup <hostname>\n");
        return 1;
    }

    uint8_t ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], ip, &from_dns, "nslookup") != 0) {
        return 1;
    }

    printf("%s -> ", argv[1]);
    netcli_print_ip4(ip);
    if (!from_dns) {
        printf(" (literal)");
    }
    printf("\n");
    return 0;
}

static int cmd_ping(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: ping <ip|host> [count]\n");
        return 1;
    }

    uint8_t ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], ip, &from_dns, "ping") != 0) {
        return 1;
    }
    (void)from_dns;

    int count = 4;
    if (argc >= 3) {
        long v = atoi(argv[2]);
        if (v <= 0 || v > 20) {
            printf("ping: count must be 1..20\n");
            return 1;
        }
        count = (int)v;
    }

    printf("PING ");
    netcli_print_ip4(ip);
    printf(":\n");

    int success = 0;
    uint32_t rtt_min = 0xFFFFFFFFu;
    uint32_t rtt_max = 0u;
    uint64_t rtt_sum = 0u;

    for (int i = 0; i < count; i++) {
        uint32_t rtt_ms = 0;
        if (sys_net_ping(ip, 1000, &rtt_ms) == 0) {
            printf("  reply from ");
            netcli_print_ip4(ip);
            printf(": time=%u ms\n", (unsigned)rtt_ms);
            success++;
            if (rtt_ms < rtt_min) rtt_min = rtt_ms;
            if (rtt_ms > rtt_max) rtt_max = rtt_ms;
            rtt_sum += rtt_ms;
        } else {
            printf("  request timeout\n");
        }
        if (i + 1 < count) sys_sleep(250);
    }

    printf("Ping statistics: %d/%d replies\n", success, count);
    if (success > 0) {
        uint32_t rtt_avg = (uint32_t)(rtt_sum / (uint64_t)success);
        printf("RTT min/avg/max: %u/%u/%u ms\n",
               (unsigned)rtt_min, (unsigned)rtt_avg, (unsigned)rtt_max);
    }
    return (success > 0) ? 0 : 1;
}

static int cmd_traceroute(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: traceroute <ip|host> [max_hops]\n");
        return 1;
    }

    uint8_t target_ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], target_ip, &from_dns, "traceroute") != 0) {
        return 1;
    }

    int max_hops = 20;
    if (argc >= 3) {
        long v = atoi(argv[2]);
        if (v <= 0 || v > 64) {
            printf("traceroute: max_hops must be 1..64\n");
            return 1;
        }
        max_hops = (int)v;
    }

    printf("traceroute to ");
    if (from_dns) {
        printf("%s (", argv[1]);
        netcli_print_ip4(target_ip);
        printf(")");
    } else {
        netcli_print_ip4(target_ip);
    }
    printf(", %d hops max\n", max_hops);

    for (int ttl = 1; ttl <= max_hops; ttl++) {
        struct net_trace_probe_req req;
        struct net_trace_probe_rsp rsp;
        memset(&req, 0, sizeof(req));
        memset(&rsp, 0, sizeof(rsp));

        memcpy(req.dst_ip, target_ip, 4);
        req.ttl = (uint8_t)ttl;
        req.timeout_ms = 1200;

        if (sys_net_trace_probe(&req, &rsp) < 0) {
            printf("%d  error\n", ttl);
            return 1;
        }

        printf("%d  ", ttl);
        if (!rsp.ok) {
            printf("*\n");
            continue;
        }

        netcli_print_ip4(rsp.hop_ip);
        printf("  %u ms\n", (unsigned)rsp.rtt_ms);

        if (rsp.reached_dst) {
            return 0;
        }

        sys_sleep(100);
    }

    printf("trace incomplete after %d hops\n", max_hops);
    return 1;
}

static int cmd_tcping(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: tcping <ip|host> <port> [count]\n");
        return 1;
    }

    uint8_t target_ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], target_ip, &from_dns, "tcping") != 0) {
        return 1;
    }

    long port_long = atoi(argv[2]);
    if (port_long <= 0 || port_long > 65535) {
        printf("tcping: port must be 1..65535\n");
        return 1;
    }
    uint16_t port = (uint16_t)port_long;

    int count = 4;
    if (argc >= 4) {
        long v = atoi(argv[3]);
        if (v <= 0 || v > 20) {
            printf("tcping: count must be 1..20\n");
            return 1;
        }
        count = (int)v;
    }

    printf("TCPING ");
    if (from_dns) {
        printf("%s (", argv[1]);
        netcli_print_ip4(target_ip);
        printf(")");
    } else {
        netcli_print_ip4(target_ip);
    }
    printf(":%u\n", (unsigned)port);

    int got_reply = 0;
    int open_count = 0;
    int closed_count = 0;
    int timeout_count = 0;
    uint32_t rtt_min = 0xFFFFFFFFu;
    uint32_t rtt_max = 0u;
    uint64_t rtt_sum = 0u;

    for (int i = 0; i < count; i++) {
        bool ok = false;
        bool open = false;
        uint32_t rtt_ms = 0;
        if (netcli_run_tcp_probe_once(target_ip, port, 1200, &ok, &open, &rtt_ms) < 0) {
            printf("  error\n");
            return 1;
        }

        if (!ok) {
            printf("  timeout\n");
            timeout_count++;
        } else if (open) {
            printf("  open: time=%u ms\n", (unsigned)rtt_ms);
            open_count++;
            got_reply++;
            if (rtt_ms < rtt_min) rtt_min = rtt_ms;
            if (rtt_ms > rtt_max) rtt_max = rtt_ms;
            rtt_sum += rtt_ms;
        } else {
            printf("  closed: time=%u ms\n", (unsigned)rtt_ms);
            closed_count++;
            got_reply++;
            if (rtt_ms < rtt_min) rtt_min = rtt_ms;
            if (rtt_ms > rtt_max) rtt_max = rtt_ms;
            rtt_sum += rtt_ms;
        }

        if (i + 1 < count) {
            sys_sleep(250);
        }
    }

    printf("TCPING statistics: open=%d closed=%d timeout=%d\n",
           open_count, closed_count, timeout_count);
    if (got_reply > 0) {
        uint32_t rtt_avg = (uint32_t)(rtt_sum / (uint64_t)got_reply);
        printf("RTT min/avg/max: %u/%u/%u ms\n",
               (unsigned)rtt_min, (unsigned)rtt_avg, (unsigned)rtt_max);
    }

    return (got_reply > 0) ? 0 : 1;
}

static int cmd_scan(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: scan <ip|host> [--fast|--full] [timeout_ms]\n");
        printf("       scan <ip|host> <start> [end] [timeout_ms]\n");
        printf("Examples:\n");
        printf("  scan 192.168.76.1\n");
        printf("  scan 192.168.76.1 --fast\n");
        printf("  scan google.com --full 120\n");
        printf("  scan google.com 1 1024 300\n");
        return 1;
    }

    uint8_t target_ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], target_ip, &from_dns, "scan") != 0) {
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
            return 1;
        }
    }

    if (preset_fast || preset_full) {
        if (argc > argi + 1) {
            printf("scan: too many arguments for preset mode\n");
            return 1;
        }
        if (argc == argi + 1) {
            long t = atoi(argv[argi]);
            if (t < 50 || t > 5000) {
                printf("scan: timeout_ms must be 50..5000\n");
                return 1;
            }
            timeout_ms = (uint32_t)t;
        }
    } else {
        if (argc >= 3) {
            long s = atoi(argv[2]);
            if (s <= 0 || s > 65535) {
                printf("scan: start port must be 1..65535\n");
                return 1;
            }
            long e = s;
            if (argc >= 4) {
                e = atoi(argv[3]);
                if (e <= 0 || e > 65535) {
                    printf("scan: end port must be 1..65535\n");
                    return 1;
                }
            }
            if (s > e) {
                printf("scan: start must be <= end\n");
                return 1;
            }
            if ((e - s + 1) > 2048) {
                printf("scan: range too large (max 2048 ports)\n");
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
    return 0;
}

static int cmd_netcheck(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "google.com";

    struct net_info info;
    if (sys_net_info(&info) < 0) {
        printf("netcheck: network unavailable\n");
        return 1;
    }

    printf("=== Netcheck ===\n");
    printf("Link: %s\n", info.link_up ? "up" : "down");
    printf("IPv4: %s\n", info.configured ? "configured" : "not configured");
    printf("IP: ");
    netcli_print_ip4(info.ip);
    printf("  GW: ");
    netcli_print_ip4(info.gateway);
    printf("  DNS: ");
    netcli_print_ip4(info.dns);
    printf("\n");

    if (!info.link_up || !info.configured) {
        printf("Result: FAIL\n");
        return 1;
    }

    int failures = 0;
    int warnings = 0;

    if (!netcli_ip4_is_zero(info.gateway)) {
        uint32_t gw_rtt = 0;
        if (sys_net_ping(info.gateway, 1000, &gw_rtt) == 0) {
            printf("gateway ping: ok (%u ms)\n", (unsigned)gw_rtt);
        } else {
            printf("gateway ping: fail\n");
            failures++;
        }
    } else {
        printf("gateway ping: skipped (no gateway)\n");
        warnings++;
    }

    uint8_t target_ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(target, target_ip, &from_dns, "netcheck") != 0) {
        printf("Result: FAIL\n");
        return 1;
    }

    if (from_dns) {
        printf("dns resolve: ok (%s -> ", target);
        netcli_print_ip4(target_ip);
        printf(")\n");
    } else {
        printf("dns resolve: skipped (target is IPv4 literal)\n");
    }

    uint32_t ping_rtt = 0;
    if (sys_net_ping(target_ip, 1400, &ping_rtt) == 0) {
        printf("target ping: ok (%u ms)\n", (unsigned)ping_rtt);
    } else {
        printf("target ping: timeout/fail\n");
        failures++;
    }

    bool tcp_ok = false;
    bool tcp_open = false;
    uint32_t tcp_rtt = 0;
    if (netcli_run_tcp_probe_once(target_ip, 443, 1200, &tcp_ok, &tcp_open, &tcp_rtt) < 0) {
        printf("tcp 443 probe: syscall error\n");
        warnings++;
    } else if (!tcp_ok) {
        printf("tcp 443 probe: timeout\n");
        warnings++;
    } else if (tcp_open) {
        printf("tcp 443 probe: open (%u ms)\n", (unsigned)tcp_rtt);
    } else {
        printf("tcp 443 probe: closed (%u ms)\n", (unsigned)tcp_rtt);
    }

    printf("Result: %s", (failures == 0) ? "PASS" : "FAIL");
    if (warnings > 0) {
        printf(" (warnings=%d)", warnings);
    }
    printf("\n");
    return (failures == 0) ? 0 : 1;
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static int build_abs_path(const char* cmd, char* out, size_t out_size) {
    if (!cmd || !out || out_size == 0) return -1;
    size_t cmd_len = (size_t)strlen(cmd);
    if (cmd_len == 0) return -1;

    if (cmd[0] == '/') {
        if (cmd_len >= out_size) return -1;
        strcpy(out, cmd);
        return 0;
    }

    size_t cwd_len = (size_t)strlen(cwd);
    size_t need_slash = (cwd_len > 0 && cwd[cwd_len - 1] != '/') ? 1 : 0;
    if (cwd_len + need_slash + cmd_len >= out_size) return -1;

    size_t pos = 0;
    for (size_t i = 0; i < cwd_len; i++) out[pos++] = cwd[i];
    if (need_slash) out[pos++] = '/';
    for (size_t i = 0; i < cmd_len; i++) out[pos++] = cmd[i];
    out[pos] = '\0';
    return 0;
}

static bool basename_has_dot(const char* path) {
    if (!path) return false;
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    for (const char* p = base; *p; p++) {
        if (*p == '.') return true;
    }
    return false;
}

static int append_elf_ext(const char* in, char* out, size_t out_size) {
    if (!in || !out || out_size == 0) return -1;
    size_t len = (size_t)strlen(in);
    if (len + 4 >= out_size) return -1;
    strcpy(out, in);
    strcat(out, ".ELF");
    return 0;
}

static void to_upper_path(const char* in, char* out, size_t out_size) {
    if (!in || !out || out_size == 0) return;
    size_t i = 0;
    while (in[i] && i + 1 < out_size) {
        out[i] = upper_ascii(in[i]);
        i++;
    }
    out[i] = '\0';
}

static int build_exec_cmdline(const char* exec_path, int arg_count, char** args, char* out, size_t out_size) {
    if (!exec_path || !out || out_size == 0) return -1;

    size_t pos = 0;
    size_t path_len = (size_t)strlen(exec_path);
    if (path_len == 0 || path_len >= out_size) return -1;

    memcpy(out + pos, exec_path, path_len);
    pos += path_len;

    for (int i = 0; i < arg_count; i++) {
        const char* arg = args[i];
        if (!arg || !arg[0]) continue;

        size_t arg_len = (size_t)strlen(arg);
        if (pos + 1 + arg_len >= out_size) return -1;

        out[pos++] = ' ';
        memcpy(out + pos, arg, arg_len);
        pos += arg_len;
    }

    out[pos] = '\0';
    return 0;
}

static long try_exec_command(const char* cmd, int arg_count, char** args) {
    char abs_path[MAX_PATH];
    if (build_abs_path(cmd, abs_path, sizeof(abs_path)) != 0) return -1;

    char exec_cmdline[MAX_CMD_LEN];
    if (build_exec_cmdline(abs_path, arg_count, args, exec_cmdline, sizeof(exec_cmdline)) != 0) return -1;

    long pid = sys_exec(exec_cmdline);
    if (pid >= 0) return pid;

    char upper_path[MAX_PATH];
    to_upper_path(abs_path, upper_path, sizeof(upper_path));
    if (strcmp(upper_path, abs_path) != 0) {
        if (build_exec_cmdline(upper_path, arg_count, args, exec_cmdline, sizeof(exec_cmdline)) != 0) return -1;
        pid = sys_exec(exec_cmdline);
        if (pid >= 0) return pid;
    }

    if (!basename_has_dot(abs_path)) {
        char abs_elf[MAX_PATH];
        if (append_elf_ext(abs_path, abs_elf, sizeof(abs_elf)) == 0) {
            if (build_exec_cmdline(abs_elf, arg_count, args, exec_cmdline, sizeof(exec_cmdline)) != 0) return -1;
            pid = sys_exec(exec_cmdline);
            if (pid >= 0) return pid;

            char upper_elf[MAX_PATH];
            to_upper_path(abs_elf, upper_elf, sizeof(upper_elf));
            if (strcmp(upper_elf, abs_elf) != 0) {
                if (build_exec_cmdline(upper_elf, arg_count, args, exec_cmdline, sizeof(exec_cmdline)) != 0) return -1;
                pid = sys_exec(exec_cmdline);
                if (pid >= 0) return pid;
            }
        }
    }

    return -1;
}

void parse_line(char* line) {
    argc = 0;
    char* token = strtok(line, " \t\n\r");
    
    while (token && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n\r");
    }
    argv[argc] = NULL;
}

int execute_command(char* line) {
    if (!line || !*line) return 0;
    
    parse_line(line);
    
    if (argc == 0) return 0;

    uintptr_t line_start = (uintptr_t)line;
    uintptr_t line_end = line_start + MAX_CMD_LEN;
    uintptr_t cmd_ptr = (uintptr_t)argv[0];
    if (cmd_ptr < line_start || cmd_ptr >= line_end) {
        printf("Parse error\n");
        return 1;
    }
    
    const char* cmd = argv[0];
    
    if (strcmp(cmd, "help") == 0) return cmd_help(argc, argv);
    if (strcmp(cmd, "echo") == 0) return cmd_echo(argc, argv);
    if (strcmp(cmd, "history") == 0) return cmd_history(argc, argv);
    if (strcmp(cmd, "dir") == 0) return cmd_dir(argc, argv);
    if (strcmp(cmd, "ls") == 0) return cmd_dir(argc, argv);
    if (strcmp(cmd, "pwd") == 0) return cmd_pwd(argc, argv);
    if (strcmp(cmd, "cd") == 0) return cmd_cd(argc, argv);
    if (strcmp(cmd, "run") == 0) return cmd_run(argc, argv);
    if (strcmp(cmd, "clear") == 0) return cmd_clear(argc, argv);
    if (strcmp(cmd, "sysinfo") == 0) return cmd_sysinfo(argc, argv);
    if (strcmp(cmd, "net") == 0 || strcmp(cmd, "ifconfig") == 0) return cmd_net(argc, argv);
    if (strcmp(cmd, "netstat") == 0) return cmd_netstat(argc, argv);
    if (strcmp(cmd, "dns") == 0) return cmd_dns(argc, argv);
    if (strcmp(cmd, "nslookup") == 0) return cmd_nslookup(argc, argv);
    if (strcmp(cmd, "ping") == 0) return cmd_ping(argc, argv);
    if (strcmp(cmd, "traceroute") == 0 || strcmp(cmd, "trace") == 0) return cmd_traceroute(argc, argv);
    if (strcmp(cmd, "tcping") == 0) return cmd_tcping(argc, argv);
    if (strcmp(cmd, "scan") == 0 || strcmp(cmd, "portscan") == 0) return cmd_scan(argc, argv);
    if (strcmp(cmd, "netcheck") == 0 || strcmp(cmd, "diagnet") == 0) return cmd_netcheck(argc, argv);
    if (strcmp(cmd, "exit") == 0) return cmd_exit(argc, argv);
    if (strcmp(cmd, "quit") == 0) return cmd_exit(argc, argv);

    long pid = try_exec_command(cmd, argc - 1, &argv[1]);
    if (pid >= 0) {
        return 0;
    }
    
    printf("Unknown command: %s\n", cmd);
    return 1;
}

int main(void) {
    sys_clear();
    printf("SzczupakOS Shell v1.0\n");
    printf("Type 'help' for available commands\n\n");

    while (1) {
        sys_write("[", 1);
        sys_write(cwd, strlen(cwd));
        sys_write("]$ ", 3);
        
        memset(line_buf, 0, MAX_CMD_LEN);
        long len = sys_read(line_buf, MAX_CMD_LEN - 1);
        
        if (len <= 0) {
            sys_sleep(100);
            continue;
        }

        if (len >= MAX_CMD_LEN) {
            line_buf[MAX_CMD_LEN - 1] = '\0';
            continue;
        }
        
        if (len > 0 && line_buf[len - 1] == '\n') {
            line_buf[len - 1] = '\0';
        }

        history_add(line_buf);
        
        execute_command(line_buf);
    }
    
    return 0;
}
