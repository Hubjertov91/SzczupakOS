#ifndef USER_NETCLI_H
#define USER_NETCLI_H

#include <stdint.h>
#include <stdbool.h>

void netcli_print_ip4(const uint8_t ip[4]);
void netcli_print_mac(const uint8_t mac[6]);
int netcli_parse_ipv4(const char* s, uint8_t out[4]);
bool netcli_contains_char(const char* s, char needle);
bool netcli_ip4_is_zero(const uint8_t ip[4]);

int netcli_resolve_target_ipv4(const char* target, uint8_t out_ip[4], bool* out_from_dns, const char* context);

const char* netcli_tcp_service_name(uint16_t port);
int netcli_run_tcp_probe_once(const uint8_t ip[4], uint16_t port, uint32_t timeout_ms,
                              bool* out_ok, bool* out_open, uint32_t* out_rtt_ms);

#endif
