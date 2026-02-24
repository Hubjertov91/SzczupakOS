#include <stdio.h>
#include <syscall.h>
#include <netcli.h>

int main(int argc, char** argv) {
    const char* target = (argc >= 2) ? argv[1] : "google.com";

    struct net_info info;
    if (sys_net_info(&info) < 0) {
        printf("netcheck: network unavailable\n");
        sys_exit(1);
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
        sys_exit(1);
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
        sys_exit(1);
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

    int rc = (failures == 0) ? 0 : 1;
    sys_exit(rc);
    return rc;
}
