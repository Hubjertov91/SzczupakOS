#include <fs/tmpfs.h>
#include <mm/heap.h>
#include <drivers/serial.h>

typedef struct tmpfs_data {
    void* data;
    size_t allocated_size;
} tmpfs_data_t;

static void strcpy_safe(char* dst, const char* src, size_t max) {
    size_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static vfs_node_t* tmpfs_create_file(vfs_node_t* parent, const char* name) {
    if (!parent || parent->type != VFS_DIRECTORY) return NULL;
    if (vfs_find_child(parent, name)) return NULL;
    
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    
    tmpfs_data_t* data = (tmpfs_data_t*)kmalloc(sizeof(tmpfs_data_t));
    if (!data) {
        kfree(node);
        return NULL;
    }
    
    data->data = NULL;
    data->allocated_size = 0;
    
    strcpy_safe(node->name, name, sizeof(node->name));
    node->type = VFS_FILE;
    node->size = 0;
    node->fs_data = data;
    node->fs = parent->fs;
    node->parent = parent;
    node->first_child = NULL;
    node->next_sibling = parent->first_child;
    parent->first_child = node;
    
    return node;
}

static vfs_node_t* tmpfs_create_dir(vfs_node_t* parent, const char* name) {
    if (!parent || parent->type != VFS_DIRECTORY) return NULL;
    if (vfs_find_child(parent, name)) return NULL;
    
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    
    strcpy_safe(node->name, name, sizeof(node->name));
    node->type = VFS_DIRECTORY;
    node->size = 0;
    node->fs_data = NULL;
    node->fs = parent->fs;
    node->parent = parent;
    node->first_child = NULL;
    node->next_sibling = parent->first_child;
    parent->first_child = node;
    
    return node;
}

static bool tmpfs_delete(vfs_node_t* node) {
    if (!node) return false;
    if (node->type == VFS_DIRECTORY && node->first_child) return false;
    
    vfs_node_t* parent = node->parent;
    if (!parent) return false;
    
    if (parent->first_child == node) {
        parent->first_child = node->next_sibling;
    } else {
        vfs_node_t* prev = parent->first_child;
        while (prev && prev->next_sibling != node) {
            prev = prev->next_sibling;
        }
        if (prev) prev->next_sibling = node->next_sibling;
    }
    
    if (node->fs_data) {
        tmpfs_data_t* data = (tmpfs_data_t*)node->fs_data;
        if (data->data) kfree(data->data);
        kfree(data);
    }
    
    kfree(node);
    return true;
}

static bool tmpfs_read(vfs_node_t* node, void* buffer, size_t offset, size_t size) {
    if (!node || node->type != VFS_FILE || !node->fs_data) return false;
    
    tmpfs_data_t* data = (tmpfs_data_t*)node->fs_data;
    if (!data->data || offset >= node->size) return false;
    
    size_t read_size = size;
    if (offset + read_size > node->size) {
        read_size = node->size - offset;
    }
    
    for (size_t i = 0; i < read_size; i++) {
        ((char*)buffer)[i] = ((char*)data->data)[offset + i];
    }
    
    return true;
}

static bool tmpfs_write(vfs_node_t* node, const void* write_data, size_t offset, size_t size) {
    if (!node || node->type != VFS_FILE || !node->fs_data) return false;
    
    tmpfs_data_t* data = (tmpfs_data_t*)node->fs_data;
    size_t needed_size = offset + size;
    
    if (needed_size > data->allocated_size) {
        size_t new_size = needed_size * 2;
        void* new_data = kmalloc(new_size);
        if (!new_data) return false;
        
        if (data->data) {
            for (size_t i = 0; i < node->size; i++) {
                ((char*)new_data)[i] = ((char*)data->data)[i];
            }
            kfree(data->data);
        }
        
        data->data = new_data;
        data->allocated_size = new_size;
    }
    
    for (size_t i = 0; i < size; i++) {
        ((char*)data->data)[offset + i] = ((char*)write_data)[i];
    }
    
    if (offset + size > node->size) {
        node->size = offset + size;
    }
    
    return true;
}

static vfs_operations_t tmpfs_ops = {
    .create_file = tmpfs_create_file,
    .create_dir = tmpfs_create_dir,
    .delete = tmpfs_delete,
    .read = tmpfs_read,
    .write = tmpfs_write
};

vfs_filesystem_t* tmpfs_create(void) {
    vfs_filesystem_t* fs = (vfs_filesystem_t*)kmalloc(sizeof(vfs_filesystem_t));
    if (!fs) return NULL;
    
    vfs_node_t* root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!root) {
        kfree(fs);
        return NULL;
    }
    
    root->name[0] = '/';
    root->name[1] = '\0';
    root->type = VFS_DIRECTORY;
    root->size = 0;
    root->fs_data = NULL;
    root->fs = fs;
    root->parent = NULL;
    root->first_child = NULL;
    root->next_sibling = NULL;
    
    fs->name = "tmpfs";
    fs->ops = &tmpfs_ops;
    fs->root = root;
    fs->private_data = NULL;
    
    serial_write("[tmpfs] Created\n");
    return fs;
}