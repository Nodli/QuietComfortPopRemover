#include "windows.h"

// IMMDeviceEnumerator, IMMDeviceCollection, IMMDevice, ...
#include <Mmdeviceapi.h> 

// IAudioClient
#include <Audioclient.h>

// PKEY_Device_FriendlyName
#include "Functiondiscoverykeys_devpkey.h" 

/*
#define DEVICE_STATE_ACTIVE      0x00000001
#define DEVICE_STATE_DISABLED    0x00000002
#define DEVICE_STATE_NOTPRESENT  0x00000004
#define DEVICE_STATE_UNPLUGGED   0x00000008

Bose QC Headphones (as eRender)  : {0.0.0.00000000}.{4c668472 - de36 - 4631 - a5d0 - 77b01fc6a5d5}
Bose QC Headset    (as eCapture) : {0.0.1.00000000}.{cca6d7b1 - c873 - 474c - 96ac - c5e3aa36775e}
Speaker (Realtek(R) Audio)       : {0.0.0.00000000}.{7b672db1-401f-4b9c-8611-a89e8655a8e4}

Connection Callbacks:
OnDeviceStateChanged({0.0.0.00000000}.{4c668472 - de36 - 4631 - a5d0 - 77b01fc6a5d5}, DEVICE_STATE_NOTPRESENT)
OnDeviceStateChanged({0.0.1.00000000}.{cca6d7b1 - c873 - 474c - 96ac - c5e3aa36775e}, DEVICE_STATE_NOTPRESENT)
OnDeviceStateChanged({0.0.0.00000000}.{4c668472 - de36 - 4631 - a5d0 - 77b01fc6a5d5}, DEVICE_STATE_UNPLUGGED)
OnDeviceStateChanged({0.0.0.00000000}.{4c668472 - de36 - 4631 - a5d0 - 77b01fc6a5d5}, DEVICE_STATE_ACTIVE)
OnDefaultDeviceChanged(eRender, eConsole, {0.0.0.00000000}.{4c668472-de36-4631-a5d0-77b01fc6a5d5})
OnDefaultDeviceChanged(eRender, eMultimedia, {0.0.0.00000000}.{4c668472-de36-4631-a5d0-77b01fc6a5d5})
OnDefaultDeviceChanged(eRender, eCommunications, {0.0.0.00000000}.{4c668472-de36-4631-a5d0-77b01fc6a5d5})
OnDeviceStateChanged({0.0.1.00000000}.{cca6d7b1 - c873 - 474c - 96ac - c5e3aa36775e}, DEVICE_STATE_UNPLUGGED)
OnDeviceStateChanged({0.0.1.00000000}.{cca6d7b1 - c873 - 474c - 96ac - c5e3aa36775e}, DEVICE_STATE_ACTIVE)
OnDefaultDeviceChanged(eCapture, eCommunications, {0.0.1.00000000}.{cca6d7b1-c873-474c-96ac-c5e3aa36775e})

Disconnection Callbacks:
OnDeviceStateChanged({0.0.0.00000000}.{4c668472 - de36 - 4631 - a5d0 - 77b01fc6a5d5}, DEVICE_STATE_UNPLUGGED)
OnDefaultDeviceChanged(eRender, eConsole, {0.0.0.00000000}.{7b672db1-401f-4b9c-8611-a89e8655a8e4})
OnDefaultDeviceChanged(eRender, eMultimedia, {0.0.0.00000000}.{7b672db1-401f-4b9c-8611-a89e8655a8e4})
OnDefaultDeviceChanged(eRender, eCommunications, {0.0.0.00000000}.{7b672db1-401f-4b9c-8611-a89e8655a8e4})
OnDeviceStateChanged({0.0.1.00000000}.{cca6d7b1 - c873 - 474c - 96ac - c5e3aa36775e}, DEVICE_STATE_UNPLUGGED)
OnDefaultDeviceChanged(eCapture, eCommunications, {0.0.1.00000000}.{68871b99-8a50-4ba8-b025-10f7e9de39da})
*/

constexpr size_t g_staticStringMaxSize = 4096u;
constexpr size_t g_staticStringMaxLength = g_staticStringMaxSize - 1u;

WCHAR g_searchDeviceName[g_staticStringMaxSize];

// watcher can read g_foundDeviceId / g_foundDeviceIdLength anytime but needs to enter g_criticalSection to modify them
// main needs to enter g_criticalSection to read g_foundDeviceId / g_foundDeviceIdLength and cannot modify them
CRITICAL_SECTION g_criticalSection;
WCHAR g_foundDeviceId[g_staticStringMaxSize];

HANDLE g_foundDeviceWakeupEvent;
HANDLE g_audioRenderWakeupEvent;

