#pragma once
#include "LyricDownloadCommon.h"

//酷狗音乐歌词和专辑封面下载
class CKugouLyricDownload : public CLyricDownloadCommon
{
public:
    std::wstring GetSearchUrl(const std::wstring& key_words, int result_count = 20) override;
    std::wstring GetAlbumCoverURL(const wstring& song_id) override;
    std::wstring GetOnlineUrl(const wstring& song_id) override;
    void DisposeSearchResult(vector<ItemInfo>& down_list, const wstring& search_result, int result_count) override;
    int RequestSearch(const std::wstring& url, std::wstring& result) override;
    bool DownloadLyric(const wstring& song_id, wstring& result, bool download_translate) override;
    bool DisposeLryic(wstring& lyric_str, bool download_translate) override;
};
