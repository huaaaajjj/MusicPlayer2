// LyricBatchDownloadDlg.cpp : 实现文件
//

#include "stdafx.h"
#include "MusicPlayer2.h"
#include "LyricBatchDownloadDlg.h"
#include "SongDataManager.h"
#include "COSUPlayerHelper.h"
#include "IniHelper.h"
#include "SongInfoHelper.h"
#include "MusicPlayerCmdHelper.h"
#include "Lyric.h"
#include "AudioTag.h"

namespace
{
    constexpr UINT_PTR BATCH_STATISTICS_TIMER_ID = 1;
}


// CLyricBatchDownloadDlg 对话框

IMPLEMENT_DYNAMIC(CLyricBatchDownloadDlg, CBaseDialog)

CLyricBatchDownloadDlg::CLyricBatchDownloadDlg(CWnd* pParent /*=NULL*/)
    : CBaseDialog(IDD_LYRIC_BATCH_DOWN_DIALOG, pParent)
{

}

CLyricBatchDownloadDlg::~CLyricBatchDownloadDlg()
{
}

CString CLyricBatchDownloadDlg::GetDialogName() const
{
    return _T("LyricBatchDownloadDlg");
}

bool CLyricBatchDownloadDlg::InitializeControls()
{
    UpdateDownloadSourceTitle();
    SetDlgControlText(IDC_LYRIC_BDL_SOURCE_BTN, L"TXT_LYRIC_BDL_SWITCH_SOURCE");
    wstring temp;
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_DL_OPT");
    SetDlgItemTextW(IDC_TXT_LYRIC_BDL_DL_OPT_STATIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_SKIP_ALREADY");
    SetDlgItemTextW(IDC_SKIP_EXIST_CHECK, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_SAVE_ENCODE_SEL");
    SetDlgItemTextW(IDC_TXT_LYRIC_BDL_SAVE_ENCODE_SEL_STATIC, temp.c_str());
    // IDC_COMBO1
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_WITH_TRANSLATION");
    SetDlgItemTextW(IDC_DOWNLOAD_TRASNLATE_CHECK2, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_SAVE_DIR_SEL");
    SetDlgItemTextW(IDC_TXT_LYRIC_BDL_SAVE_DIR_SEL_STATIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_SAVE_DIR_LYRIC");
    SetDlgItemTextW(IDC_SAVE_TO_LYRIC_FOLDER, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_SAVE_DIR_SONG");
    SetDlgItemTextW(IDC_SAVE_TO_SONG_FOLDER, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_SONG_LIST");
    SetDlgItemTextW(IDC_TXT_LYRIC_BDL_SONG_LIST_STATIC, temp.c_str());
    // IDC_SONG_LIST1
    temp = L"";
    SetDlgItemTextW(IDC_PROGRESS_BAR, temp.c_str());
    SetDlgItemTextW(IDC_INFO_STATIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_DL_START");
    SetDlgItemTextW(IDC_START_DOWNLOAD, temp.c_str());
    // IDCANCEL

    RepositionTextBasedControls({
        { CtrlTextInfo::L1, IDC_PROGRESS_BAR },
        { CtrlTextInfo::C0, IDC_INFO_STATIC },
        { CtrlTextInfo::R1, IDC_START_DOWNLOAD, CtrlTextInfo::W32 },
        { CtrlTextInfo::R2, IDCANCEL, CtrlTextInfo::W32 }
        });
    return true;
}

void CLyricBatchDownloadDlg::SaveConfig() const
{
    CIniHelper ini(theApp.m_config_path);
    ini.WriteInt(L"lyric_batch_download", L"save_as_utf8", static_cast<int>(m_save_code));
    ini.WriteBool(L"lyric_batch_download", L"download_translate", m_download_translate);
    ini.WriteBool(L"lyric_batch_download", L"save_to_song_folder", m_save_to_song_folder);
    ini.Save();
}

void CLyricBatchDownloadDlg::LoadConfig()
{
    CIniHelper ini(theApp.m_config_path);
    m_save_code = static_cast<CodeType>(ini.GetInt(L"lyric_batch_download", L"save_as_utf8", 1));
    m_download_translate = ini.GetBool(L"lyric_batch_download", L"download_translate", true);
    m_save_to_song_folder = ini.GetBool(L"lyric_batch_download", L"save_to_song_folder", true);
}

void CLyricBatchDownloadDlg::EnableControls(bool enable)
{
    m_skip_exist_check.EnableWindow(enable);
    m_save_code_combo.EnableWindow(enable);
    m_download_translate_chk.EnableWindow(enable);
    GetDlgItem(IDC_SAVE_TO_SONG_FOLDER)->EnableWindow(enable);
    if (m_lyric_path_not_exit)
        GetDlgItem(IDC_SAVE_TO_LYRIC_FOLDER)->EnableWindow(FALSE);
    else
        GetDlgItem(IDC_SAVE_TO_LYRIC_FOLDER)->EnableWindow(enable);
    GetDlgItem(IDC_BATCH_ACTION_TAB)->EnableWindow(enable);
    GetDlgItem(IDC_LYRIC_BDL_SOURCE_BTN)->EnableWindow(enable);
    GetDlgItem(IDC_START_DOWNLOAD)->EnableWindow(enable);
}

bool CLyricBatchDownloadDlg::SaveLyric(const wchar_t* path, const wstring& lyric_wcs, CodeType code_type, bool* char_cannot_convert)
{
    string lyric_str = CCommon::UnicodeToStr(lyric_wcs, code_type, char_cannot_convert);
    ofstream out_put{ path, std::ios::binary };
    if (out_put.fail())
        return false;
    out_put << lyric_str;
    return true;
}

void CLyricBatchDownloadDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_SKIP_EXIST_CHECK, m_skip_exist_check);
    DDX_Control(pDX, IDC_COMBO1, m_save_code_combo);
    DDX_Control(pDX, IDC_SONG_LIST1, m_song_list_ctrl);
    DDX_Control(pDX, IDC_DOWNLOAD_TRASNLATE_CHECK2, m_download_translate_chk);
    DDX_Control(pDX, IDC_INFO_STATIC, m_info_static);
    DDX_Control(pDX, IDC_PROGRESS_BAR, m_progress_bar);
}


BEGIN_MESSAGE_MAP(CLyricBatchDownloadDlg, CBaseDialog)
    ON_BN_CLICKED(IDC_START_DOWNLOAD, &CLyricBatchDownloadDlg::OnBnClickedStartDownload)
    ON_NOTIFY(TCN_SELCHANGE, IDC_BATCH_ACTION_TAB, &CLyricBatchDownloadDlg::OnTcnSelchangeActionTab)
    ON_BN_CLICKED(IDC_LYRIC_BDL_SOURCE_BTN, &CLyricBatchDownloadDlg::OnBnClickedSwitchSource)
    ON_BN_CLICKED(IDC_LYRIC_BDL_EMBED, &CLyricBatchDownloadDlg::OnBnClickedEmbedLyric)
    ON_BN_CLICKED(IDC_LYRIC_BDL_COVER, &CLyricBatchDownloadDlg::OnBnClickedDownloadCover)
    ON_BN_CLICKED(IDC_LYRIC_BDL_EMBED_COVER, &CLyricBatchDownloadDlg::OnBnClickedEmbedCover)
    ON_BN_CLICKED(IDC_SKIP_EXIST_CHECK, &CLyricBatchDownloadDlg::OnBnClickedSkipExistCheck)
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_CBN_SELCHANGE(IDC_COMBO1, &CLyricBatchDownloadDlg::OnCbnSelchangeCombo1)
    ON_BN_CLICKED(IDC_DOWNLOAD_TRASNLATE_CHECK2, &CLyricBatchDownloadDlg::OnBnClickedDownloadTrasnlateCheck2)
    ON_MESSAGE(WM_BATCH_DOWNLOAD_COMPLATE, &CLyricBatchDownloadDlg::OnBatchDownloadComplate)
    ON_WM_CLOSE()
    ON_BN_CLICKED(IDC_SAVE_TO_SONG_FOLDER, &CLyricBatchDownloadDlg::OnBnClickedSaveToSongFolder)
    ON_BN_CLICKED(IDC_SAVE_TO_LYRIC_FOLDER, &CLyricBatchDownloadDlg::OnBnClickedSaveToLyricFolder)
END_MESSAGE_MAP()


// CLyricBatchDownloadDlg 消息处理程序


BOOL CLyricBatchDownloadDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    // TODO:  在此添加额外的初始化
    SetIcon(IconMgr::IconType::IT_Download_Batch, FALSE);
    SetIcon(IconMgr::IconType::IT_Download_Batch, TRUE);
    SetButtonIcon(IDC_START_DOWNLOAD, IconMgr::IconType::IT_Download_Batch);

    CenterWindow();

    if (CTabCtrl* action_tab = (CTabCtrl*)GetDlgItem(IDC_BATCH_ACTION_TAB))
    {
        action_tab->InsertItem(0, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_DL_START").c_str());
        action_tab->InsertItem(1, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_EMBED_START").c_str());
        action_tab->InsertItem(2, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_COVER_START").c_str());
        action_tab->InsertItem(3, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_EMBED_COVER_START").c_str());
        action_tab->SetCurSel(0);
    }
    UpdateActionTab();

    LoadConfig();

    //设置列表控件主题颜色
    //m_song_list_ctrl.SetColor(theApp.m_app_setting_data.theme_color);

    //初始化控件的状态
    m_skip_exist_check.SetCheck(m_skip_exist);
    m_save_code_combo.AddString(_T("ANSI"));
    m_save_code_combo.AddString(_T("UTF-8"));
    m_save_code_combo.SetCurSel(static_cast<int>(m_save_code));
    m_download_translate_chk.SetCheck(m_download_translate);
    if (m_save_to_song_folder)
        ((CButton*)GetDlgItem(IDC_SAVE_TO_SONG_FOLDER))->SetCheck(TRUE);
    else
        ((CButton*)GetDlgItem(IDC_SAVE_TO_LYRIC_FOLDER))->SetCheck(TRUE);
    //判断歌词文件夹是否存在
    bool lyric_path_exist = CCommon::FolderExist(theApp.m_lyric_setting_data.AbsoluteLyricPath());
    if (!lyric_path_exist)		//如果歌词文件不存在，则禁用“保存到歌词文件夹”单选按钮，并强制选中“保存到歌曲所在目录”
    {
        ((CButton*)GetDlgItem(IDC_SAVE_TO_LYRIC_FOLDER))->EnableWindow(FALSE);
        ((CButton*)GetDlgItem(IDC_SAVE_TO_LYRIC_FOLDER))->SetCheck(FALSE);
        ((CButton*)GetDlgItem(IDC_SAVE_TO_SONG_FOLDER))->SetCheck(TRUE);
        m_save_to_song_folder = true;
        m_lyric_path_not_exit = true;
    }

    //初始化歌曲列表控件
    //设置各列的宽度
    CRect rect;
    m_song_list_ctrl.GetWindowRect(rect);
    int width0, width1, width2, width3, width4;
    width0 = rect.Width() / 10;
    width1 = rect.Width() * 2 / 10;
    width2 = rect.Width() * 2 / 10;
    width3 = rect.Width() * 3 / 10;
    width4 = rect.Width() - width0 - width1 - width2 - width3 - theApp.DPI(20) - 1;
    //插入列
    m_song_list_ctrl.SetExtendedStyle(m_song_list_ctrl.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    m_song_list_ctrl.InsertColumn(0, theApp.m_str_table.LoadText(L"TXT_SERIAL_NUMBER").c_str(), LVCFMT_LEFT, width0);		//插入第1列
    m_song_list_ctrl.InsertColumn(1, theApp.m_str_table.LoadText(L"TXT_TITLE").c_str(), LVCFMT_LEFT, width1);		//插入第2列
    m_song_list_ctrl.InsertColumn(2, theApp.m_str_table.LoadText(L"TXT_ARTIST").c_str(), LVCFMT_LEFT, width2);		//插入第3列
    m_song_list_ctrl.InsertColumn(3, theApp.m_str_table.LoadText(L"TXT_FILE_NAME").c_str(), LVCFMT_LEFT, width3);		//插入第3列
    m_song_list_ctrl.InsertColumn(4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS").c_str(), LVCFMT_LEFT, width4);		//插入第4列
    //插入项目
    for (size_t i{}; i < m_playlist.size(); i++)
    {
        CString tmp;
        tmp.Format(_T("%d"), i + 1);
        m_song_list_ctrl.InsertItem(i, tmp);
        m_song_list_ctrl.SetItemText(i, 1, m_playlist[i].GetTitle().c_str());
        m_song_list_ctrl.SetItemText(i, 2, m_playlist[i].GetArtist().c_str());
        m_song_list_ctrl.SetItemText(i, 3, m_playlist[i].GetFileName().c_str());
    }

    ShowBatchStatistics(GetCurrentAction());

    m_progress_bar.SetBackgroundColor(GetSysColor(COLOR_BTNFACE));
    m_progress_bar.ShowWindow(SW_HIDE);

    return TRUE;  // return TRUE unless you set the focus to a control
                  // 异常: OCX 属性页应返回 FALSE
}


void CLyricBatchDownloadDlg::OnBnClickedStartDownload()
{
    StartAction(GetCurrentAction());
}


void CLyricBatchDownloadDlg::OnBnClickedEmbedLyric()
{
    StartAction(BatchAction::EmbedLyric);
}


void CLyricBatchDownloadDlg::OnBnClickedDownloadCover()
{
    StartAction(BatchAction::DownloadCover);
}


void CLyricBatchDownloadDlg::OnBnClickedEmbedCover()
{
    StartAction(BatchAction::EmbedCover);
}


CLyricBatchDownloadDlg::BatchAction CLyricBatchDownloadDlg::GetCurrentAction()
{
    const int selected = (GetDlgItem(IDC_BATCH_ACTION_TAB) != nullptr)
        ? static_cast<CTabCtrl*>(GetDlgItem(IDC_BATCH_ACTION_TAB))->GetCurSel()
        : 0;
    switch (selected)
    {
    case 1: return BatchAction::EmbedLyric;
    case 2: return BatchAction::DownloadCover;
    case 3: return BatchAction::EmbedCover;
    default: return BatchAction::DownloadLyric;
    }
}


void CLyricBatchDownloadDlg::UpdateActionTab()
{
    m_current_action = GetCurrentAction();
    const wchar_t* text_key = L"TXT_LYRIC_BDL_DL_START";
    switch (m_current_action)
    {
    case BatchAction::EmbedLyric: text_key = L"TXT_LYRIC_BDL_EMBED_START"; break;
    case BatchAction::DownloadCover: text_key = L"TXT_LYRIC_BDL_COVER_START"; break;
    case BatchAction::EmbedCover: text_key = L"TXT_LYRIC_BDL_EMBED_COVER_START"; break;
    default: break;
    }
    SetDlgItemTextW(IDC_START_DOWNLOAD, theApp.m_str_table.LoadText(text_key).c_str());
}


void CLyricBatchDownloadDlg::OnTcnSelchangeActionTab(NMHDR* pNMHDR, LRESULT* pResult)
{
    UpdateActionTab();
    *pResult = 0;
}


void CLyricBatchDownloadDlg::UpdateDownloadSourceTitle()
{
    wstring temp;
    if (theApp.m_general_setting_data.lyric_download_service == GeneralSettingData::LDS_KUGOU)
        temp = theApp.m_str_table.LoadText(L"TITLE_LYRIC_BDL_KUGOU");
    else if (theApp.m_general_setting_data.lyric_download_service == GeneralSettingData::LDS_QQMUSIC)
        temp = theApp.m_str_table.LoadText(L"TITLE_LYRIC_BDL_QQMUSIC");
    else
        temp = theApp.m_str_table.LoadText(L"TITLE_LYRIC_BDL");
    SetWindowTextW(temp.c_str());
}


void CLyricBatchDownloadDlg::OnBnClickedSwitchSource()
{
    if (m_pThread != nullptr)
        return;

    CMenu source_menu;
    if (!source_menu.CreatePopupMenu())
        return;
    source_menu.AppendMenu(MF_STRING, GeneralSettingData::LDS_NETEASE + 1, theApp.m_str_table.LoadText(L"TXT_OPT_DATA_NETEASE_CLOUD_MUSIC").c_str());
    source_menu.AppendMenu(MF_STRING, GeneralSettingData::LDS_QQMUSIC + 1, theApp.m_str_table.LoadText(L"TXT_OPT_DATA_QQ_MUSIC").c_str());
    source_menu.AppendMenu(MF_STRING, GeneralSettingData::LDS_KUGOU + 1, theApp.m_str_table.LoadText(L"TXT_OPT_DATA_KUGOU_MUSIC").c_str());
    source_menu.CheckMenuRadioItem(GeneralSettingData::LDS_NETEASE + 1, GeneralSettingData::LDS_KUGOU + 1,
        theApp.m_general_setting_data.lyric_download_service + 1, MF_BYCOMMAND);

    CRect button_rect;
    GetDlgItem(IDC_LYRIC_BDL_SOURCE_BTN)->GetWindowRect(button_rect);
    const UINT command = source_menu.TrackPopupMenu(TPM_RIGHTALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        button_rect.right, button_rect.bottom, this);
    if (command == 0 || command > GeneralSettingData::LDS_KUGOU + 1 || m_pThread != nullptr)
        return;

    const auto service = static_cast<GeneralSettingData::LyricDownloadService>(command - 1);
    if (service == theApp.m_general_setting_data.lyric_download_service)
        return;
    theApp.m_general_setting_data.lyric_download_service = service;
    theApp.InitLyricDownload();
    CIniHelper ini(theApp.m_config_path);
    ini.WriteInt(L"general", L"lyric_download_service", service);
    ini.Save();
    UpdateDownloadSourceTitle();
}


void CLyricBatchDownloadDlg::StartAction(BatchAction action)
{
    m_progress_bar.ShowWindow(SW_SHOW);
    m_progress_bar.SetProgress(0);

    //清除状态列
    for (size_t i{}; i < m_playlist.size(); i++)
        m_song_list_ctrl.SetItemText(i, 4, _T(""));

    if (action == BatchAction::DownloadLyric)
        m_downloaded_songs.clear();
    if (action == BatchAction::EmbedLyric)
    {
        CSingleLock lock(&m_cs, TRUE);
        m_pending_embeds.clear();
    }
    if (action == BatchAction::EmbedCover)
    {
        CSingleLock lock(&m_cs, TRUE);
        m_pending_cover_embeds.clear();
    }

    ShowBatchStatistics(action);
    EnableControls(false);

    m_thread_info = ThreadInfo();
    m_thread_info.hwnd = GetSafeHwnd();
    m_thread_info.action = action;
    m_thread_info.download_translate = m_download_translate;
    m_thread_info.save_to_song_folder = m_save_to_song_folder;
    m_thread_info.skip_exist = m_skip_exist;
    m_thread_info.save_code = m_save_code;
    m_thread_info.list_ctrl = &m_song_list_ctrl;
    m_thread_info.static_ctrl = &m_info_static;
    m_thread_info.progress_bar = &m_progress_bar;
    m_thread_info.playlist = &m_playlist;
    m_thread_info.downloaded_songs = &m_downloaded_songs;
    m_thread_info.pending_embeds = &m_pending_embeds;
    m_thread_info.pending_cover_embeds = &m_pending_cover_embeds;
    m_thread_info.cs = &m_cs;

    theApp.m_batch_download_dialog_exit = false;
    m_pThread = AfxBeginThread(ThreadFunc, &m_thread_info);
    if (m_pThread != nullptr)
        SetTimer(BATCH_STATISTICS_TIMER_ID, 1000, nullptr);
}


void CLyricBatchDownloadDlg::OnBnClickedSkipExistCheck()
{
    // TODO: 在此添加控件通知处理程序代码
    m_skip_exist = (m_skip_exist_check.GetCheck() != 0);
}


void CLyricBatchDownloadDlg::OnTimer(UINT_PTR timer_id)
{
    if (timer_id == BATCH_STATISTICS_TIMER_ID && m_pThread != nullptr)
        ShowBatchStatistics(m_thread_info.action);
    CBaseDialog::OnTimer(timer_id);
}


void CLyricBatchDownloadDlg::OnDestroy()
{
    KillTimer(BATCH_STATISTICS_TIMER_ID);
    CBaseDialog::OnDestroy();

    // TODO: 在此处添加消息处理程序代码
    SaveConfig();

}


void CLyricBatchDownloadDlg::OnCbnSelchangeCombo1()
{
    // TODO: 在此添加控件通知处理程序代码
    //获取组合框中选中的编码格式
    switch (m_save_code_combo.GetCurSel())
    {
    case 1: m_save_code = CodeType::UTF8; break;
    default: m_save_code = CodeType::ANSI; break;
    }
}


void CLyricBatchDownloadDlg::OnBnClickedDownloadTrasnlateCheck2()
{
    // TODO: 在此添加控件通知处理程序代码
    m_download_translate = (m_download_translate_chk.GetCheck() != 0);
}

//按批量下载设置取得歌词文件路径
static wstring GetBatchLyricPath(const SongInfo& song, bool save_to_song_folder)
{
    wstring file_name;
    wstring dir;
    bool save_to_lyric_folder = (!save_to_song_folder && CCommon::FolderExist(theApp.m_lyric_setting_data.AbsoluteLyricPath()));
    if (song.is_cue || COSUPlayerHelper::IsOsuFile(song.file_path) || save_to_lyric_folder)
    {
        file_name = CSongInfoHelper::GetDisplayStr(song, DF_ARTIST_TITLE);
        CCommon::FileNameNormalize(file_name);
        dir = theApp.m_lyric_setting_data.AbsoluteLyricPath();
    }
    else
    {
        file_name = CFilePathHelper(song.GetFileName()).ReplaceFileExtension(nullptr);
        dir = CFilePathHelper(song.file_path).GetDir();
    }
    return dir + file_name + L".lrc";
}

//查找歌曲已经存在的本地歌词文件：优先使用媒体库关联歌词，再尝试歌曲目录和歌词目录
static wstring FindBatchLyricPath(const SongInfo& song, bool save_to_song_folder)
{
    SongInfo song_info{ CSongDataManager::GetInstance().GetSongInfo3(song) };
    if (!song_info.lyric_file.empty() && song_info.lyric_file != NO_LYRIC_STR && CCommon::FileExist(song_info.lyric_file))
        return song_info.lyric_file;

    wstring path = GetBatchLyricPath(song, save_to_song_folder);
    if (CCommon::FileExist(path))
        return path;

    path = GetBatchLyricPath(song, !save_to_song_folder);
    if (CCommon::FileExist(path))
        return path;

    return wstring();
}


//工作线程函数
UINT CLyricBatchDownloadDlg::ThreadFunc(LPVOID lpParam)
{
    CCommon::SetThreadLanguageList(theApp.m_str_table.GetLanguageTag());
    ThreadInfo* pInfo = (ThreadInfo*)lpParam;

    if (pInfo->action == BatchAction::EmbedLyric)
        return EmbedLyricThread(pInfo);
    if (pInfo->action == BatchAction::DownloadCover)
        return DownloadCoverThread(pInfo);
    if (pInfo->action == BatchAction::EmbedCover)
        return EmbedCoverThread(pInfo);

    //依次下载列表中每一首歌曲的歌词
    for (size_t i{}; i < pInfo->playlist->size(); i++)
    {
        if (theApp.m_batch_download_dialog_exit)
            return 0;
        int percent = i * 100 / pInfo->playlist->size();
        wstring info = theApp.m_str_table.LoadTextFormat(L"TXT_LYRIC_BDL_INFO_DOWNLOADING_INFO", { percent });
        pInfo->static_ctrl->SetWindowText(info.c_str());
        pInfo->progress_bar->SetProgress(percent);

        pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_DOWNLOADING").c_str());
        pInfo->list_ctrl->EnsureVisible(i, FALSE);

        //设置要保存的歌词的路径
        wstring file_name;
        wstring dir;
        const SongInfo& cur_song{ pInfo->playlist->at(i) };
        bool save_to_lyric_folder = (!pInfo->save_to_song_folder && CCommon::FolderExist(theApp.m_lyric_setting_data.AbsoluteLyricPath()));	//是否保存到歌曲所在文件夹
        //保存到歌词文件
        if (cur_song.is_cue || COSUPlayerHelper::IsOsuFile(cur_song.file_path) || save_to_lyric_folder)
        {
            file_name = CSongInfoHelper::GetDisplayStr(cur_song, DF_ARTIST_TITLE);
            CCommon::FileNameNormalize(file_name);
            dir = theApp.m_lyric_setting_data.AbsoluteLyricPath();
        }
        //保存到歌曲所在目录
        else
        {
            file_name = CFilePathHelper(cur_song.GetFileName()).ReplaceFileExtension(nullptr);
            dir = CFilePathHelper(cur_song.file_path).GetDir();
        }
        //歌词保存路径
        wstring lyric_path = dir + file_name + L".lrc";

        //判断歌词是否已经存在
        bool lyric_exist = CCommon::FileExist(lyric_path) || (!cur_song.lyric_file.empty());
        if (pInfo->skip_exist && lyric_exist)                   //如果设置了跳过已存在歌词的曲目，并且歌词已经存在，则跳过它
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_SKIPPED").c_str());
            continue;
        }

        //设置搜索关键字
        wstring search_result;      //查找歌曲返回的结果
        wstring lyric_str;          //下载好的歌词
        wstring keyword;            //查找的关键字
        if (cur_song.IsTitleEmpty())      //如果没有标题信息，就把文件名设为搜索关键字
        {
            keyword = cur_song.GetFileName();
            size_t index = keyword.rfind(L'.');         //查找最后一个点
            keyword = keyword.substr(0, index);         //去掉扩展名
        }
        else if (cur_song.IsArtistEmpty())	//如果有标题信息但是没有艺术家信息，就把标题设为搜索关键字
        {
            keyword = cur_song.title;
        }
        else		//否则将“艺术家 标题”设为搜索关键字
        {
            keyword = cur_song.artist + L' ' + cur_song.title;
        }

        //搜索歌曲
        wstring keyword_url = CInternetCommon::URLEncode(keyword);      //将搜索关键字转换成URL编码
        CString url = theApp.GetLyricDownload()->GetSearchUrl(keyword_url).c_str();
        int rtn = theApp.GetLyricDownload()->RequestSearch(wstring(url), search_result);       //发送歌曲搜索的网络请求
        if (theApp.m_batch_download_dialog_exit)        //由于CLyricDownloadCommon::HttpPost函数执行的时间比较长，所有在这里执行判断是否退出线程的处理
            return 0;
        if (rtn != 0)
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_NETWORK_FAILED").c_str());
            continue;
        }

        //处理返回结果
        SongInfo song_info_ori{ CSongDataManager::GetInstance().GetSongInfo3(cur_song) };
        vector<CLyricDownloadCommon::ItemInfo> down_list;
        theApp.GetLyricDownload()->DisposeSearchResult(down_list, search_result);		//处理返回的查找结果，并将结果保存在down_list容器里
        if (down_list.empty())
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_CANNOT_FIND_THIS_SONG").c_str());
            song_info_ori.SetNoOnlineLyric(true);
            CSongDataManager::GetInstance().AddItem(song_info_ori);
            continue;
        }

        //计算最佳选择项
        wstring title = cur_song.title;
        wstring artist = cur_song.artist;
        wstring album = cur_song.album;
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_TITLE") == title) title.clear();
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ARTIST") == artist) artist.clear();
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ALBUM") == album) album.clear();
        int best_matched = CLyricDownloadCommon::SelectMatchedItem(down_list, title, artist, album, cur_song.GetFileName(), true);
        if (best_matched < 0)
        {
            song_info_ori.SetNoOnlineLyric(true);
            CSongDataManager::GetInstance().AddItem(song_info_ori);
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_NO_MATCHED_LYRIC").c_str());
            continue;
        }

        //下载歌词
        theApp.GetLyricDownload()->DownloadLyric(down_list[best_matched].id, lyric_str, pInfo->download_translate);
        if (lyric_str.empty())
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_DOWNLOAD_FAILED").c_str());
            continue;
        }

        //处理歌词文本
        if (!theApp.GetLyricDownload()->DisposeLryic(lyric_str, pInfo->download_translate))
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_SONG_NO_LYRIC").c_str());
            continue;
        }

        song_info_ori.SetSongId(down_list[best_matched].id);
        CSongDataManager::GetInstance().AddItem(song_info_ori);

        //在歌词前面添加标签
        CLyricDownloadCommon::AddLyricTag(lyric_str, down_list[best_matched].id, down_list[best_matched].title, down_list[best_matched].artist, down_list[best_matched].album);

        //保存歌词
        bool char_cannot_convert{};
        if (CLyricBatchDownloadDlg::SaveLyric(lyric_path.c_str(), lyric_str, pInfo->save_code, &char_cannot_convert))
        {
            pInfo->downloaded_songs->insert(cur_song.file_path);
            if (char_cannot_convert)
                pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_ENCODE_WARNING").c_str());    //char_cannot_convert为true，则说明有无法转换的Unicode字符
            else
                pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_SUCCEEDED").c_str());
        }
        else
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_FILE_WRITE_FAILED").c_str());
        }

        if (pInfo->download_translate)
        {
            auto lyric_type = CLyrics::LyricType::LY_LRC;
            if (theApp.m_general_setting_data.lyric_download_service == GeneralSettingData::LDS_NETEASE)
                lyric_type = CLyrics::LyricType::LY_LRC_NETEASE;
            CLyrics lyrics{ lyric_path, lyric_type };		//打开保存过的歌词
            lyrics.SaveLyric2(theApp.m_general_setting_data.download_lyric_text_and_translation_in_same_line);
        }

    }
    ::PostMessage(pInfo->hwnd, WM_BATCH_DOWNLOAD_COMPLATE, 0, 0);
    return 0;
}


UINT CLyricBatchDownloadDlg::EmbedLyricThread(ThreadInfo* pInfo)
{
    CCommon::SetThreadLanguageList(theApp.m_str_table.GetLanguageTag());

    for (size_t i{}; i < pInfo->playlist->size(); i++)
    {
        if (theApp.m_batch_download_dialog_exit)
            return 0;
        int percent = static_cast<int>(i * 100 / pInfo->playlist->size());
        wstring info = theApp.m_str_table.LoadTextFormat(L"TXT_LYRIC_BDL_INFO_EMBEDDING", { percent });
        pInfo->static_ctrl->SetWindowText(info.c_str());
        pInfo->progress_bar->SetProgress(percent);

        const SongInfo& song = pInfo->playlist->at(i);
        pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBEDDING").c_str());
        pInfo->list_ctrl->EnsureVisible(i, FALSE);

        if (song.is_cue || COSUPlayerHelper::IsOsuFile(song.file_path))
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_SKIPPED").c_str());
            continue;
        }

        CFilePathHelper file_path(song.file_path);
        wstring ext = file_path.GetFileExtension();
        if (!CAudioTag::IsFileTypeLyricWriteSupport(ext))
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_UNSUPPORTED").c_str());
            continue;
        }

        wstring lyric_path = FindBatchLyricPath(song, pInfo->save_to_song_folder);
        if (lyric_path.empty())
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_NO_LRC").c_str());
            continue;
        }

        CLyrics lyrics(lyric_path);
        wstring lyric_text = lyrics.GetLyricsString();
        if (lyric_text.empty())
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_NO_LRC").c_str());
            continue;
        }

        SongInfo song_info{ CSongDataManager::GetInstance().GetSongInfo3(song) };
        CAudioTag audio_tag(song_info);
        bool has_inner_lyric = !audio_tag.GetAudioLyric().empty();
        bool newly_downloaded = pInfo->downloaded_songs != nullptr && pInfo->downloaded_songs->count(song.file_path) != 0;

        if (has_inner_lyric && !newly_downloaded)
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_SKIPPED_EXIST").c_str());
            continue;
        }

        if (song.file_path == CPlayer::GetInstance().GetCurrentSongInfo().file_path)
        {
            PendingEmbed pending;
            pending.row = static_cast<int>(i);
            pending.song_info = song_info;
            pending.lyric = lyric_text;
            pending.lyric_path = lyric_path;
            {
                CSingleLock lock(pInfo->cs, TRUE);
                pInfo->pending_embeds->push_back(pending);
            }
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_PENDING").c_str());
            continue;
        }

        if (audio_tag.WriteAudioLyric(lyric_text))
        {
            song_info.lyric_file = lyric_path;
            CSongDataManager::GetInstance().AddItem(song_info);
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_SUCCEEDED").c_str());
        }
        else
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_FAILED").c_str());
        }
    }

    ::PostMessage(pInfo->hwnd, WM_BATCH_DOWNLOAD_COMPLATE, 0, 0);
    return 0;
}


