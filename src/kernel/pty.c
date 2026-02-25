#include <kernel/pty.h>
#include <kernel/string.h>

#define PTY_MAX_COUNT 8
#define PTY_BUF_SIZE 4096

typedef struct {
    char data[PTY_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} pty_ring_t;

typedef struct {
    bool used;
    uint32_t slave_pid;
    pty_ring_t host_to_slave;
    pty_ring_t slave_to_host;
} pty_t;

static pty_t g_ptys[PTY_MAX_COUNT];

static inline uint64_t irq_save_disable(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    __asm__ volatile("push %0; popfq" : : "r"(flags) : "memory", "cc");
}

static void ring_reset(pty_ring_t* r) {
    if (!r) return;
    r->head = 0;
    r->tail = 0;
    r->count = 0;
}

static bool ring_push(pty_ring_t* r, char c) {
    if (!r || r->count >= PTY_BUF_SIZE) return false;
    r->data[r->head] = c;
    r->head = (r->head + 1) % PTY_BUF_SIZE;
    r->count++;
    return true;
}

static bool ring_pop(pty_ring_t* r, char* out) {
    if (!r || !out || r->count == 0) return false;
    *out = r->data[r->tail];
    r->tail = (r->tail + 1) % PTY_BUF_SIZE;
    r->count--;
    return true;
}

static bool pty_slot_valid_locked(int32_t id) {
    if (id < 0 || id >= PTY_MAX_COUNT) return false;
    return g_ptys[id].used;
}

void pty_init(void) {
    uint64_t flags = irq_save_disable();
    memset(g_ptys, 0, sizeof(g_ptys));
    irq_restore(flags);
}

int32_t pty_open(void) {
    uint64_t flags = irq_save_disable();
    for (int32_t i = 0; i < PTY_MAX_COUNT; i++) {
        if (g_ptys[i].used) continue;
        g_ptys[i].used = true;
        g_ptys[i].slave_pid = 0;
        ring_reset(&g_ptys[i].host_to_slave);
        ring_reset(&g_ptys[i].slave_to_host);
        irq_restore(flags);
        return i;
    }
    irq_restore(flags);
    return -1;
}

int32_t pty_close(int32_t id) {
    uint64_t flags = irq_save_disable();
    if (!pty_slot_valid_locked(id)) {
        irq_restore(flags);
        return -1;
    }
    g_ptys[id].used = false;
    g_ptys[id].slave_pid = 0;
    ring_reset(&g_ptys[id].host_to_slave);
    ring_reset(&g_ptys[id].slave_to_host);
    irq_restore(flags);
    return 0;
}

bool pty_is_open(int32_t id) {
    uint64_t flags = irq_save_disable();
    bool ok = pty_slot_valid_locked(id);
    irq_restore(flags);
    return ok;
}

int32_t pty_attach_slave(int32_t id, uint32_t pid) {
    if (pid == 0) return -1;
    uint64_t flags = irq_save_disable();
    if (!pty_slot_valid_locked(id)) {
        irq_restore(flags);
        return -1;
    }
    if (g_ptys[id].slave_pid != 0 && g_ptys[id].slave_pid != pid) {
        irq_restore(flags);
        return -1;
    }
    g_ptys[id].slave_pid = pid;
    irq_restore(flags);
    return 0;
}

void pty_detach_slave(uint32_t pid) {
    if (pid == 0) return;
    uint64_t flags = irq_save_disable();
    for (int32_t i = 0; i < PTY_MAX_COUNT; i++) {
        if (!g_ptys[i].used) continue;
        if (g_ptys[i].slave_pid == pid) {
            g_ptys[i].slave_pid = 0;
        }
    }
    irq_restore(flags);
}

static bool pty_task_attached_locked(task_t* task) {
    if (!task || task->pty_id < 0 || task->pty_id >= PTY_MAX_COUNT) return false;
    pty_t* p = &g_ptys[task->pty_id];
    if (!p->used) return false;
    return p->slave_pid == task->pid;
}

bool pty_task_attached(task_t* task) {
    uint64_t flags = irq_save_disable();
    bool ok = pty_task_attached_locked(task);
    irq_restore(flags);
    return ok;
}

bool pty_slave_has_input(task_t* task) {
    uint64_t flags = irq_save_disable();
    bool has = false;
    if (pty_task_attached_locked(task)) {
        pty_t* p = &g_ptys[task->pty_id];
        has = p->host_to_slave.count > 0;
    }
    irq_restore(flags);
    return has;
}

size_t pty_slave_read(task_t* task, char* buf, size_t len) {
    if (!buf || len == 0) return 0;

    uint64_t flags = irq_save_disable();
    if (!pty_task_attached_locked(task)) {
        irq_restore(flags);
        return 0;
    }

    pty_t* p = &g_ptys[task->pty_id];
    size_t n = 0;
    while (n < len) {
        char c = 0;
        if (!ring_pop(&p->host_to_slave, &c)) break;
        buf[n++] = c;
    }
    irq_restore(flags);
    return n;
}

size_t pty_slave_write(task_t* task, const char* buf, size_t len) {
    if (!buf || len == 0) return 0;

    uint64_t flags = irq_save_disable();
    if (!pty_task_attached_locked(task)) {
        irq_restore(flags);
        return 0;
    }

    pty_t* p = &g_ptys[task->pty_id];
    size_t n = 0;
    while (n < len) {
        if (!ring_push(&p->slave_to_host, buf[n])) break;
        n++;
    }
    irq_restore(flags);
    return n;
}

size_t pty_host_read(int32_t id, char* buf, size_t len) {
    if (!buf || len == 0) return 0;

    uint64_t flags = irq_save_disable();
    if (!pty_slot_valid_locked(id)) {
        irq_restore(flags);
        return 0;
    }

    pty_t* p = &g_ptys[id];
    size_t n = 0;
    while (n < len) {
        char c = 0;
        if (!ring_pop(&p->slave_to_host, &c)) break;
        buf[n++] = c;
    }
    irq_restore(flags);
    return n;
}

size_t pty_host_write(int32_t id, const char* buf, size_t len) {
    if (!buf || len == 0) return 0;

    uint64_t flags = irq_save_disable();
    if (!pty_slot_valid_locked(id)) {
        irq_restore(flags);
        return 0;
    }

    pty_t* p = &g_ptys[id];
    size_t n = 0;
    while (n < len) {
        if (!ring_push(&p->host_to_slave, buf[n])) break;
        n++;
    }
    irq_restore(flags);
    return n;
}

size_t pty_host_out_available(int32_t id) {
    uint64_t flags = irq_save_disable();
    size_t out = 0;
    if (pty_slot_valid_locked(id)) {
        out = g_ptys[id].slave_to_host.count;
    }
    irq_restore(flags);
    return out;
}
