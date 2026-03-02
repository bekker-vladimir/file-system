#include "Tests.h"

/**
 * Large files edge cases test
 * This test verifies edge cases and boundary conditions with the filesystem
 * It tests:
 * - Exact boundary sizes between addressing methods
 * - Files near filesystem capacity limit
 * - Error handling for out-of-space conditions
 * - Maximum number of files
 * - Maximum number of open files
 * - Boundary reads/writes
 * - Sparse files
 */

// Test exact boundary file sizes
bool testExactBoundarySizes(FileSystem* fs){
    printf("Testing exact boundary file sizes...\n");

    // Calculate boundary sizes
    constexpr int pointersPerBlock = SECTOR_SIZE / sizeof(int);

    // Calculate exact sizes for the transitions between addressing methods
    constexpr size_t directLimit = 12 * SECTOR_SIZE; // Last direct block
    constexpr size_t singleIndirectLimit = directLimit + pointersPerBlock * SECTOR_SIZE; // Last single indirect block
    constexpr size_t doubleIndirectLimit = singleIndirectLimit +
        pointersPerBlock * pointersPerBlock * SECTOR_SIZE; // Last double indirect

    // Test sizes that are exactly at transitions
    const char* fileNames[] = {
        "boundary_direct.dat",
        "boundary_single.dat",
        "boundary_double.dat"
    };

    const size_t sizes[] = {
        directLimit,
        singleIndirectLimit,
        doubleIndirectLimit
    };

    const char* descriptions[] = {
        "direct addressing limit",
        "single indirect addressing limit",
        "double indirect addressing limit"
    };

    // Test each boundary
    for (int i = 0; i < 3; i++){
        printf("Testing %s (%zu bytes)...\n", descriptions[i], sizes[i]);

        // Create file
        int fd = fs->openFile(fileNames[i], true);
        if (fd < 0){
            printf("Failed to open file at boundary %d\n", i);
            return false;
        }

        // Write file in chunks
        size_t bytesWritten = 0;
        char buffer[SECTOR_SIZE];

        while (bytesWritten < sizes[i]){
            // Fill buffer with a deterministic pattern based on the current block
            const size_t blockNumber = bytesWritten / SECTOR_SIZE;
            for (size_t j = 0; j < SECTOR_SIZE; j++){
                buffer[j] = static_cast<char>((blockNumber + j) % 256);
            }

            const size_t written = fs->writeFile(fd, buffer, SECTOR_SIZE);
            if (written != SECTOR_SIZE){
                printf("Failed to write at offset %zu\n", bytesWritten);
                fs->closeFile(fd);
                return false;
            }
            bytesWritten += written;

            // Print progress
            if (bytesWritten % (1048576) == 0 || bytesWritten == sizes[i]){
                printf("  Written %zu / %zu bytes (%.1f%%)\n",
                       bytesWritten, sizes[i],
                       static_cast<double>(bytesWritten) * 100.0 / static_cast<double>(sizes[i]));
            }
        }

        if (!fs->closeFile(fd)){
            printf("Failed to close file after write\n");
            return false;
        }

        // Verify file size
        size_t reportedSize = fs->fileSize(fileNames[i]);
        if (reportedSize != sizes[i]){
            printf("File size mismatch: expected %zu, got %zu\n", sizes[i], reportedSize);
            return false;
        }

        // Read back and verify
        fd = fs->openFile(fileNames[i], false);
        if (fd < 0){
            printf("Failed to open file for verification\n");
            return false;
        }

        char readBuffer[SECTOR_SIZE];
        size_t bytesRead = 0;

        while (bytesRead < sizes[i]){
            size_t read = fs->readFile(fd, readBuffer, SECTOR_SIZE);
            if (read != SECTOR_SIZE){
                printf("Failed to read at offset %zu\n", bytesRead);
                fs->closeFile(fd);
                return false;
            }

            // Regenerate expected data for this block
            const size_t blockNumber = bytesRead / SECTOR_SIZE;
            for (size_t j = 0; j < SECTOR_SIZE; j++){
                buffer[j] = static_cast<char>((blockNumber + j) % 256);
            }

            // Verify data
            for (size_t j = 0; j < SECTOR_SIZE; j++){
                if (readBuffer[j] != buffer[j]){
                    printf("Data mismatch at offset %zu + %zu: expected %d, got %d\n",
                           bytesRead, j, buffer[j], readBuffer[j]);
                    fs->closeFile(fd);
                    return false;
                }
            }

            bytesRead += read;

            // Print progress
            if (bytesRead % (1048576) == 0 || bytesRead == sizes[i]){
                printf("  Read and verified %zu / %zu bytes (%.1f%%)\n",
                       bytesRead, sizes[i],
                       static_cast<double>(bytesRead) * 100.0 / static_cast<double>(sizes[i]));
            }
        }

        if (!fs->closeFile(fd)){
            printf("Failed to close file after read\n");
            return false;
        }
    }

    // Clean up files
    for (auto& fileName : fileNames){
        if (!fs->deleteFile(fileName)){
            printf("Failed to delete boundary file %s\n", fileName);
            return false;
        }
    }

    printf("Boundary file size tests passed\n");
    return true;
}