//下载专辑封面文件（带超时控制，避免单张封面卡住整个批次）
static bool DownloadCoverFile(const wstring& url, const wstring& save_path)
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


UINT CLyricBatchDownloadDlg::DownloadCoverThread(ThreadInfo* pInfo)
{
    CCommon::SetThreadLanguageList(theApp.m_str_table.GetLanguageTag());
    CMusicPlayerCmdHelper helper;

    for (size_t i{}; i < pInfo->playlist->size(); i++)
    {
        if (theApp.m_batch_download_dialog_exit)
            return 0;
        int percent = static_cast<int>(i * 100 / pInfo->playlist->size());
        wstring info = theApp.m_str_table.LoadTextFormat(L"TXT_LYRIC_BDL_INFO_COVER_DOWNLOADING", { percent });
        pInfo->static_ctrl->SetWindowText(info.c_str());
        pInfo->progress_bar->SetProgress(percent);

        const SongInfo& song = pInfo->playlist->at(i);
        pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_DOWNLOADING").c_str());
        pInfo->list_ctrl->EnsureVisible(i, FALSE);

        if (song.is_cue || COSUPlayerHelper::IsOsuFile(song.file_path) || CCommon::IsURL(song.file_path))
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_SKIPPED").c_str());
            continue;
        }

        if (!helper.SearchAlbumCover(song).empty())
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EXIST").c_str());
            continue;
        }

        SongInfo song_info{ CSongDataManager::GetInstance().GetSongInfo3(song) };
        wstring title = song_info.title;
        wstring artist = song_info.artist;
        wstring album = song_info.album;
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_TITLE") == title) title.clear();
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ARTIST") == artist) artist.clear();
        if (theApp.m_str_table.LoadText(L"TXT_EMPTY_ALBUM") == album) album.clear();

        DownloadResult download_result;
        CLyricDownloadCommon::ItemInfo item = theApp.GetLyricDownload()->SearchSongAndGetMatched(title, artist, album, song.GetFileName(), false, &download_result);
        if (download_result != DR_SUCCESS || item.id.empty())
        {
            if (download_result == DR_NETWORK_ERROR)
                pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_NETWORK_FAILED").c_str());
            else
                pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_NO_MATCH").c_str());
            continue;
        }

        wstring cover_url = theApp.GetLyricDownload()->GetAlbumCoverURL(item.id);
        if (cover_url.empty())
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_FAILED").c_str());
            continue;
        }

        wstring cover_name;
        if (!item.album.empty() && item.album == song.album)
            cover_name = item.album;
        else
            cover_name = CFilePathHelper(song.GetFileName()).ReplaceFileExtension(nullptr);
        CCommon::FileNameNormalize(cover_name);
        if (cover_name.empty())
            cover_name = L"cover";

        CFilePathHelper url_path(cover_url);
        wstring cover_ext = url_path.GetFileExtension(false, true);
        if (cover_ext.empty())
            cover_ext = L".jpg";
        bool save_to_album_folder = (!theApp.m_general_setting_data.save_album_to_song_folder && CCommon::FolderExist(theApp.m_app_setting_data.AbsoluteAlbumCoverPath()));
        wstring cover_dir = save_to_album_folder ? theApp.m_app_setting_data.AbsoluteAlbumCoverPath() : CFilePathHelper(song.file_path).GetDir();
        wstring cover_path = cover_dir + cover_name + cover_ext;
        if (CCommon::FileExist(cover_path))
            ::DeleteFile(cover_path.c_str());

        bool downloaded = DownloadCoverFile(cover_url, cover_path);
        if (downloaded && CCommon::FileExist(cover_path))
        {
            if (!save_to_album_folder)
                ::SetFileAttributes(cover_path.c_str(), FILE_ATTRIBUTE_HIDDEN);
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_SUCCEEDED").c_str());
        }
        else
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_FAILED").c_str());
        }
    }

    ::PostMessage(pInfo->hwnd, WM_BATCH_DOWNLOAD_COMPLATE, 0, 0);
    return 0;
}


