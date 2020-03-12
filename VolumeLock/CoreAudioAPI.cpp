#include "CoreAudioAPI.h"

#include <stdexcept>
#include <algorithm>
#include <thread>

#include <Functiondiscoverykeys_devpkey.h>

#pragma region AudioSession

AudioSession::AudioSession(CComPtr<IAudioSessionControl2> s) : session(s), volume(s)
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

	if (m_ProcessId)
	{
		auto hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_ProcessId);
		if (hp)
		{
			DWORD buflen = 260;
			std::vector<wchar_t> buf(buflen);
			if (QueryFullProcessImageName(hp, 0, buf.data(), &buflen))
			{
				m_ProcessPath = buf.data();
			}
			CloseHandle(hp);
		}
	}

	ThrowIfError(session->RegisterAudioSessionNotification(this));
}

AudioSession::~AudioSession()
{
	std::lock_guard lock(m_mutex);
	volume.Release();
	session->UnregisterAudioSessionNotification(this);
	// 在后台线程释放，Windows 系统本身会莫名出现多线程竞争状态，长时间卡死在释放阶段
	std::thread::thread([](IAudioSessionControl2* session) {
		session->Release();
		}, session.Detach()).detach();
}

AudioSessionState AudioSession::GetState()
{
	AudioSessionState state;
	ThrowIfError(session->GetState(&state));
	return state;
}

bool AudioSession::IsSystemSoundsSession()
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

void AudioSession::SetMute(bool mute)
{
	ThrowIfError(volume->SetMute(mute, nullptr));
}

bool AudioSession::GetMute()
{
	BOOL mute;
	ThrowIfError(volume->GetMute(&mute));
	return mute;
}

void AudioSession::SetVolume(int v)
{
	if (v < 0) v = 0;
	else if (v > 100) v = 100;
	ThrowIfError(volume->SetMasterVolume(v / 100.0f, nullptr));
}

int AudioSession::GetVolume()
{
	float v;
	ThrowIfError(volume->GetMasterVolume(&v));
	return (int)(v * 100 + 0.5);
}

void AudioSession::RegisterNotification(AudioSessionEvents* cb)
{
	std::lock_guard lock(m_mutex);
	m_callback.insert(cb);
}

void AudioSession::UnregisterNotification(AudioSessionEvents* cb)
{
	std::lock_guard lock(m_mutex);
	m_callback.erase(cb);
}

void AudioSession::RegisterNotification_Inner(AudioSessionEvents_Inner* cb)
{
	std::lock_guard lock(m_mutex);
	m_callback_inner.insert(cb);
}

void AudioSession::UnregisterNotification_Inner(AudioSessionEvents_Inner* cb)
{
	std::lock_guard lock(m_mutex);
	m_callback_inner.erase(cb);
}

void AudioSession::FireVolumeChanged(int volume)
{
	for (auto&& cb : m_callback)
	{
		cb->OnVolumeChanged(shared_from_this(), volume);
	}
}

void AudioSession::FireStateChanged(AudioSessionState state)
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

void AudioSession::FireSessionDisconnected(AudioSessionDisconnectReason reason)
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

HRESULT __stdcall AudioSession::OnDisplayNameChanged(LPCWSTR NewDisplayName, LPCGUID EventContext)
{
	m_DisplayName = NewDisplayName;
	return S_OK;
}

HRESULT __stdcall AudioSession::OnIconPathChanged(LPCWSTR NewIconPath, LPCGUID EventContext)
{
	m_IconPath = NewIconPath;
	return S_OK;
}

HRESULT __stdcall AudioSession::OnSimpleVolumeChanged(float NewVolume, BOOL NewMute, LPCGUID EventContext)
{
	FireVolumeChanged((UINT32)(100 * NewVolume + 0.5));
	return S_OK;
}

HRESULT __stdcall AudioSession::OnChannelVolumeChanged(DWORD ChannelCount, float NewChannelVolumeArray[], DWORD ChangedChannel, LPCGUID EventContext)
{
	return S_OK;
}

HRESULT __stdcall AudioSession::OnGroupingParamChanged(LPCGUID NewGroupingParam, LPCGUID EventContext)
{
	return S_OK;
}

