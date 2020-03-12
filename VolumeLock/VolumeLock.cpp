#include <iostream>
#include <map>
#include <sstream>
#include <regex>
#include <optional>
#include <algorithm>
#include <mutex>

#include <windows.h>
#include <yaml-cpp/yaml.h>

#include "CoreAudioAPI.h"
#include "Log.h"

using namespace std;

struct ConfigItem
{
    enum class PathType
    {
        FullPath,
        FileName,
        Regex
    } Type;
    wstring Path;
    int Volume;
};

filesystem::path GetExePath()
{
    wchar_t buf[MAX_PATH + 1];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    filesystem::path path(buf);
    return path.parent_path();
}

std::optional<std::wstring> StringToWide(const std::string& s, const std::locale& loc = std::locale(""))
{
    typedef std::codecvt<wchar_t, char, mbstate_t> facet_type;
    mbstate_t mbst = mbstate_t();

    const char* frombegin = s.data();
    const char* fromend = frombegin + s.size();
    const char* fromnext = nullptr;

    std::vector<wchar_t> tobuf(s.size() + 1);
    wchar_t* tobegin = tobuf.data();
    wchar_t* toend = tobegin + tobuf.size();
    wchar_t* tonext = nullptr;

    if (std::use_facet<facet_type>(loc).in(mbst, frombegin, fromend, fromnext, tobegin, toend, tonext) != facet_type::ok)
    {
        return {};
    }
    return tobuf.data();
}

std::optional<std::wstring> Utf8ToWide(const std::string& s)
{
    // TODO: 由于 C++17 标准弃用了一大批编码转换类，到 C++20 才有对应的替代品，
    // 而当前 C++20 仍处于测试阶段，所以该函数是暂时的，以后将会迁移到 C++20

    // 暂时尝试通过 locale 的方式来转换编码，虽然 UTF-8 和 UTF-16 都跟 locale 无关，
    // 如果不考虑跨平台，可以调用 Windows API 来实现

    auto locstr = {
        ".65001",
        "zh-CN.65001",
        "zh_CN.UTF-8",
        "en-US.65001",
        "en_US.UTF-8"
    };
    std::locale loc;
    for (auto i : locstr)
    {
        try
        {
            loc = std::locale(i);
            return StringToWide(s, loc);
        }
        catch (...)
        {
            continue;
        }
    }
    return {};
}

string ToLower_Copy(const string& s)
{
    string ss(s);
    std::transform(s.begin(), s.end(), ss.begin(), tolower);
    return ss;
}

wstring ToLower_Copy(const wstring& s)
{
    wstring ss(s);
    std::transform(s.begin(), s.end(), ss.begin(), tolower);
    return ss;
}

namespace YAML {
    template<>
    struct convert<wstring> {
        static bool decode(const Node& node, wstring& rhs) {
            auto ws = Utf8ToWide(node.as<string>());
            if (!ws.has_value())
            {
                return false;
            }
            rhs = ws.value();
            return true;
        }
    };

    template<>
    struct convert<ConfigItem> {
        static bool decode(const Node& node, ConfigItem& rhs) {
            auto type = ToLower_Copy(node["type"].as<string>());
            if (type == "fullpath")
            {
                rhs.Type = ConfigItem::PathType::FullPath;
            }
            else if (type == "filename")
            {
                rhs.Type = ConfigItem::PathType::FileName;
            }
            else if (type == "regex")
            {
                rhs.Type = ConfigItem::PathType::Regex;
            }
            else
            {
                return false;
            }
            rhs.Path = node["path"].as<wstring>();
            rhs.Volume = node["volume"].as<int>();
            return true;
        }
    };
}

class VolumeLock : private AudioDeviceEvents, private AudioSessionEvents, private AudioDeviceEnumeratorEvents
{
public:
    VolumeLock(const filesystem::path& configpath)
    {
        auto config = YAML::LoadFile(configpath.string());
        for (size_t i = 0; i < config.size(); i++)
        {
            m_configs.emplace_back(config[i].as<ConfigItem>());
        }

        auto device = m_enumerator.GetDefaultDevice();
        OnDefaultDeviceChanged(device);
        m_enumerator.RegisterNotification(this);
    }

    ~VolumeLock()
    {
        ClearTargetSession();
        m_device.reset();
    }

private:

    void ReloadSession()
    {
        lock_guard lock(m_mutex);
        //Log("重载会话列表 ...");
        auto sessions = m_device->GetAllSession();
        for (auto& item : sessions)
        {
            OnSessionAdded(m_device, item);
        }
    }

    void ClearTargetSession()
    {
        lock_guard lock(m_mutex);
        for (auto&& i : m_targetsessions)
        {
            i->UnregisterNotification(this);
        }
        m_targetsessions.clear();
        m_pidToVolume.clear();
    }