UINT CLyricBatchDownloadDlg::EmbedCoverThread(ThreadInfo* pInfo)
{
    CCommon::SetThreadLanguageList(theApp.m_str_table.GetLanguageTag());
    CMusicPlayerCmdHelper helper;

    for (size_t i{}; i < pInfo->playlist->size(); i++)
    {
        if (theApp.m_batch_download_dialog_exit)
            return 0;
        int percent = static_cast<int>(i * 100 / pInfo->playlist->size());
        wstring info = theApp.m_str_table.LoadTextFormat(L"TXT_LYRIC_BDL_INFO_COVER_EMBEDDING", { percent });
        pInfo->static_ctrl->SetWindowText(info.c_str());
        pInfo->progress_bar->SetProgress(percent);

        const SongInfo& song = pInfo->playlist->at(i);
        pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EMBEDDING").c_str());
        pInfo->list_ctrl->EnsureVisible(i, FALSE);

        if (song.is_cue || COSUPlayerHelper::IsOsuFile(song.file_path) || CCommon::IsURL(song.file_path))
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_SKIPPED").c_str());
            continue;
        }

        CFilePathHelper file_path(song.file_path);
        wstring ext = file_path.GetFileExtension();
        if (!CAudioTag::IsFileTypeCoverWriteSupport(ext))
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_UNSUPPORTED").c_str());
            continue;
        }

        wstring album_cover_path = helper.SearchAlbumCover(song);
        if (album_cover_path.empty())
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_NO_COVER").c_str());
            continue;
        }

        SongInfo song_info{ CSongDataManager::GetInstance().GetSongInfo3(song) };
        if (song.file_path == CPlayer::GetInstance().GetCurrentSongInfo().file_path)
        {
            PendingCoverEmbed pending;
            pending.row = static_cast<int>(i);
            pending.song_info = song_info;
            pending.cover_path = album_cover_path;
            {
                CSingleLock lock(pInfo->cs, TRUE);
                pInfo->pending_cover_embeds->push_back(pending);
            }
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_PENDING").c_str());
            continue;
        }

        CAudioTag audio_tag(song_info);
        if (audio_tag.WriteAlbumCover(album_cover_path))
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_SUCCEEDED").c_str());
        }
        else
        {
            pInfo->list_ctrl->SetItemText(i, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_FAILED").c_str());
        }
    }

    ::PostMessage(pInfo->hwnd, WM_BATCH_DOWNLOAD_COMPLATE, 0, 0);
    return 0;
}


