#include "CFileSystem.h"

bool FileSystem::createFs(const BlockDevice& dev){
    auto* fs = new FileSystem();
    fs->m_blockDevice = dev;

    fs->m_superBlock.m_inodeCount = DIR_ENTRIES_MAX;
    fs->m_superBlock.m_filesCount = 0;
    fs->m_superBlock.m_inodesTableIndex = 1;
    fs->m_superBlock.m_dataBlocksIndex = inodesTableSectors + 1;
    fs->m_superBlock.m_dataBlocksCount = static_cast<int>(dev.m_Sectors) - fs->m_superBlock.m_dataBlocksIndex;
    fs->m_superBlock.m_freeDataBlocksCount = fs->m_superBlock.m_dataBlocksCount;
    fs->m_superBlock.m_firstFreeBlockIndex = fs->m_superBlock.m_dataBlocksIndex;
    fs->m_superBlock.m_firstFreeInodeIndex = 0;

    if (fs->m_blockDevice.m_Write(0, &fs->m_superBlock, 1) != 1){
        delete fs;
        return false;
    }

    // Initialize inodes
    char sectorBuffer[SECTOR_SIZE];
    const auto inodes = reinterpret_cast<Inode*>(sectorBuffer);

    for (int sector = 0; sector < inodesTableSectors; sector++){
        for (int i = 0; i < inodesPerSector; i++){
            const int inodeIndex = sector * inodesPerSector + i;
            if (inodeIndex >= DIR_ENTRIES_MAX) break;

            memset(&inodes[i], 0, sizeof(Inode));
            inodes[i].m_inUse = false;
            inodes[i].m_nextFreeInodeIndex = inodeIndex + 1;
            inodes[i].m_fileSize = 0;
            memset(inodes[i].m_fileName, 0, FILENAME_LEN_MAX + 1);
        }

        if (sector == inodesTableSectors - 1){
            const int lastInodeInSector = std::min(inodesPerSector, DIR_ENTRIES_MAX - sector * inodesPerSector) - 1;
            inodes[lastInodeInSector].m_nextFreeInodeIndex = -1;
        }

        if (fs->m_blockDevice.m_Write(fs->m_superBlock.m_inodesTableIndex + sector, sectorBuffer, 1) != 1){
            delete fs;
            return false;
        }
    }

    // Initialize blocks
    DataBlock dataBlock{};
    for (int i = fs->m_superBlock.m_dataBlocksIndex;
         i < fs->m_superBlock.m_dataBlocksIndex + fs->m_superBlock.m_dataBlocksCount - 1; i++){
        dataBlock.m_inUse = false;
        dataBlock.m_nextFreeBlockIndex = i + 1;
        if (fs->m_blockDevice.m_Write(i, &dataBlock, 1) != 1){
            delete fs;
            return false;
        }
    }

    dataBlock.m_inUse = false;
    dataBlock.m_nextFreeBlockIndex = -1;
    if (fs->m_blockDevice.m_Write(fs->m_superBlock.m_dataBlocksIndex + fs->m_superBlock.m_dataBlocksCount - 1,
                             &dataBlock, 1) != 1){
        delete fs;
        return false;
    }

    delete fs;
    return true;
}

bool FileSystem::umount(){
    if (!m_wasMounted) return false;

    for (int i = 0; i < OPEN_FILES_MAX; ++i){
        if (m_openFiles[i].m_inUse){
            if (!closeFile(i)){
                return false;
            }
        }
    }

    if (m_blockDevice.m_Write(0, &m_superBlock, 1) != 1){
        return false;
    }

    char sectorBuffer[SECTOR_SIZE];
    auto* inodes = reinterpret_cast<Inode*>(sectorBuffer);

    for (int i = 0; i < inodesTableSectors; i++){
        for (int j = 0; j < inodesPerSector && i * inodesPerSector + j < DIR_ENTRIES_MAX; j++){
            inodes[j] = m_inodes[i * inodesPerSector + j];
        }

        if (m_blockDevice.m_Write(m_superBlock.m_inodesTableIndex + i, sectorBuffer, 1) != 1){
            return false;
        }
    }

    m_wasMounted = false;
    return true;
}

