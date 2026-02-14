#include <kernel/vfs.h>
#include <kernel/heap.h>
#include <kernel/vga.h>
#include <kernel/serial.h>

static vfs_node_t* vfs_root = NULL;

static int strcmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return -1;
    
    while (*s1 && *s1 == *s2) { 
        s1++; 
        s2++; 
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void vfs_init(void) {
    vfs_root = NULL;
    serial_write("[VFS] Initialized\n");
}

bool vfs_mount(vfs_filesystem_t* fs, const char* mountpoint) {
    if (!fs || !fs->root || !mountpoint) {
        serial_write("[VFS] ERROR: Invalid mount parameters\n");
        return false;
    }
    
    if (strcmp(mountpoint, "/") == 0) {
        vfs_root = fs->root;
        serial_write("[VFS] Mounted ");
        serial_write(fs->name);
        serial_write(" at /\n");
        return true;
    }
    
    serial_write("[VFS] ERROR: Can only mount at /\n");
    return false;
}

bool vfs_mount_at(vfs_filesystem_t* fs, const char* path) {
    if (!fs || !fs->root) return false;
    
    if (strcmp(path, "/") == 0) {
        return vfs_mount(fs, "/");
    }
    
    const char* name = path;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') name = &path[i + 1];
    }
    
    vfs_node_t* mount_point = vfs_find_child(vfs_root, name);
    
    if (!mount_point) {
        mount_point = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
        if (!mount_point) return false;
        
        int i = 0;
        while (name[i] && i < MAX_FILENAME - 1) {
            mount_point->name[i] = name[i];
            i++;
        }
        mount_point->name[i] = '\0';
        
        mount_point->type = VFS_DIRECTORY;
        mount_point->size = 0;
        mount_point->fs_data = NULL;
        mount_point->parent = vfs_root;
        mount_point->next_sibling = vfs_root->first_child;
        vfs_root->first_child = mount_point;
        
        serial_write("[VFS] Created mount point ");
        serial_write(name);
        serial_write("\n");
    } else {
        serial_write("[VFS] Found existing mount point ");
        serial_write(name);
        serial_write("\n");
    }
    
    mount_point->fs = fs;
    mount_point->first_child = fs->root->first_child;
    
    vfs_node_t* child = mount_point->first_child;
    while (child) {
        child->parent = mount_point;
        child = child->next_sibling;
    }
    
    serial_write("[VFS] Mounted ");
    serial_write(fs->name);
    serial_write(" at ");
    serial_write(path);
    serial_write("\n");
    
    return true;
}

vfs_node_t* vfs_get_root(void) {
    return vfs_root;
}

vfs_node_t* vfs_find_child(vfs_node_t* parent, const char* name) {
    if (!parent || parent->type != VFS_DIRECTORY) return NULL;
    
    vfs_node_t* child = parent->first_child;
    while (child) {
        if (strcmp(child->name, name) == 0) return child;
        child = child->next_sibling;
    }
    return NULL;
}

vfs_node_t* vfs_resolve_path(const char* path) {
    if (!vfs_root || path[0] != '/') return NULL;
    if (strcmp(path, "/") == 0) return vfs_root;
    
    vfs_node_t* current = vfs_root;
    char token[MAX_FILENAME];
    int token_idx = 0;
    int i = 1;
    
    while (path[i]) {
        if (path[i] == '/') {
            if (token_idx > 0) {
                token[token_idx] = '\0';
                current = vfs_find_child(current, token);
                if (!current) return NULL;
                token_idx = 0;
            }
            i++;
        } else {
            if (token_idx < MAX_FILENAME - 1) {
                token[token_idx++] = path[i];
            }
            i++;
        }
    }
    
    if (token_idx > 0) {
        token[token_idx] = '\0';
        current = vfs_find_child(current, token);
    }
    
    return current;
}

vfs_node_t* vfs_create_file(vfs_node_t* parent, const char* name) {
    if (!parent || !parent->fs || !parent->fs->ops->create_file) return NULL;
    return parent->fs->ops->create_file(parent, name);
}

vfs_node_t* vfs_create_directory(vfs_node_t* parent, const char* name) {
    if (!parent || !parent->fs || !parent->fs->ops->create_dir) return NULL;
    return parent->fs->ops->create_dir(parent, name);
}

bool vfs_delete(vfs_node_t* node) {
    if (!node || !node->fs || !node->fs->ops->delete) return false;
    return node->fs->ops->delete(node);
}

bool vfs_read(vfs_node_t* node, void* buffer, size_t offset, size_t size) {
    if (!node || !node->fs || !node->fs->ops->read) return false;
    return node->fs->ops->read(node, buffer, offset, size);
}

bool vfs_write(vfs_node_t* node, const void* data, size_t offset, size_t size) {
    if (!node || !node->fs || !node->fs->ops->write) return false;
    return node->fs->ops->write(node, data, offset, size);
}

void vfs_list_directory(vfs_node_t* dir) {
    if (!dir || dir->type != VFS_DIRECTORY) {
        vga_write("Not a directory\n");
        return;
    }
    
    vfs_node_t* child = dir->first_child;
    if (!child) {
        vga_write("(empty)\n");
        return;
    }
    
    while (child) {
        if (child->type == VFS_DIRECTORY) {
            vga_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            vga_write(child->name);
            vga_write("/");
        } else {
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            vga_write(child->name);
        }
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vga_write("\n");
        child = child->next_sibling;
    }
}

vfs_node_t* vfs_open(const char* path, int flags) {
    (void)flags;
    return vfs_resolve_path(path);
}

void vfs_close(vfs_node_t* node) {
    (void)node;
}