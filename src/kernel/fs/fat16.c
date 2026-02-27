#include <fs/fat16.h>
#include <drivers/ata.h>
#include <mm/heap.h>
#include <drivers/serial.h>
#include <kernel/vga.h>
#include <kernel/string.h>

#define FAT16_SECTOR_SIZE 512
#define FAT16_MAX_FILENAME 11

typedef struct {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed)) fat16_boot_sector_t;

typedef struct {
    uint8_t name[11];
    uint8_t attr;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat16_dir_entry_t;

typedef struct {
    uint32_t start_lba;
    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;
    uint16_t root_entries;
    uint8_t sectors_per_cluster;
    uint16_t sectors_per_fat;
    uint8_t num_fats;
} fat16_data_t;

static fat16_data_t* fs_data = NULL;

static uint32_t fat16_root_dir_sector_count(void) {
    if (!fs_data) return 0;
    return (((uint32_t)fs_data->root_entries * sizeof(fat16_dir_entry_t)) + FAT16_SECTOR_SIZE - 1) / FAT16_SECTOR_SIZE;
}

static void fat16_name_to_83(const char* filename, char* name83) {
    int i = 0, j = 0;
    for (i = 0; i < 11; i++) name83[i] = ' ';
    i = 0;
    while (filename[j] && filename[j] != '.' && i < 8) {
        name83[i++] = (filename[j] >= 'a' && filename[j] <= 'z') ? filename[j] - 32 : filename[j];
        j++;
    }
    if (filename[j] == '.') {
        j++;
        i = 8;
        while (filename[j] && i < 11) {
            name83[i++] = (filename[j] >= 'a' && filename[j] <= 'z') ? filename[j] - 32 : filename[j];
            j++;
        }
    }
}

static uint16_t fat16_read_fat_entry(uint16_t cluster) {
    if (!fs_data || cluster < 2) return 0;
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fs_data->fat_start + (fat_offset / FAT16_SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % FAT16_SECTOR_SIZE;
    uint8_t sector[FAT16_SECTOR_SIZE];
    if (!ata_read_sector(fat_sector, sector)) return 0;
    return *(uint16_t*)&sector[entry_offset];
}

static bool fat16_write_fat_entry(uint16_t cluster, uint16_t value) {
    if (!fs_data || cluster < 2) return false;
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fs_data->fat_start + (fat_offset / FAT16_SECTOR_SIZE);
    uint32_t entry_offset = fat_offset % FAT16_SECTOR_SIZE;
    uint8_t sector[FAT16_SECTOR_SIZE];
    for (int f = 0; f < fs_data->num_fats; f++) {
        if (!ata_read_sector(fat_sector + f * fs_data->sectors_per_fat, sector)) return false;
        *(uint16_t*)&sector[entry_offset] = value;
        if (!ata_write_sector(fat_sector + f * fs_data->sectors_per_fat, sector)) return false;
    }
    return true;
}

static uint16_t fat16_allocate_cluster(void) {
    for (uint16_t i = 2; i < 0xFFF0; i++) {
        if (fat16_read_fat_entry(i) == 0) {
            if (!fat16_write_fat_entry(i, 0xFFFF)) return 0;
            uint8_t zero_sector[FAT16_SECTOR_SIZE] = {0};
            uint32_t sector = fs_data->data_start + (i - 2) * fs_data->sectors_per_cluster;
            for (int s = 0; s < fs_data->sectors_per_cluster; s++) {
                if (!ata_write_sector(sector + s, zero_sector)) return 0;
            }
            return i;
        }
    }
    return 0;
}

static bool fat16_release_cluster_chain(uint16_t start_cluster) {
    if (start_cluster < 2) return true;

    uint16_t cluster = start_cluster;
    for (uint32_t guard = 0; guard < 0x10000u; guard++) {
        if (cluster < 2 || cluster >= 0xFFF8) {
            return true;
        }

        uint16_t next = fat16_read_fat_entry(cluster);
        if (!fat16_write_fat_entry(cluster, 0)) {
            return false;
        }
        if (next == cluster) {
            return true;
        }
        cluster = next;
    }

    return false;
}

static bool fat16_directory_cluster_empty(uint16_t start_cluster) {
    if (start_cluster < 2) return true;

    uint16_t cluster = start_cluster;
    for (uint32_t guard = 0; guard < 0x10000u; guard++) {
        if (cluster < 2 || cluster >= 0xFFF8) {
            return true;
        }

        uint32_t first_sector = fs_data->data_start + (cluster - 2) * fs_data->sectors_per_cluster;
        for (uint8_t s = 0; s < fs_data->sectors_per_cluster; s++) {
            uint8_t sector[FAT16_SECTOR_SIZE];
            if (!ata_read_sector(first_sector + s, sector)) {
                return false;
            }

            fat16_dir_entry_t* entries = (fat16_dir_entry_t*)sector;
            int entries_per_sector = FAT16_SECTOR_SIZE / (int)sizeof(fat16_dir_entry_t);
            for (int i = 0; i < entries_per_sector; i++) {
                fat16_dir_entry_t* e = &entries[i];
                if (e->name[0] == 0x00) {
                    return true;
                }
                if ((uint8_t)e->name[0] == 0xE5) continue;
                if (e->attr == 0x0F) continue;
                if (e->name[0] == '.') continue;
                return false;
            }
        }

        uint16_t next = fat16_read_fat_entry(cluster);
        if (next == cluster) return false;
        cluster = next;
    }

    return false;
}

static bool fat16_read(vfs_node_t* node, void* buffer, size_t offset, size_t size) {
    if (!node || !buffer || !fs_data || node->type != VFS_FILE) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    if (offset >= node->size) {
        return false;
    }

    size_t bytes_to_read = size;
    size_t max_read = node->size - offset;
    if (bytes_to_read > max_read) {
        bytes_to_read = max_read;
    }

    uint16_t cluster = (uint16_t)(uint64_t)node->fs_data;
    if (cluster < 2) {
        return false;
    }

    uint32_t cluster_size = (uint32_t)fs_data->sectors_per_cluster * FAT16_SECTOR_SIZE;
    size_t skip_clusters = offset / cluster_size;
    size_t intra_cluster_offset = offset % cluster_size;

    while (skip_clusters > 0 && cluster >= 2 && cluster < 0xFFF8) {
        cluster = fat16_read_fat_entry(cluster);
        skip_clusters--;
    }

    if (cluster < 2 || cluster >= 0xFFF8) {
        return false;
    }

    size_t bytes_read = 0;
    uint8_t* buf = (uint8_t*)buffer;

    while (bytes_read < bytes_to_read && cluster >= 2 && cluster < 0xFFF8) {
        uint32_t cluster_first_sector = fs_data->data_start + (cluster - 2) * fs_data->sectors_per_cluster;

        for (uint8_t s = 0; s < fs_data->sectors_per_cluster && bytes_read < bytes_to_read; s++) {
            uint8_t sector_buf[FAT16_SECTOR_SIZE];
            if (!ata_read_sector(cluster_first_sector + s, sector_buf)) {
                return false;
            }

            size_t sector_start = (size_t)s * FAT16_SECTOR_SIZE;
            if (intra_cluster_offset >= sector_start + FAT16_SECTOR_SIZE) {
                continue;
            }

            size_t sector_offset = 0;
            if (intra_cluster_offset > sector_start) {
                sector_offset = intra_cluster_offset - sector_start;
            }

            size_t copy_size = FAT16_SECTOR_SIZE - sector_offset;
            if (copy_size > bytes_to_read - bytes_read) {
                copy_size = bytes_to_read - bytes_read;
            }

            memcpy(buf + bytes_read, sector_buf + sector_offset, copy_size);
            bytes_read += copy_size;
        }

        intra_cluster_offset = 0;
        cluster = fat16_read_fat_entry(cluster);
    }

    return bytes_read == bytes_to_read;
}

static bool fat16_write(vfs_node_t* node, const void* data, size_t offset, size_t size) {
    if (!node || !data || !fs_data || node->type != VFS_FILE) return false;
    if (size == 0) return true;

    size_t end_offset = offset + size;
    if (end_offset < offset) return false;

    uint32_t cluster_size = (uint32_t)fs_data->sectors_per_cluster * FAT16_SECTOR_SIZE;
    if (cluster_size == 0) return false;

    uint16_t cluster = (uint16_t)(uint64_t)node->fs_data;
    if (cluster < 2) {
        cluster = fat16_allocate_cluster();
        if (cluster == 0) return false;
        node->fs_data = (void*)(uint64_t)cluster;
    }

    size_t skip_clusters = offset / cluster_size;
    size_t intra_cluster_offset = offset % cluster_size;
    uint16_t current = cluster;

    while (skip_clusters > 0) {
        uint16_t next = fat16_read_fat_entry(current);
        if (next < 2 || next >= 0xFFF8) {
            next = fat16_allocate_cluster();
            if (next == 0) return false;
            if (!fat16_write_fat_entry(current, next)) return false;
        }
        current = next;
        skip_clusters--;
    }

    size_t bytes_written = 0;
    const uint8_t* src = (const uint8_t*)data;

    while (bytes_written < size) {
        uint32_t cluster_first_sector = fs_data->data_start + (current - 2) * fs_data->sectors_per_cluster;

        for (uint8_t s = 0; s < fs_data->sectors_per_cluster && bytes_written < size; s++) {
            size_t sector_start = (size_t)s * FAT16_SECTOR_SIZE;
            if (intra_cluster_offset >= sector_start + FAT16_SECTOR_SIZE) {
                continue;
            }

            size_t sector_offset = 0;
            if (intra_cluster_offset > sector_start) {
                sector_offset = intra_cluster_offset - sector_start;
            }

            size_t copy_size = FAT16_SECTOR_SIZE - sector_offset;
            if (copy_size > size - bytes_written) {
                copy_size = size - bytes_written;
            }

            uint8_t sector_buf[FAT16_SECTOR_SIZE];
            if (!ata_read_sector(cluster_first_sector + s, sector_buf)) return false;
            memcpy(sector_buf + sector_offset, src + bytes_written, copy_size);
            if (!ata_write_sector(cluster_first_sector + s, sector_buf)) return false;
            bytes_written += copy_size;
        }

        intra_cluster_offset = 0;
        if (bytes_written >= size) break;

        uint16_t next = fat16_read_fat_entry(current);
        if (next < 2 || next >= 0xFFF8) {
            next = fat16_allocate_cluster();
            if (next == 0) return false;
            if (!fat16_write_fat_entry(current, next)) return false;
        }
        current = next;
    }

    if (end_offset > node->size) node->size = end_offset;
    return true;
}

static vfs_node_t* fat16_create_file(vfs_node_t* parent, const char* name) {
    if (!parent || !name || !fs_data || parent->type != VFS_DIRECTORY || parent->parent != NULL) return NULL;

    char name83[11];
    fat16_name_to_83(name, name83);

    uint8_t root_sector[FAT16_SECTOR_SIZE];
    fat16_dir_entry_t* entries = NULL;
    const uint32_t root_sector_count = fat16_root_dir_sector_count();
    const int entries_per_sector = FAT16_SECTOR_SIZE / (int)sizeof(fat16_dir_entry_t);
    uint32_t free_sector = 0;
    int free_slot = -1;

    for (uint32_t sector_idx = 0; sector_idx < root_sector_count; sector_idx++) {
        if (!ata_read_sector(fs_data->root_start + sector_idx, root_sector)) return NULL;
        entries = (fat16_dir_entry_t*)root_sector;
        for (int i = 0; i < entries_per_sector; i++) {
            if (entries[i].name[0] != 0x00 && (uint8_t)entries[i].name[0] != 0xE5 && entries[i].attr != 0x0F) {
                if (memcmp(entries[i].name, name83, 11) == 0) {
                    return NULL;
                }
            }
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                free_slot = i;
                free_sector = sector_idx;
                goto found_slot;
            }
        }
    }

found_slot:
    if (free_slot == -1) return NULL;

    memset(&entries[free_slot], 0, sizeof(fat16_dir_entry_t));
    for (int i = 0; i < 11; i++) entries[free_slot].name[i] = name83[i];
    entries[free_slot].attr = 0x20;
    entries[free_slot].cluster_low = 0;
    entries[free_slot].file_size = 0;

    if (!ata_write_sector(fs_data->root_start + free_sector, root_sector)) return NULL;

    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
    int name_idx = 0;
    for (int i = 0; i < 11; i++) { if (name83[i] != ' ') { node->name[name_idx++] = name83[i]; if (i == 7 && name83[8] != ' ') node->name[name_idx++] = '.'; } }
    node->name[name_idx] = '\0';
    node->type = VFS_FILE;
    node->size = 0;
    node->fs_data = NULL;
    node->fs = parent->fs;
    node->parent = parent;
    node->first_child = NULL;
    node->next_sibling = parent->first_child;
    parent->first_child = node;
    return node;
}

static vfs_node_t* fat16_create_dir(vfs_node_t* parent, const char* name) {
    if (!parent || !name || !fs_data || parent->type != VFS_DIRECTORY || parent->parent != NULL) return NULL;

    char name83[11];
    fat16_name_to_83(name, name83);

    uint8_t root_sector[FAT16_SECTOR_SIZE];
    fat16_dir_entry_t* entries = NULL;
    const uint32_t root_sector_count = fat16_root_dir_sector_count();
    const int entries_per_sector = FAT16_SECTOR_SIZE / (int)sizeof(fat16_dir_entry_t);
    uint32_t free_sector = 0;
    int free_slot = -1;

    for (uint32_t sector_idx = 0; sector_idx < root_sector_count; sector_idx++) {
        if (!ata_read_sector(fs_data->root_start + sector_idx, root_sector)) return NULL;
        entries = (fat16_dir_entry_t*)root_sector;
        for (int i = 0; i < entries_per_sector; i++) {
            if (entries[i].name[0] != 0x00 && (uint8_t)entries[i].name[0] != 0xE5 && entries[i].attr != 0x0F) {
                if (memcmp(entries[i].name, name83, 11) == 0) {
                    return NULL;
                }
            }
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                free_slot = i;
                free_sector = sector_idx;
                goto found_slot;
            }
        }
    }

found_slot:
    if (free_slot == -1) return NULL;

    uint16_t cluster = fat16_allocate_cluster();
    if (cluster == 0) return NULL;

    memset(&entries[free_slot], 0, sizeof(fat16_dir_entry_t));
    for (int i = 0; i < 11; i++) entries[free_slot].name[i] = name83[i];
    entries[free_slot].attr = 0x10;
    entries[free_slot].cluster_low = cluster;
    entries[free_slot].file_size = 0;

    if (!ata_write_sector(fs_data->root_start + free_sector, root_sector)) {
        fat16_write_fat_entry(cluster, 0);
        return NULL;
    }

    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) {
        fat16_write_fat_entry(cluster, 0);
        return NULL;
    }
    int name_idx = 0;
    for (int i = 0; i < 11; i++) { if (name83[i] != ' ') node->name[name_idx++] = name83[i]; }
    node->name[name_idx] = '\0';
    node->type = VFS_DIRECTORY;
    node->size = 0;
    node->fs_data = (void*)(uint64_t)cluster;
    node->fs = parent->fs;
    node->parent = parent;
    node->first_child = NULL;
    node->next_sibling = parent->first_child;
    parent->first_child = node;
    return node;
}

