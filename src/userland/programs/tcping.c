#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <netcli.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: tcping <ip|host> <port> [count]\n");
        sys_exit(1);
        return 1;
    }

    uint8_t target_ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], target_ip, &from_dns, "tcping") != 0) {
        sys_exit(1);
        return 1;
    }

    long port_long = atoi(argv[2]);
    if (port_long <= 0 || port_long > 65535) {
        printf("tcping: port must be 1..65535\n");
        sys_exit(1);
        return 1;
    }
    uint16_t port = (uint16_t)port_long;

    int count = 4;
    if (argc >= 4) {
        long v = atoi(argv[3]);
        if (v <= 0 || v > 20) {
            printf("tcping: count must be 1..20\n");
            sys_exit(1);
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
            sys_exit(1);
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

    sys_exit((got_reply > 0) ? 0 : 1);
    return (got_reply > 0) ? 0 : 1;
}
