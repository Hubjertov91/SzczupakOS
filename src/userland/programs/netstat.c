#include <stdio.h>
#include <syscall.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct net_stats st;
    if (sys_net_stats(&st) < 0) {
        printf("netstat: unavailable\n");
        sys_exit(1);
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

    sys_exit(0);
    return 0;
}