FileSystem* FileSystem::mount(const BlockDevice& dev){
    auto* fs = new FileSystem();
    fs->m_blockDevice = dev;

    if (fs->m_blockDevice.m_Read(0, &fs->m_superBlock, 1) != 1){
        delete fs;
        return nullptr;
    }

    char sectorBuffer[SECTOR_SIZE];
    const auto inodes = reinterpret_cast<Inode*>(sectorBuffer);

    for (int i = 0; i < DIR_ENTRIES_MAX; i++){
        fs->m_inodes[i].m_inUse = false;
        fs->m_inodes[i].m_nextFreeInodeIndex = i + 1;
        fs->m_inodes[i].m_fileSize = 0;
        memset(fs->m_inodes[i].m_fileName, 0, FILENAME_LEN_MAX + 1);
    }
    fs->m_inodes[DIR_ENTRIES_MAX - 1].m_nextFreeInodeIndex = -1;

    for (int i = 0; i < inodesTableSectors; i++){
        if (fs->m_blockDevice.m_Read(fs->m_superBlock.m_inodesTableIndex + i, sectorBuffer, 1) != 1){
            delete fs;
            return nullptr;
        }
        for (int j = 0; j < inodesPerSector && i * inodesPerSector + j < DIR_ENTRIES_MAX; j++){
            const int inodeIndex = i * inodesPerSector + j;
            fs->m_inodes[inodeIndex] = inodes[j];

            // if inode is in use, update the free inode chain
            if (fs->m_inodes[inodeIndex].m_inUse){
                // find the previous free inode
                int prev = -1;
                for (int k = 0; k < DIR_ENTRIES_MAX; k++){
                    if (fs->m_inodes[k].m_nextFreeInodeIndex == inodeIndex){
                        prev = k;
                        break;
                    }
                }

                if (prev != -1){
                    // update the chain to skip this inode
                    fs->m_inodes[prev].m_nextFreeInodeIndex = fs->m_inodes[inodeIndex].m_nextFreeInodeIndex;
                } else if (fs->m_superBlock.m_firstFreeInodeIndex == inodeIndex){
                    // this was the first free inode
                    fs->m_superBlock.m_firstFreeInodeIndex = fs->m_inodes[inodeIndex].m_nextFreeInodeIndex;
                }
            }
        }
    }

    for (auto& file : fs->m_openFiles){
        file.m_inUse = false;
        file.m_inodeIndex = -1;
        file.m_position = 0;
        file.m_writeMode = false;
    }

    fs->m_wasMounted = true;
    return fs;
}

size_t FileSystem::fileSize(const char* fileName){
    if (!m_wasMounted) return SIZE_MAX;
    if (const auto fileIndex = findFile(fileName); fileIndex != -1)
        return m_inodes[fileIndex].m_fileSize;
    return SIZE_MAX;
}

int FileSystem::openFile(const char* fileName, bool writeMode){
    if (!m_wasMounted) return -1;

    // looking for free file descriptor
    int fd = -1;
    for (int i = 0; i < OPEN_FILES_MAX; ++i){
        if (!m_openFiles[i].m_inUse){
            fd = i;
            break;
        }
    }

    if (fd == -1) return -1; // no free file descriptors

    auto inodeIndex = findFile(fileName);

    if (writeMode){
        if (inodeIndex == -1){
            // create new file;
            inodeIndex = allocateInode();
            if (inodeIndex == -1) return -1; // no free i-nodes
            strcpy(m_inodes[inodeIndex].m_fileName, fileName);
        } else{
            // truncate
            m_inodes[inodeIndex].m_fileSize = 0;
        }
    } else{
        if (inodeIndex == -1) return -1;
    }

    m_openFiles[fd].m_inodeIndex = inodeIndex;
    m_openFiles[fd].m_position = 0;
    m_openFiles[fd].m_writeMode = writeMode;
    m_openFiles[fd].m_inUse = true;

    return fd;
}

