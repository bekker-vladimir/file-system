#include "Tests.h"

void testDeleteFiles(){
    const BlockDevice dev = createDisk();
    assert(FileSystem::createFs(dev));
    doneDisk();

    FileSystem* fs = FileSystem::mount(openDisk());
    assert(fs);

    // Test 1: Create and delete single file
    const char* testFile = "test1.txt";
    constexpr size_t fileSize = 1024; // 1KB

    // Create and write to file
    int fd = fs->openFile(testFile, true);
    assert(fd != -1);

    char* writeBuffer = new char[fileSize];
    for (size_t i = 0; i < fileSize; i++){
        writeBuffer[i] = static_cast<char>(i % 256);
    }

    size_t written = fs->writeFile(fd, writeBuffer, fileSize);
    assert(written == fileSize);
    assert(fs->closeFile(fd));
    delete[] writeBuffer;

    // Verify file exists
    File fileInfo{};
    bool found = fs->findFirst(fileInfo);
    assert(found);
    assert(strcmp(fileInfo.m_FileName, testFile) == 0);
    assert(fileInfo.m_FileSize == fileSize);

    // Delete file
    assert(fs->deleteFile(testFile));

    // Verify file is deleted
    found = fs->findFirst(fileInfo);
    assert(!found);

    // Test 2: Create multiple files and delete them in different order
    const char* testFiles[] = {
        "file1.txt",
        "file2.txt",
        "file3.txt"
    };
    constexpr size_t fileSizes[] = {
        2048, // 2KB
        3072, // 3KB
        4096 // 4KB
    };

    // Create files
    for (size_t i = 0; i < std::size(testFiles); i++){
        fd = fs->openFile(testFiles[i], true);
        assert(fd != -1);

        writeBuffer = new char[fileSizes[i]];
        for (size_t j = 0; j < fileSizes[i]; j++){
            writeBuffer[j] = static_cast<char>((i + j) % 256);
        }

        written = fs->writeFile(fd, writeBuffer, fileSizes[i]);
        assert(written == fileSizes[i]);
        assert(fs->closeFile(fd));
        delete[] writeBuffer;
    }

    // Verify all files exist
    size_t foundCount = 0;
    found = fs->findFirst(fileInfo);
    while (found){
        bool sizeMatch = false;
        for (size_t i = 0; i < std::size(testFiles); i++){
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

    // Delete files in different order
    assert(fs->deleteFile(testFiles[1])); // Delete middle file first
    assert(fs->deleteFile(testFiles[2])); // Then last file
    assert(fs->deleteFile(testFiles[0])); // Finally first file

    // Verify all files are deleted
    found = fs->findFirst(fileInfo);
    assert(!found);

    // Test 3: Try to delete non-existent file
    assert(!fs->deleteFile("non_existent.txt"));

    // Test 4: Create, open and try to delete file
    fd = fs->openFile("open_file.txt", true);
    assert(fd != -1);
    assert(!fs->deleteFile("open_file.txt")); // Should fail because file is open
    assert(fs->closeFile(fd));
    assert(fs->deleteFile("open_file.txt")); // Should succeed after closing

    // Test 5: Create files with same content, delete one, verify others
    const char* sameContentFiles[] = {
        "same1.txt",
        "same2.txt",
        "same3.txt"
    };
    constexpr size_t sameSize = 1024;

    // Create files with same content
    for (auto& file : sameContentFiles){
        fd = fs->openFile(file, true);
        assert(fd != -1);

        writeBuffer = new char[sameSize];
        for (size_t j = 0; j < sameSize; j++){
            writeBuffer[j] = static_cast<char>(j % 256);
        }

        written = fs->writeFile(fd, writeBuffer, sameSize);
        assert(written == sameSize);
        assert(fs->closeFile(fd));
        delete[] writeBuffer;
    }

    // Delete one file
    assert(fs->deleteFile(sameContentFiles[1]));

    // Verify other files still exist and have correct content
    for (size_t i = 0; i < std::size(sameContentFiles); i++){
        if (i == 1) continue; // Skip deleted file

        fd = fs->openFile(sameContentFiles[i], false);
        assert(fd != -1);

        char* readBuffer = new char[sameSize];
        const size_t read = fs->readFile(fd, readBuffer, sameSize);
        assert(read == sameSize);

        for (size_t j = 0; j < sameSize; j++){
            assert(readBuffer[j] == static_cast<char>(j % 256));
        }

        assert(fs->closeFile(fd));
        delete[] readBuffer;
    }

    // Cleanup
    assert(fs->umount());
    delete fs;
    doneDisk();
}
