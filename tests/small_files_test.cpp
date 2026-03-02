#include "Tests.h"

void testFileCreation() {
    printf("Starting testFileCreation...\n");
    
    // Create and format the disk
    printf("Creating and formatting disk...\n");
    assert(FileSystem::createFs(createDisk()));
    doneDisk();

    // Mount the filesystem
    printf("Mounting filesystem...\n");
    FileSystem *fs = FileSystem::mount(openDisk());
    assert(fs != nullptr);

    // Test 1: Create a single file
    printf("Test 1: Creating single file...\n");
    int fd = fs->openFile("test1.txt", true);
    assert(fd >= 0);
    assert(fs->closeFile(fd));

    // Test 2: Create multiple files
    printf("Test 2: Creating multiple files...\n");
    for (int i = 0; i < 5; i++) {
        char filename[32];
        snprintf(filename, sizeof(filename), "file_%d.txt", i);
        printf("Creating %s...\n", filename);
        fd = fs->openFile(filename, true);
        assert(fd >= 0);
        assert(fs->closeFile(fd));
    }

    // Test 3: Try to create a file that already exists
    printf("Test 3: Creating existing file...\n");
    fd = fs->openFile("test1.txt", true);
    assert(fd >= 0); // Should succeed and truncate
    assert(fs->closeFile(fd));

    // Test 4: Try to open non-existent file for reading
    printf("Test 4: Opening non-existent file for reading...\n");
    fd = fs->openFile("nonexistent.txt", false);
    assert(fd == -1); // Should fail

    // Test 5: Create file with maximum length name
    printf("Test 5: Creating file with max length name...\n");
    char longName[FILENAME_LEN_MAX + 1];
    memset(longName, 'a', FILENAME_LEN_MAX);
    longName[FILENAME_LEN_MAX] = '\0';
    fd = fs->openFile(longName, true);
    assert(fd >= 0);
    assert(fs->closeFile(fd));

    // Clean up
    printf("Unmounting filesystem...\n");
    assert(fs->umount());
    delete fs;
    doneDisk();
    printf("Test completed successfully!\n");
}

void smallFilesTest() {
    try {
        testFileCreation();
        printf("All tests passed!\n");
    } catch (const char* msg) {
        printf("Error: %s\n", msg);
    } catch (...) {
        printf("Unknown error occurred\n");
    }
}