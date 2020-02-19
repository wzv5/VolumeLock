#pragma once

#include <vector>
#include <string>
#include <stdexcept>
#include <memory>
#include <functional>
#include <algorithm>
#include <map>
#include <thread>

#include <atlcomcli.h>
#include <mmdeviceapi.h>
#include <AudioSessionTypes.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <audiopolicy.h>

#include "ComHelper.h"

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

	std::wstring GetDisplayName()
	{
		return m_DisplayName;
	}

	DWORD GetProcessId()
	{
		return m_ProcessId;
	}

	std::wstring GetId()
	{
		return m_Id;
	}

	std::wstring GetInstanceId()
	{
		return m_InstanceId;
	}

	std::wstring GetIconPath()
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

	void RegisterNotification(std::function<void(std::shared_ptr<AudioSession>, int)> onvolumechanged = {}, std::function<void(std::shared_ptr<AudioSession>, AudioSessionState)> onstatechanged = {}, std::function<void(std::shared_ptr<AudioSession>, AudioSessionDisconnectReason)> onsessiondisconnected = {})
	{
		cb_OnVolumeChanged = onvolumechanged;
		cb_OnStateChanged = onstatechanged;
		cb_OnSessionDisconnected = onsessiondisconnected;
	}

	void UnregisterNotification()
	{
		cb_OnVolumeChanged = {};
		cb_OnStateChanged = {};
		cb_OnSessionDisconnected = {};
	}

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
		if (cb_OnVolumeChanged)
		{
			cb_OnVolumeChanged(shared_from_this(), (UINT32)(100 * NewVolume + 0.5));
		}
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
		if (cb_OnStateChanged)
		{
			cb_OnStateChanged(shared_from_this(), NewState);
		}
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE OnSessionDisconnected(
		/* [annotation][in] */
		_In_  AudioSessionDisconnectReason DisconnectReason)
	{
		if (cb_OnSessionDisconnected)
		{
			cb_OnSessionDisconnected(shared_from_this(), DisconnectReason);
		}
		return S_OK;
	}

private:
	CComPtr<IAudioSessionControl2> session;
	CComQIPtr<ISimpleAudioVolume> volume;

	std::wstring m_DisplayName;
	DWORD m_ProcessId;
	std::wstring m_Id;
	std::wstring m_InstanceId;
	std::wstring m_IconPath;

	std::function<void(std::shared_ptr<AudioSession>, AudioSessionDisconnectReason)> cb_OnSessionDisconnected;
	std::function<void(std::shared_ptr<AudioSession>, AudioSessionState)> cb_OnStateChanged;
	std::function<void(std::shared_ptr<AudioSession>, int)> cb_OnVolumeChanged;
};

class AudioSessionManager : private UnknownImp<IAudioSessionNotification>
{
public:
	AudioSessionManager(CComPtr<IAudioSessionManager2> mgr) : manager(mgr)
	{
		ThrowIfError(manager->RegisterSessionNotification(this));
	}

	virtual ~AudioSessionManager()
	{
		manager->UnregisterSessionNotification(this);
	}

	std::vector<std::shared_ptr<AudioSession>> GetAllSession()
	{
		std::vector<std::shared_ptr<AudioSession>> result;
		CComPtr<IAudioSessionEnumerator> sessionenum;
		ThrowIfError(manager->GetSessionEnumerator(&sessionenum));
		int sessioncount = 0;
		ThrowIfError(sessionenum->GetCount(&sessioncount));
		for (int i = 0; i < sessioncount; i++)
		{
			CComPtr<IAudioSessionControl> session;
			ThrowIfError(sessionenum->GetSession(i, &session));
			CComQIPtr<IAudioSessionControl2> session2(session);
			auto wrapper = std::make_shared<AudioSession>(session2);
			result.push_back(wrapper);
		}
		return result;
	}

	void RegisterNotification(std::function<void(std::shared_ptr<AudioSession>)> cb = {})
	{
		cb_OnSessionCreated = cb;
	}

	void UnregisterNotification()
	{
		cb_OnSessionCreated = {};
	}

private:
	virtual HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* NewSession)
	{
		if (cb_OnSessionCreated)
		{
			CComQIPtr<IAudioSessionControl2> session2(NewSession);
			auto wrapper = std::make_shared<AudioSession>(session2);
			cb_OnSessionCreated(wrapper);
		}
		return S_OK;
	}

private:
	CComPtr<IAudioSessionManager2> manager;
	std::function<void(std::shared_ptr<AudioSession>)> cb_OnSessionCreated;
};

