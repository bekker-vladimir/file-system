#pragma once
#include "../src/CFileSystem.h"
#include "cassert"

// Basic functional tests
void testMkFs();
void testWrite();
void smallFilesTest();
void testDeleteFiles();
void testSmallFilesContent();

// Large file tests
void testLargeFiles();
void testLargeFilesEdgeCasesComprehensive();

// Helpers used by tests
BlockDevice createDisk();
BlockDevice openDisk();
void doneDisk();
