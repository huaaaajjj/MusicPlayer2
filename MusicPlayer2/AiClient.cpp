#include "stdafx.h"
#include "AiClient.h"
#include "InternetCommon.h"
#include "IniHelper.h"
#include "MusicPlayer2.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

const vector<CAiClient::ProviderPreset>& CAiClient::GetPresets()
{
    static const vector<ProviderPreset> presets
    {
        { L"DeepSeek",          AF_OPENAI,    L"https://api.deepseek.com",                      L"deepseek-chat" },
        { L"智谱GLM",           AF_OPENAI,    L"https://open.bigmodel.cn/api/paas/v4",          L"glm-4-flash" },
        { L"Kimi",              AF_OPENAI,    L"https://api.moonshot.cn/v1",                    L"moonshot-v1-8k" },
        { L"通义千问",          AF_OPENAI,    L"https://dashscope.aliyuncs.com/compatible-mode/v1", L"qwen-plus" },
        { L"豆包（火山方舟）",  AF_OPENAI,    L"https://ark.cn-beijing.volces.com/api/v3",      L"doubao-seed-1.6-flash" },
        { L"硅基流动",          AF_OPENAI,    L"https://api.siliconflow.cn/v1",                 L"Qwen/Qwen2.5-7B-Instruct" },
        { L"腾讯混元",          AF_OPENAI,    L"https://api.hunyuan.cloud.tencent.com/v1",      L"hunyuan-lite" },
        { L"百度千帆",          AF_OPENAI,    L"https://qianfan.baidubce.com/v2",               L"ernie-speed-8k" },
        { L"MiniMax",           AF_OPENAI,    L"https://api.minimax.chat/v1",                   L"MiniMax-Text-01" },
        { L"OpenAI",            AF_OPENAI,    L"https://api.openai.com/v1",                     L"gpt-4o-mini" },
        { L"Google Gemini",     AF_GEMINI,    L"https://generativelanguage.googleapis.com/v1beta", L"gemini-2.0-flash" },
        { L"Anthropic Claude",  AF_ANTHROPIC, L"https://api.anthropic.com",                     L"claude-3-5-haiku-latest" },
        { L"Ollama（本地）",    AF_OPENAI,    L"http://localhost:11434/v1",                     L"qwen2.5:7b" },
    };
    return presets;
}

wstring CAiClient::GetPresetName(int index)
{
    if (index >= 0 && index < static_cast<int>(GetPresets().size()))
        return GetPresets()[index].name;
    return theApp.m_str_table.LoadText(L"TXT_AI_CUSTOM_PROVIDER");
}

wstring CAiClient::GetFormatName(int format)
{
    switch (format)
    {
    case AF_GEMINI: return L"Google Gemini";
    case AF_ANTHROPIC: return L"Anthropic Claude";
    default: return theApp.m_str_table.LoadText(L"TXT_AI_FORMAT_OPENAI");
    }
}

void CAiClient::LoadConfig(int& preset_index, int& api_format, wstring& base_url, wstring& api_key, wstring& model)
{
    CIniHelper ini(theApp.m_config_path);
    preset_index = ini.GetInt(L"ai_organize", L"preset_index", 0);
    api_format = ini.GetInt(L"ai_organize", L"api_format", AF_OPENAI);
    base_url = ini.GetString(L"ai_organize", L"base_url", L"");
    api_key = ini.GetString(L"ai_organize", L"api_key", L"");
    model = ini.GetString(L"ai_organize", L"model", L"");
}

void CAiClient::SaveConfig(int preset_index, int api_format, const wstring& base_url, const wstring& api_key, const wstring& model)
{
    CIniHelper ini(theApp.m_config_path);
    ini.WriteInt(L"ai_organize", L"preset_index", preset_index);
    ini.WriteInt(L"ai_organize", L"api_format", api_format);
    ini.WriteString(L"ai_organize", L"base_url", base_url);
    ini.WriteString(L"ai_organize", L"api_key", api_key);
    ini.WriteString(L"ai_organize", L"model", model);
    ini.Save();
}

void CAiClient::GetConfig(int& api_format, wstring& base_url, wstring& api_key, wstring& model)
{
    int preset_index;
    LoadConfig(preset_index, api_format, base_url, api_key, model);
}

bool CAiClient::IsConfigValid()
{
    int api_format;
    wstring base_url, api_key, model;
    GetConfig(api_format, base_url, api_key, model);
    return (!base_url.empty() && !model.empty());
}

wstring CAiClient::TrimUrlSlash(wstring url)
{
    while (!url.empty() && (url.back() == L'/' || url.back() == L'\\'))
        url.pop_back();
    return url;
}

