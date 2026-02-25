#ifndef _KERNEL_PTY_H
#define _KERNEL_PTY_H

#include <stdint.h>
#include <task/task.h>

void pty_init(void);
int32_t pty_open(void);
int32_t pty_close(int32_t id);
bool pty_is_open(int32_t id);

int32_t pty_attach_slave(int32_t id, uint32_t pid);
void pty_detach_slave(uint32_t pid);

bool pty_task_attached(task_t* task);
bool pty_slave_has_input(task_t* task);
size_t pty_slave_read(task_t* task, char* buf, size_t len);
size_t pty_slave_write(task_t* task, const char* buf, size_t len);

size_t pty_host_read(int32_t id, char* buf, size_t len);
size_t pty_host_write(int32_t id, const char* buf, size_t len);
size_t pty_host_out_available(int32_t id);

#endif
