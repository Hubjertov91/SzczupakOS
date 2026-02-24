#include <stdio.h>
#include <syscall.h>
#include <netcli.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: nslookup <hostname>\n");
        sys_exit(1);
        return 1;
    }

    uint8_t ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(argv[1], ip, &from_dns, "nslookup") != 0) {
        sys_exit(1);
        return 1;
    }

    printf("%s -> ", argv[1]);
    netcli_print_ip4(ip);
    if (!from_dns) {
        printf(" (literal)");
    }
    printf("\n");

    sys_exit(0);
    return 0;
}
