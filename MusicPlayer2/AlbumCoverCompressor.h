#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

struct IStream;

class CAlbumCoverCompressor
{
public:
    static constexpr uint64_t MAX_COVER_BYTES = 4ULL * 1024 * 1024;

    CAlbumCoverCompressor() = default;
    ~CAlbumCoverCompressor();
    CAlbumCoverCompressor(const CAlbumCoverCompressor&) = delete;
    CAlbumCoverCompressor& operator=(const CAlbumCoverCompressor&) = delete;

    bool Prepare(const std::wstring& source_path);
    const std::wstring& GetPath() const { return m_path; }

private:
    bool SaveTemporaryImage(IStream* stream, ULONG size);
    void Clear();

    std::wstring m_path;
    std::wstring m_temporary_path;
    HANDLE m_source_handle{ INVALID_HANDLE_VALUE };
};
