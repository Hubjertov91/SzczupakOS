#include <kernel/fat16.h>
#include <drivers/ata.h>
#include <kernel/heap.h>
#include <kernel/serial.h>
#include <kernel/vga.h>

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
            fat16_write_fat_entry(i, 0xFFFF);
            uint8_t zero_sector[FAT16_SECTOR_SIZE] = {0};
            uint32_t sector = fs_data->data_start + (i - 2) * fs_data->sectors_per_cluster;
            for (int s = 0; s < fs_data->sectors_per_cluster; s++) ata_write_sector(sector + s, zero_sector);
            return i;
        }
    }
    return 0;
}

static bool fat16_read(vfs_node_t* node, void* buffer, size_t offset, size_t size) {
    serial_write("[FAT16] Read request: size=");
    serial_write_dec(size);
    serial_write(" offset=");
    serial_write_dec(offset);
    serial_write(" file_size=");
    serial_write_dec(node->size);
    serial_write("\n");
    
    if (!node || !fs_data || node->type != VFS_FILE) {
        serial_write("[FAT16] Invalid params\n");
        return false;
    }
    
    uint16_t cluster = (uint16_t)(uint64_t)node->fs_data;
    serial_write("[FAT16] Start cluster: ");
    serial_write_dec(cluster);
    serial_write("\n");
    
    if (cluster == 0) {
        serial_write("[FAT16] Cluster is 0!\n");
        return false;
    }
    
    size_t bytes_read = 0;
    uint8_t* buf = (uint8_t*)buffer;
    
    while (bytes_read < size && cluster < 0xFFF8) {
        uint32_t cluster_size = fs_data->sectors_per_cluster * FAT16_SECTOR_SIZE;
        uint32_t sector = fs_data->data_start + (cluster - 2) * fs_data->sectors_per_cluster;
        
        serial_write("[FAT16] Reading cluster ");
        serial_write_dec(cluster);
        serial_write(" at sector ");
        serial_write_dec(sector);
        serial_write("\n");
        
        for (uint8_t s = 0; s < fs_data->sectors_per_cluster && bytes_read < size; s++) {
            uint8_t sector_buf[FAT16_SECTOR_SIZE];
            if (!ata_read_sector(sector + s, sector_buf)) {
                serial_write("[FAT16] Read sector failed\n");
                return false;
            }
            
            size_t sector_offset = (offset > cluster_size * (cluster - 2) + s * FAT16_SECTOR_SIZE) ? 
                                   offset - (cluster_size * (cluster - 2) + s * FAT16_SECTOR_SIZE) : 0;
            size_t copy_size = FAT16_SECTOR_SIZE - sector_offset;
            if (copy_size > size - bytes_read) copy_size = size - bytes_read;
            
            for (size_t i = 0; i < copy_size; i++) {
                buf[bytes_read++] = sector_buf[sector_offset + i];
            }
        }
        
        cluster = fat16_read_fat_entry(cluster);
    }
    
    serial_write("[FAT16] Read ");
    serial_write_dec(bytes_read);
    serial_write(" bytes. First 16 bytes: ");
    for(int i = 0; i < 16 && i < bytes_read; i++) {
        serial_write_hex(buf[i]);
        serial_write(" ");
    }
    serial_write("\n");
    
    return bytes_read > 0;
}

static bool fat16_write(vfs_node_t* node, const void* data, size_t offset, size_t size) {
    if (!node || !fs_data || node->type != VFS_FILE) return false;
    uint16_t cluster = (uint16_t)(uint64_t)node->fs_data;
    if (cluster == 0) {
        cluster = fat16_allocate_cluster();
        if (cluster == 0) return false;
        node->fs_data = (void*)(uint64_t)cluster;
    }
    size_t cluster_offset = offset / (fs_data->sectors_per_cluster * FAT16_SECTOR_SIZE);
    cluster += cluster_offset;
    uint32_t sector = fs_data->data_start + (cluster - 2) * fs_data->sectors_per_cluster;
    uint8_t sector_buf[FAT16_SECTOR_SIZE];
    if (offset > 0 || size < FAT16_SECTOR_SIZE) {
        if (!ata_read_sector(sector, sector_buf)) return false;
    }
    for (size_t i = 0; i < size && (offset + i) < FAT16_SECTOR_SIZE; i++) sector_buf[offset + i] = ((uint8_t*)data)[i];
    if (!ata_write_sector(sector, sector_buf)) return false;
    if (offset + size > node->size) node->size = offset + size;
    return true;
}