bool FileSystem::closeFile(int fd){
    if (!isValidFd(fd)) return false;
    if (!m_openFiles[fd].m_inUse) return false;
    if (!m_wasMounted) return false;

    if (m_openFiles[fd].m_writeMode){
        Inode& inode = m_inodes[m_openFiles[fd].m_inodeIndex];
        inode.m_fileSize = m_openFiles[fd].m_position;
    }

    m_openFiles[fd].m_inodeIndex = -1;
    m_openFiles[fd].m_position = 0;
    m_openFiles[fd].m_writeMode = false;
    m_openFiles[fd].m_inUse = false;
    return true;
}

size_t FileSystem::readFile(int fd, void* data, size_t len){
    if (!m_wasMounted) return 0;
    if (!isValidFd(fd)) return 0;

    OpenFile& file = m_openFiles[fd];
    const Inode& inode = m_inodes[file.m_inodeIndex];

    const size_t bytesToRead = std::min(inode.m_fileSize - file.m_position, len);
    if (bytesToRead == 0) return 0;

    size_t bytesRead = 0;
    const auto dst = static_cast<char*>(data);

    while (bytesRead < bytesToRead){
        const int blockIndex = getBlockIndex(inode, file.m_position + bytesRead); // find the block to read
        if (blockIndex == -1) break;

        char buffer[SECTOR_SIZE];
        if (m_blockDevice.m_Read(blockIndex, buffer, 1) != 1) break;

        const size_t offsetInBlock = (file.m_position + bytesRead) % SECTOR_SIZE;
        const size_t bytesToCopy = std::min(SECTOR_SIZE - offsetInBlock, bytesToRead - bytesRead);

        memcpy(dst + bytesRead, buffer + offsetInBlock, bytesToCopy);
        bytesRead += bytesToCopy;
    }
    file.m_position += bytesRead;
    return bytesRead;
}

size_t FileSystem::writeFile(int fd, const void* data, size_t len){
    if (!m_wasMounted) return 0;
    if (!isValidFd(fd)) return 0;

    auto& file = m_openFiles[fd];
    if (!file.m_writeMode) return 0;

    Inode& inode = m_inodes[file.m_inodeIndex];

    size_t bytesWritten = 0;
    const auto src = static_cast<const char*>(data);

    while (bytesWritten < len){
        int blockIndex = getBlockIndex(inode, file.m_position + bytesWritten); // find the block to write
        if (blockIndex == -1){
            blockIndex = allocateBlock();
            if (blockIndex == -1){
                file.m_position += bytesWritten;
                inode.m_fileSize = file.m_position;
                return bytesWritten;
            }

            if (!setBlockIndex(inode, file.m_position + bytesWritten, blockIndex)){
                freeBlock(blockIndex);
                file.m_position += bytesWritten;
                inode.m_fileSize = file.m_position;
                return bytesWritten;
            }
        }

        char buffer[SECTOR_SIZE];
        if (m_blockDevice.m_Read(blockIndex, buffer, 1) != 1){
            file.m_position += bytesWritten;
            inode.m_fileSize = file.m_position;
            return bytesWritten;
        }

        const size_t offsetInBlock = (file.m_position + bytesWritten) % SECTOR_SIZE;
        const size_t bytesToCopy = std::min(SECTOR_SIZE - offsetInBlock, len - bytesWritten);

        memcpy(buffer + offsetInBlock, src + bytesWritten, bytesToCopy);

        if (m_blockDevice.m_Write(blockIndex, buffer, 1) != 1){
            file.m_position += bytesWritten;
            inode.m_fileSize = file.m_position;
            return bytesWritten;
        }

        bytesWritten += bytesToCopy;
    }

    file.m_position += bytesWritten;
    inode.m_fileSize = file.m_position;

    return bytesWritten;
}

