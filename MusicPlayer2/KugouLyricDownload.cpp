#include "stdafx.h"
#include "KugouLyricDownload.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <iterator>

using json = nlohmann::json;

namespace
{
    const wchar_t* KUGOU_HEADERS = L"Referer: https://www.kugou.com/\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)";

    bool Base64Decode(const std::string& input, std::string& output)
    {
        static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int decode_table[256];
        std::fill(std::begin(decode_table), std::end(decode_table), -1);
        for (size_t i{}; i < base64_chars.size(); ++i)
            decode_table[static_cast<unsigned char>(base64_chars[i])] = static_cast<int>(i);

        output.clear();
        output.reserve(input.size() * 3 / 4);
        unsigned int value{};
        int value_bits{ -8 };
        for (unsigned char ch : input)
        {
            if (ch == '=')
                break;
            if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
                continue;
            const int decoded = decode_table[ch];
            if (decoded < 0)
                return false;
            value = (value << 6) | decoded;
            value_bits += 6;
            if (value_bits >= 0)
            {
                output.push_back(static_cast<char>((value >> value_bits) & 0xFF));
                value_bits -= 8;
            }
        }
        return !output.empty();
    }

    wstring NormalizeLyricText(wstring lyric)
    {
        if (!lyric.empty() && lyric.front() == 0xFEFF)
            lyric.erase(lyric.begin());

        wstring result;
        result.reserve(lyric.size() + lyric.size() / 20);
        for (wchar_t ch : lyric)
        {
            if (ch == L'\r')
                continue;
            if (ch == L'\n')
                result += L"\r\n";
            else
                result += ch;
        }
        return result;
    }

    wstring GetJsonString(const json& data, const char* key)
    {
        if (!data.contains(key) || !data[key].is_string())
            return wstring();
        return CCommon::StrToUnicode(data[key].get<std::string>(), CodeType::UTF8);
    }

    wstring NormalizeCoverUrl(wstring url)
    {
        if (url.empty())
            return url;
        for (size_t pos = url.find(L"{size}"); pos != wstring::npos; pos = url.find(L"{size}", pos))
            url.replace(pos, 6, L"480");
        if (url.rfind(L"http://", 0) == 0)
            url.replace(0, 7, L"https://");
        return url;
    }
}

std::wstring CKugouLyricDownload::GetSearchUrl(const std::wstring& key_words, int result_count)
{
    CString url;
    url.Format(L"http://mobilecdn.kugou.com/api/v3/search/song?format=json&keyword=%s&page=1&pagesize=%d&showtype=1", key_words.c_str(), result_count);
    return url.GetString();
}

void CKugouLyricDownload::DisposeSearchResult(vector<ItemInfo>& down_list, const wstring& search_result, int result_count)
{
    down_list.clear();
    try
    {
        json data = json::parse(search_result);
        if (!data.contains("data") || !data["data"].contains("info") || !data["data"]["info"].is_array())
            return;

        int count{};
        for (const auto& song_item : data["data"]["info"])
        {
            if (result_count > 0 && count >= result_count)
                break;
            ItemInfo item;
            item.id = GetJsonString(song_item, "hash");
            item.title = GetJsonString(song_item, "songname");
            item.artist = GetJsonString(song_item, "singername");
            item.album = GetJsonString(song_item, "album_name");
            item.duration = song_item.value("duration", 0) * 1000;
            if (item.id.empty())
                continue;
            CInternetCommon::DeleteStrSlash(item.title);
            CInternetCommon::DeleteStrSlash(item.artist);
            CInternetCommon::DeleteStrSlash(item.album);
            down_list.push_back(item);
            ++count;
        }
    }
    catch (const std::exception& e)
    {
        TRACE(L"KugouLyricDownload JSON parse error: %hs\n", e.what());
    }
}

std::wstring CKugouLyricDownload::GetAlbumCoverURL(const wstring& song_id)
{
    if (song_id.empty())
        return wstring();

    CString url;
    url.Format(L"https://m.kugou.com/app/i/getSongInfo.php?cmd=playInfo&hash=%s", song_id.c_str());
    wstring result;
    if (CInternetCommon::HttpGet(url.GetString(), result, KUGOU_HEADERS) != CInternetCommon::SUCCESS)
        return wstring();

    try
    {
        json data = json::parse(result);
        wstring cover_url = GetJsonString(data, "album_img");
        if (cover_url.empty() && data.contains("trans_param"))
            cover_url = GetJsonString(data["trans_param"], "union_cover");
        return NormalizeCoverUrl(cover_url);
    }
    catch (const std::exception& e)
    {
        TRACE(L"KugouLyricDownload cover JSON parse error: %hs\n", e.what());
    }
    return wstring();
}

std::wstring CKugouLyricDownload::GetOnlineUrl(const wstring& song_id)
{
    return L"https://www.kugou.com/song/#hash=" + song_id;
}

int CKugouLyricDownload::RequestSearch(const std::wstring& url, std::wstring& result)
{
    return CInternetCommon::HttpGet(url, result, KUGOU_HEADERS);
}

bool CKugouLyricDownload::DownloadLyric(const wstring& song_id, wstring& result, bool download_translate)
{
    if (song_id.empty())
        return false;

    CString search_url;
    search_url.Format(L"https://krcs.kugou.com/search?ver=1&man=yes&client=mobi&keyword=&duration=&hash=%s", song_id.c_str());
    wstring search_result;
    if (CInternetCommon::HttpGet(search_url.GetString(), search_result, KUGOU_HEADERS) != CInternetCommon::SUCCESS)
        return false;

    try
    {
        json data = json::parse(search_result);
        if (!data.contains("candidates") || !data["candidates"].is_array() || data["candidates"].empty())
            return false;

        const auto& candidate = data["candidates"].front();
        wstring lyric_id = GetJsonString(candidate, "id");
        wstring access_key = GetJsonString(candidate, "accesskey");
        if (lyric_id.empty() || access_key.empty())
            return false;

        CString download_url;
        download_url.Format(L"https://lyrics.kugou.com/download?ver=1&client=pc&id=%s&accesskey=%s&fmt=lrc&charset=utf8", lyric_id.c_str(), access_key.c_str());
        return CInternetCommon::HttpGet(download_url.GetString(), result, KUGOU_HEADERS) == CInternetCommon::SUCCESS;
    }
    catch (const std::exception& e)
    {
        TRACE(L"KugouLyricDownload lyric JSON parse error: %hs\n", e.what());
        return false;
    }
}

bool CKugouLyricDownload::DisposeLryic(wstring& lyric_str, bool download_translate)
{
    try
    {
        json data = json::parse(lyric_str);
        wstring content = GetJsonString(data, "content");
        if (content.empty())
            return false;

        std::string encoded = CCommon::UnicodeToAscii(content);
        std::string decoded;
        if (Base64Decode(encoded, decoded))
            lyric_str = CCommon::StrToUnicode(decoded, CodeType::UTF8);
        else
            lyric_str = content;

        lyric_str = NormalizeLyricText(lyric_str);
        return !lyric_str.empty();
    }
    catch (const std::exception& e)
    {
        TRACE(L"KugouLyricDownload lyric decode error: %hs\n", e.what());
        return false;
    }
}
