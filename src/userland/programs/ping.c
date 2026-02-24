#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <netcli.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: ping <ip|host> [count]\n");
        sys_exit(1);
        return 1;
    }

    uint8_t ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], ip, &from_dns, "ping") != 0) {
        sys_exit(1);
        return 1;
    }
    (void)from_dns;

    int count = 4;
    if (argc >= 3) {
        long v = atoi(argv[2]);
        if (v <= 0 || v > 20) {
            printf("ping: count must be 1..20\n");
            sys_exit(1);
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

    sys_exit((success > 0) ? 0 : 1);
    return (success > 0) ? 0 : 1;
}