bool FileSystem::deleteFile(const char* fileName){
    if (!m_wasMounted) return false;
    const auto inodeIndex = findFile(fileName);

    if (inodeIndex == -1) return false;

    for (const auto& file : m_openFiles){
        if (file.m_inUse && file.m_inodeIndex == inodeIndex){
            return false;
        }
    }

    Inode& inode = m_inodes[inodeIndex];
    const int pointersAmount = SECTOR_SIZE / sizeof(int);

    // free direct addresses
    for (auto& directAddress : inode.m_directAddresses){
        if (directAddress != -1){
            if (!freeBlock(directAddress)) return false;

            directAddress = -1;
        }
    }

    // free single indirect address
    if (inode.m_singleIndirectAddress != -1){
        int indices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_singleIndirectAddress, indices, 1) != 1){
            return false;
        }

        for (const auto& index : indices){
            if (index != -1 && !freeBlock(index)){
                return false;
            }
        }

        if (!freeBlock(inode.m_singleIndirectAddress)) return false;
        inode.m_singleIndirectAddress = -1;
    }

    // free double indirect address
    if (inode.m_doubleIndirectAddress != -1){
        int firstLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_doubleIndirectAddress, firstLevelIndices, 1) != 1){
            return false;
        }

        for (const auto& firstLevelIndex : firstLevelIndices){
            if (firstLevelIndex != -1){
                int secondLevelIndices[pointersAmount];
                if (m_blockDevice.m_Read(firstLevelIndex, secondLevelIndices, 1) != 1){
                    return false;
                }

                for (const auto& secondLevelIndex : secondLevelIndices){
                    if (secondLevelIndex != -1 && !freeBlock(secondLevelIndex)) return false;
                }

                if (!freeBlock(firstLevelIndex)){
                    return false;
                }
            }
        }

        if (!freeBlock(inode.m_doubleIndirectAddress)) return false;
        inode.m_doubleIndirectAddress = -1;
    }

    // free triple indirect address
    if (inode.m_tripleIndirectAddress != -1){
        int firstLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_tripleIndirectAddress, firstLevelIndices, 1) != 1){
            return false;
        }

        for (const auto& firstLevelIndex : firstLevelIndices){
            if (firstLevelIndex != -1){
                int secondLevelIndices[pointersAmount];
                if (m_blockDevice.m_Read(firstLevelIndex, secondLevelIndices, 1) != 1){
                    return false;
                }

                for (const auto& secondLevelIndex : secondLevelIndices){
                    if (secondLevelIndex != -1){
                        int thirdLevelIndices[pointersAmount];
                        if (m_blockDevice.m_Read(secondLevelIndex, thirdLevelIndices, 1) != 1){
                            return false;
                        }
                        for (const auto& thirdLevelIndex : thirdLevelIndices){
                            if (thirdLevelIndex != -1 && !freeBlock(thirdLevelIndex)) return false;
                        }
                        if (!freeBlock(secondLevelIndex)) return false;
                    }
                }
                if (!freeBlock(firstLevelIndex)) return false;
            }
        }
        if (!freeBlock(inode.m_tripleIndirectAddress)) return false;
        inode.m_tripleIndirectAddress = -1;
    }

    inode.m_nextFreeInodeIndex = m_superBlock.m_firstFreeInodeIndex;
    m_superBlock.m_firstFreeInodeIndex = inodeIndex;
    inode.m_inUse = false;
    inode.m_fileSize = 0;
    memset(inode.m_fileName, 0, FILENAME_LEN_MAX + 1);

    m_superBlock.m_filesCount--;
    return true;
}

bool FileSystem::findFirst(File& file){
    if (!m_wasMounted) return false;
    m_findPosition = 0;

    while (m_findPosition < DIR_ENTRIES_MAX){
        if (m_inodes[m_findPosition].m_inUse){
            strncpy(file.m_FileName, m_inodes[m_findPosition].m_fileName, FILENAME_LEN_MAX);
            file.m_FileName[FILENAME_LEN_MAX] = '\0';
            file.m_FileSize = m_inodes[m_findPosition].m_fileSize;
            m_findPosition++;
            return true;
        }
        m_findPosition++;
    }
    return false;
}

