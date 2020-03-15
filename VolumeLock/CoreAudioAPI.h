#pragma once

#include <string>
#include <memory>
#include <functional>
#include <filesystem>
#include <set>
#include <mutex>
#include <vector>
#include <map>
#include <optional>

#include <Windows.h>
#include <atlbase.h>
#include <mmdeviceapi.h>
#include <AudioSessionTypes.h>
#include <audiopolicy.h>

#include "ComHelper.h"

class AudioSession;
class AudioDevice;

class AudioSessionEvents
{
public:
	virtual void OnVolumeChanged(std::shared_ptr<AudioSession> session, int volume) {}
	virtual void OnStateChanged(std::shared_ptr<AudioSession> session, bool active) {}
};

class AudioSessionEvents_Inner
{
public:
	virtual void OnStateChanged(std::shared_ptr<AudioSession> session, AudioSessionState state) {}
	virtual void OnDisconnected(std::shared_ptr<AudioSession> session, AudioSessionDisconnectReason reason) {}
};

class AudioDeviceEvents
{
public:
	virtual void OnSessionAdded(std::shared_ptr<AudioDevice> device, std::shared_ptr<AudioSession> session) {}
	virtual void OnSessionRemoved(std::shared_ptr<AudioDevice> device, std::shared_ptr<AudioSession> session, int reason) {}
};

class AudioDeviceEnumeratorEvents
{
public:
	virtual void OnDeviceAdded(std::shared_ptr<AudioDevice> device) {}
	virtual void OnDeviceRemoved(std::shared_ptr<AudioDevice> device) {}
	virtual void OnDeviceStateChanged(std::shared_ptr<AudioDevice> device, DWORD state) {}
	virtual void OnDefaultDeviceChanged(std::shared_ptr<AudioDevice> device) {}
};

class AudioSession : private UnknownImp<IAudioSessionEvents>, public std::enable_shared_from_this<AudioSession>
{
public:
	AudioSession(CComPtr<IAudioSessionControl2> s);

	virtual ~AudioSession();

	const std::wstring& GetDisplayName()
	{
		return m_DisplayName;
	}

	DWORD GetProcessId()
	{
		return m_ProcessId;
	}

	const std::wstring& GetId()
	{
		return m_Id;
	}

	const std::wstring& GetInstanceId()
	{
		return m_InstanceId;
	}

	const std::wstring& GetIconPath()
	{
		return m_IconPath;
	}

	const std::filesystem::path& GetProcessPath()
	{
		return m_ProcessPath;
	}

	AudioSessionState GetState();

	bool IsSystemSoundsSession();

	void SetMute(bool mute);

	bool GetMute();

	void SetVolume(int v);

	int GetVolume();

	void RegisterNotification(AudioSessionEvents* cb);

	void UnregisterNotification(AudioSessionEvents* cb);

private:
	friend class AudioDevice;

	void RegisterNotification_Inner(AudioSessionEvents_Inner* cb);

	void UnregisterNotification_Inner(AudioSessionEvents_Inner* cb);

	void FireVolumeChanged(int volume);

	void FireStateChanged(AudioSessionState state);

	void FireSessionDisconnected(AudioSessionDisconnectReason reason);

#pragma region IAudioSessionEvents

	virtual HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext);

	virtual HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext);

	virtual HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext);

	virtual HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD ChannelCount, float NewChannelVolumeArray[], DWORD ChangedChannel, LPCGUID EventContext);

	virtual HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext);

	virtual HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState NewState);

	virtual HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason);

#pragma endregion

private:
	CComPtr<IAudioSessionControl2> session;
	CComQIPtr<ISimpleAudioVolume> volume;

	std::wstring m_DisplayName;
	DWORD m_ProcessId;
	std::wstring m_Id;
	std::wstring m_InstanceId;
	std::wstring m_IconPath;
	std::filesystem::path m_ProcessPath;

	std::set<AudioSessionEvents*> m_callback;
	std::set<AudioSessionEvents_Inner*> m_callback_inner;

	std::mutex m_mutex;
};

class AudioDevice : private UnknownImp<IAudioSessionNotification>, public std::enable_shared_from_this<AudioDevice>, private AudioSessionEvents_Inner
{
public:
	AudioDevice(CComPtr<IMMDevice> mmd);

	virtual ~AudioDevice();

	const std::wstring& GetId()
	{
		return m_Id;
	}

	const std::wstring& GetFriendlyName()
	{
		return m_FriendlyName;
	}

	const std::wstring& GetDeviceDesc()
	{
		return m_DeviceDesc;
	}

	const std::wstring& GetInterfaceFriendlyName()
	{
		return m_InterfaceFriendlyName;
	}

	AudioSessionState GetState();

	std::vector<std::shared_ptr<AudioSession>> GetAllSession();

	void RegisterNotification(AudioDeviceEvents* cb);

	void UnregisterNotification(AudioDeviceEvents* cb);

private:
	void InitSessions();

	void FireSessionAdd(std::shared_ptr<AudioSession> session);

	void FireSessionRemove(std::shared_ptr<AudioSession> session, int reason);

#pragma region AudioSessionEvents_Inner

	virtual void OnStateChanged(std::shared_ptr<AudioSession> session, AudioSessionState state) override;

	virtual void OnDisconnected(std::shared_ptr<AudioSession> session, AudioSessionDisconnectReason reason) override;

#pragma endregion

#pragma region IAudioSessionNotification

	virtual HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* NewSession);

#pragma endregion

private:
	CComPtr<IMMDevice> device;
	CComPtr<IAudioSessionManager2> manager;

	std::wstring m_Id;
	std::wstring m_FriendlyName;
	std::wstring m_DeviceDesc;
	std::wstring m_InterfaceFriendlyName;

	std::set<std::shared_ptr<AudioSession>> m_sessions;
	std::set<AudioDeviceEvents*> m_callback;

	std::mutex m_mutex;
	bool m_initSessions = false;
};

class AudioDeviceEnumerator : private UnknownImp<IMMNotificationClient>
{
public:
	AudioDeviceEnumerator();

	virtual ~AudioDeviceEnumerator();

	std::shared_ptr<AudioDevice> GetDefaultDevice();

	void RegisterNotification(AudioDeviceEnumeratorEvents* cb);

	void UnregisterNotification(AudioDeviceEnumeratorEvents* cb);

private:
	std::optional<std::shared_ptr<AudioDevice>> GetDeviceById(const std::wstring& id);

	void FireDeviceStateChanged(std::shared_ptr<AudioDevice> device, DWORD state);

	void FireDeviceAdded(std::shared_ptr<AudioDevice> device);

	void FireDeviceRemoved(std::shared_ptr<AudioDevice> device);

	void FireDefaultDeviceChanged(std::shared_ptr<AudioDevice> device);

#pragma region IMMNotificationClient

	virtual HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState);

	virtual HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR pwstrDeviceId);

	virtual HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR pwstrDeviceId);

	virtual HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId);

	virtual HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key);

#pragma endregion

private:
	CComPtr<IMMDeviceEnumerator> enumerator;

	std::map<std::wstring, std::shared_ptr<AudioDevice>> m_devices;
	std::set<AudioDeviceEnumeratorEvents*> m_callback;

	std::mutex m_mutex;
};
