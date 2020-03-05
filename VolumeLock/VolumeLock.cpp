#include <iostream>
#include <map>
#include <sstream>
#include <filesystem>

#include <windows.h>

#include "CoreAudioAPI.h"
#include "Log.h"

using namespace std;

class VolumeLock : private AudioDeviceEvents, private AudioSessionEvents, private AudioDeviceEnumeratorEvents
{
public:
    VolumeLock(const filesystem::path& path, int volume) : m_targetprocess(path), m_targetvolume(volume)
    {
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
        //Log("重载会话列表 ...");
        auto sessions = m_device->GetAllSession();
        for (auto& item : sessions)
        {
            OnSessionAdded(m_device, item);
        }
    }

    void ClearTargetSession()
    {
        for (auto&& i : m_targetsessions)
        {
            i->UnregisterNotification(this);
        }
        m_targetsessions.clear();
    }

    virtual void OnVolumeChanged(std::shared_ptr<AudioSession> session, int volume) override
    {
        if (volume != m_targetvolume)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 设置目标进程音量：" << volume << " => " << m_targetvolume);
            session->SetVolume(m_targetvolume);
        }
    }

    virtual void OnSessionRemoved(std::shared_ptr<AudioDevice> device, std::shared_ptr<AudioSession> session, int reason) override
    {
        if (m_targetsessions.find(session) == m_targetsessions.end())
        {
            return;
        }
        session->UnregisterNotification(this);
        m_targetsessions.erase(session);
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
        auto pid = session->GetProcessId();
        auto path = session->GetProcessPath();
        if (m_targetprocess == path)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 发现目标进程");
            //Log(session->GetInstanceId());
            m_targetsessions.insert(session);
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
    filesystem::path m_targetprocess;
    int m_targetvolume;

    AudioDeviceEnumerator m_enumerator;
    shared_ptr<AudioDevice> m_device;
    set<shared_ptr<AudioSession>> m_targetsessions;
};



int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(0, 0);
    
    // 使用本地语言环境，除了数值，不要对数值使用逗号分割
    locale::global(locale(locale::classic(), locale(""), locale::all & (locale::all ^ locale::numeric)));

    if (argc != 3)
    {
        filesystem::path path(argv[0]);
        wcerr << L"用法：" << path.filename().wstring() << L" <进程文件名> <音量>" << endl;
        return 0;
    }

    filesystem::path targetprocess = argv[1];
    int targetvolume = stoi(argv[2]);

    Log(wstringstream() << L"锁定 [" << targetprocess.wstring() << L"] 的音量为 [" << targetvolume << L"]");
    Log("正在初始化 ...");

    VolumeLock lock(targetprocess, targetvolume);

    Log("开始运行，按回车键退出 ...");
    cin.get();
    Log("结束");
}