HRESULT __stdcall AudioSession::OnStateChanged(AudioSessionState NewState)
{
	FireStateChanged(NewState);
	return S_OK;
}

HRESULT __stdcall AudioSession::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason)
{
	FireSessionDisconnected(DisconnectReason);
	return S_OK;
}

#pragma endregion

#pragma region AudioDevice

AudioDevice::AudioDevice(CComPtr<IMMDevice> mmd) : device(mmd)
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
}

AudioDevice::~AudioDevice()
{
	std::lock_guard lock(m_mutex);
	manager->UnregisterSessionNotification(this);
	for (auto&& i : m_sessions)
	{
		i->UnregisterNotification_Inner(this);
	}
}

AudioSessionState AudioDevice::GetState()
{
	DWORD state;
	ThrowIfError(device->GetState(&state));
	return static_cast<AudioSessionState>(state);
}

std::vector<std::shared_ptr<AudioSession>> AudioDevice::GetAllSession()
{
	std::lock_guard lock(m_mutex);
	InitSessions();
	std::vector<std::shared_ptr<AudioSession>> result;
	for (auto&& i : m_sessions)
	{
		result.push_back(i);
	}
	return result;
}

void AudioDevice::RegisterNotification(AudioDeviceEvents* cb)
{
	std::lock_guard lock(m_mutex);
	InitSessions();
	m_callback.insert(cb);
}

void AudioDevice::UnregisterNotification(AudioDeviceEvents* cb)
{
	std::lock_guard lock(m_mutex);
	m_callback.erase(cb);
}

void AudioDevice::InitSessions()
{
	if (m_initSessions)
	{
		return;
	}
	m_initSessions = true;

	ThrowIfError(manager->RegisterSessionNotification(this));

	CComPtr<IAudioSessionEnumerator> sessionenum;
	if (SUCCEEDED(manager->GetSessionEnumerator(&sessionenum)))
	{
		int sessioncount = 0;
		ThrowIfError(sessionenum->GetCount(&sessioncount));
		for (int i = 0; i < sessioncount; i++)
		{
			try
			{
				CComPtr<IAudioSessionControl> session;
				ThrowIfError(sessionenum->GetSession(i, &session));
				CComQIPtr<IAudioSessionControl2> session2(session);
				auto wrapper = std::make_shared<AudioSession>(session2);
				m_sessions.insert(wrapper);
				wrapper->RegisterNotification_Inner(this);
			}
			catch (const std::exception&)
			{
				// 任何原因失败都忽略该会话
			}
		}
	}
}

void AudioDevice::FireSessionAdd(std::shared_ptr<AudioSession> session)
{
	for (auto&& cb : m_callback)
	{
		cb->OnSessionAdded(shared_from_this(), session);
	}
}

void AudioDevice::FireSessionRemove(std::shared_ptr<AudioSession> session, int reason)
{
	for (auto&& cb : m_callback)
	{
		cb->OnSessionRemoved(shared_from_this(), session, reason);
	}
}

void AudioDevice::OnStateChanged(std::shared_ptr<AudioSession> session, AudioSessionState state)
{
	if (state == AudioSessionStateExpired)
	{
		OnDisconnected(session, (AudioSessionDisconnectReason)1000);
	}
}

void AudioDevice::OnDisconnected(std::shared_ptr<AudioSession> session, AudioSessionDisconnectReason reason)
{
	std::lock_guard lock(m_mutex);
	session->UnregisterNotification_Inner(this);
	// TODO: 猜测 API 内部在一个遍历循环中回调，回调中删除其中的成员会导致崩溃或异常
	// 暂时解决方案是在后台线程中释放
	std::thread::thread([](std::shared_ptr<AudioSession> s) {
		Sleep(1000);
		s.reset();
		}, session).detach();
		m_sessions.erase(session);
		m_sessions.erase(session);
		FireSessionRemove(session, reason);
}

