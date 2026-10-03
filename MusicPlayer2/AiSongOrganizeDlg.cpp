#include "stdafx.h"
#include "AiSongOrganizeDlg.h"
#include "AiSettingDlg.h"
#include "MusicPlayer2.h"
#include "SongDataManager.h"
#include "COSUPlayerHelper.h"
#include "IniHelper.h"
#include "LyricDownloadCommon.h"
#include "InternetCommon.h"
#include "nlohmann/json.hpp"

#include <fstream>
#include <sstream>
#include <set>
#include <map>

using json = nlohmann::json;

//AI分析的系统提示词
static const wchar_t* AI_SYSTEM_PROMPT = LR"(你是一个音乐元数据整理助手。用户会给出一个音频文件的文件名和现有标签信息（可能残缺、错误或混乱）。请根据文件名和你的音乐知识推断出这首歌正确的元数据。
严格只输出一个JSON对象，不要输出任何其他文字、解释或markdown代码块标记，格式如下：
{"title":"歌曲标题","artist":"艺术家","album":"专辑名","album_artist":"专辑艺术家","confidence":85,"changed":true}
规则：
1. changed表示推断出的元数据与原标签相比是否有实质性修正，如果原标签信息看起来已经正确，则为false，各字段仍需返回
2. confidence是对推断结果正确性的置信度（0-100的整数）
3. 无法从文件名推断专辑或专辑艺术家时返回空字符串
4. 多位艺术家之间用“/”分隔
5. 不要凭空编造信息，不确定时降低confidence
6. 文件名中常见的杂质（音质标注、网站名、序号、括号注释等）应忽略或清理)";

//向状态文本追加一段翻译文本，非首段时自动插入分隔符
static void AiAppendStatus(wstring& status, const wchar_t* key)
{
    if (!status.empty())
        status += theApp.m_str_table.LoadText(L"TXT_AI_STATUS_SEPARATOR");
    status += theApp.m_str_table.LoadText(key);
}

//保存歌词到文件（UTF-8格式），返回是否成功
static bool AiSaveLyric(const wchar_t* path, const wstring& lyric_wcs)
{
    string lyric_str = CCommon::UnicodeToStr(lyric_wcs, CodeType::UTF8);
    ofstream out_put{ path, std::ios::binary };
    if (out_put.fail())
        return false;
    out_put << lyric_str;
    return true;
}

//下载一个文件到本地（带超时控制，替代无超时的URLDownloadToFile，避免在异常网络环境下卡死）
static bool AiDownloadFile(const wstring& url, const wstring& save_path)
{
    try
    {
        CInternetSession session;
        session.SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, 15000);
        session.SetOption(INTERNET_OPTION_SEND_TIMEOUT, 15000);
        session.SetOption(INTERNET_OPTION_RECEIVE_TIMEOUT, 30000);
        CStdioFile* pFile = session.OpenURL(url.c_str(), 1, INTERNET_FLAG_TRANSFER_BINARY | INTERNET_FLAG_RELOAD);
        if (pFile == nullptr)
            return false;
        ofstream out_file(save_path.c_str(), std::ios::binary);
        bool ok = !out_file.fail();
        char buff[8192];
        UINT read_size;
        while (ok && (read_size = pFile->Read(buff, sizeof(buff))) > 0)
        {
            out_file.write(buff, read_size);
            ok = !out_file.fail();
        }
        out_file.close();
        pFile->Close();
        delete pFile;
        return ok;
    }
    catch (CInternetException* e)
    {
        e->Delete();
        return false;
    }
    catch (...)
    {
        return false;
    }
}

