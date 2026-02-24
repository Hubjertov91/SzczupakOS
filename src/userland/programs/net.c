#include <stdio.h>
#include <syscall.h>
#include <netcli.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct net_info info;
    if (sys_net_info(&info) < 0) {
        printf("net: unavailable\n");
        sys_exit(1);
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

    sys_exit(0);
    return 0;
}