static vfs_node_t* fat16_create_file(vfs_node_t* parent, const char* name) {
    if (!parent || !fs_data) return NULL;
    uint8_t root_sector[FAT16_SECTOR_SIZE];
    if (!ata_read_sector(fs_data->root_start, root_sector)) return NULL;
    fat16_dir_entry_t* entries = (fat16_dir_entry_t*)root_sector;
    int free_slot = -1;
    for (int i = 0; i < fs_data->root_entries; i++) {
        if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) { free_slot = i; break; }
    }
    if (free_slot == -1) return NULL;
    char name83[11];
    fat16_name_to_83(name, name83);
    for (int i = 0; i < 11; i++) entries[free_slot].name[i] = name83[i];
    entries[free_slot].attr = 0x20;
    entries[free_slot].cluster_low = 0;
    entries[free_slot].file_size = 0;
    if (!ata_write_sector(fs_data->root_start, root_sector)) return NULL;
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
    serial_write("[FAT16] Created file: "); serial_write(node->name); serial_write("\n");
    return node;
}

static vfs_node_t* fat16_create_dir(vfs_node_t* parent, const char* name) {
    if (!parent || !fs_data) return NULL;
    uint16_t cluster = fat16_allocate_cluster();
    if (cluster == 0) return NULL;
    uint8_t root_sector[FAT16_SECTOR_SIZE];
    if (!ata_read_sector(fs_data->root_start, root_sector)) return NULL;
    fat16_dir_entry_t* entries = (fat16_dir_entry_t*)root_sector;
    int free_slot = -1;
    for (int i = 0; i < fs_data->root_entries; i++) { if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) { free_slot = i; break; } }
    if (free_slot == -1) return NULL;
    char name83[11];
    fat16_name_to_83(name, name83);
    for (int i = 0; i < 11; i++) entries[free_slot].name[i] = name83[i];
    entries[free_slot].attr = 0x10;
    entries[free_slot].cluster_low = cluster;
    entries[free_slot].file_size = 0;
    if (!ata_write_sector(fs_data->root_start, root_sector)) return NULL;
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) return NULL;
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
    serial_write("[FAT16] Created directory: "); serial_write(node->name); serial_write("\n");
    return node;
}

static bool fat16_delete(vfs_node_t* node) { serial_write("[FAT16] delete not implemented\n"); return false; }

static vfs_operations_t fat16_ops = { .create_file = fat16_create_file, .create_dir = fat16_create_dir, .delete = fat16_delete, .read = fat16_read, .write = fat16_write };

bool fat16_mount(uint32_t start_lba) {
    uint8_t boot_sector[FAT16_SECTOR_SIZE];
    if (!ata_read_sector(start_lba, boot_sector)) { serial_write("[FAT16] Failed to read boot sector\n"); return false; }
    fat16_boot_sector_t* bs = (fat16_boot_sector_t*)boot_sector;
    if (bs->bytes_per_sector != FAT16_SECTOR_SIZE) { serial_write("[FAT16] Invalid sector size\n"); return false; }
    fs_data = (fat16_data_t*)kmalloc(sizeof(fat16_data_t));
    if (!fs_data) return false;
    fs_data->start_lba = start_lba;
    fs_data->fat_start = start_lba + bs->reserved_sectors;
    fs_data->root_start = fs_data->fat_start + (bs->num_fats * bs->sectors_per_fat);
    fs_data->data_start = fs_data->root_start + ((bs->root_entries * 32) / FAT16_SECTOR_SIZE);
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
    ata_read_sector(fs_data->root_start, root_sector);
    fat16_dir_entry_t* entries = (fat16_dir_entry_t*)root_sector;
    vfs_node_t* last_child = NULL;
    for (int i = 0; i < fs_data->root_entries; i++) {
        if (entries[i].name[0] == 0x00) break;
        if ((uint8_t)entries[i].name[0] == 0xE5) continue;
        if (entries[i].attr == 0x0F) continue;
        vfs_node_t* child = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
        if (!child) continue;
        int name_idx = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) child->name[name_idx++] = entries[i].name[j];
        if (entries[i].name[8] != ' ') { child->name[name_idx++] = '.'; for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) child->name[name_idx++] = entries[i].name[j]; }
        child->name[name_idx] = '\0';
        child->type = (entries[i].attr & 0x10) ? VFS_DIRECTORY : VFS_FILE;
        child->size = entries[i].file_size;
        child->fs_data = (void*)(uint64_t)entries[i].cluster_low;
        child->fs = fs;
        child->parent = root;
        child->first_child = NULL;
        child->next_sibling = NULL;
        if (!last_child) root->first_child = child; else last_child->next_sibling = child;
        last_child = child;
    }
    fs->name = "fat16";
    fs->ops = &fat16_ops;
    fs->root = root;
    fs->private_data = fs_data;
    serial_write("[FAT16] Filesystem created\n");
    return fs;
}