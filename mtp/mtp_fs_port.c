#include <stdio.h>
#include <string.h>
#include "mtp_fs_port.h"
#include "lfs.h"
#include "stm32f4xx_hal.h"

#define LFS_FLASH_START_ADDR 0x08020000

#define LFS_READ_SIZE       1
#define LFS_PROG_SIZE       1
#define LFS_BLOCK_SIZE      (32 * 1024)
#define LFS_PHYSICAL_SECTOR_SIZE  (128 * 1024)
#define LFS_LOGICAL_BLOCK_SIZE    (32 * 1024)
#define LFS_BLOCK_COUNT     (3*4)
#define LFS_CACHE_SIZE      256
#define LFS_LOOKAHEAD_SIZE  16
#define LFS_BLOCK_CYCLES    100
#define LFS_BUFFER_SIZE     16

static uint8_t read_buffer[LFS_CACHE_SIZE];
static uint8_t prog_buffer[LFS_CACHE_SIZE];
static uint8_t lookahead_buffer[LFS_CACHE_SIZE];
lfs_t _lfs;

#define LFS_FLASH_START_ADDR 0x08020000 
#define LFS_START_SECTOR     FLASH_SECTOR_5
__attribute__((aligned(4))) static uint8_t sector_backup_cache[3 * 32 * 1024];

static int lfs_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    uint32_t addr = (uint32_t)c->context + (block * c->block_size) + off;
    memcpy(buffer, (void*)addr, size);
    return LFS_ERR_OK;
}

static int lfs_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    uint32_t addr = (uint32_t)c->context + (block * c->block_size) + off;
    const uint8_t *p_buf = (const uint8_t *)buffer;
    
    HAL_FLASH_Unlock();
    
    for(lfs_size_t i = 0; i < size; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, p_buf[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            __enable_irq();
            return LFS_ERR_IO;
        }
    }
    
    HAL_FLASH_Lock();
    return LFS_ERR_OK;
}

static int lfs_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
    uint32_t physical_sector_idx = block / 4;  // 物理扇区索引: 0, 1, 2
    uint32_t sub_block_idx       = block % 4;  // 子块索引: 0, 1, 2, 3

    uint32_t physical_base_addr = (uint32_t)c->context + (physical_sector_idx * LFS_PHYSICAL_SECTOR_SIZE);

    // 2. 将同一个物理扇区内，除了当前要擦除的块以外的另外 3 个块（96KB），读入 RAM 缓存
    uint32_t cache_offset = 0;
    for (int i = 0; i < 4; i++) {
        if (i == sub_block_idx) continue; // 跳过当前准备擦除的块，它不需要抢救
        
        uint32_t neighbor_addr = physical_base_addr + (i * LFS_LOGICAL_BLOCK_SIZE);
        memcpy(&sector_backup_cache[cache_offset], (void*)neighbor_addr, LFS_LOGICAL_BLOCK_SIZE);
        cache_offset += LFS_LOGICAL_BLOCK_SIZE;
    }

    // --- 危险区开始：关闭中断，擦除 128KB ---
    __disable_irq();
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector = LFS_START_SECTOR + physical_sector_idx; 
    EraseInitStruct.NbSectors = 1;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
        HAL_FLASH_Lock();
        return LFS_ERR_IO;
    }

    // 3. 将缓存在 RAM 里的 3 个块（96KB），分别写回到它们原本的物理地址
    cache_offset = 0;
    uint32_t words_to_write = LFS_LOGICAL_BLOCK_SIZE / 4; // 每个逻辑块有多少个 32位(word)

    for (int i = 0; i < 4; i++) {
        if (i == sub_block_idx) continue; // 跳过刚才被擦掉的那个块，让它保持 0xFF
        
        uint32_t neighbor_addr = physical_base_addr + (i * LFS_LOGICAL_BLOCK_SIZE);
        uint32_t *p_cache_word = (uint32_t *)&sector_backup_cache[cache_offset];
        
        for (uint32_t j = 0; j < words_to_write; j++) {
            // 跳过空数据(0xFFFFFFFF)，极大缩短关中断和写 Flash 的时间
            if (p_cache_word[j] != 0xFFFFFFFF) {
                if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, neighbor_addr + (j * 4), p_cache_word[j]) != HAL_OK) {
                    HAL_FLASH_Lock();
                    __enable_irq();
                    return LFS_ERR_IO;
                }
            }
        }
        cache_offset += LFS_LOGICAL_BLOCK_SIZE;
    }

    HAL_FLASH_Lock();
    __enable_irq();

    return LFS_ERR_OK;
}

