#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <syscall.h>
#include <netcli.h>

#define HTTP_RESPONSE_CAP 4096

static bool starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    size_t i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return false;
        i++;
    }
    return true;
}

static bool copy_text(char* dst, size_t cap, const char* src) {
    if (!dst || !src || cap == 0) return false;
    size_t len = (size_t)strlen(src);
    if (len + 1 > cap) return false;
    memcpy(dst, src, len + 1);
    return true;
}

static bool copy_slice(char* dst, size_t cap, const char* begin, size_t len) {
    if (!dst || !begin || cap == 0) return false;
    if (len + 1 > cap) return false;
    memcpy(dst, begin, len);
    dst[len] = '\0';
    return true;
}

static int parse_port_value(const char* s, uint16_t* out_port) {
    if (!s || !out_port || !s[0]) return -1;
    long v = atoi(s);
    if (v <= 0 || v > 65535) return -1;
    *out_port = (uint16_t)v;
    return 0;
}

static int parse_timeout_value(const char* s, uint32_t* out_timeout) {
    if (!s || !out_timeout || !s[0]) return -1;
    long v = atoi(s);
    if (v < 200 || v > 30000) return -1;
    *out_timeout = (uint32_t)v;
    return 0;
}

static int parse_http_url(const char* url,
                          char host[NET_HTTP_HOST_MAX],
                          char path[NET_HTTP_PATH_MAX],
                          uint16_t* out_port) {
    if (!url || !host || !path || !out_port) return -1;
    if (!starts_with(url, "http://")) return -1;

    const char* p = url + 7;
    if (*p == '\0') return -1;

    const char* slash = strchr(p, '/');
    const char* host_end = slash ? slash : (p + strlen(p));

    const char* colon = NULL;
    for (const char* it = p; it < host_end; it++) {
        if (*it == ':') {
            colon = it;
        }
    }

    if (colon) {
        if (!copy_slice(host, NET_HTTP_HOST_MAX, p, (size_t)(colon - p))) return -1;
        uint16_t port = 0;
        if (parse_port_value(colon + 1, &port) != 0) return -1;
        *out_port = port;
    } else {
        if (!copy_slice(host, NET_HTTP_HOST_MAX, p, (size_t)(host_end - p))) return -1;
        *out_port = 80u;
    }

    if (host[0] == '\0') return -1;

    if (slash) {
        if (!copy_text(path, NET_HTTP_PATH_MAX, slash)) return -1;
    } else {
        if (!copy_text(path, NET_HTTP_PATH_MAX, "/")) return -1;
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: http <url> [timeout_ms]\n");
        printf("   or: http <host> [path] [port] [timeout_ms]\n");
        sys_exit(1);
        return 1;
    }

    char host[NET_HTTP_HOST_MAX];
    char path[NET_HTTP_PATH_MAX];
    uint16_t port = 80u;
    uint32_t timeout_ms = 6000u;

    if (starts_with(argv[1], "http://")) {
        if (parse_http_url(argv[1], host, path, &port) != 0) {
            printf("http: invalid URL (only http:// is supported)\n");
            sys_exit(1);
            return 1;
        }

        if (argc >= 3) {
            if (parse_timeout_value(argv[2], &timeout_ms) != 0) {
                printf("http: timeout must be 200..30000 ms\n");
                sys_exit(1);
                return 1;
            }
        }

        if (argc > 3) {
            printf("http: too many arguments\n");
            sys_exit(1);
            return 1;
        }
    } else {
        if (!copy_text(host, sizeof(host), argv[1])) {
            printf("http: host too long\n");
            sys_exit(1);
            return 1;
        }

        if (argc >= 3) {
            if (!copy_text(path, sizeof(path), argv[2])) {
                printf("http: path too long\n");
                sys_exit(1);
                return 1;
            }
        } else {
            if (!copy_text(path, sizeof(path), "/")) {
                sys_exit(1);
                return 1;
            }
        }

        if (path[0] != '/') {
            printf("http: path must start with '/'\n");
            sys_exit(1);
            return 1;
        }

        if (argc >= 4) {
            if (parse_port_value(argv[3], &port) != 0) {
                printf("http: port must be 1..65535\n");
                sys_exit(1);
                return 1;
            }
        }

        if (argc >= 5) {
            if (parse_timeout_value(argv[4], &timeout_ms) != 0) {
                printf("http: timeout must be 200..30000 ms\n");
                sys_exit(1);
                return 1;
            }
        }

        if (argc > 5) {
            printf("http: too many arguments\n");
            sys_exit(1);
            return 1;
        }
    }

    uint8_t ip[4];
    bool from_dns = false;
    if (netcli_resolve_target_ipv4(host, ip, &from_dns, "http") != 0) {
        sys_exit(1);
        return 1;
    }

    static uint8_t body[HTTP_RESPONSE_CAP];
    memset(body, 0, sizeof(body));

    struct net_http_get_req req;
    struct net_http_get_rsp rsp;
    memset(&req, 0, sizeof(req));
    memset(&rsp, 0, sizeof(rsp));

    memcpy(req.dst_ip, ip, 4);
    req.dst_port = port;
    req.timeout_ms = timeout_ms;
    req.out_body_addr = (uint64_t)(uintptr_t)body;
    req.out_body_capacity = (uint32_t)sizeof(body);
    strcpy(req.host, host);
    strcpy(req.path, path);

    if (sys_net_http_get(&req, &rsp) < 0) {
        printf("http: syscall error\n");
        sys_exit(1);
        return 1;
    }

    if (!rsp.ok) {
        printf("http: request failed");
        if (rsp.status_code != 0) {
            printf(" (status %u)", (unsigned)rsp.status_code);
        }
        printf("\n");
        sys_exit(1);
        return 1;
    }

    printf("HTTP %u", (unsigned)rsp.status_code);
    if (from_dns) {
        printf(" (%s -> ", host);
        netcli_print_ip4(ip);
        printf(")");
    }
    printf(", %u bytes", (unsigned)rsp.body_length);
    if (rsp.truncated) {
        printf(" (truncated)");
    }
    printf("\n\n");

    if (rsp.body_length > 0) {
        if (rsp.body_length > sizeof(body)) {
            printf("http: invalid response size\n");
            sys_exit(1);
            return 1;
        }

        sys_write((const char*)body, rsp.body_length);
        if (body[rsp.body_length - 1] != '\n') {
            printf("\n");
        }
    }

    sys_exit(0);
    return 0;
}
