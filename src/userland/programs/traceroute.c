#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <netcli.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: traceroute <ip|host> [max_hops]\n");
        sys_exit(1);
        return 1;
    }

    uint8_t target_ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], target_ip, &from_dns, "traceroute") != 0) {
        sys_exit(1);
        return 1;
    }

    int max_hops = 20;
    if (argc >= 3) {
        long v = atoi(argv[2]);
        if (v <= 0 || v > 64) {
            printf("traceroute: max_hops must be 1..64\n");
            sys_exit(1);
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
            sys_exit(1);
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
            sys_exit(0);
            return 0;
        }

        sys_sleep(100);
    }

    printf("trace incomplete after %d hops\n", max_hops);
    sys_exit(1);
    return 1;
}
