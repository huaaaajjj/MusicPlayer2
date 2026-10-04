#include "stdafx.h"
#include "EmbeddedMedia.h"
#include "taglib/fileref.h"
#include "taglib/mpegfile.h"
#include "taglib/flacfile.h"
#include "taglib/mp4file.h"
#include "taglib/mp4coverart.h"
#include "taglib/asffile.h"
#include "taglib/wavfile.h"
#include "taglib/id3v2tag.h"
#include "taglib/id3v2header.h"
#include "taglib/attachedpictureframe.h"
#include "taglib/unsynchronizedlyricsframe.h"
#include "taglib/apetag.h"
#include "taglib/xiphcomment.h"
#include "taglib/flacpicture.h"

namespace
{
    struct MediaData
    {
        TagLib::ByteVector cover;
        TagLib::String lyrics;
        bool has_cover{};
        bool has_lyrics{};
        bool unsupported{};

        void AddCover(const TagLib::ByteVector& data)
        {
            has_cover = true;
            if (cover.isEmpty() && !data.isEmpty())
                cover = data;
        }

        void AddLyrics(const TagLib::String& text)
        {
            has_lyrics = true;
            if (lyrics.isEmpty() && !text.isEmpty())
                lyrics = text;
        }
    };

    void ProcessId3(TagLib::ID3v2::Tag* tag, MediaData& media, bool remove)
    {
        if (tag == nullptr)
            return;
        if (remove)
        {
            tag->removeFrames("APIC");
            tag->removeFrames("USLT");
            return;
        }
        if (!tag->frameList("SYLT").isEmpty())
            media.unsupported = true;
        for (auto frame : tag->frameList("APIC"))
        {
            auto picture = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame);
            if (picture == nullptr)
                media.unsupported = true;
            else
                media.AddCover(picture->picture());
        }
        for (auto frame : tag->frameList("USLT"))
        {
            auto lyrics = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame*>(frame);
            if (lyrics == nullptr)
                media.unsupported = true;
            else
                media.AddLyrics(lyrics->text());
        }
    }

    void ProcessApe(TagLib::APE::Tag* tag, MediaData& media, bool remove)
    {
        if (tag == nullptr)
            return;
        const auto items = tag->itemListMap();
        for (const auto& item : items)
        {
            const auto key = item.first.upper();
            const bool cover = key.startsWith("COVER ART (");
            const bool lyrics = key == "LYRICS" || key == "UNSYNCEDLYRICS";
            if (remove && (cover || lyrics))
                tag->removeItem(item.first);
            else if (cover)
            {
                const auto data = item.second.binaryData();
                const int separator = data.find('\0');
                if (separator < 0)
                    media.unsupported = true;
                else
                    media.AddCover(data.mid(separator + 1));
            }
            else if (lyrics)
                media.AddLyrics(item.second.toString());
        }
    }

    void ProcessXiph(TagLib::Ogg::XiphComment* tag, MediaData& media, bool remove)
    {
        if (tag == nullptr)
            return;
        const auto fields = tag->fieldListMap();
        for (const auto& field : fields)
        {
            if (field.first.upper() == "LYRICS" || field.first.upper() == "UNSYNCEDLYRICS")
            {
                if (remove)
                    tag->removeFields(field.first);
                else
                {
                    media.has_lyrics = true;
                    for (const auto& text : field.second)
                        media.AddLyrics(text);
                }
            }
        }
        if (remove)
        {
            tag->removeAllPictures();
            tag->removeFields("COVERART");
            tag->removeFields("COVERARTMIME");
        }
        else
        {
            for (auto picture : tag->pictureList())
                media.AddCover(picture->data());
            const auto legacy = fields.find("COVERART");
            if (legacy != fields.end())
            {
                for (const auto& text : legacy->second)
                    media.AddCover(TagLib::ByteVector::fromBase64(text.data(TagLib::String::UTF8)));
            }
        }
    }

    bool ProcessTags(TagLib::File* file, MediaData& media, bool remove)
    {
        if (auto mpeg = dynamic_cast<TagLib::MPEG::File*>(file))
        {
            ProcessId3(mpeg->ID3v2Tag(), media, remove);
            ProcessApe(mpeg->APETag(), media, remove);
        }
        else if (auto flac = dynamic_cast<TagLib::FLAC::File*>(file))
        {
            ProcessId3(flac->ID3v2Tag(), media, remove);
            ProcessXiph(flac->xiphComment(), media, remove);
            if (remove)
                flac->removePictures();
            else
            {
                for (auto picture : flac->pictureList())
                    media.AddCover(picture->data());
            }
        }
        else if (auto wav = dynamic_cast<TagLib::RIFF::WAV::File*>(file))
            ProcessId3(wav->ID3v2Tag(), media, remove);
        else if (auto mp4 = dynamic_cast<TagLib::MP4::File*>(file))
        {
            auto tag = mp4->tag();
            if (tag == nullptr)
                return false;
            const auto items = tag->itemMap();
            for (const auto& item : items)
            {
                const bool lyrics = item.first == "\251lyr" || item.first == "----:com.apple.iTunes:Lyrics";
                const bool cover = item.first == "covr";
                if (remove && (lyrics || cover))
                    tag->removeItem(item.first);
                else if (lyrics)
                {
                    media.has_lyrics = true;
                    for (const auto& text : item.second.toStringList())
                        media.AddLyrics(text);
                }
                else if (cover)
                {
                    media.has_cover = true;
                    for (const auto& picture : item.second.toCoverArtList())
                        media.AddCover(picture.data());
                }
            }
        }
        else if (auto asf = dynamic_cast<TagLib::ASF::File*>(file))
        {
            auto tag = asf->tag();
            if (tag == nullptr)
                return false;
            const auto attributes = tag->attributeListMap();
            for (const auto& attribute : attributes)
            {
                const auto key = attribute.first.upper();
                const bool lyrics = key == "WM/LYRICS" || key == "LYRICS";
                const bool cover = key == "WM/PICTURE";
                if (key == "WM/LYRICS_SYNCHRONISED")
                    media.unsupported = true;
                if (remove && (lyrics || cover))
                    tag->removeItem(attribute.first);
                else if (lyrics || cover)
                {
                    for (const auto& value : attribute.second)
                    {
                        if (lyrics)
                            media.AddLyrics(value.toString());
                        else
                            media.AddCover(value.toPicture().picture());
                    }
                }
            }
        }
        else
            return false;
        return true;
    }

    std::wstring CoverExtension(const TagLib::ByteVector& data)
    {
        if (data.startsWith(TagLib::ByteVector("\xff\xd8\xff", 3)))
            return L".jpg";
        if (data.startsWith(TagLib::ByteVector("\x89PNG\r\n\x1a\n", 8)))
            return L".png";
        if (data.startsWith("GIF87a") || data.startsWith("GIF89a"))
            return L".gif";
        if (data.startsWith("BM"))
            return L".bmp";
        if (data.startsWith("RIFF") && data.mid(8, 4) == "WEBP")
            return L".webp";
        return {};
    }

    bool SaveSidecar(const std::wstring& directory, const std::wstring& path, const char* data, size_t size)
    {
        wchar_t temporary[MAX_PATH]{};
        if (size > MAXDWORD || GetTempFileNameW(directory.c_str(), L"mp2", 0, temporary) == 0)
            return false;
        HANDLE output = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool saved = false;
        if (output != INVALID_HANDLE_VALUE)
        {
            DWORD written{};
            saved = WriteFile(output, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
            if (saved)
                saved = FlushFileBuffers(output) != FALSE;
            if (!CloseHandle(output))
                saved = false;
        }
        if (saved)
            saved = MoveFileExW(temporary, path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!saved)
            DeleteFileW(temporary);
        return saved;
    }
}

