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

#if !defined(AZCORE_EXCLUDE_ZLIB)

#include <AzCore/Compression/Compression.h>
#include <AzCore/Memory/SystemAllocator.h>

using namespace AZ;

//////////////////////////////////////////////////////////////////////////
// ZLib
#define NO_GZIP
#include <zlib.h>

//=========================================================================
// Compress
// [3/21/2011]
//=========================================================================
ZLib::ZLib(IAllocator* workMemAllocator)
    : m_strDeflate(NULL)
    , m_strInflate(NULL)
{
    m_workMemoryAllocator = workMemAllocator;
    if (m_workMemoryAllocator == NULL)
    {
        m_workMemoryAllocator = &AllocatorInstance<SystemAllocator>::Get();
    }
}

//=========================================================================
// ~ZLib
// [3/21/2011]
//=========================================================================
ZLib::~ZLib()
{
    if (m_strDeflate)
    {
        StopCompressor();
    }
    if (m_strInflate)
    {
        StopDecompressor();
    }
}

//=========================================================================
// AllocateMem
// [3/21/2011]
//=========================================================================
void* ZLib::AllocateMem(void* userData, unsigned int items, unsigned int size)
{
    IAllocator* allocator = reinterpret_cast<IAllocator*>(userData);
    return allocator->Allocate(items * size, 4, 0, "ZLib", __FILE__, __LINE__);
}

//=========================================================================
// FreeMem
// [3/21/2011]
//=========================================================================
void ZLib::FreeMem(void* userData, void* address)
{
    IAllocator* allocator = reinterpret_cast<IAllocator*>(userData);
    allocator->DeAllocate(address);
}

//=========================================================================
// StartCompressor
// [3/21/2011]
//=========================================================================
void ZLib::StartCompressor(unsigned int compressionLevel)
{
    AZ_Assert(m_strDeflate == NULL, "Compressor already started!");
    m_strDeflate = reinterpret_cast< z_stream* >(AllocateMem(m_workMemoryAllocator, 1, sizeof(z_stream)));
    m_strDeflate->zalloc = &ZLib::AllocateMem;
    m_strDeflate->zfree = &ZLib::FreeMem;
    m_strDeflate->opaque = m_workMemoryAllocator;
    int r = deflateInit(m_strDeflate, compressionLevel);
    (void)r;
    AZ_Assert(r == Z_OK, "ZLib internal error - deflateInit() failed !!!\n");
}

//=========================================================================
// StopCompressor
// [3/21/2011]
//=========================================================================
void ZLib::StopCompressor()
{
    AZ_Assert(m_strDeflate != NULL, "Compressor not started!");
    deflateEnd(m_strDeflate);
    FreeMem(m_workMemoryAllocator, m_strDeflate);
    m_strDeflate = NULL;
}

//=========================================================================
// ResetCompressor
// [12/17/2012]
//=========================================================================
void ZLib::ResetCompressor()
{
    AZ_Assert(m_strDeflate != NULL, "Compressor not started!");
    int r = deflateReset(m_strDeflate);
    (void)r;
    AZ_Assert(r == Z_OK, "ZLib inconsistent state - deflateReset() failed !!!\n");
}

//=========================================================================
// Compress
// [3/21/2011]
//=========================================================================
unsigned int ZLib::Compress(const void* data, unsigned int& dataSize, void* compressedData, unsigned int compressedDataSize, FlushType flushType)
{
    AZ_Assert(m_strDeflate != NULL, "Compressor not started!");
    m_strDeflate->avail_in = dataSize;
    m_strDeflate->next_in = (unsigned char*)data;
    m_strDeflate->avail_out = compressedDataSize;
    m_strDeflate->next_out = reinterpret_cast<unsigned char*>(compressedData);
    int flush;
    switch (flushType)
    {
    case FT_PARTIAL_FLUSH:
        flush = Z_PARTIAL_FLUSH;
        break;
    case FT_SYNC_FLUSH:
        flush = Z_SYNC_FLUSH;
        break;
    case FT_FULL_FLUSH:
        flush = Z_FULL_FLUSH;
        break;
    case FT_FINISH:
        flush = Z_FINISH;
        break;
    case FT_BLOCK:
        flush = Z_BLOCK;
        break;
    case FT_TREES:
        flush = Z_TREES;
        break;
    case FT_NO_FLUSH:
    default:
        flush = Z_NO_FLUSH;
    }
    int r = deflate(m_strDeflate, flush);
    (void)r;
    AZ_Assert(r >= Z_OK || r == Z_BUF_ERROR, "ZLib compress internal error %d", r);
    dataSize = m_strDeflate->avail_in;
    return compressedDataSize - m_strDeflate->avail_out;
}

//=========================================================================
// GetMinCompressedBufferSize
// [3/21/2011]
//=========================================================================
unsigned int ZLib::GetMinCompressedBufferSize(unsigned int sourceDataSize)
{
    AZ_Assert(m_strDeflate != NULL, "Compressor not started!");
    return static_cast<unsigned int>(deflateBound(m_strDeflate, sourceDataSize));
}