void SearchHeadphonesDeviceIdByName(IMMDeviceEnumerator* _enumerator) {
	HANDLE logFile = CreateFileA("QuietComfortPopRemover.log", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (g_searchDeviceName[0u] == L'\0') {
		if (logFile) {
			const char* str = "No headset device name provided to QuietComfortPopRemover.exe as argument. Listing connected device names :\r\n";
			size_t size = strlen(str);
			DWORD tmp;
			WriteFile(logFile, str, size, &tmp, NULL);
		}
	}
	else {
		if (logFile) {
			DWORD tmp;

			const char* prefix = "Starting search for a headset with name : \"";
			size_t sizePrefix = strlen(prefix);
			WriteFile(logFile, prefix, sizePrefix, &tmp, NULL);

			constexpr size_t mbyteMaxSize = g_staticStringMaxSize * sizeof(WCHAR);
			char mbyte[mbyteMaxSize];
			size_t mbyteLen = wcstombs(mbyte, g_searchDeviceName, mbyteMaxSize);
			WriteFile(logFile, mbyte, mbyteLen, &tmp, NULL);

			const char* postfix = "\" while listing connected device names.\r\n";
			size_t sizePostfix = strlen(postfix);
			WriteFile(logFile, postfix, sizePostfix, &tmp, NULL);
		}
	}

	HRESULT result;

	// Enumerate devices

	IMMDeviceCollection* deviceCollection;
	result = _enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection);
	if (FAILED(result)) return;

	UINT deviceCount;
	result = deviceCollection->GetCount(&deviceCount);

	for (UINT idevice = 0u; idevice != deviceCount; ++idevice) {
		IMMDevice* device;
		result = deviceCollection->Item(idevice, &device);
		if (FAILED(result)) continue;

		// Devices that can have an audio stream need to be in the DEVICE_STATE_ACTIVE state

		DWORD deviceState;
		result = device->GetState(&deviceState);
		if (FAILED(result) || deviceState != DEVICE_STATE_ACTIVE) {
			device->Release();
			continue;
		}

		// Device name comparison

		IPropertyStore* devicePropertyStore;
		result = device->OpenPropertyStore(STGM_READ, &devicePropertyStore);
		if (FAILED(result)) {
			device->Release();
			continue;
		}

		PROPVARIANT propertyName;
		PropVariantInit(&propertyName);

		result = devicePropertyStore->GetValue(PKEY_Device_FriendlyName, &propertyName);
		if (SUCCEEDED(result) && propertyName.vt != VT_EMPTY) {
			if (logFile) {
				DWORD tmp;

				constexpr size_t mbyteMaxSize = g_staticStringMaxSize * sizeof(WCHAR);
				char mbyte[mbyteMaxSize];

				size_t mbyteLen = wcstombs(mbyte, propertyName.pwszVal, mbyteMaxSize);
				WriteFile(logFile, mbyte, mbyteLen, &tmp, NULL);

				char EOL[2u] = { '\r', '\n' };
				WriteFile(logFile, EOL, 2u, &tmp, NULL);
			}

			if (g_searchDeviceName[0u] != L'\0') {
				int nameComparison = wcscmp(g_searchDeviceName, propertyName.pwszVal);
				if (nameComparison == 0) {

					// Register the id of the device with a matching name

					LPWSTR deviceId;
					result = device->GetId(&deviceId);
					if (SUCCEEDED(result)) {
						EnterCriticalSection(&g_criticalSection);
						size_t deviceIdLength = wcslen(deviceId);
						memcpy(g_foundDeviceId, deviceId, (deviceIdLength + 1u) * sizeof(WCHAR));
						LeaveCriticalSection(&g_criticalSection);

						CoTaskMemFree(deviceId);
						PropVariantClear(&propertyName);
						devicePropertyStore->Release();
						deviceCollection->Release();

						if (logFile) {
							DWORD tmp;

							const char* str = "Successfully found the headset at startup ; stopping search.";
							size_t size = strlen(str);
							WriteFile(logFile, str, size, &tmp, NULL);
							CloseHandle(logFile);
						}

						return;
					}
				}
			}
		}

		PropVariantClear(&propertyName);
		devicePropertyStore->Release();
		device->Release();
	}

	deviceCollection->Release();

	if (logFile) CloseHandle(logFile);
}

struct DeviceWatcher : IMMNotificationClient {
	ULONG AddRef() { return 0u; }
	ULONG Release() { return 0u; }
	HRESULT QueryInterface(REFIID riif, VOID** ppvInterface) { return S_OK; }