static bool fat16_delete(vfs_node_t* node) {
    if (!node || !node->parent || !fs_data) return false;
    if (node->parent->parent != NULL) return false;
    if (node->type == VFS_DIRECTORY && node->first_child) return false;

    char name83[11];
    fat16_name_to_83(node->name, name83);

    uint8_t root_sector[FAT16_SECTOR_SIZE];
    fat16_dir_entry_t* entries = NULL;
    int found_slot = -1;
    uint32_t found_sector = 0;
    bool found_entry = false;
    bool end_of_dir = false;

    const uint32_t root_sector_count = fat16_root_dir_sector_count();
    const int entries_per_sector = FAT16_SECTOR_SIZE / (int)sizeof(fat16_dir_entry_t);

    for (uint32_t sector_idx = 0; sector_idx < root_sector_count && !end_of_dir; sector_idx++) {
        if (!ata_read_sector(fs_data->root_start + sector_idx, root_sector)) return false;
        entries = (fat16_dir_entry_t*)root_sector;
        for (int i = 0; i < entries_per_sector; i++) {
            if (entries[i].name[0] == 0x00) {
                end_of_dir = true;
                break;
            }
            if ((uint8_t)entries[i].name[0] == 0xE5 || entries[i].attr == 0x0F) {
                continue;
            }
            if (memcmp(entries[i].name, name83, 11) == 0) {
                found_slot = i;
                found_sector = sector_idx;
                found_entry = true;
                break;
            }
        }
        if (found_entry) {
            break;
        }
    }

    if (!found_entry || found_slot < 0 || !entries) return false;

    fat16_dir_entry_t* entry = &entries[found_slot];
    bool is_dir = (entry->attr & 0x10u) != 0u;
    uint16_t start_cluster = entry->cluster_low;

    if (is_dir && !fat16_directory_cluster_empty(start_cluster)) {
        return false;
    }

    if (start_cluster >= 2 && !fat16_release_cluster_chain(start_cluster)) {
        return false;
    }

    entry->name[0] = 0xE5;
    entry->attr = 0;
    entry->cluster_low = 0;
    entry->cluster_high = 0;
    entry->file_size = 0;

    if (!ata_write_sector(fs_data->root_start + found_sector, root_sector)) {
        return false;
    }

    vfs_node_t* parent = node->parent;
    if (parent->first_child == node) {
        parent->first_child = node->next_sibling;
    } else {
        vfs_node_t* prev = parent->first_child;
        while (prev && prev->next_sibling != node) {
            prev = prev->next_sibling;
        }
        if (!prev) {
            return false;
        }
        prev->next_sibling = node->next_sibling;
    }

    node->parent = NULL;
    node->next_sibling = NULL;
    node->first_child = NULL;
    kfree(node);
    return true;
}

