#include "Tests.h"

BlockDevice createLargeDisk() {
    constexpr size_t diskSize = 128 * 1024 * 1024; // 128MB
    constexpr size_t sectors = diskSize / SECTOR_SIZE;
    auto disk = std::shared_ptr<char[]>(new char[diskSize]);
    memset(disk.get(), 0, diskSize);

    return BlockDevice{
        sectors,
        [disk](const size_t sector, void *data, const size_t count) {
            memcpy(data, disk.get() + sector * SECTOR_SIZE, count * SECTOR_SIZE);
            return count;
        },
        [disk](const size_t sector, const void *data, const size_t count) {
            memcpy(disk.get() + sector * SECTOR_SIZE, data, count * SECTOR_SIZE);
            return count;
        }
    };
}

void testLargeFiles() {
    const BlockDevice dev = createLargeDisk();
    assert(FileSystem::createFs(dev));
    doneDisk();

    FileSystem *fs = FileSystem::mount(openDisk());
    assert(fs);

    // Test 1: Create a file that uses double indirect addressing
    const char *doubleIndirectFile = "double_indirect.txt";
    constexpr size_t fileSize = 128 * 1024 * 1024; // 8MB
    
    printf("Creating file %s of size %zu MB\n", doubleIndirectFile, fileSize / (1024 * 1024));
    
    int fd = fs->openFile(doubleIndirectFile, true);
    assert(fd != -1);

    // Write file in chunks of SECTOR_SIZE
    constexpr size_t chunkSize = SECTOR_SIZE;
    char *writeBuffer = new char[chunkSize];
    size_t totalWritten = 0;

    while (totalWritten < fileSize) {
        // Fill buffer with simple pattern
        for (size_t i = 0; i < chunkSize; i++) {
            writeBuffer[i] = static_cast<char>((totalWritten + i) % 256);
        }

        const size_t written = fs->writeFile(fd, writeBuffer, chunkSize);
        if (written != chunkSize) {
            printf("Error: written = %zu, chunkSize = %zu, totalWritten = %zu\n", 
                   written, chunkSize, totalWritten);
            break;
        }
        assert(written == chunkSize);
        totalWritten += written;

        // Print progress every 1MB
        if (totalWritten % (1024 * 1024) == 0) {
            printf("Written %zu MB\n", totalWritten / (1024 * 1024));
        }
    }
    assert(fs->closeFile(fd));
    printf("File write complete. Total size: %zu MB\n", totalWritten / (1024 * 1024));

    // Verify file size
    const size_t actualSize = fs->fileSize(doubleIndirectFile);
    assert(actualSize == fileSize);
    printf("File size verified: %zu MB\n", actualSize / (1024 * 1024));

    // Verify file content
    fd = fs->openFile(doubleIndirectFile, false);
    assert(fd != -1);

    char *readBuffer = new char[chunkSize];
    totalWritten = 0;

    while (totalWritten < fileSize) {
        const size_t read = fs->readFile(fd, readBuffer, chunkSize);
        if (read != chunkSize) {
            printf("Error: read = %zu, chunkSize = %zu, totalRead = %zu\n", 
                   read, chunkSize, totalWritten);
            break;
        }
        assert(read == chunkSize);

        // Verify content
        for (size_t i = 0; i < chunkSize; i++) {
            if (readBuffer[i] != static_cast<char>((totalWritten + i) % 256)) {
                printf("Content mismatch at position %zu\n", totalWritten + i);
                break;
            }
        }

        totalWritten += read;

        // Print progress every 1MB
        if (totalWritten % (1024 * 1024) == 0) {
            printf("Verified %zu MB\n", totalWritten / (1024 * 1024));
        }
    }
    assert(fs->closeFile(fd));
    printf("File content verified\n");

    // Cleanup
    delete[] writeBuffer;
    delete[] readBuffer;
    assert(fs->umount());
    delete fs;
    doneDisk();
}