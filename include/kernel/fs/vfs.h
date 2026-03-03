#ifndef _KERNEL_VFS_H
#define _KERNEL_VFS_H

#include "stdint.h"

#define MAX_FILENAME 256
#define MAX_PATH 1024

typedef enum {
    VFS_FILE,
    VFS_DIRECTORY
} vfs_node_type_t;

struct vfs_node;
struct vfs_filesystem;

typedef struct vfs_operations {
    struct vfs_node* (*create_file)(struct vfs_node* parent, const char* name);
    struct vfs_node* (*create_dir)(struct vfs_node* parent, const char* name);
    bool (*delete)(struct vfs_node* node);
    bool (*read)(struct vfs_node* node, void* buffer, size_t offset, size_t size);
    bool (*write)(struct vfs_node* node, const void* data, size_t offset, size_t size);
} vfs_operations_t;

typedef struct vfs_node {
    char name[MAX_FILENAME];
    vfs_node_type_t type;
    size_t size;
    void* fs_data;
    struct vfs_filesystem* fs;
    struct vfs_node* parent;
    struct vfs_node* first_child;
    struct vfs_node* next_sibling;
} vfs_node_t;

typedef struct vfs_filesystem {
    const char* name;
    vfs_operations_t* ops;
    vfs_node_t* root;
    void* private_data;
} vfs_filesystem_t;

void vfs_init(void);
bool vfs_mount(vfs_filesystem_t* fs, const char* mountpoint);
bool vfs_mount_at(vfs_filesystem_t* fs, const char* path);
vfs_node_t* vfs_get_root(void);
vfs_node_t* vfs_resolve_path(const char* path);
vfs_node_t* vfs_find_child(vfs_node_t* parent, const char* name);
bool vfs_ensure_directory(const char* path);
void vfs_list_directory(vfs_node_t* dir);

vfs_node_t* vfs_create_file(vfs_node_t* parent, const char* name);
vfs_node_t* vfs_create_directory(vfs_node_t* parent, const char* name);
bool vfs_delete(vfs_node_t* node);
bool vfs_read(vfs_node_t* node, void* buffer, size_t offset, size_t size);
bool vfs_write(vfs_node_t* node, const void* data, size_t offset, size_t size);

vfs_node_t* vfs_open(const char* path, int flags);
void vfs_close(vfs_node_t* node);

#endif