//应用阶段处理一首歌曲，返回要显示在状态列的文本；can_skip为false时表示有需要重试的失败，不记录到断点
static wstring AiApplyOneSong(CAiSongOrganizeDlg::ThreadInfo* pInfo, int row, bool& can_skip)
{
    wstring status;
    can_skip = true;
    const SongInfo& song = pInfo->songs->at(row);
    AiSongInfo ai;
    {
        CSingleLock lock(pInfo->cs, TRUE);
        ai = pInfo->ai_results->at(row);
    }
    if (!ai.analyzed)
        return theApp.m_str_table.LoadText(L"TXT_AI_STATUS_NOT_ANALYZED");
    //cue分轨和osu文件不支持整理
    if (song.is_cue || COSUPlayerHelper::IsOsuFile(song.file_path))
        return theApp.m_str_table.LoadText(L"TXT_AI_STATUS_SKIPPED");

    SongInfo song_info = CSongDataManager::GetInstance().GetSongInfo3(song);
    CFilePathHelper file_path(song.file_path);
    wstring ext = file_path.GetFileExtension();
    //正在播放的文件被播放器占用，写入操作需延迟到主线程在CPlayer::ReOpen保护下完成
    bool file_locked = (song.file_path == CPlayer::GetInstance().GetCurrentSongInfo().file_path);
    CAiSongOrganizeDlg::PendingWrite pending;
    pending.row = row;

    //1.将AI识别结果写回标签
    if (pInfo->write_tag && ai.changed)
    {
        if (!CAudioTag::IsFileTypeTagWriteSupport(ext))
        {
            AiAppendStatus(status, L"TXT_AI_STATUS_EMBED_UNSUPPORTED");
        }
        else
        {
            //AI结果为空时保留原值
            if (!ai.title.empty()) song_info.title = ai.title;
            if (!ai.artist.empty()) song_info.artist = ai.artist;
            if (!ai.album.empty()) song_info.album = ai.album;
            if (!ai.album_artist.empty()) song_info.album_artist = ai.album_artist;
            if (file_locked)
            {
                pending.write_tag = true;
            }
            else
            {
                CAudioTag audio_tag(song_info);
                if (audio_tag.WriteAudioTag())
                {
                    AiAppendStatus(status, L"TXT_AI_STATUS_TAG_OK");
                }
                else
                {
                    AiAppendStatus(status, L"TXT_AI_STATUS_WRITE_FAILED");
                    can_skip = false;
                }
            }
        }
    }

    //2.搜索歌曲（下载歌词和封面共用一次搜索结果）
    bool item_found = false;
    CLyricDownloadCommon::ItemInfo item;
    if (pInfo->download_lyric || pInfo->download_cover)
    {
        wstring title = ai.title.empty() ? song_info.title : ai.title;
        wstring artist = ai.artist.empty() ? song_info.artist : ai.artist;
        wstring album = ai.album.empty() ? song_info.album : ai.album;
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_TITLE") == title) title.clear();
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ARTIST") == artist) artist.clear();
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ALBUM") == album) album.clear();
        DownloadResult download_result;
        item = theApp.GetLyricDownload()->SearchSongAndGetMatched(title, artist, album, song.GetFileName(), false, &download_result);
        item_found = (download_result == DR_SUCCESS && !item.id.empty());
        if (!item_found)
        {
            if (download_result == DR_NETWORK_ERROR)
            {
                AiAppendStatus(status, L"TXT_AI_STATUS_NETWORK_FAILED");
                can_skip = false;
            }
            else
            {
                AiAppendStatus(status, L"TXT_AI_STATUS_NOT_FOUND");
            }
        }
    }

    //3.下载歌词并保存为lrc文件
    wstring lyric_str;
    bool lyric_saved = false;
    if (item_found && pInfo->download_lyric)
    {
        theApp.GetLyricDownload()->DownloadLyric(item.id, lyric_str, false);
        if (!lyric_str.empty() && theApp.GetLyricDownload()->DisposeLryic(lyric_str, false))
        {
            CLyricDownloadCommon::AddLyricTag(lyric_str, item.id, item.title, item.artist, item.album);
            wstring file_name = CFilePathHelper(song.GetFileName()).ReplaceFileExtension(nullptr);
            wstring lyric_path = file_path.GetDir() + file_name + L".lrc";
            if (AiSaveLyric(lyric_path.c_str(), lyric_str))
            {
                lyric_saved = true;
                AiAppendStatus(status, L"TXT_AI_STATUS_LYRIC_OK");
            }
        }
        if (!lyric_saved)
        {
            AiAppendStatus(status, L"TXT_AI_STATUS_NO_LYRIC");
        }
    }

    //4.内嵌歌词到音频文件
    if (lyric_saved && pInfo->embed_lyric)
    {
        if (!CAudioTag::IsFileTypeLyricWriteSupport(ext))
        {
            AiAppendStatus(status, L"TXT_AI_STATUS_EMBED_UNSUPPORTED");
        }
        else if (file_locked)
        {
            pending.lyric = lyric_str;
        }
        else
        {
            CAudioTag audio_tag(song_info);
            if (audio_tag.WriteAudioLyric(lyric_str))
            {
                AiAppendStatus(status, L"TXT_AI_STATUS_EMBED_OK");
            }
            else
            {
                AiAppendStatus(status, L"TXT_AI_STATUS_WRITE_FAILED");
                can_skip = false;
            }
        }
    }

    //5.下载专辑封面
    wstring cover_path;
    bool cover_saved = false;
    if (item_found && pInfo->download_cover)
    {
        wstring cover_url = theApp.GetLyricDownload()->GetAlbumCoverURL(item.id);
        if (!cover_url.empty())
        {
            wstring album_name = song_info.album.empty() ? song_info.title : song_info.album;
            CCommon::FileNameNormalize(album_name);
            CFilePathHelper url_path(cover_url);
            cover_path = file_path.GetDir() + album_name + url_path.GetFileExtension(false, true);
            if (CCommon::FileExist(cover_path))
                ::DeleteFile(cover_path.c_str());
            if (AiDownloadFile(cover_url, cover_path))
            {
                ::SetFileAttributes(cover_path.c_str(), FILE_ATTRIBUTE_HIDDEN);
                cover_saved = true;
                AiAppendStatus(status, L"TXT_AI_STATUS_COVER_OK");
            }
            else
            {
                AiAppendStatus(status, L"TXT_AI_STATUS_DOWNLOAD_FAILED");
                can_skip = false;
            }
        }
    }

    //6.内嵌专辑封面
    if (cover_saved && pInfo->embed_cover)
    {
        if (!CAudioTag::IsFileTypeCoverWriteSupport(ext))
        {
            AiAppendStatus(status, L"TXT_AI_STATUS_EMBED_UNSUPPORTED");
        }
        else if (file_locked)
        {
            pending.cover_path = cover_path;
        }
        else
        {
            CAudioTag audio_tag(song_info);
            if (audio_tag.WriteAlbumCover(cover_path))
            {
                AiAppendStatus(status, L"TXT_AI_STATUS_COVER_EMBED_OK");
            }
            else
            {
                AiAppendStatus(status, L"TXT_AI_STATUS_WRITE_FAILED");
                can_skip = false;
            }
        }
    }

    //保存歌曲ID等缓存信息
    if (item_found)
        CSongDataManager::GetInstance().AddItem(song_info);

    //有被占用而无法在本线程写入的操作时，登记到待写入队列，由主线程在应用阶段结束后完成
    if (pending.write_tag || !pending.lyric.empty() || !pending.cover_path.empty())
    {
        pending.song_info = song_info;
        pending.status = status;        //主线程从这里续写状态
        {
            CSingleLock lock(pInfo->cs, TRUE);
            pInfo->pending_writes->push_back(pending);
        }
        can_skip = false;              //主线程写完后才记入断点
        AiAppendStatus(status, L"TXT_AI_STATUS_PENDING_MAIN");
    }

    if (status.empty())
        status = theApp.m_str_table.LoadText(L"TXT_AI_STATUS_NOTHING_DONE");
    return status;
}

//---------- 断点检查点文件的读写 ----------

//检查点文件路径（保存在程序配置目录）
wstring CAiSongOrganizeDlg::CheckpointFilePath()
{
    return theApp.m_config_dir + L"ai_organize_checkpoint.json";
}

//检查点写入节流：距上次写入不足2秒且非强制时跳过。
//ponytail: 全局节流，所有调用点都在m_cs锁内所以无需额外同步；丢失上限为2秒的分析成果
static ULONGLONG g_checkpoint_last_save_tick{ 0 };
#define CHECKPOINT_SAVE_INTERVAL_MS 2000

//写入检查点文件（先写临时文件再替换，保证中断时不会写坏），需在m_cs锁内调用
void CAiSongOrganizeDlg::SaveCheckpoint(bool force)
{
    SaveCheckpointStatic(m_analyze_ckpt, m_apply_done, force);
}

