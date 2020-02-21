#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <memory>
#include <functional>
#include <algorithm>
#include <map>
#include <thread>
#include <set>

#include <atlbase.h>
#include <mmdeviceapi.h>
#include <AudioSessionTypes.h>
#include <Functiondiscoverykeys_devpkey.h>
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
	AudioSession(CComPtr<IAudioSessionControl2> s) : session(s), volume(s)
	{

		LPWSTR pStr = nullptr;
		ThrowIfError(session->GetDisplayName(&pStr));
		m_DisplayName = pStr;
		CoTaskMemFree(pStr);
		pStr = nullptr;

		ThrowIfError(session->GetProcessId(&m_ProcessId));

		ThrowIfError(session->GetSessionIdentifier(&pStr));
		m_Id = pStr;
		CoTaskMemFree(pStr);
		pStr = nullptr;

		ThrowIfError(session->GetSessionInstanceIdentifier(&pStr));
		m_InstanceId = pStr;
		CoTaskMemFree(pStr);
		pStr = nullptr;

		ThrowIfError(session->GetIconPath(&pStr));
		m_IconPath = pStr;
		CoTaskMemFree(pStr);
		pStr = nullptr;

		ThrowIfError(session->RegisterAudioSessionNotification(this));
	}

	virtual ~AudioSession()
	{
		volume.Release();
		session->UnregisterAudioSessionNotification(this);
		// 在后台线程释放，Windows 系统本身会莫名出现多线程竞争状态，长时间卡死在释放阶段
		std::thread::thread([](IAudioSessionControl2* session) {
			session->Release();
		}, session.Detach()).detach();
	}

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

	AudioSessionState GetState()
	{
		AudioSessionState state;
		ThrowIfError(session->GetState(&state));
		return state;
	}

	bool IsSystemSoundsSession()
	{
		auto hr = session->IsSystemSoundsSession();
		if (hr == S_OK)
		{
			return true;
		}
		else if (hr == S_FALSE)
		{
			return false;
		}
		throw new std::runtime_error("");
	}

	void SetMute(bool mute)
	{
		ThrowIfError(volume->SetMute(mute, nullptr));
	}

	bool GetMute()
	{
		BOOL mute;
		ThrowIfError(volume->GetMute(&mute));
		return mute;
	}

	void SetVolume(int v)
	{
		if (v < 0) v = 0;
		else if (v > 100) v = 100;
		ThrowIfError(volume->SetMasterVolume(v / 100.0f, nullptr));
	}

	int GetVolume()
	{
		float v;
		ThrowIfError(volume->GetMasterVolume(&v));
		return (int)(v * 100 + 0.5);
	}

	void RegisterNotification(AudioSessionEvents* cb)
	{
		m_callback.insert(cb);
	}

	void UnregisterNotification(AudioSessionEvents* cb)
	{
		m_callback.erase(cb);
	}

private:
	friend class AudioDevice;

	void RegisterNotification_Inner(AudioSessionEvents_Inner* cb)
	{
		m_callback_inner.insert(cb);
	}

	void UnregisterNotification_Inner(AudioSessionEvents_Inner* cb)
	{
		m_callback_inner.erase(cb);
	}

	void FireVolumeChanged(int volume)
	{
		for (auto&& cb : m_callback)
		{
			cb->OnVolumeChanged(shared_from_this(), volume);
		}
	}

	void FireStateChanged(AudioSessionState state)
	{
		if (state == AudioSessionStateActive || state == AudioSessionStateInactive)
		{
			for (auto&& cb : m_callback)
			{
				cb->OnStateChanged(shared_from_this(), state == AudioSessionStateActive);
			}
		}
		else
		{
			// 以下操作可能导致当前对象被释放，先备份
			auto self = shared_from_this();
			std::vector<AudioSessionEvents_Inner*> cb_copy(m_callback_inner.size());
			std::copy(m_callback_inner.begin(), m_callback_inner.end(), cb_copy.begin());
			for (auto&& cb : cb_copy)
			{
				cb->OnStateChanged(self, state);
			}
		}
	}

	void FireSessionDisconnected(AudioSessionDisconnectReason reason)
	{
		// 以下操作可能导致当前对象被释放，先备份
		auto self = shared_from_this();
		std::vector<AudioSessionEvents_Inner*> cb_copy(m_callback_inner.size());
		std::copy(m_callback_inner.begin(), m_callback_inner.end(), cb_copy.begin());
		for (auto&& cb : cb_copy)
		{
			cb->OnDisconnected(self, reason);
		}
	}

