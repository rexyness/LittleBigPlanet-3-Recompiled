// mutepid <exe name> <seconds>: for that long, mutes every audio session (default render device) that
// belongs to a process with that executable name, as soon as it appears. For muting test runs of a
// game that has no volume setting of its own.
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

static std::set<DWORD> Pids(const wchar_t* exe) {
    std::set<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (!_wcsicmp(pe.szExeFile, exe)) out.insert(pe.th32ProcessID);
    CloseHandle(snap);
    return out;
}

// mutepid <exe> <seconds> [unmute]: "unmute" sets every session of that exe unmuted instead (Windows
// remembers a per-app mute by executable path, so a test must unmute before its game stops). Each
// session's state as first seen is printed.
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const wchar_t* exe = argv[1];
    const int seconds = _wtoi(argv[2]);
    const bool unmute = argc > 3 && !_wcsicmp(argv[3], L"unmute");
    std::set<std::wstring> reported;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::set<std::pair<DWORD, std::wstring>> muted;
    for (int i = 0; i < seconds * 5; ++i) {
        const auto pids = Pids(exe);
        IMMDeviceEnumerator* en = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                       reinterpret_cast<void**>(&en)))) {
            IMMDeviceCollection* devs = nullptr;
            if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs))) {
                UINT n = 0;
                devs->GetCount(&n);
                for (UINT d = 0; d < n; ++d) {
                    IMMDevice* dev = nullptr;
                    if (FAILED(devs->Item(d, &dev))) continue;
                    IAudioSessionManager2* mgr = nullptr;
                    if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                                reinterpret_cast<void**>(&mgr)))) {
                        IAudioSessionEnumerator* se = nullptr;
                        if (SUCCEEDED(mgr->GetSessionEnumerator(&se))) {
                            int c = 0;
                            se->GetCount(&c);
                            for (int k = 0; k < c; ++k) {
                                IAudioSessionControl* ctl = nullptr;
                                if (FAILED(se->GetSession(k, &ctl))) continue;
                                IAudioSessionControl2* ctl2 = nullptr;
                                DWORD pid = 0;
                                if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&ctl2)))) {
                                    ctl2->GetProcessId(&pid);
                                    ctl2->Release();
                                }
                                if (pids.count(pid)) {
                                    ISimpleAudioVolume* vol = nullptr;
                                    if (SUCCEEDED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&vol)))) {
                                        BOOL m = FALSE;
                                        vol->GetMute(&m);
                                        const std::wstring key = std::to_wstring(pid) + L"/" + std::to_wstring(d) + L"/" + std::to_wstring(k);
                                        if (reported.insert(key).second) {
                                            std::printf("session of pid %lu (device %u) first seen %s\n", pid, d, m ? "MUTED" : "unmuted");
                                            std::fflush(stdout);
                                        }
                                        if (!unmute && !m) {
                                            vol->SetMute(TRUE, nullptr);
                                            std::printf("muted an audio session of pid %lu (device %u)\n", pid, d);
                                            std::fflush(stdout);
                                        } else if (unmute && m) {
                                            vol->SetMute(FALSE, nullptr);
                                            std::printf("unmuted an audio session of pid %lu (device %u)\n", pid, d);
                                            std::fflush(stdout);
                                        }
                                        vol->Release();
                                    }
                                }
                                ctl->Release();
                            }
                            se->Release();
                        }
                        mgr->Release();
                    }
                    dev->Release();
                }
                devs->Release();
            }
            en->Release();
        }
        Sleep(200);
    }
    CoUninitialize();
    return 0;
}