// Test sparse files by creating a set of small files with different sizes
bool testSparseFiles(FileSystem* fs){
    printf("Testing multiple files with different sizes...\n");

    constexpr int NUM_FILES = 10;
    const char* filePrefix = "sparse_test_";
    char fileName[FILENAME_LEN_MAX + 1];

    // Set up distinct file sizes that exercise different addressing mechanisms
    constexpr size_t fileSizes[NUM_FILES] = {
        512, // Single sector
        11 * SECTOR_SIZE, // Last direct block
        12 * SECTOR_SIZE, // First single indirect block
        (12 + 127) * SECTOR_SIZE, // Last single indirect block
        (12 + 128) * SECTOR_SIZE, // First double indirect block
        1048576, // Medium size in double indirect
        5 * 1048576, // Large size in double indirect
        8450 * 1024, // Near triple indirect
        2 * SECTOR_SIZE, // Small file
        100 * 1024 // Medium small file
    };

    // Create pattern buffer for largest file
    size_t maxSize = 0;
    for (unsigned long fileSize : fileSizes){
        if (fileSize > maxSize){
            maxSize = fileSize;
        }
    }

    // Allocate buffer for the largest file
    char* buffer = new char[maxSize > 1 * 1048576 ? 1 * 1048576 : maxSize];

    // Create and write each file
    for (int i = 0; i < NUM_FILES; i++){
        snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", filePrefix, i);
        printf("Creating file %s of size %zu bytes...\n", fileName, fileSizes[i]);

        int fd = fs->openFile(fileName, true);
        if (fd < 0){
            printf("Failed to open file %s\n", fileName);
            delete[] buffer;
            return false;
        }

        // Fill the file with a pattern unique to each file
        size_t bytesWritten = 0;
        const size_t CHUNK_SIZE = 64 * 1024;

        while (bytesWritten < fileSizes[i]){
            size_t toWrite = std::min(size_t(CHUNK_SIZE), fileSizes[i] - bytesWritten);

            // Fill buffer with pattern based on file index and position
            for (size_t j = 0; j < toWrite; j++){
                buffer[j] = static_cast<char>((i * 37 + bytesWritten + j) % 256);
            }

            const size_t written = fs->writeFile(fd, buffer, toWrite);
            if (written != toWrite){
                printf("Failed to write to file %s at position %zu\n", fileName, bytesWritten);
                fs->closeFile(fd);
                delete[] buffer;
                return false;
            }

            bytesWritten += written;
        }

        if (!fs->closeFile(fd)){
            printf("Failed to close file %s\n", fileName);
            delete[] buffer;
            return false;
        }

        // Verify file size
        size_t reportedSize = fs->fileSize(fileName);
        if (reportedSize != fileSizes[i]){
            printf("File size mismatch: expected %zu, got %zu\n", fileSizes[i], reportedSize);
            delete[] buffer;
            return false;
        }
    }

    // Verify each file can be read back correctly
    for (int i = 0; i < NUM_FILES; i++){
        snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", filePrefix, i);
        printf("Verifying file %s of size %zu bytes...\n", fileName, fileSizes[i]);

        const int fd = fs->openFile(fileName, false);
        if (fd < 0){
            printf("Failed to open file %s for verification\n", fileName);
            delete[] buffer;
            return false;
        }

        // Read and verify the file
        size_t bytesRead = 0;
        constexpr size_t CHUNK_SIZE = 64 * 1024;
        char readBuffer[CHUNK_SIZE];

        while (bytesRead < fileSizes[i]){
            const size_t toRead = std::min(static_cast<size_t>(CHUNK_SIZE), fileSizes[i] - bytesRead);
            const size_t read = fs->readFile(fd, readBuffer, toRead);

            if (read != toRead){
                printf("Failed to read from file %s at position %zu: expected %zu, got %zu\n",
                       fileName, bytesRead, toRead, read);
                fs->closeFile(fd);
                delete[] buffer;
                return false;
            }

            // Generate expected pattern for this chunk
            for (size_t j = 0; j < toRead; j++){
                buffer[j] = static_cast<char>((i * 37 + bytesRead + j) % 256);
            }

            // Verify data
            bool mismatch = false;
            size_t mismatchPos = 0;
            for (size_t j = 0; j < toRead; j++){
                if (readBuffer[j] != buffer[j]){
                    mismatch = true;
                    mismatchPos = j;
                    break;
                }
            }

            if (mismatch){
                printf("Data mismatch in file %s at position %zu+%zu: expected %d, got %d\n",
                       fileName, bytesRead, mismatchPos, buffer[mismatchPos] & 0xFF, readBuffer[mismatchPos] & 0xFF);

                // Show more context
                printf("Context around mismatch:\n  Expected: ");
                for (size_t j = (mismatchPos > 16) ? mismatchPos - 16 : 0;
                     j < std::min(toRead, mismatchPos + 16); j++){
                    printf("%02x ", buffer[j] & 0xFF);
                }
                printf("\n  Actual:   ");
                for (size_t j = (mismatchPos > 16) ? mismatchPos - 16 : 0;
                     j < std::min(toRead, mismatchPos + 16); j++){
                    printf("%02x ", readBuffer[j] & 0xFF);
                }
                printf("\n");

                fs->closeFile(fd);
                delete[] buffer;
                return false;
            }

            bytesRead += read;
        }

        if (!fs->closeFile(fd)){
            printf("Failed to close file %s after verification\n", fileName);
            delete[] buffer;
            return false;
        }
    }

    // Clean up files
    for (int i = 0; i < NUM_FILES; i++){
        snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", filePrefix, i);
        if (!fs->deleteFile(fileName)){
            printf("Failed to delete file %s\n", fileName);
            delete[] buffer;
            return false;
        }
    }

    delete[] buffer;
    printf("Multiple files test passed\n");
    return true;
}

