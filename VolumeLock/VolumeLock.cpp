#include <iostream>
#include <map>
#include <sstream>
#include <filesystem>

#include <windows.h>

#include "CoreAudioAPI.h"
#include "Log.h"

using namespace std;

wstring GetProcessImage(DWORD pid)
{
    if (pid == 0)
    {
        return {};
    }
    wstring result;
    auto hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hp)
    {
        DWORD buflen = 260;
        vector<wchar_t> buf(buflen);
        if (QueryFullProcessImageName(hp, 0, buf.data(), &buflen))
        {
            result = buf.data();
        }
        CloseHandle(hp);
    }
    return result;
}

class VolumeLock
{
public:
    VolumeLock(const filesystem::path& path, int volume) : m_targetprocess(path), m_targetvolume(volume)
    {
        auto device = m_enumerator.GetDefaultDevice();
        OnDefaultDeviceChanged(device);
        m_enumerator.RegisterNotification(
            bind(&VolumeLock::OnDeviceStateChanged, this, placeholders::_1, placeholders::_2),
            bind(&VolumeLock::OnDefaultDeviceChanged, this, placeholders::_1));
    }

    ~VolumeLock()
    {
        m_targetsessions.clear();
        m_sessionMgr.reset();
        m_device.reset();
    }

    void OnVolumeChanged(shared_ptr<AudioSession> session, int v)
    {
        if (v != m_targetvolume)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 设置目标进程音量：" << v << " => " << m_targetvolume);
            session->SetVolume(m_targetvolume);
        }
    }

    void OnStateChanged(shared_ptr<AudioSession> session, AudioSessionState s)
    {
        if (s == AudioSessionStateExpired)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 进程已停止");
            m_targetsessions.erase(session->GetInstanceId());
            //Log(stringstream() << "当前监视中的会话数量：" << m_targetsessions.size());
        }
    }

    void OnDisconnected(shared_ptr<AudioSession> session, AudioSessionDisconnectReason r)
    {
        Log(stringstream() << "[" << session->GetProcessId() << "] 进程已断开");
        m_targetsessions.erase(session->GetInstanceId());
        //Log(stringstream() << "当前监视中的会话数量：" << m_targetsessions.size());
    }

    void OnSessionCreated(shared_ptr<AudioSession> session)
    {
        auto pid = session->GetProcessId();
        auto path = GetProcessImage(pid);
        if (m_targetprocess == path)
        {
            Log(stringstream() << "[" << session->GetProcessId() << "] 发现目标进程");
            //Log(session->GetInstanceId());
            m_targetsessions[session->GetInstanceId()] = session;
            session->RegisterNotification(
                bind(&VolumeLock::OnVolumeChanged, this, placeholders::_1, placeholders::_2),
                bind(&VolumeLock::OnStateChanged, this, placeholders::_1, placeholders::_2),
                bind(&VolumeLock::OnDisconnected, this, placeholders::_1, placeholders::_2));
            //Log(stringstream() << "当前监视中的会话数量：" << m_targetsessions.size());
            OnVolumeChanged(session, session->GetVolume());
            //session->SetVolume(g_targetvolume);
        }
        else
        {
            session->SetVolume(100);
        }
    }

    void ReloadSession()
    {
        //Log("重载会话列表 ...");
        m_targetsessions.clear();
        auto sessions = m_sessionMgr->GetAllSession();
        for (auto& item : sessions)
        {
            OnSessionCreated(item);
        }
    }

    void OnDefaultDeviceChanged(shared_ptr<AudioDevice> device)
    {
        Log(wstringstream() << L"默认输出设备：" << device->GetFriendlyName());
        m_device = device;
        m_sessionMgr = device->GetSessionManager();
        m_sessionMgr->RegisterNotification(bind(&VolumeLock::OnSessionCreated, this, placeholders::_1));
        ReloadSession();
    }

    void OnDeviceStateChanged(shared_ptr<AudioDevice> device, DWORD state)
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
        m_targetsessions.clear();

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
    shared_ptr<AudioSessionManager> m_sessionMgr;
    map<wstring, shared_ptr<AudioSession>> m_targetsessions;
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

    VolumeLock lock(targetprocess, targetvolume);

    Log("开始运行，按回车键退出 ...");
    cin.get();
    Log("结束");
}
