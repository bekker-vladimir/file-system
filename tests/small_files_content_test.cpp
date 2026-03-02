#include "Tests.h"

void testSmallFilesContent(){
    const BlockDevice dev = createDisk();
    assert(FileSystem::createFs(dev));
    doneDisk();

    FileSystem* fs = FileSystem::mount(openDisk());
    assert(fs);

    // Test 1: Write multiple small files with different sizes and verify their content
    const char* testFiles[] = {
        "file1.txt",
        "file2.txt",
        "file3.txt",
        "file4.txt"
    };
    constexpr size_t fileSizes[] = {
        1024, // 1KB
        2048, // 2KB
        3072, // 3KB
        4096 // 4KB
    };

    // Write files with specific patterns
    for (size_t i = 0; i < std::size(testFiles); i++){
        const int fd = fs->openFile(testFiles[i], true);
        assert(fd != -1);

        // Create a buffer with a specific pattern for each file
        char* writeBuffer = new char[fileSizes[i]];
        for (size_t j = 0; j < fileSizes[i]; j++){
            writeBuffer[j] = static_cast<char>((i + j) % 256);
        }

        const size_t written = fs->writeFile(fd, writeBuffer, fileSizes[i]);
        assert(written == fileSizes[i]);
        assert(fs->closeFile(fd));
        delete[] writeBuffer;
    }

    // Verify file sizes
    File fileInfo{};
    bool found = fs->findFirst(fileInfo);
    size_t foundCount = 0;
    while (found){
        bool sizeMatch = false;
        for (size_t i = 0; i < std::size(fileSizes); i++){
            if (strcmp(fileInfo.m_FileName, testFiles[i]) == 0 &&
                fileInfo.m_FileSize == fileSizes[i]){
                sizeMatch = true;
                foundCount++;
                break;
            }
        }
        assert(sizeMatch);
        found = fs->findNext(fileInfo);
    }
    assert(foundCount == std::size(testFiles));

    // Verify file contents
    for (size_t i = 0; i < std::size(testFiles); i++){
        const int fd = fs->openFile(testFiles[i], false);
        assert(fd != -1);

        char* readBuffer = new char[fileSizes[i]];
        const size_t read = fs->readFile(fd, readBuffer, fileSizes[i]);
        assert(read == fileSizes[i]);

        // Verify content pattern
        for (size_t j = 0; j < fileSizes[i]; j++){
            assert(readBuffer[j] == static_cast<char>((i + j) % 256));
        }

        assert(fs->closeFile(fd));
        delete[] readBuffer;
    }

    // Test 2: Write and verify files with random sizes
    constexpr int numRandomFiles = 10;
    char randomFiles[numRandomFiles][FILENAME_LEN_MAX + 1];
    size_t randomSizes[numRandomFiles];

    for (int i = 0; i < numRandomFiles; i++){
        snprintf(randomFiles[i], FILENAME_LEN_MAX + 1, "random%d.txt", i);
        randomSizes[i] = rand() % 4096 + 1; // Random size between 1 and 4096 bytes

        const int fd = fs->openFile(randomFiles[i], true);
        assert(fd != -1);

        char* writeBuffer = new char[randomSizes[i]];
        for (size_t j = 0; j < randomSizes[i]; j++){
            writeBuffer[j] = static_cast<char>(rand() % 256);
        }

        size_t written = fs->writeFile(fd, writeBuffer, randomSizes[i]);
        assert(written == randomSizes[i]);
        assert(fs->closeFile(fd));
        delete[] writeBuffer;
    }

    // Verify random files
    found = fs->findFirst(fileInfo);
    foundCount = 0;
    while (found){
        for (int i = 0; i < numRandomFiles; i++){
            if (strcmp(fileInfo.m_FileName, randomFiles[i]) == 0){
                assert(fileInfo.m_FileSize == randomSizes[i]);
                foundCount++;
                break;
            }
        }
        found = fs->findNext(fileInfo);
    }
    assert(foundCount == numRandomFiles);

    // Cleanup
    assert(fs->umount());
    delete fs;
    doneDisk();
}