bool FileSystem::findNext(File& file){
    if (!m_wasMounted) return false;

    while (m_findPosition < DIR_ENTRIES_MAX){
        if (m_inodes[m_findPosition].m_inUse){
            strncpy(file.m_FileName, m_inodes[m_findPosition].m_fileName, FILENAME_LEN_MAX);
            file.m_FileName[FILENAME_LEN_MAX] = '\0';
            file.m_FileSize = m_inodes[m_findPosition].m_fileSize;
            m_findPosition++;
            return true;
        }
        m_findPosition++;
    }
    return false;
}

int FileSystem::findFile(const char* fileName) const{
    for (int i = 0; i < DIR_ENTRIES_MAX; ++i){
        if (m_inodes[i].m_inUse && strcmp(m_inodes[i].m_fileName, fileName) == 0)
            return i;
    }
    return -1;
}

int FileSystem::allocateInode(){
    const auto inodeIndex = m_superBlock.m_firstFreeInodeIndex;
    if (inodeIndex == -1)
        return -1; // no free i-nodes

    m_superBlock.m_firstFreeInodeIndex = m_inodes[inodeIndex].m_nextFreeInodeIndex;

    m_inodes[inodeIndex].m_inUse = true;
    m_inodes[inodeIndex].m_fileSize = 0;
    m_inodes[inodeIndex].m_nextFreeInodeIndex = -1;

    for (auto& directAddress : m_inodes[inodeIndex].m_directAddresses) directAddress = -1;
    m_inodes[inodeIndex].m_singleIndirectAddress = -1;
    m_inodes[inodeIndex].m_doubleIndirectAddress = -1;
    m_inodes[inodeIndex].m_tripleIndirectAddress = -1;

    m_superBlock.m_filesCount++;

    return inodeIndex;
}

int FileSystem::allocateBlock(){
    const auto blockIndex = m_superBlock.m_firstFreeBlockIndex;
    if (blockIndex == -1){
        return -1; // no free data blocks
    }

    DataBlock dataBlock{};
    if (m_blockDevice.m_Read(blockIndex, &dataBlock, 1) != 1){
        return -1;
    }

    m_superBlock.m_firstFreeBlockIndex = dataBlock.m_nextFreeBlockIndex;

    dataBlock.m_inUse = true;
    dataBlock.m_nextFreeBlockIndex = -1;

    if (m_blockDevice.m_Write(blockIndex, &dataBlock, 1) != 1){
        return -1;
    }

    m_superBlock.m_freeDataBlocksCount--;
    return blockIndex;
}

bool FileSystem::freeBlock(const int blockIndex){
    if (blockIndex == -1) return true;

    if (blockIndex < m_superBlock.m_dataBlocksIndex ||
        blockIndex >= m_superBlock.m_dataBlocksIndex + m_superBlock.m_dataBlocksCount){
        return false;
    }

    DataBlock block{};
    if (m_blockDevice.m_Read(blockIndex, &block, 1) != 1){
        return false;
    }

    if (!block.m_inUse) return true;

    block.m_inUse = false;
    block.m_nextFreeBlockIndex = m_superBlock.m_firstFreeBlockIndex;

    if (m_blockDevice.m_Write(blockIndex, &block, 1) != 1){
        return false;
    }

    m_superBlock.m_firstFreeBlockIndex = blockIndex;
    m_superBlock.m_freeDataBlocksCount++;
    return true;
}

