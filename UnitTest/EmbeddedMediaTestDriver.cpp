#include "../MusicPlayer2/stdafx.h"
#include "../MusicPlayer2/EmbeddedMedia.h"
#include "../MusicPlayer2/AlbumCoverCompressor.h"
#include <gdiplus.h>

int wmain(int argument_count, wchar_t* arguments[])
{
    if (argument_count == 4 && std::wstring(arguments[1]) == L"--prepare-cover")
    {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token{};
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
            return 5;
        int result = 3;
        std::wstring prepared_path;
        {
            CAlbumCoverCompressor cover;
            if (cover.Prepare(arguments[2]))
            {
                prepared_path = cover.GetPath();
                result = CopyFileW(prepared_path.c_str(), arguments[3], TRUE) ? 0 : 6;
            }
        }
        if (!prepared_path.empty() && prepared_path != arguments[2] &&
            GetFileAttributesW(prepared_path.c_str()) != INVALID_FILE_ATTRIBUTES)
            result = 7;
        Gdiplus::GdiplusShutdown(token);
        return result;
    }
    if (argument_count != 2)
        return 4;
    std::wstring lyrics;
    std::wstring cover;
    return static_cast<int>(CEmbeddedMedia::ExportAndRemove(arguments[1], lyrics, cover));
}
