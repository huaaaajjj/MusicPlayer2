#pragma once
#include "Common.h"
#include "Player.h"
#include "AiClient.h"
#include "ListCtrlEx.h"
#include "PlayerProgressBar.h"
#include "BaseDialog.h"
#include <set>
#include <map>

// CAiSongOrganizeDlg 对话框
//AI自动整理歌曲：先用AI（多线程并发）分析播放列表中每首歌的正确元数据，用户勾选后批量应用
//（写回标签、下载歌词、内嵌歌词、下载/内嵌专辑封面）
//支持断点保存：分析结果和应用进度持久化到检查点文件，中断后可恢复，不重复消耗API

class CAiSongOrganizeDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CAiSongOrganizeDlg)

public:
    CAiSongOrganizeDlg(CWnd* pParent = NULL);   // 标准构造函数
    virtual ~CAiSongOrganizeDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_AI_ORGANIZE_DIALOG };
#endif

#define WM_AI_ANALYZE_COMPLATE (WM_USER + 140)      //AI分析完成消息
#define WM_AI_APPLY_COMPLATE (WM_USER + 141)        //整理应用完成消息

    //工作阶段
    enum Phase
    {
        PH_ANALYZE,     //AI分析阶段
        PH_APPLY,       //应用阶段
    };

    //正在播放的曲目被播放器占用，无法在工作线程中写入：
    //CPlayer::ReOpen全程持有播放状态互斥量，且MusicControl(OPEN)内部会向主窗口阻塞式SendMessage，
    //在工作线程中调用有死锁风险（项目中所有ReOpen调用点均在主线程）。
    //因此工作线程照常完成网络部分，把需要写文件的操作登记到此结构，应用阶段结束后由主线程统一写入。
    struct PendingWrite
    {
        int row{ -1 };
        SongInfo song_info;         //已合并AI结果的标签信息
        wstring lyric;              //要内嵌的歌词（为空表示不内嵌）
        wstring cover_path;         //要内嵌的封面路径（为空表示不内嵌）
        bool write_tag{ false };    //是否需要写回标签
        wstring status;             //工作线程已累积的状态文本，由主线程续写
    };

    //用于向工作线程传递数据的结构体（多线程共享，只读指针+原子变量）
    struct ThreadInfo
    {
        CListCtrl* list_ctrl;
        CStatic* static_ctrl;
        CPlayerProgressBar* progress_bar;
        const vector<SongInfo>* songs;          //播放列表快照
        vector<AiSongInfo>* ai_results;         //AI分析结果（与songs对应）
        CCriticalSection* cs;                   //保护ai_results与断点数据的临界区
        int phase;                              //Phase
        bool write_tag;
        bool download_lyric;
        bool embed_lyric;
        bool download_cover;
        bool embed_cover;
        HWND hwnd;
        //多线程任务分发
        volatile LONG* p_next_index;            //共享的任务游标（原子递增取号）
        volatile LONG* p_processed;             //已完成的任务数
        volatile LONG* p_active;                //活动线程数（最后一个退出的线程发完成消息）
        const vector<int>* selected_rows;       //应用阶段的行号列表
        int total;                              //任务总数
        //断点保存
        std::map<wstring, AiSongInfo>* analyze_ckpt;    //分析结果检查点（文件路径->结果）
        std::set<wstring>* apply_done;                  //应用完成的文件路径
        vector<PendingWrite>* pending_writes;           //需要主线程完成的写入（正在播放的曲目）
    };

    //工作线程函数（多线程并发，根据phase执行分析或应用）
    static UINT ThreadFunc(LPVOID lpParam);

    //列表控件的列索引
    enum ListColumn
    {
        COL_INDEX = 0,
        COL_FILE_NAME,
        COL_ORIGINAL,
        COL_AI_TITLE,
        COL_AI_ARTIST,
        COL_AI_ALBUM,
        COL_CONFIDENCE,
        COL_STATUS,
    };

protected:
    CButton m_write_tag_chk;
    CButton m_download_lyric_chk;
    CButton m_embed_lyric_chk;
    CButton m_download_cover_chk;
    CButton m_embed_cover_chk;
    CEdit m_concurrency_edit;
    CListCtrl m_song_list_ctrl;
    CStatic m_info_static;
    CPlayerProgressBar m_progress_bar;
    CButton m_cancel_btn;
    CButton m_select_all_btn;

    vector<SongInfo> m_songs;               //对话框初始化时播放列表的快照
    vector<AiSongInfo> m_ai_results;        //每首歌的AI分析结果
    CCriticalSection m_cs;                  //保护m_ai_results与断点数据的临界区

    //整理选项
    bool m_write_tag{ true };
    bool m_download_lyric{ true };
    bool m_embed_lyric{ true };
    bool m_download_cover{ true };
    bool m_embed_cover{ false };
    int m_concurrency{ 3 };                 //并发线程数（1~8）

    //断点保存数据
    std::map<wstring, AiSongInfo> m_analyze_ckpt;   //分析结果检查点
    std::set<wstring> m_apply_done;                 //应用阶段已完成的文件路径
    vector<int> m_selected_rows;                    //应用阶段勾选的行号（快照）
    vector<PendingWrite> m_pending_writes;          //待主线程写入的曲目（正在播放的文件）

    CWinThread* m_pThreads[8]{};            //工作线程数组
    int m_thread_count{};                   //实际创建的线程数
    ThreadInfo m_thread_info;               //传递给工作线程的参数
    volatile LONG m_next_index{};           //任务游标
    volatile LONG m_processed{};            //已完成任务数
    volatile LONG m_active_threads{};       //活动线程数

    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

    void SaveConfig() const;
    void LoadConfig();
    void EnableControls(bool enable);       //启用或禁用控件
    void FillThreadInfo(int phase);         //填充线程参数
    void StartThread(int phase);            //启动多个工作线程
    void StopThread();                      //等待所有工作线程退出
    //从检查点恢复分析结果到列表
    void RestoreAnalyzeFromCheckpoint();
    //将一行分析结果填到列表
    void FillAnalyzedRow(int row, const AiSongInfo& result);
    //在主线程用CPlayer::ReOpen保护下完成被占用文件的写入（应用阶段结束后调用）
    void FlushPendingWrites();

public:
    //断点检查点文件（工作线程通过ThreadInfo中的容器调用）
    static wstring CheckpointFilePath();
    //force为false时按2秒节流，避免每完成一首都在锁内重写整个json
    static void SaveCheckpointStatic(const std::map<wstring, AiSongInfo>& analyze_ckpt, const std::set<wstring>& apply_done, bool force = false);

protected:

    //断点检查点文件的读写
    void LoadCheckpoint();
    void SaveCheckpoint(bool force = false);    //需在m_cs锁内调用

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnBnClickedStartAnalyze();
    afx_msg void OnBnClickedSelectAll();
    afx_msg void OnBnClickedApplySelected();
    afx_msg void OnBnClickedSettingBtn();
    afx_msg void OnBnClickedCancelBtn();
    afx_msg void OnDestroy();
protected:
    afx_msg LRESULT OnAnalyzeComplate(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnApplyComplate(WPARAM wParam, LPARAM lParam);
public:
    virtual void OnCancel();
    virtual void OnOK();
};