//通用响应发送与错误处理：发送请求并解析出模型回复文本
static int AiSendAndParse(const wstring& url, const string& body, const wstring& headers,
    const std::function<bool(const json&, wstring&)>& response_parser, wstring& response, wstring& error_info)
{
    wstring result;
    int rtn = CInternetCommon::SendHttpRequest(true, url, result, body, headers);
    if (rtn != CInternetCommon::SUCCESS)
    {
        error_info = theApp.m_str_table.LoadText(L"MSG_AI_NETWORK_FAILED");
        return CAiClient::AI_NETWORK_ERROR;
    }

    json data;
    try
    {
        data = json::parse(result);
    }
    catch (...)
    {
        error_info = theApp.m_str_table.LoadText(L"MSG_AI_RESPONSE_PARSE_FAILED");
        return CAiClient::AI_PARSE_ERROR;
    }

    //通用错误字段处理（OpenAI/Gemini为error对象，Anthropic为error.message）
    if (data.contains("error") && data["error"].is_object())
    {
        error_info = CCommon::StrToUnicode(data["error"].value("message", ""), CodeType::UTF8);
        return CAiClient::AI_API_ERROR;
    }

    if (response_parser(data, response))
        return CAiClient::AI_SUCCESS;

    error_info = theApp.m_str_table.LoadText(L"MSG_AI_RESPONSE_PARSE_FAILED");
    return CAiClient::AI_PARSE_ERROR;
}

int CAiClient::ChatCompleteOpenAI(const wstring& base_url, const wstring& api_key, const wstring& model, const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info)
{
    json request;
    request["model"] = CCommon::UnicodeToStr(model, CodeType::UTF8_NO_BOM);
    json messages = json::array();
    json system_msg;
    system_msg["role"] = "system";
    system_msg["content"] = CCommon::UnicodeToStr(system_prompt, CodeType::UTF8_NO_BOM);
    messages.push_back(system_msg);
    json user_msg;
    user_msg["role"] = "user";
    user_msg["content"] = CCommon::UnicodeToStr(user_content, CodeType::UTF8_NO_BOM);
    messages.push_back(user_msg);
    request["messages"] = messages;
    request["temperature"] = 0.2;

    wstring headers = L"Content-Type: application/json";
    if (!api_key.empty())
        headers += (L"\r\nAuthorization: Bearer " + api_key);

    return AiSendAndParse(TrimUrlSlash(base_url) + L"/chat/completions", request.dump(), headers,
        [](const json& data, wstring& response) {
            if (!(data.contains("choices") && data["choices"].is_array() && !data["choices"].empty()
                && data["choices"][0].contains("message") && data["choices"][0]["message"].is_object()))
                return false;
            response = CCommon::StrToUnicode(data["choices"][0]["message"].value("content", ""), CodeType::UTF8);
            return true;
        }, response, error_info);
}

int CAiClient::ChatCompleteGemini(const wstring& base_url, const wstring& api_key, const wstring& model, const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info)
{
    wstring url = TrimUrlSlash(base_url) + L"/models/" + model + L":generateContent";

    json request;
    json system_instruction;
    system_instruction["parts"] = json::array({ { {"text", CCommon::UnicodeToStr(system_prompt, CodeType::UTF8_NO_BOM)} } });
    request["system_instruction"] = system_instruction;
    json content;
    content["role"] = "user";
    content["parts"] = json::array({ { {"text", CCommon::UnicodeToStr(user_content, CodeType::UTF8_NO_BOM)} } });
    request["contents"] = json::array({ content });
    json generation_config;
    generation_config["temperature"] = 0.2;
    request["generationConfig"] = generation_config;

    wstring headers = L"Content-Type: application/json";
    if (!api_key.empty())
        headers += (L"\r\nx-goog-api-key: " + api_key);

    return AiSendAndParse(url, request.dump(), headers,
        [](const json& data, wstring& response) {
            if (!(data.contains("candidates") && data["candidates"].is_array() && !data["candidates"].empty()
                && data["candidates"][0].contains("content") && data["candidates"][0]["content"].contains("parts")
                && data["candidates"][0]["content"]["parts"].is_array()))
                return false;
            wstring text;
            for (const auto& part : data["candidates"][0]["content"]["parts"])
            {
                if (part.is_object() && part.contains("text"))
                    text += CCommon::StrToUnicode(part["text"].get<string>(), CodeType::UTF8);
            }
            response = text;
            return !text.empty();
        }, response, error_info);
}

int CAiClient::ChatCompleteAnthropic(const wstring& base_url, const wstring& api_key, const wstring& model, const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info)
{
    wstring url = TrimUrlSlash(base_url) + L"/v1/messages";

    json request;
    request["model"] = CCommon::UnicodeToStr(model, CodeType::UTF8_NO_BOM);
    request["max_tokens"] = 2048;
    request["system"] = CCommon::UnicodeToStr(system_prompt, CodeType::UTF8_NO_BOM);
    json message;
    message["role"] = "user";
    message["content"] = CCommon::UnicodeToStr(user_content, CodeType::UTF8_NO_BOM);
    request["messages"] = json::array({ message });

    wstring headers = L"Content-Type: application/json\r\nanthropic-version: 2023-06-01";
    if (!api_key.empty())
        headers += (L"\r\nx-api-key: " + api_key);

    return AiSendAndParse(url, request.dump(), headers,
        [](const json& data, wstring& response) {
            if (!(data.contains("content") && data["content"].is_array()))
                return false;
            wstring text;
            for (const auto& part : data["content"])
            {
                if (part.is_object() && part.value("type", "") == "text" && part.contains("text"))
                    text += CCommon::StrToUnicode(part["text"].get<string>(), CodeType::UTF8);
            }
            response = text;
            return !text.empty();
        }, response, error_info);
}