int FileSystem::getBlockIndex(const Inode& inode, const size_t position) const{
    const size_t blockNumber = position / SECTOR_SIZE;

    // direct address
    if (blockNumber < 12){
        return inode.m_directAddresses[blockNumber];
    }

    const int pointersAmount = SECTOR_SIZE / sizeof(int); // the amount of pointers that are stored in one sector

    // single indirect address
    if (blockNumber < 12 + pointersAmount){
        if (inode.m_singleIndirectAddress == -1)
            return -1;

        int indices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_singleIndirectAddress, indices, 1) != 1)
            return -1;

        return indices[blockNumber - 12];
    }

    // double indirect address
    if (blockNumber < 12 + pointersAmount + pointersAmount * pointersAmount){
        if (inode.m_doubleIndirectAddress == -1)
            return -1;

        int firstLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_doubleIndirectAddress, firstLevelIndices, 1) != 1)
            return -1;

        const size_t firstLevelIndex = (blockNumber - 12 - pointersAmount) / pointersAmount;
        if (firstLevelIndices[firstLevelIndex] == -1)
            return -1;

        int secondLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(firstLevelIndices[firstLevelIndex], secondLevelIndices, 1) != 1)
            return -1;

        const size_t secondLevelIndex = (blockNumber - 12 - pointersAmount) % pointersAmount;
        return secondLevelIndices[secondLevelIndex];
    }

    // triple indirect address
    if (blockNumber < 12 + pointersAmount + pointersAmount * pointersAmount +
        pointersAmount * pointersAmount * pointersAmount){
        if (inode.m_tripleIndirectAddress == -1)
            return -1;

        int firstLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_tripleIndirectAddress, firstLevelIndices, 1) != 1)
            return -1;

        const size_t remainingBlocks = blockNumber - 12 - pointersAmount - pointersAmount * pointersAmount;
        const size_t firstLevelIndex = remainingBlocks / (pointersAmount * pointersAmount);
        if (firstLevelIndices[firstLevelIndex] == -1)
            return -1;

        int secondLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(firstLevelIndices[firstLevelIndex], secondLevelIndices, 1) != 1)
            return -1;

        const size_t secondLevelIndex = remainingBlocks % (pointersAmount * pointersAmount) / pointersAmount;
        if (secondLevelIndices[secondLevelIndex] == -1)
            return -1;

        int thirdLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(secondLevelIndices[secondLevelIndex], thirdLevelIndices, 1) != 1)
            return -1;

        const size_t thirdLevelIndex = remainingBlocks % pointersAmount;
        return thirdLevelIndices[thirdLevelIndex];
    }

    return -1;
}

