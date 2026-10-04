#pragma once
#include <string>

class CEmbeddedMedia
{
public:
    enum class Result
    {
        Exported,
        Empty,
        Unsupported,
        Failed
    };

    static Result ExportAndRemove(const std::wstring& file_path, std::wstring& lyric_path, std::wstring& cover_path);
};