CEmbeddedMedia::Result CEmbeddedMedia::ExportAndRemove(const std::wstring& file_path, std::wstring& lyric_path, std::wstring& cover_path)
{
    lyric_path.clear();
    cover_path.clear();
    TagLib::FileRef reference(file_path.c_str(), false);
    if (reference.isNull() || !reference.file()->isValid() || reference.file()->readOnly())
        return Result::Failed;
    MediaData media;
    if (!ProcessTags(reference.file(), media, false) || media.unsupported)
        return Result::Unsupported;
    if (!media.has_cover && !media.has_lyrics)
        return Result::Empty;
    if (media.has_lyrics && media.lyrics.isEmpty())
        return Result::Unsupported;
    const auto separator = file_path.find_last_of(L"\\/");
    const auto extension = file_path.find_last_of(L'.');
    if (separator == std::wstring::npos || extension == std::wstring::npos || extension <= separator)
        return Result::Failed;
    const auto directory = file_path.substr(0, separator + 1);
    const auto base_path = file_path.substr(0, extension);
    const auto cover_extension = CoverExtension(media.cover);
    if (media.has_cover && cover_extension.empty())
        return Result::Unsupported;
    if (media.has_cover && !SaveSidecar(directory, base_path + cover_extension, media.cover.data(), media.cover.size()))
        return Result::Failed;
    if (media.has_lyrics)
    {
        const auto text = media.lyrics.to8Bit(true);
        if (!SaveSidecar(directory, base_path + L".lrc", text.data(), text.size()))
            return Result::Failed;
    }
    if (media.has_cover)
    {
        for (const auto suffix : { L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp" })
        {
            if (cover_extension == suffix)
                continue;
            const auto previous = base_path + suffix;
            if (!DeleteFileW(previous.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
                return Result::Failed;
        }
    }
    ProcessTags(reference.file(), media, true);
    bool saved{};
    if (auto mpeg = dynamic_cast<TagLib::MPEG::File*>(reference.file()))
    {
        const auto tag = mpeg->ID3v2Tag();
        const auto version = tag != nullptr && tag->header()->majorVersion() == 3 ? TagLib::ID3v2::v3 : TagLib::ID3v2::v4;
        saved = mpeg->save(TagLib::MPEG::File::AllTags, TagLib::File::StripNone, version, TagLib::File::DoNotDuplicate);
    }
    else
        saved = reference.save();
    if (!saved)
        return Result::Failed;
    if (media.has_lyrics)
        lyric_path = base_path + L".lrc";
    if (media.has_cover)
        cover_path = base_path + cover_extension;
    return Result::Exported;
}
