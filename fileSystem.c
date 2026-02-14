#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

/*
    Disk img: disk.img
    size:     64 MiB
    block:    4096 bytes
    blocks:   64 MiB / 4096 = 16384
*/

#define SFS_BLOCK_AMOUT  16384
#define SFS_DIRECT_PTRS  12
#define NAME_MAX         32
#define SFS_MAGIC        0x53465331u
#define SFS_VERSION      1u
#define SFS_BLOCK_SIZE   4096u
#define SFS_INODE_COUNT  1024

typedef struct inode {
    uint8_t  type;                    /* 1=file, 2=dir */
    uint32_t size;                    /* bytes in file/dir */
    uint32_t blocks[SFS_DIRECT_PTRS]; /* direct block pointers */
    uint16_t mode;                    /* optional */
} inode_t;

typedef struct {
    uint32_t inode;
    uint8_t  type;
    char     name[NAME_MAX];
} dir;

typedef struct SuperBlock {
    uint32_t magic;             // "SFS1"
    uint32_t version;           // 1
    uint32_t block_size;        // 4096
    uint32_t total_blocks;      // 16384
    uint32_t inode_count;       // 1024
    uint32_t inode_bitmap_block;
    uint32_t data_bitmap_block;
    uint32_t inode_table_block;
    uint32_t data_block_start;
    uint32_t root_inode;
} SuperBlock;

SuperBlock init_superBlock(void)
{
    SuperBlock superBlock = {0};

    superBlock.magic = SFS_MAGIC;
    superBlock.version = SFS_VERSION;
    superBlock.block_size = SFS_BLOCK_SIZE;
    superBlock.total_blocks = SFS_BLOCK_AMOUT;
    superBlock.inode_count = SFS_INODE_COUNT;

    superBlock.inode_bitmap_block = 1;
    superBlock.data_bitmap_block = 2;
    superBlock.inode_table_block = 3;

    uint32_t inode_bytes = SFS_INODE_COUNT * sizeof(inode_t);
    uint32_t inode_blocks = (inode_bytes + SFS_BLOCK_SIZE - 1) / SFS_BLOCK_SIZE;

    superBlock.data_block_start = superBlock.inode_table_block + inode_blocks;
    superBlock.root_inode  = 0;

    return superBlock;
}

static int writeSuperBlock(const SuperBlock *sb)
{
    int fd = open("/home/magshimim/disk.img", O_WRONLY);
    if (fd == -1)
        return 0;

    uint8_t block[SFS_BLOCK_SIZE] = {0};
    memcpy(block, sb, sizeof(SuperBlock));
    
    if (lseek(fd, 0, SEEK_SET) == -1) {
        close(fd);
        return 0;
    }

    if (write(fd, block, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE) {
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}

/*
 * init both bitmaps:
 * inode bitmap: inode 0 allocated
 * data bitmap:  first data block allocated
 */
static int init_bitmaps(const SuperBlock *sb)
{
    int fd = open("/home/magshimim/disk.img", O_WRONLY);
    if (fd == -1)
        return 0;

    uint8_t block[SFS_BLOCK_SIZE] = {0};

    /* inode bitmap */
    block[0] |= 1 << 0; /* inode 0 used */

    if (lseek(fd, (off_t)sb->inode_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET) == -1) {
        close(fd);
        return 0;
    }

    if (write(fd, block, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE) {
        close(fd);
        return 0;
    }
    
    /* data bitmap */
    memset(block, 0, SFS_BLOCK_SIZE);
    uint32_t b = sb->data_block_start;
    block[b / 8] |= (1 << (b % 8)); /* first data block used */

    if (lseek(fd, (off_t)sb->data_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET) == -1) {
        close(fd);
        return 0;
    }

    if (write(fd, block, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE) {
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}

/*
 *  init inode table:
 *  zero all inodes
 *  inode 0 = root directory
 */
static int init_inode_table(const SuperBlock *sb)
{
    int fd = open("/home/magshimim/disk.img", O_WRONLY);
    if (fd == -1)
        return 0;

    if (lseek(fd, (off_t)sb->inode_table_block * SFS_BLOCK_SIZE, SEEK_SET) == -1) {
        close(fd);
        return 0;
    }

    inode_t table[SFS_INODE_COUNT];
    memset(table, 0, sizeof(table));

    table[0].type      = 2;                  /* directory */
    table[0].size      = sizeof(dir) * 2;    /* . and .. */
    table[0].blocks[0] = sb->data_block_start;

    size_t bytes = sizeof(table);

    if (write(fd, table, bytes) != (ssize_t)bytes) {
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}

/*
 * Write root dir block at data_block_start:
 * entries:
 *    [0] .  - inode 0
 *    [1] .. - inode 1
 */
static int write_root_directory_block(const SuperBlock *sb)
{
    int fd = open("/home/magshimim/disk.img", O_WRONLY);
    if (fd == -1)
        return 0;

    uint8_t block[SFS_BLOCK_SIZE] = {0};
    dir *entries = (dir *)block;

    entries[0].inode = 0;
    entries[0].type  = 2;
    strcpy(entries[0].name, ".");

    entries[1].inode = 0;
    entries[1].type  = 2;
    strcpy(entries[1].name, "..");

    if (lseek(fd, (off_t)sb->data_block_start * SFS_BLOCK_SIZE, SEEK_SET) == -1) {
        close(fd);
        return 0;
    }

    if (write(fd, block, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE) {
        close(fd);
        return 0;
    }

    close(fd);
    return 1;
}

void mkfs(void)
{
    SuperBlock sb = init_superBlock();

    printf("writeSuperBlock: %d\n", writeSuperBlock(&sb));
    printf("init_bitmaps: %d\n", init_bitmaps(&sb));
    printf("init_inode_table: %d\n", init_inode_table(&sb));
    printf("write_root_directory: %d\n", write_root_directory_block(&sb));
}

int main(void)
{
    mkfs();
    return 0;
}