bool FileSystem::setBlockIndex(Inode& inode, const size_t position, const int blockIndex){
    const size_t blockNumber = position / SECTOR_SIZE;

    if (blockNumber < 12){
        inode.m_directAddresses[blockNumber] = blockIndex;
        return true;
    }

    const int pointersAmount = SECTOR_SIZE / sizeof(int);

    // single indirect
    if (blockNumber < 12 + pointersAmount){
        if (inode.m_singleIndirectAddress == -1){
            const int indirectBlock = allocateBlock();
            if (indirectBlock == -1) return false;

            int indices[pointersAmount];
            for (auto& index : indices){
                index = -1;
            }

            if (m_blockDevice.m_Write(indirectBlock, indices, 1) != 1){
                freeBlock(indirectBlock);
                return false;
            }

            inode.m_singleIndirectAddress = indirectBlock;
        }

        int indices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_singleIndirectAddress, indices, 1) != 1)
            return false;

        indices[blockNumber - 12] = blockIndex;

        if (m_blockDevice.m_Write(inode.m_singleIndirectAddress, indices, 1) != 1)
            return false;

        return true;
    }

    // double indirect
    if (blockNumber < 12 + pointersAmount + pointersAmount * pointersAmount){
        if (inode.m_doubleIndirectAddress == -1){
            const int doubleIndirectBlock = allocateBlock();
            if (doubleIndirectBlock == -1){
                return false;
            }

            int indices[pointersAmount];
            for (auto& index : indices){
                index = -1;
            }

            if (m_blockDevice.m_Write(doubleIndirectBlock, indices, 1) != 1){
                freeBlock(doubleIndirectBlock);
                return false;
            }

            inode.m_doubleIndirectAddress = doubleIndirectBlock;
        }

        int firstLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_doubleIndirectAddress, firstLevelIndices, 1) != 1){
            return false;
        }

        const size_t firstLevelIndex = (blockNumber - 12 - pointersAmount) / pointersAmount;

        if (firstLevelIndices[firstLevelIndex] == -1){
            const int indirectBlock = allocateBlock();
            if (indirectBlock == -1){
                return false;
            }

            int indices[pointersAmount];
            for (auto& index : indices){
                index = -1;
            }

            if (m_blockDevice.m_Write(indirectBlock, indices, 1) != 1){
                freeBlock(indirectBlock);
                return false;
            }

            firstLevelIndices[firstLevelIndex] = indirectBlock;
            if (m_blockDevice.m_Write(inode.m_doubleIndirectAddress, firstLevelIndices, 1) != 1){
                return false;
            }
        }

        int secondLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(firstLevelIndices[firstLevelIndex], secondLevelIndices, 1) != 1){
            return false;
        }

        const size_t secondLevelIndex = (blockNumber - 12 - pointersAmount) % pointersAmount;
        secondLevelIndices[secondLevelIndex] = blockIndex;

        if (m_blockDevice.m_Write(firstLevelIndices[firstLevelIndex], secondLevelIndices, 1) != 1){
            return false;
        }

        return true;
    }

    // triple indirect
    if (blockNumber < 12 + pointersAmount + pointersAmount * pointersAmount +
        pointersAmount * pointersAmount * pointersAmount){
        if (inode.m_tripleIndirectAddress == -1){
            const int tripleIndirectBlock = allocateBlock();
            if (tripleIndirectBlock == -1) return false;

            int indices[pointersAmount];
            for (auto& index : indices){
                index = -1;
            }

            if (m_blockDevice.m_Write(tripleIndirectBlock, indices, 1) != 1){
                freeBlock(tripleIndirectBlock);
                return false;
            }

            inode.m_tripleIndirectAddress = tripleIndirectBlock;
        }

        int firstLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(inode.m_tripleIndirectAddress, firstLevelIndices, 1) != 1)
            return false;

        const size_t remainingBlocks = blockNumber - 12 - pointersAmount - pointersAmount * pointersAmount;
        const size_t firstLevelIndex = remainingBlocks / (pointersAmount * pointersAmount);

        if (firstLevelIndices[firstLevelIndex] == -1){
            const int indirectBlock = allocateBlock();
            if (indirectBlock == -1) return false;

            int indices[pointersAmount];
            for (auto& index : indices){
                index = -1;
            }

            if (m_blockDevice.m_Write(indirectBlock, indices, 1) != 1){
                freeBlock(indirectBlock);
                return false;
            }

            firstLevelIndices[firstLevelIndex] = indirectBlock;
            if (m_blockDevice.m_Write(inode.m_tripleIndirectAddress, firstLevelIndices, 1) != 1)
                return false;
        }

        int secondLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(firstLevelIndices[firstLevelIndex], secondLevelIndices, 1) != 1)
            return false;

        const size_t secondLevelIndex = remainingBlocks % (pointersAmount * pointersAmount) / pointersAmount;

        if (secondLevelIndices[secondLevelIndex] == -1){
            const int indirectBlock = allocateBlock();
            if (indirectBlock == -1) return false;

            int indices[pointersAmount];
            for (auto& index : indices){
                index = -1;
            }

            if (m_blockDevice.m_Write(indirectBlock, indices, 1) != 1){
                freeBlock(indirectBlock);
                return false;
            }

            secondLevelIndices[secondLevelIndex] = indirectBlock;
            if (m_blockDevice.m_Write(firstLevelIndices[firstLevelIndex], secondLevelIndices, 1) != 1)
                return false;
        }

        int thirdLevelIndices[pointersAmount];
        if (m_blockDevice.m_Read(secondLevelIndices[secondLevelIndex], thirdLevelIndices, 1) != 1)
            return false;

        const size_t thirdLevelIndex = remainingBlocks % pointersAmount;
        thirdLevelIndices[thirdLevelIndex] = blockIndex;

        if (m_blockDevice.m_Write(secondLevelIndices[secondLevelIndex], thirdLevelIndices, 1) != 1)
            return false;

        return true;
    }

    return false;
}