static vfs_operations_t fat16_ops = { .create_file = fat16_create_file, .create_dir = fat16_create_dir, .delete = fat16_delete, .read = fat16_read, .write = fat16_write };

bool fat16_mount(uint32_t start_lba) {
    uint8_t boot_sector[FAT16_SECTOR_SIZE];
    if (!ata_read_sector(start_lba, boot_sector)) { serial_write("[FAT16] Failed to read boot sector\n"); return false; }
    fat16_boot_sector_t* bs = (fat16_boot_sector_t*)boot_sector;
    if (bs->bytes_per_sector != FAT16_SECTOR_SIZE) { serial_write("[FAT16] Invalid sector size\n"); return false; }
    if (bs->sectors_per_cluster == 0 || bs->num_fats == 0 || bs->sectors_per_fat == 0 || bs->root_entries == 0) {
        serial_write("[FAT16] Invalid geometry in boot sector\n");
        return false;
    }
    fs_data = (fat16_data_t*)kmalloc(sizeof(fat16_data_t));
    if (!fs_data) return false;
    fs_data->start_lba = start_lba;
    fs_data->fat_start = start_lba + bs->reserved_sectors;
    fs_data->root_start = fs_data->fat_start + (bs->num_fats * bs->sectors_per_fat);
    fs_data->data_start = fs_data->root_start + (((uint32_t)bs->root_entries * sizeof(fat16_dir_entry_t) + FAT16_SECTOR_SIZE - 1) / FAT16_SECTOR_SIZE);
    fs_data->root_entries = bs->root_entries;
    fs_data->sectors_per_cluster = bs->sectors_per_cluster;
    fs_data->sectors_per_fat = bs->sectors_per_fat;
    fs_data->num_fats = bs->num_fats;
    serial_write("[FAT16] Mounted successfully\n");
    return true;
}