HRESULT __stdcall AudioDevice::OnSessionCreated(IAudioSessionControl* NewSession)
{
	std::lock_guard lock(m_mutex);
	CComQIPtr<IAudioSessionControl2> session2(NewSession);
	auto wrapper = std::make_shared<AudioSession>(session2);
	m_sessions.insert(wrapper);
	wrapper->RegisterNotification_Inner(this);
	FireSessionAdd(wrapper);
	return S_OK;
}

#pragma endregion

#pragma region AudioDeviceEnumerator

AudioDeviceEnumerator::AudioDeviceEnumerator()
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

AudioDeviceEnumerator::~AudioDeviceEnumerator()
{
	enumerator->UnregisterEndpointNotificationCallback(this);
}

std::shared_ptr<AudioDevice> AudioDeviceEnumerator::GetDefaultDevice()
{
	std::lock_guard lock(m_mutex);
	CComPtr<IMMDevice> device;
	ThrowIfError(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));
	CComHeapPtr<WCHAR> comstr;
	ThrowIfError(device->GetId(&comstr));
	auto wrapper = GetDeviceById(std::wstring(comstr));
	if (!wrapper.has_value())
	{
		throw std::runtime_error("");
	}
	return wrapper.value();
}

void AudioDeviceEnumerator::RegisterNotification(AudioDeviceEnumeratorEvents* cb)
{
	std::lock_guard lock(m_mutex);
	m_callback.insert(cb);
}

void AudioDeviceEnumerator::UnregisterNotification(AudioDeviceEnumeratorEvents* cb)
{
	std::lock_guard lock(m_mutex);
	m_callback.erase(cb);
}

std::optional<std::shared_ptr<AudioDevice>> AudioDeviceEnumerator::GetDeviceById(const std::wstring& id)
{
	if (m_devices.find(id) == m_devices.end())
	{
		return {};
	}
	return m_devices.at(id);
}

void AudioDeviceEnumerator::FireDeviceStateChanged(std::shared_ptr<AudioDevice> device, DWORD state)
{
	for (auto&& cb : m_callback)
	{
		cb->OnDeviceStateChanged(device, state);
	}
}

void AudioDeviceEnumerator::FireDeviceAdded(std::shared_ptr<AudioDevice> device)
{
	for (auto&& cb : m_callback)
	{
		cb->OnDeviceAdded(device);
	}
}

void AudioDeviceEnumerator::FireDeviceRemoved(std::shared_ptr<AudioDevice> device)
{
	for (auto&& cb : m_callback)
	{
		cb->OnDeviceRemoved(device);
	}
}

void AudioDeviceEnumerator::FireDefaultDeviceChanged(std::shared_ptr<AudioDevice> device)
{
	for (auto&& cb : m_callback)
	{
		cb->OnDefaultDeviceChanged(device);
	}
}

HRESULT __stdcall AudioDeviceEnumerator::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
{
	std::lock_guard lock(m_mutex);
	auto device = GetDeviceById(pwstrDeviceId);
	if (device.has_value())
	{
		FireDeviceStateChanged(device.value(), dwNewState);
	}
	return S_OK;
}

HRESULT __stdcall AudioDeviceEnumerator::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
	std::lock_guard lock(m_mutex);
	CComPtr<IMMDevice> device;
	ThrowIfError(enumerator->GetDevice(pwstrDeviceId, &device));
	auto wrapper = std::make_shared<AudioDevice>(device);
	m_devices[wrapper->GetId()] = wrapper;
	FireDeviceAdded(wrapper);
	return S_OK;
}

HRESULT __stdcall AudioDeviceEnumerator::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
	std::lock_guard lock(m_mutex);
	auto device = GetDeviceById(pwstrDeviceId);
	if (device.has_value())
	{
		m_devices.erase(pwstrDeviceId);
		FireDeviceRemoved(device.value());
	}
	return S_OK;
}

HRESULT __stdcall AudioDeviceEnumerator::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId)
{
	std::lock_guard lock(m_mutex);
	if (flow == eRender && role == eConsole)
	{
		auto device = GetDeviceById(pwstrDefaultDeviceId);
		if (device.has_value())
		{
			FireDefaultDeviceChanged(device.value());
		}
	}
	return S_OK;
}

HRESULT __stdcall AudioDeviceEnumerator::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key)
{
	return S_OK;
}


#pragma endregion