int CAiClient::ChatComplete(const wstring& system_prompt, const wstring& user_content, wstring& response, wstring& error_info)
{
    int api_format;
    wstring base_url, api_key, model;
    GetConfig(api_format, base_url, api_key, model);
    if (base_url.empty() || model.empty())
    {
        error_info = theApp.m_str_table.LoadText(L"MSG_AI_CONFIG_INVALID");
        return AI_CONFIG_INVALID;
    }

    switch (api_format)
    {
    case AF_GEMINI:
        return ChatCompleteGemini(base_url, api_key, model, system_prompt, user_content, response, error_info);
    case AF_ANTHROPIC:
        return ChatCompleteAnthropic(base_url, api_key, model, system_prompt, user_content, response, error_info);
    default:
        return ChatCompleteOpenAI(base_url, api_key, model, system_prompt, user_content, response, error_info);
    }
}

int CAiClient::FetchModelList(int api_format, const wstring& base_url, const wstring& api_key, vector<wstring>& models, wstring& error_info)
{
    models.clear();
    if (base_url.empty())
    {
        error_info = theApp.m_str_table.LoadText(L"MSG_AI_CONFIG_INVALID");
        return AI_CONFIG_INVALID;
    }

    wstring url;
    wstring headers;
    //各格式的模型列表接口与响应结构不同
    bool openai_style = true;       //OpenAI和Anthropic的响应都是{"data":[{"id":...}]}
    switch (api_format)
    {
    case AF_GEMINI:
        url = TrimUrlSlash(base_url) + L"/models";
        if (!api_key.empty())
            headers = (L"x-goog-api-key: " + api_key);
        openai_style = false;
        break;
    case AF_ANTHROPIC:
        url = TrimUrlSlash(base_url) + L"/v1/models";
        headers = L"anthropic-version: 2023-06-01";
        if (!api_key.empty())
            headers += (L"\r\nx-api-key: " + api_key);
        break;
    default:
        url = TrimUrlSlash(base_url) + L"/models";
        if (!api_key.empty())
            headers = (L"Authorization: Bearer " + api_key);
        break;
    }

    wstring result;
    int rtn = CInternetCommon::SendHttpRequest(false, url, result, string(), headers);
    if (rtn != CInternetCommon::SUCCESS)
    {
        error_info = theApp.m_str_table.LoadText(L"MSG_AI_NETWORK_FAILED");
        return AI_NETWORK_ERROR;
    }

    json data;
    try
    {
        data = json::parse(result);
    }
    catch (...)
    {
        error_info = theApp.m_str_table.LoadText(L"MSG_AI_RESPONSE_PARSE_FAILED");
        return AI_PARSE_ERROR;
    }

    if (data.contains("error") && data["error"].is_object())
    {
        error_info = CCommon::StrToUnicode(data["error"].value("message", ""), CodeType::UTF8);
        return AI_API_ERROR;
    }

    if (openai_style)
    {
        if (data.contains("data") && data["data"].is_array())
        {
            for (const auto& item : data["data"])
            {
                if (item.is_object() && item.contains("id"))
                {
                    wstring model_id = CCommon::StrToUnicode(item["id"].get<string>(), CodeType::UTF8);
                    if (!model_id.empty())
                        models.push_back(model_id);
                }
            }
        }
    }
    else
    {
        //Gemini格式：{"models":[{"name":"models/gemini-2.0-flash",...}]}
        if (data.contains("models") && data["models"].is_array())
        {
            for (const auto& item : data["models"])
            {
                if (item.is_object() && item.contains("name"))
                {
                    wstring model_id = CCommon::StrToUnicode(item["name"].get<string>(), CodeType::UTF8);
                    //去掉"models/"前缀
                    const wstring prefix = L"models/";
                    if (model_id.find(prefix) == 0)
                        model_id = model_id.substr(prefix.size());
                    if (!model_id.empty())
                        models.push_back(model_id);
                }
            }
        }
    }

    if (models.empty())
    {
        error_info = theApp.m_str_table.LoadText(L"MSG_AI_RESPONSE_PARSE_FAILED");
        return AI_PARSE_ERROR;
    }
    return AI_SUCCESS;
}

bool CAiClient::TestConnection(wstring& error_info)
{
    wstring response;
    int rtn = ChatComplete(theApp.m_str_table.LoadText(L"TXT_AI_PING_SYSTEM_PROMPT"),
        theApp.m_str_table.LoadText(L"TXT_AI_PING_USER_PROMPT"), response, error_info);
    return (rtn == AI_SUCCESS && !response.empty());
}

wstring CAiClient::ExtractJson(const wstring& text)
{
    size_t start = text.find(L'{');
    size_t end = text.rfind(L'}');
    if (start == wstring::npos || end == wstring::npos || end <= start)
        return wstring();
    return text.substr(start, end - start + 1);
}
