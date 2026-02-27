#include <fs/vfs.h>
#include <mm/heap.h>
#include <kernel/vga.h>
#include <drivers/serial.h>
#include <kernel/string.h>

static vfs_node_t* vfs_root = NULL;

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static bool streq_nocase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (upper_ascii(*a) != upper_ascii(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static vfs_node_t* vfs_find_child_nocase(vfs_node_t* parent, const char* name) {
    if (!parent || parent->type != VFS_DIRECTORY || !name) return NULL;

    vfs_node_t* child = parent->first_child;
    while (child) {
        if (streq_nocase(child->name, name)) return child;
        child = child->next_sibling;
    }
    return NULL;
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
    if (!fs || !fs->root || !path || !vfs_root) {
        serial_write("[VFS] ERROR: Invalid mount_at parameters\n");
        return false;
    }
    
    if (strcmp(path, "/") == 0) {
        return vfs_mount(fs, "/");
    }

    if (path[0] != '/') {
        serial_write("[VFS] ERROR: mount path must be absolute\n");
        return false;
    }

    char parent_path[MAX_PATH];
    char name[MAX_FILENAME];
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }
    if (len <= 1) {
        serial_write("[VFS] ERROR: Invalid mount path\n");
        return false;
    }

    size_t slash = len - 1;
    while (slash > 0 && path[slash] != '/') {
        slash--;
    }
    if (path[slash] != '/') {
        serial_write("[VFS] ERROR: Invalid mount path format\n");
        return false;
    }

    size_t name_len = len - slash - 1;
    if (name_len == 0 || name_len >= sizeof(name)) {
        serial_write("[VFS] ERROR: Invalid mount name\n");
        return false;
    }

    memcpy(name, path + slash + 1, name_len);
    name[name_len] = '\0';
    if ((name_len == 1 && name[0] == '.') ||
        (name_len == 2 && name[0] == '.' && name[1] == '.')) {
        serial_write("[VFS] ERROR: Invalid mount name\n");
        return false;
    }

    if (slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        if (slash >= sizeof(parent_path)) {
            serial_write("[VFS] ERROR: mount path too long\n");
            return false;
        }
        memcpy(parent_path, path, slash);
        parent_path[slash] = '\0';
    }

    vfs_node_t* parent = vfs_resolve_path(parent_path);
    if (!parent || parent->type != VFS_DIRECTORY) {
        serial_write("[VFS] ERROR: Mount parent not found\n");
        return false;
    }

    vfs_node_t* mount_point = vfs_find_child(parent, name);
    if (!mount_point) {
        mount_point = vfs_find_child_nocase(parent, name);
    }

    if (!mount_point) {
        mount_point = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
        if (!mount_point) return false;

        memcpy(mount_point->name, name, name_len + 1u);
        mount_point->type = VFS_DIRECTORY;
        mount_point->size = 0;
        mount_point->fs_data = NULL;
        mount_point->parent = parent;
        mount_point->fs = parent->fs;
        mount_point->first_child = NULL;
        mount_point->next_sibling = parent->first_child;
        parent->first_child = mount_point;
    } else {
        if (mount_point->type != VFS_DIRECTORY) {
            serial_write("[VFS] ERROR: Mount point is not a directory\n");
            return false;
        }
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
    if (!parent || parent->type != VFS_DIRECTORY || !name) return NULL;
    
    vfs_node_t* child = parent->first_child;
    while (child) {
        if (strcmp(child->name, name) == 0) return child;
        child = child->next_sibling;
    }
    return NULL;
}

vfs_node_t* vfs_resolve_path(const char* path) {
    if (!vfs_root || !path || path[0] != '/') return NULL;
    if (strcmp(path, "/") == 0) return vfs_root;

    vfs_node_t* current = vfs_root;
    char token[MAX_FILENAME];
    size_t i = 1;

    while (path[i] != '\0') {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        size_t token_idx = 0;
        while (path[i] != '\0' && path[i] != '/') {
            if (token_idx + 1 >= MAX_FILENAME) {
                return NULL;
            }
            token[token_idx++] = path[i++];
        }
        token[token_idx] = '\0';

        if (token_idx == 1 && token[0] == '.') {
            continue;
        }

        if (token_idx == 2 && token[0] == '.' && token[1] == '.') {
            if (current->parent) {
                current = current->parent;
            }
            continue;
        }

        vfs_node_t* next = vfs_find_child(current, token);
        if (!next) {
            next = vfs_find_child_nocase(current, token);
        }
        if (!next) {
            return NULL;
        }
        current = next;
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