	HRESULT OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) { return 0; }
	HRESULT OnDeviceAdded(LPCWSTR pwstrDeviceId) { return 0; }
	HRESULT OnDeviceRemoved(LPCWSTR pwstrDeviceId) { return 0; }
	HRESULT OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) {
		if (dwNewState == DEVICE_STATE_UNPLUGGED && g_foundDeviceId[0u] != L'\0' && wcscmp(pwstrDeviceId, g_foundDeviceId) == 0) {
			EnterCriticalSection(&g_criticalSection);
			g_foundDeviceId[0u] = L'\0';
			LeaveCriticalSection(&g_criticalSection);
		}

		if (dwNewState == DEVICE_STATE_ACTIVE && g_searchDeviceName[0u] != L'\0') {
			HRESULT result;

			IMMDevice* device;
			result = enumerator->GetDevice(pwstrDeviceId, &device);
			if (SUCCEEDED(result)) {

				IPropertyStore* devicePropertyStore;
				result = device->OpenPropertyStore(STGM_READ, &devicePropertyStore);
				if (SUCCEEDED(result)) {
					PROPVARIANT propertyName;
					PropVariantInit(&propertyName);

					result = devicePropertyStore->GetValue(PKEY_Device_FriendlyName, &propertyName);
					if (SUCCEEDED(result) && propertyName.vt != VT_EMPTY) {
						int nameComparison = wcscmp(g_searchDeviceName, propertyName.pwszVal);
						if (nameComparison == 0) {
							// Register the id of the device with a matching name

							EnterCriticalSection(&g_criticalSection);

							size_t deviceIdLength = wcslen(pwstrDeviceId);
							memcpy(g_foundDeviceId, pwstrDeviceId, (deviceIdLength + 1u) * sizeof(WCHAR));

							SetEvent(g_foundDeviceWakeupEvent);

							LeaveCriticalSection(&g_criticalSection);
						}
					}

					PropVariantClear(&propertyName);
					devicePropertyStore->Release();
				}
				
				device->Release();
			}
		}

		return 0;
	}
	HRESULT OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) { return 0; }

	// Guaranteed no access to enumerator after UnregisterEndpointNotificationCallback
	IMMDeviceEnumerator* enumerator;
};

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

	LPWSTR commandLine = GetCommandLineW();

	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(commandLine, &argc);
	if (argc > 1) {
		size_t argLen = wcslen(argv[1u]);
		memcpy(g_searchDeviceName, argv[1u], (argLen + 1u) * sizeof(WCHAR));
	}

	HRESULT result;

	InitializeCriticalSection(&g_criticalSection);
	g_foundDeviceWakeupEvent = CreateEventA(NULL, false, false, NULL);
	if (!g_foundDeviceWakeupEvent) return 1u;
	g_audioRenderWakeupEvent = CreateEventA(NULL, false, false, NULL);
	if (!g_audioRenderWakeupEvent) return 1u;

	result = CoInitializeEx(NULL, COINIT_DISABLE_OLE1DDE);
	if (FAILED(result)) return 1u;

	IMMDeviceEnumerator* enumerator;
	result = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
	if (FAILED(result)) return 1u;

	DeviceWatcher watcher;
	watcher.enumerator = enumerator;
	result = enumerator->RegisterEndpointNotificationCallback(&watcher);
	if (FAILED(result)) {
		enumerator->Release();
		return 1u;
	}

	SearchHeadphonesDeviceIdByName(enumerator);

	while (true) {
		IMMDevice* device = nullptr;

		EnterCriticalSection(&g_criticalSection);
		if (g_foundDeviceId[0u] != L'\0') {
			result = enumerator->GetDevice(g_foundDeviceId, &device);
		}
		LeaveCriticalSection(&g_criticalSection);
		if (!device) goto waitForDevice;

		IAudioClient* client;
		result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client);
		if (FAILED(result)) goto finishA;

		REFERENCE_TIME shareModeDevicePeriod;
		result = client->GetDevicePeriod(&shareModeDevicePeriod, NULL);
		if (FAILED(result)) goto finishB;

		// Get the default format because no data will be written to the buffer

		WAVEFORMATEX* clientFormat;
		result = client->GetMixFormat(&clientFormat);
		if (FAILED(result)) goto finishB;

		result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, shareModeDevicePeriod, 0, clientFormat, &GUID_NULL);
		CoTaskMemFree(clientFormat);
		if (FAILED(result)) goto finishB;

		IAudioRenderClient* renderClient;
		result = client->GetService(IID_PPV_ARGS(&renderClient));
		if (FAILED(result)) goto finishB;

		UINT32 bufferFrameCount;
		result = client->GetBufferSize(&bufferFrameCount);
		if (FAILED(result)) goto finishC;

		result = client->SetEventHandle(g_audioRenderWakeupEvent);
		if (FAILED(result)) goto finishC;
		result = client->Start();
		if (FAILED(result)) goto finishC;

		while (true) {
			DWORD wait = WaitForSingleObject(g_audioRenderWakeupEvent, INFINITE);
			if (wait != WAIT_OBJECT_0) break;

			UINT32 usedFrameCount;
			result = client->GetCurrentPadding(&usedFrameCount);
			if (FAILED(result)) break;

			UINT32 requestFrameCount = bufferFrameCount - usedFrameCount;
			BYTE* tmp;
			result = renderClient->GetBuffer(requestFrameCount, &tmp);
			if (FAILED(result)) break;
			result = renderClient->ReleaseBuffer(requestFrameCount, AUDCLNT_BUFFERFLAGS_SILENT);
			if (FAILED(result)) break;
		}

		finishC:
			renderClient->Release();
		finishB:
			client->Stop();
			client->Release();
		finishA:
			device->Release();
		waitForDevice:
			WaitForSingleObject(g_foundDeviceWakeupEvent, INFINITE);
	}

	enumerator->UnregisterEndpointNotificationCallback(&watcher);
	enumerator->Release();

	CloseHandle(g_audioRenderWakeupEvent);
	CloseHandle(g_foundDeviceWakeupEvent);
	DeleteCriticalSection(&g_criticalSection);

	return 0;
}