//写入检查点文件（静态版本，供工作线程通过ThreadInfo中的容器调用），需在m_cs锁内调用
void CAiSongOrganizeDlg::SaveCheckpointStatic(const std::map<wstring, AiSongInfo>& analyze_ckpt, const std::set<wstring>& apply_done, bool force)
{
    ULONGLONG now = ::GetTickCount64();
    if (!force && now - g_checkpoint_last_save_tick < CHECKPOINT_SAVE_INTERVAL_MS)
        return;
    g_checkpoint_last_save_tick = now;

    json j;
    j["version"] = 1;
    json& ja = j["analyze"] = json::object();
    for (const auto& item : analyze_ckpt)
    {
        string key = CCommon::UnicodeToStr(item.first, CodeType::UTF8_NO_BOM);
        json& val = ja[key] = json::object();
        val["title"] = CCommon::UnicodeToStr(item.second.title, CodeType::UTF8_NO_BOM);
        val["artist"] = CCommon::UnicodeToStr(item.second.artist, CodeType::UTF8_NO_BOM);
        val["album"] = CCommon::UnicodeToStr(item.second.album, CodeType::UTF8_NO_BOM);
        val["album_artist"] = CCommon::UnicodeToStr(item.second.album_artist, CodeType::UTF8_NO_BOM);
        val["confidence"] = item.second.confidence;
        val["changed"] = item.second.changed;
    }
    json& jd = j["apply_done"] = json::array();
    for (const auto& path : apply_done)
        jd.push_back(CCommon::UnicodeToStr(path, CodeType::UTF8_NO_BOM));

    wstring path = CheckpointFilePath();
    try
    {
        ofstream file((path + L".tmp").c_str(), std::ios::binary);
        if (file.fail()) return;
        file << j.dump();
        file.close();
        ::MoveFileExW((path + L".tmp").c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    catch (...)
    {
    }
}

//读取检查点文件
void CAiSongOrganizeDlg::LoadCheckpoint()
{
    m_analyze_ckpt.clear();
    m_apply_done.clear();
    ifstream file(CheckpointFilePath().c_str());
    if (file.fail()) return;
    std::stringstream ss;
    ss << file.rdbuf();
    try
    {
        json j = json::parse(ss.str());
        if (j.contains("analyze") && j["analyze"].is_object())
        {
            for (auto& item : j["analyze"].items())
            {
                AiSongInfo info;
                info.title = CCommon::StrToUnicode(item.value().value("title", ""), CodeType::UTF8);
                info.artist = CCommon::StrToUnicode(item.value().value("artist", ""), CodeType::UTF8);
                info.album = CCommon::StrToUnicode(item.value().value("album", ""), CodeType::UTF8);
                info.album_artist = CCommon::StrToUnicode(item.value().value("album_artist", ""), CodeType::UTF8);
                info.confidence = item.value().value("confidence", 0);
                info.changed = item.value().value("changed", false);
                info.analyzed = true;
                m_analyze_ckpt[CCommon::StrToUnicode(item.key(), CodeType::UTF8)] = info;
            }
        }
        if (j.contains("apply_done") && j["apply_done"].is_array())
        {
            for (const auto& item : j["apply_done"])
                m_apply_done.insert(CCommon::StrToUnicode(item.get<string>(), CodeType::UTF8));
        }
    }
    catch (...)
    {
        //检查点文件损坏时忽略，按无断点处理
    }
}

//分析一行：调用AI推断元数据并填入预览列表
static void AiAnalyzeRow(CAiSongOrganizeDlg::ThreadInfo* pInfo, int i)
{
    const SongInfo& cur_song = pInfo->songs->at(i);
    //cue分轨和osu文件跳过
    if (cur_song.is_cue || COSUPlayerHelper::IsOsuFile(cur_song.file_path))
    {
        if (::IsWindow(pInfo->hwnd))
            pInfo->list_ctrl->SetItemText(i, CAiSongOrganizeDlg::COL_STATUS, theApp.m_str_table.LoadText(L"TXT_AI_STATUS_SKIPPED").c_str());
        return;
    }

    //断点中已分析过的曲目直接跳过，不重复调用AI接口（状态列保留RestoreAnalyzeFromCheckpoint写入的“已恢复”）
    {
        CSingleLock lock(pInfo->cs, TRUE);
        if ((*pInfo->ai_results)[i].analyzed)
            return;
    }

    //构造用户消息：文件名+现有标签
    wstring original_title = cur_song.GetTitle();
    wstring original_artist = cur_song.GetArtist();
    wstring original_album = cur_song.GetAlbum();
    if (theApp.m_str_table.LoadText(L"TXT_EMPTY_TITLE") == original_title) original_title.clear();
    if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ARTIST") == original_artist) original_artist.clear();
    if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ALBUM") == original_album) original_album.clear();
    wstring user_content = theApp.m_str_table.LoadText(L"TXT_AI_PROMPT_FILE_NAME") + cur_song.GetFileName();
    user_content += L"\n";
    user_content += theApp.m_str_table.LoadText(L"TXT_AI_PROMPT_ORIGINAL_TAG");
    user_content += (L"标题=" + original_title + L"，歌手=" + original_artist + L"，专辑=" + original_album);

    //调用AI接口，解析失败重试1次
    AiSongInfo result;
    bool analyze_ok = false;
    for (int attempt{}; attempt < 2 && !analyze_ok; attempt++)
    {
        if (theApp.m_ai_organize_dialog_exit || !::IsWindow(pInfo->hwnd))
            return;
        wstring response;
        wstring error_info;
        int rtn = CAiClient::ChatComplete(AI_SYSTEM_PROMPT, user_content, response, error_info);
        if (rtn != CAiClient::AI_SUCCESS)
            continue;
        wstring json_str = CAiClient::ExtractJson(response);
        if (json_str.empty())
            continue;
        try
        {
            json data = json::parse(json_str);
            if (!data.is_object())
                continue;
            result.title = CCommon::StrToUnicode(data.value("title", ""), CodeType::UTF8);
            result.artist = CCommon::StrToUnicode(data.value("artist", ""), CodeType::UTF8);
            result.album = CCommon::StrToUnicode(data.value("album", ""), CodeType::UTF8);
            result.album_artist = CCommon::StrToUnicode(data.value("album_artist", ""), CodeType::UTF8);
            result.confidence = data.value("confidence", 0);
            result.changed = data.value("changed", false);
            result.analyzed = true;
            analyze_ok = true;
        }
        catch (...)
        {
        }
    }

    if (!analyze_ok)
    {
        if (::IsWindow(pInfo->hwnd))
            pInfo->list_ctrl->SetItemText(i, CAiSongOrganizeDlg::COL_STATUS, theApp.m_str_table.LoadText(L"TXT_AI_STATUS_AI_FAILED").c_str());
        return;
    }

    //保存结果并写入断点
    {
        CSingleLock lock(pInfo->cs, TRUE);
        (*pInfo->ai_results)[i] = result;
        (*pInfo->analyze_ckpt)[cur_song.file_path] = result;
        CAiSongOrganizeDlg::SaveCheckpointStatic(*pInfo->analyze_ckpt, *pInfo->apply_done);
    }

    if (::IsWindow(pInfo->hwnd))
    {
        pInfo->list_ctrl->SetItemText(i, CAiSongOrganizeDlg::COL_AI_TITLE, result.title.c_str());
        pInfo->list_ctrl->SetItemText(i, CAiSongOrganizeDlg::COL_AI_ARTIST, result.artist.c_str());
        pInfo->list_ctrl->SetItemText(i, CAiSongOrganizeDlg::COL_AI_ALBUM, result.album.c_str());
        CString confidence;
        confidence.Format(_T("%d%%"), result.confidence);
        pInfo->list_ctrl->SetItemText(i, CAiSongOrganizeDlg::COL_CONFIDENCE, confidence);
        //无需修改或置信度低的条目不自动勾选
        if (result.changed && result.confidence >= 60)
            pInfo->list_ctrl->SetCheck(i, TRUE);
        pInfo->list_ctrl->SetItemText(i, CAiSongOrganizeDlg::COL_STATUS, theApp.m_str_table.LoadText(L"TXT_AI_STATUS_ANALYZED").c_str());
    }
}

//应用一行
static void AiApplyRow(CAiSongOrganizeDlg::ThreadInfo* pInfo, int i)
{
    int row = pInfo->selected_rows->at(i);
    const SongInfo& song = pInfo->songs->at(row);

    //断点中已完成的曲目跳过
    {
        CSingleLock lock(pInfo->cs, TRUE);
        if (pInfo->apply_done->count(song.file_path) != 0)
        {
            if (::IsWindow(pInfo->hwnd))
                pInfo->list_ctrl->SetItemText(row, CAiSongOrganizeDlg::COL_STATUS, theApp.m_str_table.LoadText(L"TXT_AI_STATUS_DONE_BEFORE").c_str());
            return;
        }
    }

    if (::IsWindow(pInfo->hwnd))
    {
        pInfo->list_ctrl->SetItemText(row, CAiSongOrganizeDlg::COL_STATUS, theApp.m_str_table.LoadText(L"TXT_AI_STATUS_APPLYING").c_str());
        pInfo->list_ctrl->EnsureVisible(row, FALSE);
    }

    bool can_skip = true;
    wstring status = AiApplyOneSong(pInfo, row, can_skip);
    if (can_skip)
    {
        //记录到断点（失败的曲目不记录，下次恢复时重试）
        CSingleLock lock(pInfo->cs, TRUE);
        pInfo->apply_done->insert(song.file_path);
        CAiSongOrganizeDlg::SaveCheckpointStatic(*pInfo->analyze_ckpt, *pInfo->apply_done);
    }
    if (::IsWindow(pInfo->hwnd))
        pInfo->list_ctrl->SetItemText(row, CAiSongOrganizeDlg::COL_STATUS, status.c_str());
}

// CAiSongOrganizeDlg 对话框

IMPLEMENT_DYNAMIC(CAiSongOrganizeDlg, CBaseDialog)

CAiSongOrganizeDlg::CAiSongOrganizeDlg(CWnd* pParent /*=NULL*/)
    : CBaseDialog(IDD_AI_ORGANIZE_DIALOG, pParent)
{
}

CAiSongOrganizeDlg::~CAiSongOrganizeDlg()
{
}

CString CAiSongOrganizeDlg::GetDialogName() const
{
    return _T("AiSongOrganizeDlg");
}

bool CAiSongOrganizeDlg::InitializeControls()
{
    wstring temp;
    temp = theApp.m_str_table.LoadText(L"TITLE_AI_ORGANIZE");
    SetWindowTextW(temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_OPT_GROUP");
    SetDlgItemTextW(IDC_TXT_AI_OPT_GROUP_STATIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_OPT_WRITE_TAG");
    SetDlgItemTextW(IDC_AI_OPT_WRITE_TAG, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_OPT_DOWNLOAD_LYRIC");
    SetDlgItemTextW(IDC_AI_OPT_DOWNLOAD_LYRIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_OPT_EMBED_LYRIC");
    SetDlgItemTextW(IDC_AI_OPT_EMBED_LYRIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_OPT_DOWNLOAD_COVER");
    SetDlgItemTextW(IDC_AI_OPT_DOWNLOAD_COVER, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_OPT_EMBED_COVER");
    SetDlgItemTextW(IDC_AI_OPT_EMBED_COVER, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_CONCURRENCY");
    SetDlgItemTextW(IDC_TXT_AI_CONCURRENCY_STATIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_SETTING");
    SetDlgItemTextW(IDC_AI_SETTING_BTN, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_SONG_LIST");
    SetDlgItemTextW(IDC_TXT_AI_SONG_LIST_STATIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_SELECT_ALL");
    SetDlgItemTextW(IDC_AI_SELECT_ALL, temp.c_str());
    m_select_all_btn.SetCheck(FALSE);
    temp = theApp.m_str_table.LoadText(L"TXT_AI_START_ANALYZE");
    SetDlgItemTextW(IDC_AI_START_ANALYZE, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_CANCEL");
    SetDlgItemTextW(IDC_AI_CANCEL_BTN, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_APPLY_SELECTED");
    SetDlgItemTextW(IDC_AI_APPLY_SELECTED, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_CLOSE");
    SetDlgItemTextW(IDCANCEL, temp.c_str());
    SetDlgItemTextW(IDC_AI_PROGRESS_BAR, L"");
    SetDlgItemTextW(IDC_AI_INFO_STATIC, L"");

    RepositionTextBasedControls({
        { CtrlTextInfo::L1, IDC_AI_PROGRESS_BAR },
        { CtrlTextInfo::C0, IDC_AI_INFO_STATIC },
        { CtrlTextInfo::R1, IDC_AI_START_ANALYZE, CtrlTextInfo::W32 },
        { CtrlTextInfo::R2, IDC_AI_APPLY_SELECTED, CtrlTextInfo::W32 },
        { CtrlTextInfo::R3, IDCANCEL, CtrlTextInfo::W32 }
        });
    return true;
}

void CAiSongOrganizeDlg::SaveConfig() const
{
    CIniHelper ini(theApp.m_config_path);
    ini.WriteBool(L"ai_organize", L"write_tag", m_write_tag);
    ini.WriteBool(L"ai_organize", L"download_lyric", m_download_lyric);
    ini.WriteBool(L"ai_organize", L"embed_lyric", m_embed_lyric);
    ini.WriteBool(L"ai_organize", L"download_cover", m_download_cover);
    ini.WriteBool(L"ai_organize", L"embed_cover", m_embed_cover);
    ini.WriteInt(L"ai_organize", L"concurrency", m_concurrency);
    ini.Save();
}

void CAiSongOrganizeDlg::LoadConfig()
{
    CIniHelper ini(theApp.m_config_path);
    m_write_tag = ini.GetBool(L"ai_organize", L"write_tag", true);
    m_download_lyric = ini.GetBool(L"ai_organize", L"download_lyric", true);
    m_embed_lyric = ini.GetBool(L"ai_organize", L"embed_lyric", true);
    m_download_cover = ini.GetBool(L"ai_organize", L"download_cover", true);
    m_embed_cover = ini.GetBool(L"ai_organize", L"embed_cover", false);
    m_concurrency = ini.GetInt(L"ai_organize", L"concurrency", 3);
    if (m_concurrency < 1) m_concurrency = 1;
    if (m_concurrency > 8) m_concurrency = 8;
}

void CAiSongOrganizeDlg::EnableControls(bool enable)
{
    m_write_tag_chk.EnableWindow(enable);
    m_download_lyric_chk.EnableWindow(enable);
    m_embed_lyric_chk.EnableWindow(enable);
    m_download_cover_chk.EnableWindow(enable);
    m_embed_cover_chk.EnableWindow(enable);
    m_concurrency_edit.EnableWindow(enable);
    m_select_all_btn.EnableWindow(enable);
    GetDlgItem(IDC_AI_START_ANALYZE)->EnableWindow(enable);
    GetDlgItem(IDC_AI_APPLY_SELECTED)->EnableWindow(enable);
    //正在执行任务时取消按钮可用，空闲时禁用
    m_cancel_btn.EnableWindow(!enable);
}

void CAiSongOrganizeDlg::FillThreadInfo(int phase)
{
    m_thread_info = ThreadInfo();
    m_thread_info.hwnd = GetSafeHwnd();
    m_thread_info.phase = phase;
    m_thread_info.list_ctrl = &m_song_list_ctrl;
    m_thread_info.static_ctrl = &m_info_static;
    m_thread_info.progress_bar = &m_progress_bar;
    m_thread_info.songs = &m_songs;
    m_thread_info.ai_results = &m_ai_results;
    m_thread_info.cs = &m_cs;
    m_thread_info.write_tag = m_write_tag;
    m_thread_info.download_lyric = m_download_lyric;
    m_thread_info.embed_lyric = m_embed_lyric;
    m_thread_info.download_cover = m_download_cover;
    m_thread_info.embed_cover = m_embed_cover;
    m_thread_info.p_next_index = &m_next_index;
    m_thread_info.p_processed = &m_processed;
    m_thread_info.p_active = &m_active_threads;
    m_thread_info.selected_rows = &m_selected_rows;
    m_thread_info.analyze_ckpt = &m_analyze_ckpt;
    m_thread_info.apply_done = &m_apply_done;
    m_thread_info.pending_writes = &m_pending_writes;
    m_thread_info.total = (phase == PH_APPLY) ? static_cast<int>(m_selected_rows.size()) : static_cast<int>(m_songs.size());
}

void CAiSongOrganizeDlg::StartThread(int phase)
{
    //读取并发线程数
    wchar_t buff[16]{};
    m_concurrency_edit.GetWindowText(buff, 16);
    int concurrency = _wtoi(buff);
    if (concurrency < 1) concurrency = 1;
    if (concurrency > 8) concurrency = 8;
    m_concurrency = concurrency;

    m_progress_bar.ShowWindow(SW_SHOW);
    m_next_index = 0;
    m_processed = 0;
    FillThreadInfo(phase);
    theApp.m_ai_organize_dialog_exit = false;

    //创建工作线程（先全部挂起，设置好活动线程计数后再一起恢复）
    m_thread_count = 0;
    for (int k{}; k < m_concurrency; k++)
    {
        CWinThread* pThread = AfxBeginThread(ThreadFunc, &m_thread_info, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
        if (pThread == nullptr)
            break;
        pThread->m_bAutoDelete = FALSE;     //线程句柄保持有效，退出时等待
        m_pThreads[m_thread_count++] = pThread;
    }
    m_active_threads = m_thread_count;
    for (int k{}; k < m_thread_count; k++)
        m_pThreads[k]->ResumeThread();
}

void CAiSongOrganizeDlg::StopThread()
{
    //等待所有工作线程退出。线程可能正在向UI线程SendMessage更新控件，
    //所以等待期间必须泵消息，否则会形成“UI等线程退出、线程等UI处理消息”的死锁
    for (int k{}; k < m_thread_count; k++)
    {
        CWinThread* pThread = m_pThreads[k];
        m_pThreads[k] = nullptr;
        if (pThread == nullptr)
            continue;
        HANDLE hThread = pThread->m_hThread;
        while (hThread != NULL && WaitForSingleObject(hThread, 100) == WAIT_TIMEOUT)
        {
            //泵消息，让线程的SendMessage能够被处理
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        delete pThread;     //线程已退出，释放线程对象及其句柄（m_bAutoDelete为FALSE）
    }
    m_thread_count = 0;
}

//从检查点恢复已完成的分析结果
void CAiSongOrganizeDlg::RestoreAnalyzeFromCheckpoint()
{
    for (int i{}; i < static_cast<int>(m_songs.size()); i++)
    {
        if (m_songs[i].is_cue || COSUPlayerHelper::IsOsuFile(m_songs[i].file_path))
            continue;
        AiSongInfo result;
        {
            CSingleLock lock(&m_cs, TRUE);
            auto iter = m_analyze_ckpt.find(m_songs[i].file_path);
            if (iter == m_analyze_ckpt.end())
                continue;
            result = iter->second;
            m_ai_results[i] = result;
        }
        FillAnalyzedRow(i, result);
        m_song_list_ctrl.SetItemText(i, COL_STATUS, theApp.m_str_table.LoadText(L"TXT_AI_STATUS_RESTORED").c_str());
    }
}

//将一行分析结果填到列表
void CAiSongOrganizeDlg::FillAnalyzedRow(int row, const AiSongInfo& result)
{
    m_song_list_ctrl.SetItemText(row, COL_AI_TITLE, result.title.c_str());
    m_song_list_ctrl.SetItemText(row, COL_AI_ARTIST, result.artist.c_str());
    m_song_list_ctrl.SetItemText(row, COL_AI_ALBUM, result.album.c_str());
    CString confidence;
    confidence.Format(_T("%d%%"), result.confidence);
    m_song_list_ctrl.SetItemText(row, COL_CONFIDENCE, confidence);
    //无需修改或置信度低的条目不自动勾选
    if (result.changed && result.confidence >= 60)
        m_song_list_ctrl.SetCheck(row, TRUE);
}

//在主线程完成工作线程无法写入的文件操作（目标文件正被播放器占用）。
//CPlayer::ReOpen全程持有播放状态互斥量，且内部会向主窗口阻塞式SendMessage，只能在本线程调用。
void CAiSongOrganizeDlg::FlushPendingWrites()
{
    vector<PendingWrite> pending_writes;
    {
        CSingleLock lock(&m_cs, TRUE);
        pending_writes.swap(m_pending_writes);
    }
    if (pending_writes.empty())
        return;

    for (PendingWrite& pending : pending_writes)
    {
        wstring status = pending.status;
        CFilePathHelper file_path(pending.song_info.file_path);
        wstring ext = file_path.GetFileExtension();
        //只有当前正在播放的曲目才需要（且能够）用ReOpen让出文件占用
        bool is_current_song = (pending.song_info.file_path == CPlayer::GetInstance().GetCurrentSongInfo().file_path);
        bool write_ok = true;

        //真正的写入动作
        auto do_write = [&]()
        {
            if (pending.write_tag && CAudioTag::IsFileTypeTagWriteSupport(ext))
            {
                CAudioTag audio_tag(pending.song_info);
                if (audio_tag.WriteAudioTag())
                    AiAppendStatus(status, L"TXT_AI_STATUS_TAG_OK");
                else
                {
                    AiAppendStatus(status, L"TXT_AI_STATUS_WRITE_FAILED");
                    write_ok = false;
                }
            }
            if (!pending.lyric.empty() && CAudioTag::IsFileTypeLyricWriteSupport(ext))
            {
                CAudioTag audio_tag(pending.song_info);
                if (audio_tag.WriteAudioLyric(pending.lyric))
                    AiAppendStatus(status, L"TXT_AI_STATUS_EMBED_OK");
                else
                {
                    AiAppendStatus(status, L"TXT_AI_STATUS_WRITE_FAILED");
                    write_ok = false;
                }
            }
            if (!pending.cover_path.empty() && CAudioTag::IsFileTypeCoverWriteSupport(ext))
            {
                CAudioTag audio_tag(pending.song_info);
                if (audio_tag.WriteAlbumCover(pending.cover_path))
                    AiAppendStatus(status, L"TXT_AI_STATUS_COVER_EMBED_OK");
                else
                {
                    AiAppendStatus(status, L"TXT_AI_STATUS_WRITE_FAILED");
                    write_ok = false;
                }
            }
        };

        if (is_current_song)
        {
            CPlayer::ReOpen reopen(true);
            if (!reopen.IsLockSuccess())
            {
                //让出主线程失败（极小概率），保留到下次刷新重试
                CSingleLock lock(&m_cs, TRUE);
                m_pending_writes.push_back(pending);
                continue;
            }
            do_write();
        }
        else
        {
            //已不是当前播放的曲目，文件不再被占用，直接写入
            do_write();
        }

        //写入成功的曲目才记入断点，失败的留待下次应用时重试
        if (write_ok)
        {
            CSingleLock lock(&m_cs, TRUE);
            m_apply_done.insert(pending.song_info.file_path);
            SaveCheckpoint();
        }
        if (::IsWindow(GetSafeHwnd()) && pending.row >= 0)
            m_song_list_ctrl.SetItemText(pending.row, COL_STATUS, status.c_str());
    }
}

void CAiSongOrganizeDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_AI_OPT_WRITE_TAG, m_write_tag_chk);
    DDX_Control(pDX, IDC_AI_OPT_DOWNLOAD_LYRIC, m_download_lyric_chk);
    DDX_Control(pDX, IDC_AI_OPT_EMBED_LYRIC, m_embed_lyric_chk);
    DDX_Control(pDX, IDC_AI_OPT_DOWNLOAD_COVER, m_download_cover_chk);
    DDX_Control(pDX, IDC_AI_OPT_EMBED_COVER, m_embed_cover_chk);
    DDX_Control(pDX, IDC_AI_CONCURRENCY_EDIT, m_concurrency_edit);
    DDX_Control(pDX, IDC_AI_SONG_LIST, m_song_list_ctrl);
    DDX_Control(pDX, IDC_AI_INFO_STATIC, m_info_static);
    DDX_Control(pDX, IDC_AI_PROGRESS_BAR, m_progress_bar);
    DDX_Control(pDX, IDC_AI_CANCEL_BTN, m_cancel_btn);
    DDX_Control(pDX, IDC_AI_SELECT_ALL, m_select_all_btn);
}


BEGIN_MESSAGE_MAP(CAiSongOrganizeDlg, CBaseDialog)
    ON_BN_CLICKED(IDC_AI_START_ANALYZE, &CAiSongOrganizeDlg::OnBnClickedStartAnalyze)
    ON_BN_CLICKED(IDC_AI_APPLY_SELECTED, &CAiSongOrganizeDlg::OnBnClickedApplySelected)
    ON_BN_CLICKED(IDC_AI_SELECT_ALL, &CAiSongOrganizeDlg::OnBnClickedSelectAll)
    ON_BN_CLICKED(IDC_AI_SETTING_BTN, &CAiSongOrganizeDlg::OnBnClickedSettingBtn)
    ON_BN_CLICKED(IDC_AI_CANCEL_BTN, &CAiSongOrganizeDlg::OnBnClickedCancelBtn)
    ON_WM_SIZE()
    ON_WM_DESTROY()
    ON_MESSAGE(WM_AI_ANALYZE_COMPLATE, &CAiSongOrganizeDlg::OnAnalyzeComplate)
    ON_MESSAGE(WM_AI_APPLY_COMPLATE, &CAiSongOrganizeDlg::OnApplyComplate)
END_MESSAGE_MAP()


// CAiSongOrganizeDlg 消息处理程序

BOOL CAiSongOrganizeDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    SetIcon(IconMgr::IconType::IT_Fix, FALSE);
    SetIcon(IconMgr::IconType::IT_Fix, TRUE);
    SetButtonIcon(IDC_AI_SETTING_BTN, IconMgr::IconType::IT_Setting);

    CenterWindow();

    LoadConfig();
    LoadCheckpoint();

    //初始化控件状态
    m_write_tag_chk.SetCheck(m_write_tag);
    m_download_lyric_chk.SetCheck(m_download_lyric);
    m_embed_lyric_chk.SetCheck(m_embed_lyric);
    m_download_cover_chk.SetCheck(m_download_cover);
    m_embed_cover_chk.SetCheck(m_embed_cover);
    CString concurrency;
    concurrency.Format(_T("%d"), m_concurrency);
    m_concurrency_edit.SetWindowText(concurrency);
    m_cancel_btn.EnableWindow(FALSE);       //空闲时取消按钮禁用

    //对播放列表做快照
    m_songs = CPlayer::GetInstance().GetPlayList();
    m_ai_results.resize(m_songs.size());

    //初始化歌曲列表控件
    CRect rect;
    m_song_list_ctrl.GetWindowRect(rect);
    int width = rect.Width();
    int w0 = width * 4 / 100;       //序号
    int w1 = width * 19 / 100;      //文件名
    int w2 = width * 19 / 100;      //原标签
    int w3 = width * 13 / 100;      //AI标题
    int w4 = width * 13 / 100;      //AI歌手
    int w5 = width * 13 / 100;      //AI专辑
    int w6 = width * 7 / 100;       //置信度
    int w7 = width - w0 - w1 - w2 - w3 - w4 - w5 - w6 - theApp.DPI(20) - 1;   //状态
    //注：本列表使用普通CListCtrl而非CListCtrlEx。CListCtrlEx的自定义绘制与LVS_EX_CHECKBOXES同时使用时，
    //勾选行后弹出的模态对话框会出现“已创建但始终不显示”的问题（宿主对话框被模态禁用、界面像卡死），
    //这里不需要CListCtrlEx的拖动、高亮和主题配色，用普通列表控件即可规避
    m_song_list_ctrl.SetExtendedStyle(m_song_list_ctrl.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);
    m_song_list_ctrl.InsertColumn(0, theApp.m_str_table.LoadText(L"TXT_SERIAL_NUMBER").c_str(), LVCFMT_LEFT, w0);
    m_song_list_ctrl.InsertColumn(1, theApp.m_str_table.LoadText(L"TXT_FILE_NAME").c_str(), LVCFMT_LEFT, w1);
    m_song_list_ctrl.InsertColumn(2, theApp.m_str_table.LoadText(L"TXT_AI_COL_ORIGINAL").c_str(), LVCFMT_LEFT, w2);
    m_song_list_ctrl.InsertColumn(3, theApp.m_str_table.LoadText(L"TXT_AI_COL_AI_TITLE").c_str(), LVCFMT_LEFT, w3);
    m_song_list_ctrl.InsertColumn(4, theApp.m_str_table.LoadText(L"TXT_AI_COL_AI_ARTIST").c_str(), LVCFMT_LEFT, w4);
    m_song_list_ctrl.InsertColumn(5, theApp.m_str_table.LoadText(L"TXT_AI_COL_AI_ALBUM").c_str(), LVCFMT_LEFT, w5);
    m_song_list_ctrl.InsertColumn(6, theApp.m_str_table.LoadText(L"TXT_AI_COL_CONFIDENCE").c_str(), LVCFMT_LEFT, w6);
    m_song_list_ctrl.InsertColumn(7, theApp.m_str_table.LoadText(L"TXT_AI_COL_STATUS").c_str(), LVCFMT_LEFT, w7);
    for (int i{}; i < static_cast<int>(m_songs.size()); i++)
    {
        CString tmp;
        tmp.Format(_T("%d"), i + 1);
        m_song_list_ctrl.InsertItem(i, tmp);
        m_song_list_ctrl.SetItemText(i, COL_FILE_NAME, m_songs[i].GetFileName().c_str());
        //原标签：标题 / 歌手 / 专辑
        wstring original = m_songs[i].GetTitle() + L" / " + m_songs[i].GetArtist() + L" / " + m_songs[i].GetAlbum();
        m_song_list_ctrl.SetItemText(i, COL_ORIGINAL, original.c_str());
    }

    //有断点时提示可以恢复
    if (!m_analyze_ckpt.empty() || !m_apply_done.empty())
    {
        wstring info = theApp.m_str_table.LoadTextFormat(L"TXT_AI_INFO_CHECKPOINT_FOUND", { m_analyze_ckpt.size() + m_apply_done.size() });
        m_info_static.SetWindowText(info.c_str());
    }

    m_progress_bar.SetBackgroundColor(GetSysColor(COLOR_BTNFACE));
    m_progress_bar.ShowWindow(SW_HIDE);

    return TRUE;  // return TRUE unless you set the focus to a control
                  // 异常: OCX 属性页应返回 FALSE
}


//手动调整控件布局（代替AFX_DIALOG_LAYOUT动态布局，保证缩放时控件位置稳定）
void CAiSongOrganizeDlg::OnSize(UINT nType, int cx, int cy)
{
    CBaseDialog::OnSize(nType, cx, cy);
    if (nType == SIZE_MINIMIZED)
        return;
    CWnd* pList = GetDlgItem(IDC_AI_SONG_LIST);
    if (pList == nullptr || pList->GetSafeHwnd() == nullptr)
        return;

    const int margin = theApp.DPI(7);
    const int gap = theApp.DPI(4);

    //获取一个控件的客户区矩形
    auto get_ctrl_rect = [&](UINT id, CRect& rc) -> bool {
        CWnd* pCtrl = GetDlgItem(id);
        if (pCtrl == nullptr || pCtrl->GetSafeHwnd() == nullptr)
            return false;
        pCtrl->GetWindowRect(&rc);
        ScreenToClient(&rc);
        return true;
    };

    //底行四个按钮保持各自尺寸，从右往左排列
    int right = cx - margin;
    int row_bottom = cy - margin;
    int row_top = row_bottom;
    for (UINT id : { IDCANCEL, IDC_AI_APPLY_SELECTED, IDC_AI_CANCEL_BTN, IDC_AI_START_ANALYZE })
    {
        CRect rc;
        if (!get_ctrl_rect(id, rc)) continue;
        GetDlgItem(id)->MoveWindow(right - rc.Width(), row_bottom - rc.Height(), rc.Width(), rc.Height());
        right -= (rc.Width() + gap);
        row_top = min(row_top, row_bottom - rc.Height());
    }
    int row_height = row_bottom - row_top;

    //列表表头右侧的“全选”按钮
    CRect rc_select, rc_list_title;
    if (get_ctrl_rect(IDC_AI_SELECT_ALL, rc_select))
    {
        int select_left = cx - margin - rc_select.Width();
        GetDlgItem(IDC_AI_SELECT_ALL)->MoveWindow(select_left, rc_select.top, rc_select.Width(), rc_select.Height());
        if (get_ctrl_rect(IDC_TXT_AI_SONG_LIST_STATIC, rc_list_title))
            GetDlgItem(IDC_TXT_AI_SONG_LIST_STATIC)->MoveWindow(rc_list_title.left, rc_list_title.top, select_left - gap - rc_list_title.left, rc_list_title.Height());
    }

    //"AI服务设置"按钮靠选项组右侧
    CRect rc_setting;
    if (get_ctrl_rect(IDC_AI_SETTING_BTN, rc_setting))
        GetDlgItem(IDC_AI_SETTING_BTN)->MoveWindow(cx - margin - gap - rc_setting.Width(), rc_setting.top, rc_setting.Width(), rc_setting.Height());

    //列表控件上下左右贴合
    CRect rc_list;
    if (get_ctrl_rect(IDC_AI_SONG_LIST, rc_list))
        pList->MoveWindow(rc_list.left, rc_list.top, cx - margin - rc_list.left, row_top - theApp.DPI(6) - rc_list.top);

    //进度条和信息文字贴左下，与按钮行垂直居中
    CRect rc_progress, rc_info, rc_btn;
    if (get_ctrl_rect(IDC_AI_PROGRESS_BAR, rc_progress))
    {
        GetDlgItem(IDC_AI_PROGRESS_BAR)->MoveWindow(margin, row_top + (row_height - rc_progress.Height()) / 2, rc_progress.Width(), rc_progress.Height());
    }
    if (get_ctrl_rect(IDC_AI_INFO_STATIC, rc_info) && get_ctrl_rect(IDC_AI_START_ANALYZE, rc_btn))
    {
        int info_left = margin + rc_progress.Width() + gap;
        GetDlgItem(IDC_AI_INFO_STATIC)->MoveWindow(info_left, row_top + (row_height - rc_info.Height()) / 2, rc_btn.left - gap - info_left, rc_info.Height());
    }
}


void CAiSongOrganizeDlg::OnBnClickedStartAnalyze()
{
    if (!CAiClient::IsConfigValid())
    {
        MessageBox(theApp.m_str_table.LoadText(L"MSG_AI_CONFIG_INVALID").c_str(), NULL, MB_ICONWARNING | MB_OK);
        return;
    }
    if (m_songs.empty())
    {
        MessageBox(theApp.m_str_table.LoadText(L"MSG_AI_PLAYLIST_EMPTY").c_str(), NULL, MB_ICONINFORMATION | MB_OK);
        return;
    }

    //清除AI列和状态列
    for (int i{}; i < static_cast<int>(m_songs.size()); i++)
    {
        for (int col : { COL_AI_TITLE, COL_AI_ARTIST, COL_AI_ALBUM, COL_CONFIDENCE, COL_STATUS })
            m_song_list_ctrl.SetItemText(i, col, _T(""));
        m_song_list_ctrl.SetCheck(i, FALSE);
        CSingleLock lock(&m_cs, TRUE);
        m_ai_results[i] = AiSongInfo();
    }

    //从断点恢复已完成的分析结果（这些曲目不会再调用AI接口）
    RestoreAnalyzeFromCheckpoint();

    EnableControls(false);
    StartThread(PH_ANALYZE);
}


void CAiSongOrganizeDlg::OnBnClickedSelectAll()
{
    //“全选”复选框控制列表中全部条目的勾选状态
    BOOL checked = m_select_all_btn.GetCheck();
    for (int i{}; i < m_song_list_ctrl.GetItemCount(); i++)
        m_song_list_ctrl.SetCheck(i, checked);
}


void CAiSongOrganizeDlg::OnBnClickedApplySelected()
{
    //统计勾选且已分析的行（断点中已完成的曲目跳过）
    m_selected_rows.clear();
    int done_before = 0;
    for (int i{}; i < static_cast<int>(m_songs.size()); i++)
    {
        if (!m_song_list_ctrl.GetCheck(i))
            continue;
        CSingleLock lock(&m_cs, TRUE);
        if (!m_ai_results[i].analyzed)
            continue;
        if (m_apply_done.count(m_songs[i].file_path) != 0)
        {
            done_before++;
            continue;
        }
        m_selected_rows.push_back(i);
    }
    if (m_selected_rows.empty() && done_before == 0)
    {
        MessageBox(theApp.m_str_table.LoadText(L"MSG_AI_NO_SELECTED").c_str(), NULL, MB_ICONINFORMATION | MB_OK);
        return;
    }

    //断点中已完成的条目直接标记状态
    for (int i{}; i < static_cast<int>(m_songs.size()); i++)
    {
        CSingleLock lock(&m_cs, TRUE);
        if (m_song_list_ctrl.GetCheck(i) && m_ai_results[i].analyzed && m_apply_done.count(m_songs[i].file_path) != 0)
            m_song_list_ctrl.SetItemText(i, COL_STATUS, theApp.m_str_table.LoadText(L"TXT_AI_STATUS_DONE_BEFORE").c_str());
    }

    if (m_selected_rows.empty())
        return;

    //确认操作
    wstring info = theApp.m_str_table.LoadTextFormat(L"MSG_AI_CONFIRM_APPLY", { m_selected_rows.size() });
    if (MessageBox(info.c_str(), NULL, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    EnableControls(false);
    StartThread(PH_APPLY);
}


void CAiSongOrganizeDlg::OnBnClickedSettingBtn()
{
    CAiSettingDlg setting_dlg;
    setting_dlg.DoModal();
}


void CAiSongOrganizeDlg::OnBnClickedCancelBtn()
{
    //设置退出标志，工作线程会在当前单曲处理完成后退出
    theApp.m_ai_organize_dialog_exit = true;
    m_cancel_btn.EnableWindow(FALSE);
    m_info_static.SetWindowText(theApp.m_str_table.LoadText(L"TXT_AI_CANCELLING").c_str());
}


void CAiSongOrganizeDlg::OnDestroy()
{
    CBaseDialog::OnDestroy();

    SaveConfig();
    //退出前完成被占用曲目的写入，再保存断点
    FlushPendingWrites();
    {
        CSingleLock lock(&m_cs, TRUE);
        SaveCheckpoint(true);
    }
}


//工作线程函数：多线程并发，通过原子游标领取任务，最后一个退出的线程发完成消息
UINT CAiSongOrganizeDlg::ThreadFunc(LPVOID lpParam)
{
    CCommon::SetThreadLanguageList(theApp.m_str_table.GetLanguageTag());
    ThreadInfo* pInfo = (ThreadInfo*)lpParam;

    for (;;)
    {
        if (theApp.m_ai_organize_dialog_exit || !::IsWindow(pInfo->hwnd))
            break;
        //原子领取下一个任务
        int i = static_cast<int>(InterlockedIncrement(pInfo->p_next_index)) - 1;
        if (i >= pInfo->total)
            break;

        if (pInfo->phase == PH_ANALYZE)
            AiAnalyzeRow(pInfo, i);
        else
            AiApplyRow(pInfo, i);

        //更新进度
        LONG done = InterlockedIncrement(pInfo->p_processed);
        int percent = static_cast<int>(done * 100 / pInfo->total);
        if (::IsWindow(pInfo->hwnd))
        {
            const wchar_t* info_key = (pInfo->phase == PH_ANALYZE) ? L"TXT_AI_INFO_ANALYZE" : L"TXT_AI_INFO_APPLY";
            wstring info = theApp.m_str_table.LoadTextFormat(info_key, { percent });
            pInfo->static_ctrl->SetWindowText(info.c_str());
            pInfo->progress_bar->SetProgress(percent);
        }
    }

    //最后一个退出的线程发完成消息
    if (InterlockedDecrement(pInfo->p_active) == 0 && ::IsWindow(pInfo->hwnd))
        ::PostMessage(pInfo->hwnd, (pInfo->phase == PH_ANALYZE) ? WM_AI_ANALYZE_COMPLATE : WM_AI_APPLY_COMPLATE, 0, 0);
    return 0;
}


afx_msg LRESULT CAiSongOrganizeDlg::OnAnalyzeComplate(WPARAM wParam, LPARAM lParam)
{
    m_progress_bar.SetProgress(100);
    m_info_static.SetWindowText(theApp.m_str_table.LoadText(L"TXT_AI_INFO_ANALYZE_COMPLETE").c_str());
    EnableControls(true);
    //分析检查点保留（供应用阶段中断后恢复），应用全部完成后才会清空
    return 0;
}


afx_msg LRESULT CAiSongOrganizeDlg::OnApplyComplate(WPARAM wParam, LPARAM lParam)
{
    m_progress_bar.SetProgress(100);
    m_info_static.SetWindowText(theApp.m_str_table.LoadText(L"TXT_AI_INFO_APPLY_COMPLETE").c_str());
    EnableControls(true);
    //被播放器占用而无法在工作线程写入的曲目，在此由主线程完成
    FlushPendingWrites();
    //全部完成（非取消）时清空断点
    if (!theApp.m_ai_organize_dialog_exit)
    {
        CSingleLock lock(&m_cs, TRUE);
        m_analyze_ckpt.clear();
        m_apply_done.clear();
        SaveCheckpoint(true);
    }
    //刷新当前曲目的歌词显示
    CPlayer::GetInstance().SearchLyrics(true);
    CPlayer::GetInstance().IniLyrics();
    return 0;
}


void CAiSongOrganizeDlg::OnCancel()
{
    //对话框将要关闭时，将退出标志置为true并等待线程退出
    theApp.m_ai_organize_dialog_exit = true;
    StopThread();
    DestroyWindow();
}


void CAiSongOrganizeDlg::OnOK()
{
    theApp.m_ai_organize_dialog_exit = true;
    StopThread();
    CBaseDialog::OnOK();
}
