#pragma once
#include "BaseDialog.h"
#include "AiClient.h"
#include "afxcmn.h"

//CAiSettingDlg AI服务设置对话框（模态）
class CAiSettingDlg : public CBaseDialog
{
    DECLARE_DYNAMIC(CAiSettingDlg)

public:
    CAiSettingDlg(CWnd* pParent = NULL);   // 标准构造函数
    virtual ~CAiSettingDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_AI_SETTING_DIALOG };
#endif

#define WM_AI_MODELS_FETCHED (WM_USER + 142)        //获取模型列表完成消息

protected:
    CComboBox m_provider_combo;
    CComboBox m_format_combo;
    CEdit m_url_edit;
    CEdit m_key_edit;
    CComboBox m_model_combo;
    CButton m_test_btn;
    CButton m_fetch_btn;

    int m_preset_index{ 0 };
    int m_api_format{ 0 };
    wstring m_base_url;
    wstring m_api_key;
    wstring m_model;

    //获取模型列表线程的结果（该对话框为单例模态对话框，使用静态成员传递结果）
    static vector<wstring> m_fetched_models;
    static wstring m_fetch_error_info;

    //获取模型列表工作线程的参数
    struct FetchThreadInfo
    {
        HWND hwnd;
    };
    static UINT FetchModelsThreadFunc(LPVOID lpParam);

    virtual CString GetDialogName() const override;
    virtual bool InitializeControls() override;
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

    //将界面上的配置保存到ini
    void SaveUiToConfig();

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    afx_msg void OnCbnSelchangeProviderCombo();
    afx_msg void OnBnClickedTestBtn();
    afx_msg void OnBnClickedFetchModelsBtn();
protected:
    afx_msg LRESULT OnModelsFetched(WPARAM wParam, LPARAM lParam);
public:
    virtual void OnOK();
};