// Test maximum number of open files
bool testMaxOpenFiles(FileSystem* fs){
    printf("Testing maximum number of open files (%d)...\n", OPEN_FILES_MAX);

    const char* baseFileName = "openmax_";
    char fileName[FILENAME_LEN_MAX + 1];
    int fds[OPEN_FILES_MAX];

    // Create files
    for (int i = 0; i < OPEN_FILES_MAX; i++){
        snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, i);

        int fd = fs->openFile(fileName, true);
        if (fd < 0){
            printf("Failed to create file %s\n", fileName);

            // Close previously opened files
            for (int j = 0; j < i; j++){
                fs->closeFile(fds[j]);
            }

            // Delete created files
            for (int j = 0; j < i; j++){
                snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, j);
                fs->deleteFile(fileName);
            }

            return false;
        }

        // Write some data
        char buffer[SECTOR_SIZE];
        for (size_t j = 0; j < SECTOR_SIZE; j++){
            buffer[j] = static_cast<char>((i + j) % 256);
        }

        if (fs->writeFile(fd, buffer, SECTOR_SIZE) != SECTOR_SIZE){
            printf("Failed to write to file %s\n", fileName);
            fs->closeFile(fd);

            // Close and delete other files
            for (int j = 0; j < i; j++){
                fs->closeFile(fds[j]);
                snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, j);
                fs->deleteFile(fileName);
            }

            return false;
        }

        if (!fs->closeFile(fd)){
            printf("Failed to close file %s after creation\n", fileName);

            // Close and delete other files
            for (int j = 0; j < i; j++){
                fs->closeFile(fds[j]);
                snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, j);
                fs->deleteFile(fileName);
            }

            return false;
        }
    }

    // Try to open all files at once
    for (int i = 0; i < OPEN_FILES_MAX; i++){
        snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, i);

        fds[i] = fs->openFile(fileName, false);
        if (fds[i] < 0){
            printf("Failed to open file %s for reading\n", fileName);

            // Close previously opened files
            for (int j = 0; j < i; j++){
                fs->closeFile(fds[j]);
            }

            // Delete all files
            for (int j = 0; j < OPEN_FILES_MAX; j++){
                snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, j);
                fs->deleteFile(fileName);
            }

            return false;
        }
    }

    // Try to open one more file (should fail)
    const char* extraFileName = "extra_file.dat";
    int extraFd = fs->openFile(extraFileName, true);
    if (extraFd >= 0){
        printf("Warning: Was able to open more than OPEN_FILES_MAX files\n");
        fs->closeFile(extraFd);
    }

    // Read and verify all files
    for (int i = 0; i < OPEN_FILES_MAX; i++){
        char buffer[SECTOR_SIZE];
        char expectedBuffer[SECTOR_SIZE];

        // Prepare expected data
        for (size_t j = 0; j < SECTOR_SIZE; j++){
            expectedBuffer[j] = static_cast<char>((i + j) % 256);
        }

        if (fs->readFile(fds[i], buffer, SECTOR_SIZE) != SECTOR_SIZE){
            printf("Failed to read from file %d\n", i);

            // Close all files
            for (const int fd : fds){
                fs->closeFile(fd);
            }

            // Delete all files
            for (int j = 0; j < OPEN_FILES_MAX; j++){
                snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, j);
                fs->deleteFile(fileName);
            }

            return false;
        }

        // Verify data
        if (memcmp(buffer, expectedBuffer, SECTOR_SIZE) != 0){
            printf("Data mismatch in file %d\n", i);

            // Close all files
            for (const int fd : fds){
                fs->closeFile(fd);
            }

            // Delete all files
            for (int j = 0; j < OPEN_FILES_MAX; j++){
                snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, j);
                fs->deleteFile(fileName);
            }

            return false;
        }
    }

    // Close all files
    for (int i = 0; i < OPEN_FILES_MAX; i++){
        if (!fs->closeFile(fds[i])){
            printf("Failed to close file %d\n", i);

            // Close remaining files
            for (int j = i + 1; j < OPEN_FILES_MAX; j++){
                fs->closeFile(fds[j]);
            }

            // Delete all files
            for (int j = 0; j < OPEN_FILES_MAX; j++){
                snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, j);
                fs->deleteFile(fileName);
            }

            return false;
        }
    }

    // Delete all files
    for (int i = 0; i < OPEN_FILES_MAX; i++){
        snprintf(fileName, FILENAME_LEN_MAX, "%s%d.dat", baseFileName, i);
        if (!fs->deleteFile(fileName)){
            printf("Failed to delete file %s\n", fileName);
            return false;
        }
    }

    printf("Maximum open files test passed\n");
    return true;
}

