#pragma once
#include "Common.h"

//OpenAI兼容接口的LLM客户端封装，用于AI自动整理歌曲功能
//支持三种接口格式：OpenAI兼容、Google Gemini、Anthropic Claude
//配置以ini节"ai_organize"保存在程序配置文件中
class CAiClient
{
public:
    enum AiResult
    {
        AI_SUCCESS,         //成功
        AI_CONFIG_INVALID,  //配置无效
        AI_NETWORK_ERROR,   //网络错误
        AI_API_ERROR,       //接口返回错误
        AI_PARSE_ERROR,     //响应解析失败
    };

    //接口格式
    enum ApiFormat
    {
        AF_OPENAI = 0,      //OpenAI兼容格式（绝大多数服务商）
        AF_GEMINI = 1,      //Google Gemini原生格式
        AF_ANTHROPIC = 2,   //Anthropic Claude原生格式
    };

    //服务商预设
    struct ProviderPreset
    {
        wstring name;       //显示名称
        int format;         //接口格式（ApiFormat）
        wstring base_url;   //接口基础地址
        wstring model;      //默认模型
    };

    //获取内置服务商预设列表
    static const vector<ProviderPreset>& GetPresets();

    //根据索引获取预设名称，索引无效时返回“自定义”
    static wstring GetPresetName(int index);

    //根据格式索引获取格式显示名称
    static wstring GetFormatName(int format);

    //配置的读取与保存
    static void LoadConfig(int& preset_index, int& api_format, wstring& base_url, wstring& api_key, wstring& model);
    static void SaveConfig(int preset_index, int api_format, const wstring& base_url, const wstring& api_key, const wstring& model);

    //配置是否有效（接口地址和模型名称不为空）
    static bool IsConfigValid();

    //调用chat/completions接口获取模型回复（根据配置的接口格式自动分发）
    //system_prompt：系统提示词；user_content：用户消息；response：模型回复的文本
    //error_info：失败时的错误信息
    static int ChatComplete(const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info);

    //获取模型列表，返回AI_SUCCESS等错误码
    static int FetchModelList(int api_format, const wstring& base_url, const wstring& api_key, vector<wstring>& models, wstring& error_info);

    //测试连接是否可用
    static bool TestConnection(wstring& error_info);

    //从模型回复文本中提取JSON字符串（去除markdown代码块标记等无关内容），未找到时返回空
    static wstring ExtractJson(const wstring& text);

private:
    //读取当前配置
    static void GetConfig(int& api_format, wstring& base_url, wstring& api_key, wstring& model);

    //去掉base_url末尾的斜杠
    static wstring TrimUrlSlash(wstring url);

    //三种格式的会话请求实现
    static int ChatCompleteOpenAI(const wstring& base_url, const wstring& api_key, const wstring& model, const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info);
    static int ChatCompleteGemini(const wstring& base_url, const wstring& api_key, const wstring& model, const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info);
    static int ChatCompleteAnthropic(const wstring& base_url, const wstring& api_key, const wstring& model, const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info);
};

//AI识别的结果
struct AiSongInfo
{
    wstring title;
    wstring artist;
    wstring album;
    wstring album_artist;
    int confidence{ 0 };        //置信度0~100
    bool changed{ false };      //AI是否认为需要修改
    bool analyzed{ false };     //是否已完成分析
};
