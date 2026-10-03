#include "stdafx.h"
#include "AiSettingDlg.h"
#include "MusicPlayer2.h"
#include "afxdialogex.h"


// CAiSettingDlg 对话框

IMPLEMENT_DYNAMIC(CAiSettingDlg, CBaseDialog)

vector<wstring> CAiSettingDlg::m_fetched_models;
wstring CAiSettingDlg::m_fetch_error_info;

CAiSettingDlg::CAiSettingDlg(CWnd* pParent /*=NULL*/)
    : CBaseDialog(IDD_AI_SETTING_DIALOG, pParent)
{
}

CAiSettingDlg::~CAiSettingDlg()
{
}

CString CAiSettingDlg::GetDialogName() const
{
    return _T("AiSettingDlg");
}

bool CAiSettingDlg::InitializeControls()
{
    wstring temp;
    temp = theApp.m_str_table.LoadText(L"TITLE_AI_SETTING");
    SetWindowTextW(temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_PROVIDER");
    SetDlgItemTextW(IDC_TXT_AI_PROVIDER_STATIC, temp.c_str());
    // IDC_AI_SETTING_COMBO
    temp = theApp.m_str_table.LoadText(L"TXT_AI_FORMAT");
    SetDlgItemTextW(IDC_TXT_AI_FORMAT_STATIC, temp.c_str());
    // IDC_AI_FORMAT_COMBO
    temp = theApp.m_str_table.LoadText(L"TXT_AI_BASE_URL");
    SetDlgItemTextW(IDC_TXT_AI_URL_STATIC, temp.c_str());
    // IDC_AI_URL_EDIT
    temp = theApp.m_str_table.LoadText(L"TXT_AI_API_KEY");
    SetDlgItemTextW(IDC_TXT_AI_KEY_STATIC, temp.c_str());
    // IDC_AI_KEY_EDIT
    temp = theApp.m_str_table.LoadText(L"TXT_AI_MODEL");
    SetDlgItemTextW(IDC_TXT_AI_MODEL_STATIC, temp.c_str());
    // IDC_AI_MODEL_COMBO
    temp = theApp.m_str_table.LoadText(L"TXT_AI_FETCH_MODELS");
    SetDlgItemTextW(IDC_AI_FETCH_MODELS_BTN, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_KEY_HINT");
    SetDlgItemTextW(IDC_TXT_AI_KEY_HINT_STATIC, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_AI_TEST");
    SetDlgItemTextW(IDC_AI_TEST_BTN, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_OK");
    SetDlgItemTextW(IDOK, temp.c_str());
    temp = theApp.m_str_table.LoadText(L"TXT_CANCEL");
    SetDlgItemTextW(IDCANCEL, temp.c_str());
    return true;
}

void CAiSettingDlg::DoDataExchange(CDataExchange* pDX)
{
    CBaseDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_AI_SETTING_COMBO, m_provider_combo);
    DDX_Control(pDX, IDC_AI_FORMAT_COMBO, m_format_combo);
    DDX_Control(pDX, IDC_AI_URL_EDIT, m_url_edit);
    DDX_Control(pDX, IDC_AI_KEY_EDIT, m_key_edit);
    DDX_Control(pDX, IDC_AI_MODEL_COMBO, m_model_combo);
    DDX_Control(pDX, IDC_AI_TEST_BTN, m_test_btn);
    DDX_Control(pDX, IDC_AI_FETCH_MODELS_BTN, m_fetch_btn);
}


BEGIN_MESSAGE_MAP(CAiSettingDlg, CBaseDialog)
    ON_CBN_SELCHANGE(IDC_AI_SETTING_COMBO, &CAiSettingDlg::OnCbnSelchangeProviderCombo)
    ON_BN_CLICKED(IDC_AI_TEST_BTN, &CAiSettingDlg::OnBnClickedTestBtn)
    ON_BN_CLICKED(IDC_AI_FETCH_MODELS_BTN, &CAiSettingDlg::OnBnClickedFetchModelsBtn)
    ON_MESSAGE(WM_AI_MODELS_FETCHED, &CAiSettingDlg::OnModelsFetched)
END_MESSAGE_MAP()


// CAiSettingDlg 消息处理程序

BOOL CAiSettingDlg::OnInitDialog()
{
    CBaseDialog::OnInitDialog();

    SetIcon(IconMgr::IconType::IT_Fix, FALSE);
    SetIcon(IconMgr::IconType::IT_Fix, TRUE);

    //载入配置
    CAiClient::LoadConfig(m_preset_index, m_api_format, m_base_url, m_api_key, m_model);

    //填充服务商预设下拉框
    const auto& presets = CAiClient::GetPresets();
    for (const auto& preset : presets)
        m_provider_combo.AddString(preset.name.c_str());
    m_provider_combo.AddString(theApp.m_str_table.LoadText(L"TXT_AI_CUSTOM_PROVIDER").c_str());
    if (m_preset_index < 0 || m_preset_index > static_cast<int>(presets.size()))
        m_preset_index = static_cast<int>(presets.size());
    m_provider_combo.SetCurSel(m_preset_index);

    //填充接口格式下拉框
    for (int format : { CAiClient::AF_OPENAI, CAiClient::AF_GEMINI, CAiClient::AF_ANTHROPIC })
        m_format_combo.AddString(CAiClient::GetFormatName(format).c_str());
    if (m_api_format < CAiClient::AF_OPENAI || m_api_format > CAiClient::AF_ANTHROPIC)
        m_api_format = CAiClient::AF_OPENAI;
    m_format_combo.SetCurSel(m_api_format);

    m_url_edit.SetWindowText(m_base_url.c_str());
    m_key_edit.SetWindowText(m_api_key.c_str());
    m_model_combo.SetWindowText(m_model.c_str());

    return TRUE;  // return TRUE unless you set the focus to a control
    // 异常: OCX 属性页应返回 FALSE
}

//切换服务商预设时自动填充接口格式、接口地址和模型名称
void CAiSettingDlg::OnCbnSelchangeProviderCombo()
{
    int index = m_provider_combo.GetCurSel();
    m_preset_index = index;
    const auto& presets = CAiClient::GetPresets();
    if (index >= 0 && index < static_cast<int>(presets.size()))
    {
        m_format_combo.SetCurSel(presets[index].format);
        m_url_edit.SetWindowText(presets[index].base_url.c_str());
        m_model_combo.SetWindowText(presets[index].model.c_str());
    }
}

void CAiSettingDlg::OnBnClickedTestBtn()
{
    //先把界面上当前填写的配置保存，再测试连接
    SaveUiToConfig();

    m_test_btn.EnableWindow(FALSE);
    wstring error_info;
    bool success = CAiClient::TestConnection(error_info);
    m_test_btn.EnableWindow(TRUE);

    if (success)
        MessageBox(theApp.m_str_table.LoadText(L"MSG_AI_TEST_OK").c_str(), NULL, MB_ICONINFORMATION | MB_OK);
    else
    {
        wstring info = theApp.m_str_table.LoadTextFormat(L"MSG_AI_TEST_FAILED", { error_info });
        MessageBox(info.c_str(), NULL, MB_ICONWARNING | MB_OK);
    }
}

//获取模型列表（工作线程中请求/models接口，完成后发消息回UI线程）
void CAiSettingDlg::OnBnClickedFetchModelsBtn()
{
    //先把界面上当前填写的配置保存，获取模型时使用当前配置
    SaveUiToConfig();
    if (m_base_url.empty())
    {
        MessageBox(theApp.m_str_table.LoadText(L"MSG_AI_CONFIG_INVALID").c_str(), NULL, MB_ICONWARNING | MB_OK);
        return;
    }

    m_fetch_btn.EnableWindow(FALSE);
    m_model_combo.EnableWindow(FALSE);

    static FetchThreadInfo thread_info;
    thread_info.hwnd = GetSafeHwnd();
    AfxBeginThread(FetchModelsThreadFunc, &thread_info);
}

UINT CAiSettingDlg::FetchModelsThreadFunc(LPVOID lpParam)
{
    CCommon::SetThreadLanguageList(theApp.m_str_table.GetLanguageTag());
    FetchThreadInfo* pInfo = (FetchThreadInfo*)lpParam;

    int preset_index, api_format;
    wstring base_url, api_key, model;
    CAiClient::LoadConfig(preset_index, api_format, base_url, api_key, model);
    m_fetched_models.clear();
    m_fetch_error_info.clear();
    CAiClient::FetchModelList(api_format, base_url, api_key, m_fetched_models, m_fetch_error_info);

    ::PostMessage(pInfo->hwnd, WM_AI_MODELS_FETCHED, 0, 0);
    return 0;
}

afx_msg LRESULT CAiSettingDlg::OnModelsFetched(WPARAM wParam, LPARAM lParam)
{
    m_fetch_btn.EnableWindow(TRUE);
    m_model_combo.EnableWindow(TRUE);
    if (m_fetched_models.empty())
    {
        wstring info = theApp.m_str_table.LoadTextFormat(L"MSG_AI_MODELS_FETCH_FAILED", { m_fetch_error_info });
        MessageBox(info.c_str(), NULL, MB_ICONWARNING | MB_OK);
        return 0;
    }
    //填充模型下拉框，保留当前输入的文本
    CString current_text;
    m_model_combo.GetWindowText(current_text);
    m_model_combo.ResetContent();
    for (const auto& model : m_fetched_models)
        m_model_combo.AddString(model.c_str());
    if (!current_text.IsEmpty())
        m_model_combo.SetWindowText(current_text);
    else if (m_model_combo.GetCount() > 0)
        m_model_combo.SetCurSel(0);

    wstring info = theApp.m_str_table.LoadTextFormat(L"MSG_AI_MODELS_FETCHED", { m_fetched_models.size() });
    MessageBox(info.c_str(), NULL, MB_ICONINFORMATION | MB_OK);
    return 0;
}

void CAiSettingDlg::SaveUiToConfig()
{
    wchar_t buff[512]{};
    m_url_edit.GetWindowText(buff, 512);
    m_base_url = buff;
    m_key_edit.GetWindowText(buff, 512);
    m_api_key = buff;
    m_model_combo.GetWindowText(buff, 512);
    m_model = buff;
    int format = m_format_combo.GetCurSel();
    if (format >= CAiClient::AF_OPENAI && format <= CAiClient::AF_ANTHROPIC)
        m_api_format = format;
    CAiClient::SaveConfig(m_preset_index, m_api_format, m_base_url, m_api_key, m_model);
}

void CAiSettingDlg::OnOK()
{
    SaveUiToConfig();
    CBaseDialog::OnOK();
}