vfs_filesystem_t* fat16_create(void) {
    if (!fs_data) return NULL;
    vfs_filesystem_t* fs = (vfs_filesystem_t*)kmalloc(sizeof(vfs_filesystem_t));
    if (!fs) return NULL;
    vfs_node_t* root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!root) { kfree(fs); return NULL; }
    root->name[0] = '/'; root->name[1] = '\0'; root->type = VFS_DIRECTORY; root->size = 0; root->fs_data = NULL; root->fs = fs; root->parent = NULL; root->first_child = NULL; root->next_sibling = NULL;

    uint8_t root_sector[FAT16_SECTOR_SIZE];
    const uint32_t root_sector_count = fat16_root_dir_sector_count();
    const int entries_per_sector = FAT16_SECTOR_SIZE / (int)sizeof(fat16_dir_entry_t);
    vfs_node_t* last_child = NULL;

    bool end_of_dir = false;
    for (uint32_t sector_idx = 0; sector_idx < root_sector_count && !end_of_dir; sector_idx++) {
        if (!ata_read_sector(fs_data->root_start + sector_idx, root_sector)) {
            serial_write("[FAT16] Failed to read root directory sector\n");
            continue;
        }

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)root_sector;
        for (int i = 0; i < entries_per_sector; i++) {
            fat16_dir_entry_t* entry = &entries[i];
            if (entry->name[0] == 0x00) {
                end_of_dir = true;
                break;
            }
            if ((uint8_t)entry->name[0] == 0xE5) continue;
            if (entry->attr == 0x0F) continue;

            vfs_node_t* child = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
            if (!child) continue;

            int name_idx = 0;
            for (int j = 0; j < 8 && entry->name[j] != ' '; j++) child->name[name_idx++] = entry->name[j];
            if (entry->name[8] != ' ') {
                child->name[name_idx++] = '.';
                for (int j = 8; j < 11 && entry->name[j] != ' '; j++) child->name[name_idx++] = entry->name[j];
            }

            child->name[name_idx] = '\0';
            child->type = (entry->attr & 0x10) ? VFS_DIRECTORY : VFS_FILE;
            child->size = entry->file_size;
            child->fs_data = (void*)(uint64_t)entry->cluster_low;
            child->fs = fs;
            child->parent = root;
            child->first_child = NULL;
            child->next_sibling = NULL;

            if (!last_child) root->first_child = child; else last_child->next_sibling = child;
            last_child = child;
        }
    }
    fs->name = "fat16";
    fs->ops = &fat16_ops;
    fs->root = root;
    fs->private_data = fs_data;
    serial_write("[FAT16] Filesystem created\n");
    return fs;
}
