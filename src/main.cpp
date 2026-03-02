#include "CFileSystem.h"
#include "../tests/Tests.h"

int main() {
    testMkFs();
    testWrite();
    smallFilesTest();
    testDeleteFiles();
    testSmallFilesContent();
    testLargeFiles();
    testLargeFilesEdgeCasesComprehensive();
    return EXIT_SUCCESS;
}
