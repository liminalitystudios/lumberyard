/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include <AzCore/IO/SystemFile.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/IO/FileIOEventBus.h>
#include <AzCore/Casting/numeric_cast.h>

#include <AzCore/PlatformIncl.h>
#include <AzCore/Utils/Utils.h>

#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>


using namespace AZ::IO;

namespace UnixLikePlatformUtil
{
    bool CanCreateDirRecursive(char*)
    {
        // No additional checks on these platforms, tell the caller to continue
        return true;
    }
}

namespace
{
    static const SystemFile::FileHandleType PlatformSpecificInvalidHandle = AZ_TRAIT_SYSTEMFILE_INVALID_HANDLE;
}

bool SystemFile::PlatformOpen(int mode, int platformFlags)
{
    int desiredAccess = 0;
    int permissions = S_IRWXU | S_IRGRP | S_IROTH;

    bool createPath = false;
    if ((mode & SF_OPEN_READ_WRITE) == SF_OPEN_READ_WRITE)
    {
        desiredAccess = O_RDWR;
    }
    else if ((mode & SF_OPEN_READ_ONLY) == SF_OPEN_READ_ONLY)
    {
        desiredAccess = O_RDONLY;
    }
    else if ((mode & SF_OPEN_WRITE_ONLY) == SF_OPEN_WRITE_ONLY || (mode & SF_OPEN_APPEND))
    {
        desiredAccess = O_WRONLY;
    }
    else
    {
        EBUS_EVENT(FileIOEventBus, OnError, this, nullptr, 0);
        return false;
    }

    if ((mode & SF_OPEN_CREATE_NEW) == SF_OPEN_CREATE_NEW)
    {
        desiredAccess |= O_CREAT | O_EXCL;
        createPath = (mode & SF_OPEN_CREATE_PATH) == SF_OPEN_CREATE_PATH;
    }
    else if ((mode & SF_OPEN_CREATE) ==  SF_OPEN_CREATE)
    {
        desiredAccess |= O_CREAT | O_TRUNC;
        createPath = (mode & SF_OPEN_CREATE_PATH) == SF_OPEN_CREATE_PATH;
    }
    else if ((mode & SF_OPEN_TRUNCATE))
    {
        desiredAccess |= O_TRUNC;
    }

    if (createPath)
    {
        CreatePath(m_fileName);
    }
    m_handle = open(m_fileName, desiredAccess, permissions);

    if (m_handle == PlatformSpecificInvalidHandle)
    {
        EBUS_EVENT(FileIOEventBus, OnError, this, nullptr, errno);
        return false;
    }
    else
    {
        if (mode & SF_OPEN_APPEND)
        {
            lseek(m_handle, 0, SEEK_END);
        }
    }

    return true;
}

void SystemFile::PlatformClose()
{
    if (m_handle != PlatformSpecificInvalidHandle)
    {
        close(m_handle);
        m_handle = PlatformSpecificInvalidHandle;
    }
}

namespace Platform
{
    using FileHandleType = AZ::IO::SystemFile::FileHandleType;

    void Seek(FileHandleType handle, const SystemFile* systemFile, SizeType offset, SystemFile::SeekMode mode)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            int result = lseek(handle, offset, mode);
            if (result == -1)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, errno);
            }
        }
    }

    SystemFile::SizeType Tell(FileHandleType handle, const SystemFile* systemFile)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            off_t result = lseek(handle, 0, SEEK_CUR);
            if (result == (off_t)-1)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, errno);
            }
            return aznumeric_cast<SizeType>(result);
        }

        return 0;
    }

    bool Eof(FileHandleType handle, const SystemFile* systemFile)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            off_t current = lseek(handle, 0, SEEK_CUR);
            if (current == (off_t)-1)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, current);
                return false;
            }

            off_t end = lseek(handle, 0, SEEK_END);
            if (end == (off_t)-1)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, end);
                return false;
            }

            // Reset file pointer back to from whence it came
            lseek(handle, current, SEEK_SET);

            return current == end;
        }

        return false;
    }

    AZ::u64 ModificationTime(FileHandleType handle, const SystemFile* systemFile)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            struct stat statResult;
            if (fstat(handle, &statResult) != 0)
            {
                return 0;
            }
            return aznumeric_cast<AZ::u64>(statResult.st_mtime);
        }

        return 0;
    }

    SystemFile::SizeType Read(FileHandleType handle, const SystemFile* systemFile, SizeType byteSize, void* buffer)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            ssize_t bytesRead = read(handle, buffer, byteSize);
            if (bytesRead == -1)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, errno);
                return 0;
            }
            return bytesRead;
        }

        return 0;
    }

    SystemFile::SizeType Write(FileHandleType handle, const SystemFile* systemFile, const void* buffer, SizeType byteSize)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            ssize_t result = write(handle, buffer, byteSize);
            if (result == -1)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, errno);
                return 0;
            }
            return result;
        }

        return 0;
    }

    void Flush(FileHandleType handle, const SystemFile* systemFile)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            if (fsync(handle) != 0)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, errno);
            }
        }
    }

    SystemFile::SizeType Length(FileHandleType handle, const SystemFile* systemFile)
    {
        if (handle != PlatformSpecificInvalidHandle)
        {
            struct stat stat;
            if (fstat(handle, &stat) < 0)
            {
                EBUS_EVENT(FileIOEventBus, OnError, systemFile, nullptr, 0);
                return 0;
            }
            return stat.st_size;
        }

        return 0;
    }

    bool Exists(const char* fileName)
    {
        return access(fileName, F_OK) == 0;
    }
}

