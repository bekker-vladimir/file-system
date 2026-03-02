#pragma once
#include <functional>

/**
 * Filesystem size: min 8MiB, max 1GiB
 * Filename length: min 1B, max 28B
 * Sector size: 512B
 * Max open files: 8 at a time
 * At most one filesystem mounted at a time.
 * Max file size: < 1GiB
 * Max files in the filesystem: 128
 */


constexpr int FILENAME_LEN_MAX = 28;
constexpr int DIR_ENTRIES_MAX = 128;
constexpr int OPEN_FILES_MAX = 8;
constexpr int SECTOR_SIZE = 512;
constexpr int DEVICE_SIZE_MAX = 1024 * 1024 * 1024;
constexpr int DEVICE_SIZE_MIN = 8 * 1024 * 1024;

struct File{
    char m_FileName[FILENAME_LEN_MAX + 1];
    size_t m_FileSize;
};

struct BlockDevice{
    size_t m_Sectors{};
    std::function<size_t(size_t, void*, size_t)> m_Read;
    std::function<size_t(size_t, const void*, size_t)> m_Write;
};

class FileSystem{
public:
    FileSystem() : m_wasMounted(false), m_findPosition(0){
        memset(&m_superBlock, 0, sizeof(m_superBlock));
        memset(m_inodes, 0, sizeof(m_inodes));
        memset(m_openFiles, 0, sizeof(m_openFiles));
    }

    static bool createFs(const BlockDevice& dev);

    bool umount();

    static FileSystem* mount(const BlockDevice& dev);

    size_t fileSize(const char* fileName);

    int openFile(const char* fileName, bool writeMode);

    bool closeFile(int fd);

    size_t readFile(int fd, void* data, size_t len);

    size_t writeFile(int fd, const void* data, size_t len);

    bool deleteFile(const char* fileName);

    bool findFirst(File& file);

    bool findNext(File& file);

    size_t getDiskSize() const{ return m_blockDevice.m_Sectors * SECTOR_SIZE; }

private:
    // UFS = SuperBlock, Free space management structures, Table of i-nodes, Data blocks

    struct Inode{
        // hard links aren't used, file name can be stored right in the i-node structure
        char m_fileName[FILENAME_LEN_MAX + 1];
        size_t m_fileSize;

        int m_directAddresses[12];
        int m_singleIndirectAddress;
        int m_doubleIndirectAddress;
        int m_tripleIndirectAddress;

        bool m_inUse;
        int m_nextFreeInodeIndex;
    };

    struct DataBlock{
        int m_nextFreeBlockIndex;
        bool m_inUse;
        char m_data[512 - sizeof(bool) - sizeof(int)];
    };

    struct SuperBlock{
        int m_filesCount;

        int m_inodeCount;
        int m_inodesTableIndex;
        int m_firstFreeInodeIndex;

        int m_dataBlocksCount;
        int m_freeDataBlocksCount;
        int m_firstFreeBlockIndex;
        int m_dataBlocksIndex;
    };

    struct OpenFile{
        int m_inodeIndex;
        size_t m_position;
        bool m_writeMode;
        bool m_inUse;
    };

    static bool isValidFd(const int fd){ return fd >= 0 && fd <= 7; }

    int findFile(const char* fileName) const;

    int allocateInode();

    int allocateBlock();

    bool freeBlock(int blockIndex);

    int getBlockIndex(const Inode& inode, size_t position) const;

    bool setBlockIndex(Inode& inode, size_t position, int blockIndex);

    static constexpr int inodesPerSector = SECTOR_SIZE / sizeof(Inode);
    static constexpr int inodesTableSectors = (DIR_ENTRIES_MAX + inodesPerSector - 1) / inodesPerSector;

    BlockDevice m_blockDevice;
    SuperBlock m_superBlock{};
    Inode m_inodes[DIR_ENTRIES_MAX]{};
    OpenFile m_openFiles[OPEN_FILES_MAX]{};
    bool m_wasMounted;
    int m_findPosition;
};