void CLyricBatchDownloadDlg::FlushPendingEmbeds()
{
    vector<PendingEmbed> pending_embeds;
    {
        CSingleLock lock(&m_cs, TRUE);
        pending_embeds.swap(m_pending_embeds);
    }

    for (PendingEmbed& pending : pending_embeds)
    {
        bool is_current_song = (pending.song_info.file_path == CPlayer::GetInstance().GetCurrentSongInfo().file_path);
        bool write_ok = false;

        auto do_write = [&]() {
            CAudioTag audio_tag(pending.song_info);
            return audio_tag.WriteAudioLyric(pending.lyric);
        };

        if (is_current_song)
        {
            CPlayer::ReOpen reopen(true);
            if (!reopen.IsLockSuccess())
            {
                {
                    CSingleLock lock(&m_cs, TRUE);
                    m_pending_embeds.push_back(pending);
                }
                if (::IsWindow(GetSafeHwnd()) && pending.row >= 0)
                    m_song_list_ctrl.SetItemText(pending.row, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_EMBED_FILE_LOCKED").c_str());
                continue;
            }
            write_ok = do_write();
        }
        else
        {
            write_ok = do_write();
        }

        if (write_ok)
        {
            SongInfo song_info = pending.song_info;
            song_info.lyric_file = pending.lyric_path;
            CSongDataManager::GetInstance().AddItem(song_info);
        }

        if (::IsWindow(GetSafeHwnd()) && pending.row >= 0)
            m_song_list_ctrl.SetItemText(pending.row, 4,
                theApp.m_str_table.LoadText(write_ok ? L"TXT_LYRIC_BDL_STATUS_EMBED_SUCCEEDED" : L"TXT_LYRIC_BDL_STATUS_EMBED_FAILED").c_str());
    }
}


void CLyricBatchDownloadDlg::FlushPendingCoverEmbeds()
{
    vector<PendingCoverEmbed> pending_items;
    {
        CSingleLock lock(&m_cs, TRUE);
        pending_items.swap(m_pending_cover_embeds);
    }

    for (PendingCoverEmbed& pending : pending_items)
    {
        bool is_current_song = (pending.song_info.file_path == CPlayer::GetInstance().GetCurrentSongInfo().file_path);
        bool write_ok = false;

        auto do_write = [&]() {
            CAudioTag audio_tag(pending.song_info);
            return audio_tag.WriteAlbumCover(pending.cover_path);
        };

        if (is_current_song)
        {
            CPlayer::ReOpen reopen(true);
            if (!reopen.IsLockSuccess())
            {
                {
                    CSingleLock lock(&m_cs, TRUE);
                    m_pending_cover_embeds.push_back(pending);
                }
                if (::IsWindow(GetSafeHwnd()) && pending.row >= 0)
                    m_song_list_ctrl.SetItemText(pending.row, 4, theApp.m_str_table.LoadText(L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_FILE_LOCKED").c_str());
                continue;
            }
            write_ok = do_write();
        }
        else
        {
            write_ok = do_write();
        }

        if (::IsWindow(GetSafeHwnd()) && pending.row >= 0)
            m_song_list_ctrl.SetItemText(pending.row, 4,
                theApp.m_str_table.LoadText(write_ok ? L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_SUCCEEDED" : L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_FAILED").c_str());
    }
}


afx_msg LRESULT CLyricBatchDownloadDlg::OnBatchDownloadComplate(WPARAM wParam, LPARAM lParam)
{
    KillTimer(BATCH_STATISTICS_TIMER_ID);
    m_pThread = nullptr;
    m_progress_bar.SetProgress(100);
    EnableControls(true);

    if (m_thread_info.action == BatchAction::EmbedLyric)
    {
        FlushPendingEmbeds();
        CPlayer::GetInstance().SearchLyrics(true);
        CPlayer::GetInstance().IniLyrics();
    }
    else if (m_thread_info.action == BatchAction::DownloadCover)
    {
        CPlayer::GetInstance().SearchOutAlbumCover();
        CPlayer::GetInstance().AlbumCoverGaussBlur();
    }
    else if (m_thread_info.action == BatchAction::EmbedCover)
    {
        FlushPendingCoverEmbeds();
        CPlayer::GetInstance().SearchAlbumCover();
        CPlayer::GetInstance().AlbumCoverGaussBlur();
    }
    else
    {
        CPlayer::GetInstance().SearchLyrics(true);
        CPlayer::GetInstance().IniLyrics();
    }
    ShowBatchStatistics(m_thread_info.action);
    const wchar_t* complete_key = L"TXT_LYRIC_BDL_INFO_COMPLETE";
    switch (m_thread_info.action)
    {
    case BatchAction::EmbedLyric: complete_key = L"TXT_LYRIC_BDL_INFO_EMBED_COMPLETE"; break;
    case BatchAction::DownloadCover: complete_key = L"TXT_LYRIC_BDL_INFO_COVER_COMPLETE"; break;
    case BatchAction::EmbedCover: complete_key = L"TXT_LYRIC_BDL_INFO_COVER_EMBED_COMPLETE"; break;
    default: break;
    }
    SetDlgItemText(IDC_INFO_STATIC, theApp.m_str_table.LoadText(complete_key).c_str());
    return 0;
}


void CLyricBatchDownloadDlg::ShowBatchStatistics(BatchAction action)
{
    auto status_is = [](const CString& status, std::initializer_list<const wchar_t*> keys)
    {
        for (const wchar_t* key : keys)
        {
            if (status == theApp.m_str_table.LoadText(key).c_str())
                return true;
        }
        return false;
    };

    int succeeded{};
    int skipped{};
    int failed{};
    int pending{};
    const int total = static_cast<int>(m_playlist.size());

    for (int i{}; i < total; ++i)
    {
        const CString status = m_song_list_ctrl.GetItemText(i, 4);
        if (status.IsEmpty())
        {
            ++pending;
            continue;
        }

        bool counted = false;
        switch (action)
        {
        case BatchAction::DownloadLyric:
            if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_SUCCEEDED", L"TXT_LYRIC_BDL_STATUS_ENCODE_WARNING" }))
            {
                ++succeeded;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_SKIPPED" }))
            {
                ++skipped;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_NETWORK_FAILED", L"TXT_LYRIC_BDL_STATUS_CANNOT_FIND_THIS_SONG",
                L"TXT_LYRIC_BDL_STATUS_NO_MATCHED_LYRIC", L"TXT_LYRIC_BDL_STATUS_DOWNLOAD_FAILED",
                L"TXT_LYRIC_BDL_STATUS_SONG_NO_LYRIC", L"TXT_LYRIC_BDL_STATUS_FILE_WRITE_FAILED" }))
            {
                ++failed;
                counted = true;
            }
            break;
        case BatchAction::EmbedLyric:
            if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_EMBED_SUCCEEDED" }))
            {
                ++succeeded;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_EMBED_SKIPPED_EXIST", L"TXT_LYRIC_BDL_STATUS_EMBED_NO_LRC",
                L"TXT_LYRIC_BDL_STATUS_EMBED_UNSUPPORTED", L"TXT_LYRIC_BDL_STATUS_SKIPPED" }))
            {
                ++skipped;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_EMBED_FAILED" }))
            {
                ++failed;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_EMBED_PENDING", L"TXT_LYRIC_BDL_STATUS_EMBED_FILE_LOCKED" }))
            {
                ++pending;
                counted = true;
            }
            break;
        case BatchAction::DownloadCover:
            if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_COVER_SUCCEEDED" }))
            {
                ++succeeded;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_COVER_EXIST", L"TXT_LYRIC_BDL_STATUS_SKIPPED" }))
            {
                ++skipped;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_COVER_FAILED", L"TXT_LYRIC_BDL_STATUS_COVER_NO_MATCH",
                L"TXT_LYRIC_BDL_STATUS_NETWORK_FAILED" }))
            {
                ++failed;
                counted = true;
            }
            break;
        case BatchAction::EmbedCover:
            if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_SUCCEEDED" }))
            {
                ++succeeded;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_UNSUPPORTED", L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_NO_COVER",
                L"TXT_LYRIC_BDL_STATUS_SKIPPED" }))
            {
                ++skipped;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_FAILED" }))
            {
                ++failed;
                counted = true;
            }
            else if (status_is(status, { L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_PENDING", L"TXT_LYRIC_BDL_STATUS_COVER_EMBED_FILE_LOCKED" }))
            {
                ++pending;
                counted = true;
            }
            break;
        }
        if (!counted)
            ++pending;
    }

    wstring info = theApp.m_str_table.LoadTextFormat(L"TXT_LYRIC_BDL_INFO_STATISTICS", { total, succeeded, skipped, failed, pending });
    CString previous_info;
    GetDlgItemText(IDC_LYRIC_BDL_STATISTICS_STATIC, previous_info);
    if (previous_info != info.c_str())
        SetDlgItemText(IDC_LYRIC_BDL_STATISTICS_STATIC, info.c_str());
}


void CLyricBatchDownloadDlg::OnClose()
{
    // TODO: 在此添加消息处理程序代码和/或调用默认值

    CBaseDialog::OnClose();
}


void CLyricBatchDownloadDlg::OnCancel()
{
    // TODO: 在此添加专用代码和/或调用基类
    //对话框将要关闭时，将退出标志置为true
    theApp.m_batch_download_dialog_exit = true;
    if (m_pThread != nullptr)
    {
        int rtn = WaitForSingleObject(m_pThread->m_hThread, 2000);	//等待线程退出
        // std::wstringstream wss;
        // wss << std::hex << std::uppercase << L"0x" << rtn;
        // MessageBox(wss.str().c_str(), NULL, MB_ICONINFORMATION);

    }
    DestroyWindow();
    //CBaseDialog::OnCancel();
}


void CLyricBatchDownloadDlg::OnOK()
{
    // TODO: 在此添加专用代码和/或调用基类
    //对话框将要关闭时，将退出标志置为true
    theApp.m_batch_download_dialog_exit = true;
    if (m_pThread != nullptr)
        WaitForSingleObject(m_pThread->m_hThread, 2000);	//等待线程退出

    CBaseDialog::OnOK();
}


void CLyricBatchDownloadDlg::OnBnClickedSaveToSongFolder()
{
    // TODO: 在此添加控件通知处理程序代码
    m_save_to_song_folder = true;
}


void CLyricBatchDownloadDlg::OnBnClickedSaveToLyricFolder()
{
    // TODO: 在此添加控件通知处理程序代码
    m_save_to_song_folder = false;
}