#pragma region IAudioSessionEvents

	private:
		virtual HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(
			/* [annotation][string][in] */
			_In_  LPCWSTR NewDisplayName,
			/* [in] */ LPCGUID EventContext)
		{
			m_DisplayName = NewDisplayName;
			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE OnIconPathChanged(
			/* [annotation][string][in] */
			_In_  LPCWSTR NewIconPath,
			/* [in] */ LPCGUID EventContext)
		{
			m_IconPath = NewIconPath;
			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(
			/* [annotation][in] */
			_In_  float NewVolume,
			/* [annotation][in] */
			_In_  BOOL NewMute,
			/* [in] */ LPCGUID EventContext)
		{
			FireVolumeChanged((UINT32)(100 * NewVolume + 0.5));
			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(
			/* [annotation][in] */
			_In_  DWORD ChannelCount,
			/* [annotation][size_is][in] */
			_In_reads_(ChannelCount)  float NewChannelVolumeArray[],
			/* [annotation][in] */
			_In_  DWORD ChangedChannel,
			/* [in] */ LPCGUID EventContext)
		{
			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(
			/* [annotation][in] */
			_In_  LPCGUID NewGroupingParam,
			/* [in] */ LPCGUID EventContext)
		{
			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE OnStateChanged(
			/* [annotation][in] */
			_In_  AudioSessionState NewState)
		{
			FireStateChanged(NewState);
			return S_OK;
		}

		virtual HRESULT STDMETHODCALLTYPE OnSessionDisconnected(
			/* [annotation][in] */
			_In_  AudioSessionDisconnectReason DisconnectReason)
		{
			FireSessionDisconnected(DisconnectReason);
			return S_OK;
		}

#pragma endregion

private:
	CComPtr<IAudioSessionControl2> session;
	CComQIPtr<ISimpleAudioVolume> volume;

	std::wstring m_DisplayName;
	DWORD m_ProcessId;
	std::wstring m_Id;
	std::wstring m_InstanceId;
	std::wstring m_IconPath;

	std::set<AudioSessionEvents*> m_callback;
	std::set<AudioSessionEvents_Inner*> m_callback_inner;
};

class AudioDevice : private UnknownImp<IAudioSessionNotification>, public std::enable_shared_from_this<AudioDevice>, private AudioSessionEvents_Inner
{
public:
	AudioDevice(CComPtr<IMMDevice> mmd) : device(mmd)
	{
		ThrowIfError(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, NULL, (void**)&manager));

		CComPtr<IPropertyStore> prop;
		ThrowIfError(device->OpenPropertyStore(STGM_READ, &prop));

		CComHeapPtr<WCHAR> comstr;
		ThrowIfError(device->GetId(&comstr));
		m_Id = comstr;

		PropVarStr var;
		ThrowIfError(prop->GetValue(PKEY_Device_FriendlyName, &var));
		m_FriendlyName = var;

		var.Clear();
		ThrowIfError(prop->GetValue(PKEY_Device_DeviceDesc, &var));
		m_DeviceDesc = var;

		var.Clear();
		ThrowIfError(prop->GetValue(PKEY_DeviceInterface_FriendlyName, &var));
		m_InterfaceFriendlyName = var;

		ThrowIfError(manager->RegisterSessionNotification(this));

		CComPtr<IAudioSessionEnumerator> sessionenum;
		if (SUCCEEDED(manager->GetSessionEnumerator(&sessionenum)))
		{
			int sessioncount = 0;
			ThrowIfError(sessionenum->GetCount(&sessioncount));
			for (int i = 0; i < sessioncount; i++)
			{
				CComPtr<IAudioSessionControl> session;
				ThrowIfError(sessionenum->GetSession(i, &session));
				CComQIPtr<IAudioSessionControl2> session2(session);
				auto wrapper = std::make_shared<AudioSession>(session2);
				m_sessions.insert(wrapper);
				wrapper->RegisterNotification_Inner(this);
			}
		}
	}

	~AudioDevice()
	{
		manager->UnregisterSessionNotification(this);
		for (auto&& i : m_sessions)
		{
			i->UnregisterNotification_Inner(this);
		}
	}

	const std::wstring& GetId()
	{
		return m_Id;
	}

	AudioSessionState GetState()
	{
		DWORD state;
		ThrowIfError(device->GetState(&state));
		return static_cast<AudioSessionState>(state);
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

	std::vector<std::shared_ptr<AudioSession>> GetAllSession()
	{
		std::vector<std::shared_ptr<AudioSession>> result;
		for (auto&& i : m_sessions)
		{
			result.push_back(i);
		}
		return result;
	}

	void RegisterNotification(AudioDeviceEvents* cb)
	{
		m_callback.insert(cb);
	}

	void UnregisterNotification(AudioDeviceEvents* cb)
	{
		m_callback.erase(cb);
	}

private:
	void FireSessionAdd(std::shared_ptr<AudioSession> session)
	{
		for (auto&& cb : m_callback)
		{
			cb->OnSessionAdded(shared_from_this(), session);
		}
	}

	void FireSessionRemove(std::shared_ptr<AudioSession> session, int reason)
	{
		for (auto&& cb : m_callback)
		{
			cb->OnSessionRemoved(shared_from_this(), session, reason);
		}
	}

#pragma region AudioSessionEvents_Inner

	virtual void OnStateChanged(std::shared_ptr<AudioSession> session, AudioSessionState state) override
	{
		if (state == AudioSessionStateExpired)
		{
			OnDisconnected(session, (AudioSessionDisconnectReason)1000);
		}
	}

	virtual void OnDisconnected(std::shared_ptr<AudioSession> session, AudioSessionDisconnectReason reason) override
	{
		session->UnregisterNotification_Inner(this);
		// TODO: 释放后，API 仍然会回调，导致崩溃
		// 临时解决方案是后台等待一段时间后再释放
		std::thread::thread([](std::shared_ptr<AudioSession> s) {
			Sleep(1000);
			s.reset();
			}, session).detach();
			m_sessions.erase(session);
		m_sessions.erase(session);
		FireSessionRemove(session, reason);
	}

#pragma endregion

#pragma region IAudioSessionNotification

private:
	virtual HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* NewSession)
	{
		CComQIPtr<IAudioSessionControl2> session2(NewSession);
		auto wrapper = std::make_shared<AudioSession>(session2);
		m_sessions.insert(wrapper);
		wrapper->RegisterNotification_Inner(this);
		FireSessionAdd(wrapper);
		return S_OK;
	}

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
};

class AudioDeviceEnumerator : private UnknownImp<IMMNotificationClient>
{
public:
	AudioDeviceEnumerator()
	{
		ThrowIfError(enumerator.CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER));
		ThrowIfError(enumerator->RegisterEndpointNotificationCallback(this));

		CComPtr<IMMDeviceCollection> collection;
		ThrowIfError(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL, &collection));
		UINT count = 0;
		ThrowIfError(collection->GetCount(&count));
		for (UINT i = 0; i < count; i++)
		{
			CComPtr<IMMDevice> device;
			ThrowIfError(collection->Item(i, &device));
			auto wrapper = std::make_shared<AudioDevice>(device);
			m_devices[wrapper->GetId()] = wrapper;
		}
	}

	~AudioDeviceEnumerator()
	{
		enumerator->UnregisterEndpointNotificationCallback(this);
	}

	std::shared_ptr<AudioDevice> GetDefaultDevice()
	{
		CComPtr<IMMDevice> device;
		ThrowIfError(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));
		auto wrapper = std::make_shared<AudioDevice>(device);
		return GetDeviceById(wrapper->GetId());
	}

	void RegisterNotification(AudioDeviceEnumeratorEvents* cb)
	{
		m_callback.insert(cb);
	}

	void UnregisterNotification(AudioDeviceEnumeratorEvents* cb)
	{
		m_callback.erase(cb);
	}

private:
	std::shared_ptr<AudioDevice> GetDeviceById(const std::wstring& id)
	{
		if (m_devices.find(id) == m_devices.end())
		{
			throw new std::runtime_error("");
		}
		return m_devices.at(id);
	}

	void FireDeviceStateChanged(std::shared_ptr<AudioDevice> device, DWORD state)
	{
		for (auto&& cb : m_callback)
		{
			cb->OnDeviceStateChanged(device, state);
		}
	}

	void FireDeviceAdded(std::shared_ptr<AudioDevice> device)
	{
		for (auto&& cb : m_callback)
		{
			cb->OnDeviceAdded(device);
		}
	}

	void FireDeviceRemoved(std::shared_ptr<AudioDevice> device)
	{
		for (auto&& cb : m_callback)
		{
			cb->OnDeviceRemoved(device);
		}
	}

	void FireDefaultDeviceChanged(std::shared_ptr<AudioDevice> device)
	{
		for (auto&& cb : m_callback)
		{
			cb->OnDefaultDeviceChanged(device);
		}
	}

#pragma region IMMNotificationClient

private:
	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDeviceId,
		/* [annotation][in] */
		_In_  DWORD dwNewState)
	{
		auto device = GetDeviceById(pwstrDeviceId);
		FireDeviceStateChanged(device, dwNewState);
		return S_OK;
	}

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnDeviceAdded(
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDeviceId)
	{
		CComPtr<IMMDevice> device;
		ThrowIfError(enumerator->GetDevice(pwstrDeviceId, &device));
		auto wrapper = std::make_shared<AudioDevice>(device);
		m_devices[wrapper->GetId()] = wrapper;
		FireDeviceAdded(wrapper);
		return S_OK;
	}

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnDeviceRemoved(
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDeviceId)
	{
		auto device = GetDeviceById(pwstrDeviceId);
		m_devices.erase(pwstrDeviceId);
		FireDeviceRemoved(device);
		return S_OK;
	}

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
		/* [annotation][in] */
		_In_  EDataFlow flow,
		/* [annotation][in] */
		_In_  ERole role,
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDefaultDeviceId)
	{
		auto device = GetDeviceById(pwstrDefaultDeviceId);
		FireDefaultDeviceChanged(device);
		return S_OK;
	}

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDeviceId,
		/* [annotation][in] */
		_In_  const PROPERTYKEY key)
	{
		return S_OK;
	}

#pragma endregion

private:
	CComPtr<IMMDeviceEnumerator> enumerator;

	std::map<std::wstring, std::shared_ptr<AudioDevice>> m_devices;
	std::set<AudioDeviceEnumeratorEvents*> m_callback;
};