//=========================================================================
// StartDecompressor
// [3/21/2011]
//=========================================================================
void ZLib::StartDecompressor(Header* header)
{
    AZ_Assert(m_strInflate == NULL, "Decompressor already started!");
    m_strInflate = reinterpret_cast< z_stream* >(AllocateMem(m_workMemoryAllocator, 1, sizeof(z_stream)));
    m_strInflate->zalloc = &ZLib::AllocateMem;
    m_strInflate->zfree = &ZLib::FreeMem;
    m_strInflate->opaque = m_workMemoryAllocator;
    int r = inflateInit(m_strInflate);
    (void)r;
    AZ_Assert(r == Z_OK, "ZLib internal error - inflateInit() failed !!!\n");
    if (header)
    {
        SetupDecompressHeader(*header);
    }
}

//=========================================================================
// StopDecompressor
// [3/21/2011]
//=========================================================================
void ZLib::StopDecompressor()
{
    AZ_Assert(m_strInflate != NULL, "Decompressor not started!");
    inflateEnd(m_strInflate);
    FreeMem(m_workMemoryAllocator, m_strInflate);
    m_strInflate = NULL;
}

//=========================================================================
// ResetDecompressor
// [12/17/2012]
//=========================================================================
void ZLib::ResetDecompressor(Header* header)
{
    AZ_Assert(m_strInflate != NULL, "Decompressor not started!");
    int r = inflateReset(m_strInflate);
    (void)r;
    AZ_Assert(r == Z_OK, "ZLib inconsistent state - inflateReset() failed !!!\n");
    if (header)
    {
        SetupDecompressHeader(*header);
    }
}

//=========================================================================
// SetupHeader
// [12/17/2012]
//=========================================================================
void ZLib::SetupDecompressHeader(Header header)
{
    unsigned int fakeBuffer;
    unsigned int fakeBufferSize = sizeof(fakeBuffer);
    unsigned int numDecompressed = Decompress(&header, sizeof(header), &fakeBuffer, fakeBufferSize);
    (void)numDecompressed;
    AZ_Assert(numDecompressed == sizeof(header), "If you provided a valid header it should have been processed!");
}

//=========================================================================
// Compress
// [3/21/2011]
//=========================================================================
unsigned int ZLib::Decompress(const void* compressedData, unsigned int compressedDataSize, void* data, unsigned int& dataSize, FlushType flushType)
{
    AZ_Assert(m_strInflate != NULL, "Decompressor not started!");
    m_strInflate->avail_in = compressedDataSize;
    m_strInflate->next_in = (unsigned char*)compressedData;
    m_strInflate->avail_out = dataSize;
    m_strInflate->next_out = reinterpret_cast<unsigned char*>(data);
    int flush;
    switch (flushType)
    {
    case FT_PARTIAL_FLUSH:
        flush = Z_PARTIAL_FLUSH;
        break;
    case FT_SYNC_FLUSH:
        flush = Z_SYNC_FLUSH;
        break;
    case FT_FULL_FLUSH:
        flush = Z_FULL_FLUSH;
        break;
    case FT_FINISH:
        flush = Z_FINISH;
        break;
    case FT_BLOCK:
        flush = Z_BLOCK;
        break;
    case FT_TREES:
        flush = Z_TREES;
        break;
    case FT_NO_FLUSH:
    default:
        flush = Z_NO_FLUSH;
    }
    int r = inflate(m_strInflate, flush);
    /*
    Because of the way we allow random access to our compressed streams by way of adding seek points into the end of the stream, a Z_DATA_ERROR
    will occur if the compressed stream is not decompressed sequentially. This is due to the adler32 checksum of all uncompressed data up to the Z_FINISH
    being stored in the last 4 bytes of the compressed zlib data. 
    When zlib decompresses a compressed stream it calculates a running adler32 checksum of the data and when it reaches the end of the stream it compares this running checksum
    against the checksum stored at the end of the compressed stream. When seek points are used to decompress specific offsets in the compressed stream, only a portion of the stream is decompressed.
    When the last block of a deflate stream is decompressed it compares the running adler32 against the stored adler32 checksum.
    This will be different since only a portion of the stream was decompressed and therefore the running adler32 checksum does not incorporate the entire amount of uncompressed data.
    */
    //If we have parsed all the input data and received a Z_DATA_ERROR, then assume that mismatch of the adler32 checksum occurred.
    // This may mask legitimate Z_DATA_ERROR's though
    if (r == Z_DATA_ERROR && m_strInflate->avail_in == 0)
    {
        AZ_Warning("IO", r != Z_DATA_ERROR, "ZLib inflate returned a data error, this is OK if the compressed data is being retrieved by using seek points");
    }
    else
    {
        AZ_Assert(r >= Z_OK || r == Z_BUF_ERROR, "ZLib decompress internal error %d", r);
    }
    
    dataSize = m_strInflate->avail_out;
    unsigned int processedCompressedData = compressedDataSize - m_strInflate->avail_in;
    //if( isLastChunk )
    //  inflateEnd(m_strm);
    return processedCompressedData;
}
//////////////////////////////////////////////////////////////////////////

#endif // #if !defined(AZCORE_EXCLUDE_ZLIB)
