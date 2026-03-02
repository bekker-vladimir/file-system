# Simple File System Implementation 

This project implements a simple user-space filesystem in C++. It provides a basic interface for creating, mounting, and managing a filesystem on a block device. The filesystem handles typical file operations such as opening, reading, writing, and deleting files, all while directly interacting with simulated disk sectors.

## Features

*   **Block Device Interface:** Interacts with a simulated block device via sector read and write operations.
*   **Superblock & Free Space Management:** Manages filesystem metadata, inodes, and available space dynamically using indirect block addressing.
*   **Inodes & Block Pointers:** Utilizes an inode structure that holds a file’s direct, single indirect, double indirect, and triple indirect block pointers, supporting extremely large file capacities.
*   **File Operations:** Supports creating files, reading/writing files sequentially, checking file sizes, and deleting files.
*   **Directory Management:** Maintains a simple root directory with file names and limits `(FILENAME_LEN_MAX)`. Note: Hierarchical directories are not supported.
*   **Multiple File Access:** Supports concurrent open files up to `OPEN_FILES_MAX`.

## Project Structure

*   **`src/`:** Contains the main application source files.
    *   `CFileSystem.cpp` and `CFileSystem.h`: The implementation and definition of the custom filesystem class.
    *   `main.cpp`: The entry point that executes filesystem tests.
*   **`tests/`:** Contains various test cases.
    *   Files like `simple_test.inc`, `small_files_test.inc`, `delete_files_test.inc`, etc., used for robustly testing the filesystem.
*   **`CMakeLists.txt`:** The build configuration file for compiling the application.