static int _sync(const struct lfs_config *c)
{
    UNUSED(c);
    return LFS_ERR_OK;
}
const struct lfs_config _lfs_config =
{
    .context = (void*)LFS_FLASH_START_ADDR,
    // block device operations
    .read  = lfs_flash_read,
    .prog  = lfs_flash_prog,
    .erase = lfs_flash_erase,
    .sync  = _sync,

    // block device configuration
    .read_size = LFS_READ_SIZE,
    .prog_size = LFS_PROG_SIZE,
    .block_size = LFS_BLOCK_SIZE,
    .block_count = LFS_BLOCK_COUNT,

    .cache_size = LFS_CACHE_SIZE,
    .lookahead_size = LFS_LOOKAHEAD_SIZE,
    .block_cycles = LFS_BLOCK_CYCLES,

    .read_buffer = read_buffer,
    .prog_buffer = prog_buffer,
    .lookahead_buffer = lookahead_buffer,
};

//--------------------------------------------------------------------+
// 文件描述符池 (嵌入式系统通常没有 Linux 那样的 malloc/fd 机制，所以用数组模拟)
//--------------------------------------------------------------------+
#define MAX_OPEN_FILES 4
#define MAX_OPEN_DIRS  8

static lfs_file_t mtp_files[MAX_OPEN_FILES];
static bool file_used[MAX_OPEN_FILES] = {false};

static lfs_dir_t mtp_dirs[MAX_OPEN_DIRS];
static bool dir_used[MAX_OPEN_DIRS] = {false};
static struct mtp_dirent current_dirent; // 用于 readdir 返回

#define README_CONTENT "TinyUSB MTP for CherryUSB\n"

void usbd_mtp_init(void) {
    int err = lfs_mount(&_lfs, &_lfs_config);
    if (err)
    {
        int usbd_mtp_format_store(void);
        usbd_mtp_format_store();
    }
}
//--------------------------------------------------------------------+
// 信息与通知类接口
//--------------------------------------------------------------------+

// 如果文件发生改变，可以在这里触发键盘刷新显示、播放音效等，暂时返回0即可
int usbd_mtp_notify_object_add(const char *path) { return 0; }
int usbd_mtp_notify_object_remove(const char *path) { return 0; }

// 告诉 MTP 电脑端，根目录叫什么
const char *usbd_mtp_fs_root_path(void) { 
    return "/"; 
}

// 告诉 MTP 电脑端，磁盘显示什么名字 (比如在 Windows 我的电脑里显示的盘符名称)
const char *usbd_mtp_fs_description(void) { 
    return "Keyboard Flash"; 
}

//--------------------------------------------------------------------+
// 目录操作接口
//--------------------------------------------------------------------+

int usbd_mtp_mkdir(const char *path) { 
    return lfs_mkdir(&_lfs, path); 
}

int usbd_mtp_rmdir(const char *path) { 
    return lfs_remove(&_lfs, path); 
}

int usbd_mtp_unlink(const char *path) { 
    return lfs_remove(&_lfs, path); 
}

MTP_DIR *usbd_mtp_opendir(const char *name) {
    // 找一个空闲的目录描述符
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (!dir_used[i]) {
            if (lfs_dir_open(&_lfs, &mtp_dirs[i], name) == 0) {
                dir_used[i] = true;
                return (MTP_DIR *)&mtp_dirs[i];
            }
            break;
        }
    }
    return NULL;
}

int usbd_mtp_closedir(MTP_DIR *d) {
    if (!d) return -1;
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (d == (MTP_DIR *)&mtp_dirs[i] && dir_used[i]) {
            lfs_dir_close(&_lfs, &mtp_dirs[i]);
            dir_used[i] = false;
            return 0;
        }
    }
    return -1;
}