    const ConfigItem* GetConfig(const filesystem::path& path)
    {
        for (auto&& item : m_configs)
        {
            if (item.Type == ConfigItem::PathType::FullPath)
            {
                auto path1 = ToLower_Copy(path.wstring());
                auto path2 = ToLower_Copy(item.Path);
                if (path1 == path2)
                {
                    return &item;
                }
            }
            else if (item.Type == ConfigItem::PathType::FileName)
            {
                auto path1 = ToLower_Copy(path.filename().wstring());
                auto path2 = ToLower_Copy(filesystem::path(item.Path).wstring());
                if (path1 == path2)
                {
                    return &item;
                }
            }
            else if (item.Type == ConfigItem::PathType::Regex)
            {
                std::wregex re(item.Path, std::regex::ECMAScript | std::regex::icase);
                if (std::regex_match(path.wstring(), re))
                {
                    return &item;
                }
            }
        }
        return nullptr;
    }

    virtual void OnVolumeChanged(std::shared_ptr<AudioSession> session, int volume) override
    {
        auto targetVolume = m_pidToVolume[session->GetProcessId()];
        if (volume != targetVolume)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 设置目标进程音量：" << volume << " => " << targetVolume);
            session->SetVolume(targetVolume);
        }
    }

    virtual void OnSessionRemoved(std::shared_ptr<AudioDevice> device, std::shared_ptr<AudioSession> session, int reason) override
    {
        lock_guard lock(m_mutex);
        if (m_targetsessions.find(session) == m_targetsessions.end())
        {
            return;
        }
        session->UnregisterNotification(this);
        m_targetsessions.erase(session);
        m_pidToVolume.erase(session->GetProcessId());
        if (reason == 1000)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 进程已停止");
        }
        else
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 进程已断开");
        }
        //Log(stringstream() << "当前监视中的会话数量：" << m_targetsessions.size());
    }

    virtual void OnSessionAdded(std::shared_ptr<AudioDevice> device, std::shared_ptr<AudioSession> session) override
    {
        lock_guard lock(m_mutex);
        auto config = GetConfig(session->GetProcessPath());
        if (config)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 发现目标进程");
            //Log(session->GetInstanceId());
            m_targetsessions.insert(session);
            m_pidToVolume[session->GetProcessId()] = config->Volume;
            session->RegisterNotification(this);
            //Log(stringstream() << "当前监视中的会话数量：" << m_targetsessions.size());
            OnVolumeChanged(session, session->GetVolume());
            //session->SetVolume(g_targetvolume);
        }
        else
        {
            session->SetVolume(100);
        }
    }

    virtual void OnDefaultDeviceChanged(shared_ptr<AudioDevice> device) override
    {
        lock_guard lock(m_mutex);
        Log(wstringstream() << L"默认输出设备：" << device->GetFriendlyName());
        if (m_device)
        {
            m_device->UnregisterNotification(this);
        }
        m_device = device;
        m_device->RegisterNotification(this);
        ClearTargetSession();
        ReloadSession();
    }

    virtual void OnDeviceStateChanged(shared_ptr<AudioDevice> device, DWORD state) override
    {
        lock_guard lock(m_mutex);
        switch (state)
        {
        case DEVICE_STATE_ACTIVE:
            Log(wstringstream() << L"设备已启用：" << device->GetFriendlyName());
            break;
        case DEVICE_STATE_DISABLED:
            Log(wstringstream() << L"设备已禁用：" << device->GetFriendlyName());
            break;
        case DEVICE_STATE_NOTPRESENT:
            Log(wstringstream() << L"设备已删除：" << device->GetFriendlyName());
            break;
        case DEVICE_STATE_UNPLUGGED:
            Log(wstringstream() << L"设备已拔出：" << device->GetFriendlyName());
            break;
        }
        ClearTargetSession();

        // 设备重新激活，且为默认设备，重载会话
        if (state == DEVICE_STATE_ACTIVE && device == m_device)
        {
            ReloadSession();
        }
    }

private:
    AudioDeviceEnumerator m_enumerator;
    shared_ptr<AudioDevice> m_device;
    set<shared_ptr<AudioSession>> m_targetsessions;

    vector<ConfigItem> m_configs;
    map<DWORD, int> m_pidToVolume;
    recursive_mutex m_mutex;
};

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(0, 0);
    
    // 使用本地语言环境，除了数值，不要对数值使用逗号分割
    locale::global(locale(locale::classic(), locale(""), locale::all & (locale::all ^ locale::numeric)));

    auto configpath = GetExePath() / "config.yaml";
    VolumeLock lock(configpath);

    Log("开始运行，按回车键退出 ...");
    cin.get();
    Log("结束");
}
