#include "stdafx.h"
#include "AlbumCoverCompressor.h"
#include <atlbase.h>
#include <gdiplus.h>
#include <climits>

namespace
{
    bool GetJpegEncoder(CLSID& encoder)
    {
        UINT count{};
        UINT size{};
        if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || size == 0)
            return false;
        std::vector<BYTE> buffer(size);
        auto codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
        if (Gdiplus::GetImageEncoders(count, size, codecs) != Gdiplus::Ok)
            return false;
        for (UINT index = 0; index < count; ++index)
        {
            if (wcscmp(codecs[index].MimeType, L"image/jpeg") == 0)
            {
                encoder = codecs[index].Clsid;
                return true;
            }
        }
        return false;
    }

    bool ApplyOrientation(Gdiplus::Image& image)
    {
        const UINT size = image.GetPropertyItemSize(PropertyTagOrientation);
        if (size == 0)
            return true;
        std::vector<BYTE> buffer(size);
        auto property = reinterpret_cast<Gdiplus::PropertyItem*>(buffer.data());
        if (image.GetPropertyItem(PropertyTagOrientation, size, property) != Gdiplus::Ok ||
            property->type != PropertyTagTypeShort || property->length < sizeof(USHORT) || property->value == nullptr)
            return false;
        const auto orientation = *static_cast<USHORT*>(property->value);
        const Gdiplus::RotateFlipType rotations[] = {
            Gdiplus::RotateNoneFlipNone, Gdiplus::RotateNoneFlipX,
            Gdiplus::Rotate180FlipNone, Gdiplus::Rotate180FlipX,
            Gdiplus::Rotate90FlipX, Gdiplus::Rotate90FlipNone,
            Gdiplus::Rotate270FlipX, Gdiplus::Rotate270FlipNone
        };
        if (orientation < 1 || orientation > 8)
            return false;
        return image.RotateFlip(rotations[orientation - 1]) == Gdiplus::Ok;
    }
}

CAlbumCoverCompressor::~CAlbumCoverCompressor()
{
    Clear();
}

void CAlbumCoverCompressor::Clear()
{
    if (m_source_handle != INVALID_HANDLE_VALUE)
        CloseHandle(m_source_handle);
    m_source_handle = INVALID_HANDLE_VALUE;
    if (!m_temporary_path.empty())
        DeleteFileW(m_temporary_path.c_str());
    m_temporary_path.clear();
    m_path.clear();
}

bool CAlbumCoverCompressor::SaveTemporaryImage(IStream* stream, ULONG size)
{
    LARGE_INTEGER beginning{};
    std::vector<BYTE> bytes(size);
    ULONG read{};
    if (FAILED(stream->Seek(beginning, STREAM_SEEK_SET, nullptr)) ||
        FAILED(stream->Read(bytes.data(), size, &read)) || read != size)
        return false;

    wchar_t directory[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, directory);
    GUID identifier{};
    wchar_t identifier_text[40]{};
    if (length == 0 || length >= MAX_PATH || FAILED(CoCreateGuid(&identifier)) ||
        StringFromGUID2(identifier, identifier_text, _countof(identifier_text)) == 0)
        return false;
    const std::wstring path = std::wstring(directory) + L"MusicPlayer2Cover-" + identifier_text + L".jpg";
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        return false;
    m_temporary_path = path;
    DWORD written{};
    bool saved = WriteFile(output, bytes.data(), size, &written, nullptr) && written == size;
    if (saved)
        saved = FlushFileBuffers(output) != FALSE;
    if (!CloseHandle(output))
        saved = false;
    if (saved)
        m_path = path;
    return saved;
}

bool CAlbumCoverCompressor::Prepare(const std::wstring& source_path)
{
    Clear();
    if (source_path.empty())
        return true;
    m_source_handle = CreateFileW(source_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_source_handle == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    const bool readable = GetFileSizeEx(m_source_handle, &size) != FALSE;
    if (!readable || size.QuadPart <= 0)
        return false;
    if (static_cast<uint64_t>(size.QuadPart) <= MAX_COVER_BYTES)
    {
        m_path = source_path;
        return true;
    }

    Gdiplus::Image image(source_path.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok || !ApplyOrientation(image))
        return false;
    UINT width = image.GetWidth();
    UINT height = image.GetHeight();
    if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX)
        return false;
    CLSID encoder{};
    if (!GetJpegEncoder(encoder))
        return false;

    while (true)
    {
        Gdiplus::Bitmap bitmap(width, height, PixelFormat24bppRGB);
        if (bitmap.GetLastStatus() != Gdiplus::Ok)
            return false;
        {
            Gdiplus::Graphics graphics(&bitmap);
            Gdiplus::ImageAttributes attributes;
            if (graphics.GetLastStatus() != Gdiplus::Ok ||
                graphics.Clear(Gdiplus::Color::White) != Gdiplus::Ok ||
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic) != Gdiplus::Ok ||
                attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY) != Gdiplus::Ok ||
                graphics.DrawImage(&image, Gdiplus::Rect(0, 0, width, height), 0, 0,
                    image.GetWidth(), image.GetHeight(), Gdiplus::UnitPixel, &attributes) != Gdiplus::Ok)
                return false;
        }
        for (ULONG quality : { 90UL, 80UL, 70UL, 60UL })
        {
            CComPtr<IStream> stream;
            if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
                return false;
            Gdiplus::EncoderParameters parameters{};
            parameters.Count = 1;
            parameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
            parameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
            parameters.Parameter[0].NumberOfValues = 1;
            parameters.Parameter[0].Value = &quality;
            if (bitmap.Save(stream, &encoder, &parameters) != Gdiplus::Ok)
                return false;
            STATSTG statistics{};
            if (FAILED(stream->Stat(&statistics, STATFLAG_NONAME)))
                return false;
            if (statistics.cbSize.QuadPart > 0 && statistics.cbSize.QuadPart <= MAX_COVER_BYTES)
                return SaveTemporaryImage(stream, static_cast<ULONG>(statistics.cbSize.QuadPart));
        }
        if (width == 1 && height == 1)
            return false;
        width = (std::max)(1U, static_cast<UINT>(width * 0.8));
        height = (std::max)(1U, static_cast<UINT>(height * 0.8));
    }
}