struct mtp_dirent *usbd_mtp_readdir(MTP_DIR *d) {
    if (!d) return NULL;
    lfs_dir_t *dir = (lfs_dir_t *)d;
    struct lfs_info info;

    if (lfs_dir_read(&_lfs, dir, &info) > 0) {
        strncpy(current_dirent.d_name, info.name, sizeof(current_dirent.d_name) - 1);
        current_dirent.d_name[sizeof(current_dirent.d_name) - 1] = '\0';
        // 标记文件类型 (对外部调用者来说不需要严格对应 POSIX，通常只要能区分文件即可)
        current_dirent.d_type = (info.type == LFS_TYPE_DIR) ? 4 : 8; 
        return &current_dirent;
    }
    return NULL; // 读到末尾
}

//--------------------------------------------------------------------+
// 属性读取接口
//--------------------------------------------------------------------+

int usbd_mtp_stat(const char *file, mtp_stat_t *buf) {
    struct lfs_info info;
    int res = lfs_stat(&_lfs, file, &info);
    
    if (res >= 0) {
        buf->size = info.size;
        buf->is_dir = (info.type == LFS_TYPE_DIR);
        return 0;
    }
    return -1;
}

int usbd_mtp_statfs(const char *path, struct mtp_statfs *buf) {
    (void) path; // LittleFS 通常是单分区，不需要区分 path
    
    if (!_lfs.cfg) return -1;

    buf->f_bsize = _lfs.cfg->block_size;
    buf->f_blocks = _lfs.cfg->block_count;
    
    lfs_ssize_t allocated_blocks = lfs_fs_size(&_lfs);
    if (allocated_blocks >= 0) {
        buf->f_bfree = buf->f_blocks - allocated_blocks;
    } else {
        buf->f_bfree = 0;
    }
    return 0;
}

//--------------------------------------------------------------------+
// 读写操作接口
//--------------------------------------------------------------------+

int usbd_mtp_open(const char *path, uint8_t mode) {
    // 找一个空闲的文件描述符
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_used[i]) {
            int lfs_flags = (mode == 0) ? LFS_O_RDONLY : (LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
            
            if (lfs_file_open(&_lfs, &mtp_files[i], path, lfs_flags) == 0) {
                file_used[i] = true;
                return i; // 返回数组索引作为 FD (File Descriptor)
            }
            break;
        }
    }
    return -1; // 打开失败或池已满
}

int usbd_mtp_close(int fd) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_used[fd]) {
        lfs_file_close(&_lfs, &mtp_files[fd]);
        file_used[fd] = false;
        return 0;
    }
    return -1;
}

int usbd_mtp_read(int fd, void *buf, size_t len) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_used[fd]) {
        return lfs_file_read(&_lfs, &mtp_files[fd], buf, len);
    }
    return -1;
}

int usbd_mtp_write(int fd, const void *buf, size_t len) {
    if (fd >= 0 && fd < MAX_OPEN_FILES && file_used[fd]) {
        return lfs_file_write(&_lfs, &mtp_files[fd], buf, len);
    }
    return -1;
}

int usbd_mtp_rename(const char *oldpath, const char *newpath) {
    return lfs_rename(&_lfs, oldpath, newpath);
}

int usbd_mtp_format_store(void) {
    lfs_file_t lfs_file; 
    lfs_unmount(&_lfs);
    int err = lfs_format(&_lfs, &_lfs_config);
    if (err) return -1;
    lfs_mount(&_lfs, &_lfs_config);
    lfs_mkdir(&_lfs, "支持UTF-8");
    lfs_file_open(&_lfs, &lfs_file, "README.txt", LFS_O_WRONLY | LFS_O_CREAT);
    lfs_file_write(&_lfs, &lfs_file, README_CONTENT, sizeof(README_CONTENT));
    lfs_file_close(&_lfs, &lfs_file);
    
    return 0;
}

void usbd_dfu_reset(void) {
    NVIC_SystemReset();
}