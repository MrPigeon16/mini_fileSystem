#include <linux/limits.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <strings.h>
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
    unsigned int type;                    /* 1=file, 2=dir */
    unsigned int size;                    /* bytes in file/dir */
    unsigned int block_p;

    // currnet file will not need more then 1 block
    //unsigned int blocks[SFS_DIRECT_PTRS]; /* direct block pointers - a file may need more then one block */
    
    //uint16_t mode;                    /* optional */
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

int init_bitmaps(const SuperBlock *sb)
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
 *  inode 1 = root directory
 */
int init_inode_table(const SuperBlock *sb)
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

    table[0].type = 2;                  /* directory */
    table[0].size = sizeof(dir) * 2;    /* . and .. */
    table[0].block_p = sb->data_block_start; // CHANGE IF DATA CAN TAKE MORE THEN ONE BLOCK!

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
int write_root_directory_block(const SuperBlock *sb)
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

/*
 * 
 * Find the first free inode (0)
 * And return the index
 *
 * */

int alloc_inode(const SuperBlock *sb)
{
    int fd = open("/home/magshimim/disk.img", O_RDWR);
    if (fd == -1)
        return -1;

    uint8_t bitmap[SFS_BLOCK_SIZE];

    // Read inode bitmap block
    lseek(fd, sb->inode_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
    if (read(fd, bitmap, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE) {
        close(fd);
        return -1;
    }

    // Scan for a free inode
    for (int i = 0; i < sb->inode_count; i++) {

        int byte_index = i / 8;
        int bit_index  = i % 8;

        uint8_t mask = 1 << bit_index;

        // If bit is 0 - inode is free
        if ((bitmap[byte_index] & mask) == 0) {

            // inode as allocated
            bitmap[byte_index] |= mask;

            // Write bitmap back to disk
            lseek(fd, sb->inode_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
            write(fd, bitmap, SFS_BLOCK_SIZE);

            close(fd);
            return i;   
        }
    }

    close(fd);
    return -1;
}



int free_inode(const SuperBlock *sb, int inode_index)
{
    int fd = open("/home/magshimim/disk.img", O_RDWR);
    if (fd == -1)
        return -1;

    uint8_t bitmap[SFS_BLOCK_SIZE];

    // Read inode bitmap block
    lseek(fd, sb->inode_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
    if (read(fd, bitmap, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE) {
        close(fd);
        return -1;
    }

    // bit position
    int byte_index = inode_index / 8;
    int bit_index  = inode_index % 8;

    uint8_t mask = 1 << bit_index;

    // already free
    if ((bitmap[byte_index] & mask) == 0) {
        close(fd);
        return 0;  
    }

    // free the inode
    bitmap[byte_index] &= ~mask;

    // Write bitmap back to disk
    lseek(fd, sb->inode_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
    write(fd, bitmap, SFS_BLOCK_SIZE);

    close(fd);
    return 1; 
}

int alloc_block(SuperBlock* sb)
{
    int fd = open("/home/magshimim/disk.img", O_RDWR);
    if (fd == -1) return -1;

    uint32_t bitmap[SFS_BLOCK_SIZE];

    lseek(fd, sb->data_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
    read(fd, bitmap, SFS_BLOCK_SIZE);
    
    for(int i = 0; i < SFS_BLOCK_AMOUT; ++i)
    {
        uint8_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        uint8_t mask = 1 << bit_index;
        if((bitmap[byte_index] & mask) == 0)
        {
            bitmap[byte_index] |= mask;
            lseek(fd, sb->data_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
            write(fd, bitmap, SFS_BLOCK_SIZE);
            close(fd);
            return i;
        }        
    }
    close(fd);
    return -1;
}


int free_block(const SuperBlock *sb, unsigned int b_index)
{
    int fd = open("/home/magshimim/disk.img", O_RDWR);
    if (fd == -1)
        return -1;

    uint8_t bitmap[SFS_BLOCK_SIZE];

    // Read data bitmap block
    lseek(fd, sb->data_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
    if (read(fd, bitmap, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE) {
        close(fd);
        return -1;
    }

    int byte_index = b_index / 8;
    int bit_index  = b_index % 8;

    uint8_t mask = 1 << bit_index;

    if ((bitmap[byte_index] & mask) == 0) {
        close(fd);
        return 0;
    }

    bitmap[byte_index] &= ~mask;

    lseek(fd, sb->data_bitmap_block * SFS_BLOCK_SIZE, SEEK_SET);
    write(fd, bitmap, SFS_BLOCK_SIZE);

    close(fd);
    return 1;
}

int write_to_block(int fd, const char* data, unsigned int block_number)
{
    off_t offset = block_number * SFS_BLOCK_SIZE;
    lseek(fd, offset, SEEK_SET);
    write(fd, data, SFS_BLOCK_SIZE);
    return 1; 
}

int read_block(int fd, unsigned int block_number, void *buffer)
{
    off_t offset = (off_t)block_number * SFS_BLOCK_SIZE;

    if (lseek(fd, offset, SEEK_SET) == -1)
        return -1;

    if (read(fd, buffer, SFS_BLOCK_SIZE) != SFS_BLOCK_SIZE)
        return -1;

    return 0;
}

int write_inode(const SuperBlock *sb, unsigned int number, const inode_t *inode)
{
    if (number >= sb->inode_count)
        return -1;

    int fd = open("/home/magshimim/disk.img", O_RDWR);
    if (fd == -1)
        return -1;

    off_t offset = (off_t)sb->inode_table_block * SFS_BLOCK_SIZE + (off_t)number * sizeof(inode_t);

    if (lseek(fd, offset, SEEK_SET) == -1) {
        close(fd);
        return -1;
    }

    if (write(fd, inode, sizeof(inode_t)) != (ssize_t)sizeof(inode_t)) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}


int read_inode(const SuperBlock *sb, unsigned int number, inode_t *inode)
{
    if (number >= sb->inode_count)
        return -1;

    int fd = open("/home/magshimim/disk.img", O_RDONLY);
    if (fd == -1)
        return -1;

    off_t offset = (off_t)sb->inode_table_block * SFS_BLOCK_SIZE + (off_t)number * sizeof(inode_t);

    if (lseek(fd, offset, SEEK_SET) == -1) {
        close(fd);
        return -1;
    }

    if (read(fd, inode, sizeof(inode_t)) != (ssize_t)sizeof(inode_t)) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int create_file(const SuperBlock* sb ,const char* fileName)
{
    int inode_bitmap = alloc_inode(sb);
    int data_bitmap = alloc_block(sb);

}


void mkfs(SuperBlock* sb)
{
    printf("writeSuperBlock: %d\n", writeSuperBlock(sb));
    printf("init_bitmaps: %d\n", init_bitmaps(sb));
    printf("init_inode_table: %d\n", init_inode_table(sb));
    printf("write_root_directory: %d\n", write_root_directory_block(sb));
}

int main(void)
{
    
    SuperBlock sb = init_superBlock();
    
    mkfs(&sb);
    alloc_inode(&sb);
    return 0;
}