class AudioDevice
{
public:
	AudioDevice(CComPtr<IMMDevice> mmd) : device(mmd)
	{
		ThrowIfError(device->OpenPropertyStore(STGM_READ, &prop));
	}

	std::wstring GetId()
	{
		LPWSTR deviceid;
		ThrowIfError(device->GetId(&deviceid));
		std::wstring result(deviceid);
		CoTaskMemFree(deviceid);
		return result;
	}

	AudioSessionState GetState()
	{
		DWORD state;
		ThrowIfError(device->GetState(&state));
		return static_cast<AudioSessionState>(state);
	}

	std::wstring GetFriendlyName()
	{
		PROPVARIANT var;
		PropVariantInit(&var);
		ThrowIfError(prop->GetValue(PKEY_Device_FriendlyName, &var));
		std::wstring result(var.pwszVal);
		PropVariantClear(&var);
		return result;
	}

	std::wstring GetDeviceDesc()
	{
		PROPVARIANT var;
		PropVariantInit(&var);
		ThrowIfError(prop->GetValue(PKEY_Device_DeviceDesc, &var));
		std::wstring result(var.pwszVal);
		PropVariantClear(&var);
		return result;
	}

	std::wstring GetInterfaceFriendlyName()
	{
		PROPVARIANT var;
		PropVariantInit(&var);
		ThrowIfError(prop->GetValue(PKEY_DeviceInterface_FriendlyName, &var));
		std::wstring result(var.pwszVal);
		PropVariantClear(&var);
		return result;
	}

	std::shared_ptr<AudioSessionManager> GetSessionManager()
	{
		IAudioSessionManager2* mgr = nullptr;
		ThrowIfError(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, NULL, (void**)&mgr));
		AudioSessionManager* p = new AudioSessionManager(mgr);
		return std::shared_ptr<AudioSessionManager>(p);
	}

private:
	CComPtr<IMMDevice> device;
	CComPtr<IPropertyStore> prop;
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
			m_deviceList[wrapper->GetId()] = wrapper;
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
		LPWSTR str = nullptr;
		device->GetId(&str);
		std::wstring id = str;
		CoTaskMemFree(str);
		auto wrapper = GetDeviceById(id);
		return wrapper;
	}

	void RegisterNotification(std::function<void(std::shared_ptr<AudioDevice>, DWORD)> statechanged = {}, std::function<void(std::shared_ptr<AudioDevice>)> defaultdevice = {})
	{
		cb_OnDeviceStateChanged = statechanged;
		cb_OnDefaultDeviceChanged = defaultdevice;
	}

	void UnregisterNotification()
	{
		cb_OnDeviceStateChanged = {};
		cb_OnDefaultDeviceChanged = {};
	}

private:
	std::shared_ptr<AudioDevice> GetDeviceById(const std::wstring& id)
	{
		if (m_deviceList.find(id) == m_deviceList.end())
		{
			throw new std::runtime_error("");
		}
		return m_deviceList.at(id);
	}

private:
	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDeviceId,
		/* [annotation][in] */
		_In_  DWORD dwNewState)
	{
		if (cb_OnDeviceStateChanged)
		{
			auto device = GetDeviceById(pwstrDeviceId);
			cb_OnDeviceStateChanged(device, dwNewState);
		}
		return S_OK;
	}

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnDeviceAdded(
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDeviceId)
	{
		CComPtr<IMMDevice> device;
		ThrowIfError(enumerator->GetDevice(pwstrDeviceId, &device));
		auto wrapper = std::make_shared<AudioDevice>(device);
		m_deviceList[wrapper->GetId()] = wrapper;
		return S_OK;
	}

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE OnDeviceRemoved(
		/* [annotation][in] */
		_In_  LPCWSTR pwstrDeviceId)
	{
		m_deviceList.erase(pwstrDeviceId);
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
		if (cb_OnDefaultDeviceChanged && flow == eRender && role == eConsole)
		{
			auto device = GetDeviceById(pwstrDefaultDeviceId);
			cb_OnDefaultDeviceChanged(device);
		}
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

private:
	CComPtr<IMMDeviceEnumerator> enumerator;

	std::map<std::wstring, std::shared_ptr<AudioDevice>> m_deviceList;

	std::function<void(std::shared_ptr<AudioDevice>, DWORD)> cb_OnDeviceStateChanged;
	std::function<void(std::shared_ptr<AudioDevice>)> cb_OnDefaultDeviceChanged;
};