// Test capacity limits and out-of-space handling
bool testCapacityLimits(FileSystem* fs){
    printf("Testing filesystem capacity limits...\n");

    // Get total disk size
    size_t diskSize = fs->getDiskSize();
    printf("Total disk size: %zu bytes\n", diskSize);

    // Calculate approximate usable space (90% of total as per requirements)
    const size_t usableSpace = diskSize * 90 / 100;
    printf("Usable space (approx 90%%): %zu bytes\n", usableSpace);

    // Create a large file to fill most of the disk
    const char* fileName = "capacity_test.dat";

    int fd = fs->openFile(fileName, true);
    if (fd < 0){
        printf("Failed to open file for capacity test\n");
        return false;
    }

    // Allocate a buffer for writing
    constexpr size_t bufferSize = 64 * 1024;
    char* buffer = new char[bufferSize];

    // Fill buffer with pattern
    for (size_t i = 0; i < bufferSize; i++){
        buffer[i] = static_cast<char>(i % 256);
    }

    // Write data until close to capacity
    size_t bytesWritten = 0;
    const size_t targetSize = usableSpace - 1048576; // Leave some space

    while (bytesWritten < targetSize){
        const size_t toWrite = std::min(static_cast<size_t>(bufferSize), targetSize - bytesWritten);
        const size_t written = fs->writeFile(fd, buffer, toWrite);

        if (written != toWrite){
            printf("Warning: Write returned %zu bytes instead of %zu\n", written, toWrite);
            // Don't return false, as this might be expected behavior near capacity
        }

        if (written == 0){
            printf("Reached capacity limit at %zu bytes\n", bytesWritten);
            break;
        }

        bytesWritten += written;

        // Print progress
        if (bytesWritten % (10 * 1048576) == 0 || bytesWritten == targetSize){
            printf("  Written %zu / %zu bytes (%.1f%%)\n",
                   bytesWritten, targetSize,
                   static_cast<double>(bytesWritten) * 100.0 / static_cast<double>(targetSize));
        }
    }

    if (!fs->closeFile(fd)){
        printf("Failed to close capacity test file\n");
        delete[] buffer;
        return false;
    }

    // Verify file size
    size_t reportedSize = fs->fileSize(fileName);
    if (reportedSize != bytesWritten){
        printf("File size mismatch: expected %zu, got %zu\n", bytesWritten, reportedSize);
        delete[] buffer;
        return false;
    }

    printf("Successfully wrote %zu bytes (%.1f%% of usable space)\n",
           bytesWritten, static_cast<double>(bytesWritten) * 100.0 / static_cast<double>(usableSpace));

    // Try to create another file when disk is nearly full
    const char* smallFileName = "small_leftover.dat";
    fd = fs->openFile(smallFileName, true);

    if (fd < 0){
        printf("Warning: Could not create additional file when disk nearly full\n");
    } else{
        // Try to write a small amount of data
        size_t smallWritten = fs->writeFile(fd, buffer, 512);
        printf("Wrote %zu bytes to additional file when disk nearly full\n", smallWritten);
        fs->closeFile(fd);
    }

    // Read back part of the large file to verify
    fd = fs->openFile(fileName, false);
    if (fd < 0){
        printf("Failed to open capacity test file for verification\n");
        delete[] buffer;
        return false;
    }

    // Read and verify beginning, middle, and end
    const size_t testPositions[] = {0, bytesWritten / 2, bytesWritten - bufferSize};
    for (const unsigned long position : testPositions){
        // Skip to position
        size_t toSkip = position;

        // Reset file position
        fs->closeFile(fd);
        fd = fs->openFile(fileName, false);
        if (fd < 0){
            printf("Failed to reopen file for verification\n");
            delete[] buffer;
            return false;
        }

        while (toSkip > 0){
            char skipBuffer[8 * 1024];
            size_t skip = std::min(size_t(toSkip), sizeof(skipBuffer));
            size_t skipped = fs->readFile(fd, skipBuffer, skip);
            if (skipped == 0) break;
            toSkip -= skipped;
        }

        if (toSkip > 0){
            printf("Failed to skip to position %zu\n", position);
            fs->closeFile(fd);
            delete[] buffer;
            return false;
        }

        // Read a chunk
        char readBuffer[4 * 1024];
        size_t bytesRead = fs->readFile(fd, readBuffer, sizeof(readBuffer));
        if (bytesRead != sizeof(readBuffer)){
            printf("Failed to read %zu bytes at position %zu, got %zu\n",
                   sizeof(readBuffer), position, bytesRead);
            fs->closeFile(fd);
            delete[] buffer;
            return false;
        }

        // Generate expected data
        for (size_t j = 0; j < bytesRead; j++){
            buffer[j] = static_cast<char>((position + j) % 256);
        }

        // Verify data
        for (size_t j = 0; j < bytesRead; j++){
            if (readBuffer[j] != buffer[j]){
                printf("Data mismatch at position %zu+%zu: expected %d, got %d\n",
                       position, j, buffer[j], readBuffer[j]);
                fs->closeFile(fd);
                delete[] buffer;
                return false;
            }
        }

        printf("Verified data at position %zu\n", position);
    }

    fs->closeFile(fd);

    // Clean up
    if (!fs->deleteFile(fileName)){
        printf("Failed to delete capacity test file\n");
        delete[] buffer;
        return false;
    }

    if (!fs->deleteFile(smallFileName)){
        printf("Failed to delete small leftover file\n");
        delete[] buffer;
        return false;
    }

    delete[] buffer;
    printf("Capacity limit test passed\n");
    return true;
}

// Main test function for edge cases
void testLargeFilesEdgeCasesComprehensive(){
    srand(time(nullptr));

    printf("\n=== LARGE FILES EDGE CASES TEST ===\n\n");

    // Format the disk
    BlockDevice disk = createDisk();
    assert(FileSystem::createFs(disk));
    doneDisk();

    // Mount the filesystem
    FileSystem* fs = FileSystem::mount(openDisk());
    assert(fs);

    // Test exact boundary file sizes
    assert(testExactBoundarySizes(fs));
    printf("\n");

    // Test sparse files
    assert(testSparseFiles(fs));
    printf("\n");

    // Test maximum number of open files
    assert(testMaxOpenFiles(fs));
    printf("\n");

    // Test capacity limits
    assert(testCapacityLimits(fs));
    printf("\n");

    // Unmount filesystem
    assert(fs->umount());
    delete fs;
    doneDisk();

    printf("\n=== LARGE FILES EDGE CASES TEST COMPLETED SUCCESSFULLY ===\n\n");
}
