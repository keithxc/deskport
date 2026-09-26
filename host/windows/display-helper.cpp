// SPDX-License-Identifier: GPL-3.0-or-later
// A session-scoped Windows display-mode owner. Never edits registry defaults.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QString>
#include <QUuid>
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <devguid.h>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "display-request.h"
#include "text-caret.h"

namespace {
void send(const QJsonObject& value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    std::cout << bytes.constData() << std::endl;
}
QString ownedAdapterHardware(const QString& instance) {
    if (instance.isEmpty()) return {};
    auto devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return {};
    QString result;
    SP_DEVINFO_DATA info{}; info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
        wchar_t id[1024]{}, hardware[4096]{};
        if (SetupDiGetDeviceInstanceIdW(devices, &info, id, 1024, nullptr) &&
            instance.compare(QString::fromWCharArray(id), Qt::CaseInsensitive) == 0 &&
            SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(hardware), sizeof(hardware), nullptr)) {
            result = QString::fromWCharArray(hardware);
            break;
        }
    }
    // EnumDisplayDevices exposes hardware IDs, not PnP instance IDs. Only map
    // when the verified owned instance is the sole adapter with this hardware ID.
    int matches = 0;
    for (DWORD i = 0; !result.isEmpty() && SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
        wchar_t hardware[4096]{};
        if (SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, nullptr,
                reinterpret_cast<PBYTE>(hardware), sizeof(hardware), nullptr) &&
            result.compare(QString::fromWCharArray(hardware), Qt::CaseInsensitive) == 0) ++matches;
    }
    SetupDiDestroyDeviceInfoList(devices);
    return matches == 1 ? result : QString();
}
// The elevated companion survives this helper being killed. It owns the full
// pre-sharing topology and disables the exact owned PnP instance on release.
struct DisplayLease {
    HANDLE ready=nullptr, release=nullptr, process=nullptr;
    QString error;
    HANDLE request=nullptr, completed=nullptr, mapping=nullptr;
    DisplayModeRequest* modeRequest=nullptr;
    bool start() {
        std::cerr << "Display lease: requesting Windows authorization" << std::endl;
        const auto nonce=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto prefix=QStringLiteral("Local\\DeskPort.Display.")+nonce;
        ready=CreateEventW(nullptr,TRUE,FALSE,reinterpret_cast<LPCWSTR>((prefix+".ready").utf16()));
        release=CreateEventW(nullptr,TRUE,FALSE,reinterpret_cast<LPCWSTR>((prefix+".release").utf16()));
        request=CreateEventW(nullptr,FALSE,FALSE,reinterpret_cast<LPCWSTR>((prefix+".request").utf16()));
        completed=CreateEventW(nullptr,FALSE,FALSE,reinterpret_cast<LPCWSTR>((prefix+".completed").utf16()));
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(DisplayModeRequest),reinterpret_cast<LPCWSTR>((prefix+".mode").utf16()));
        if(mapping)modeRequest=static_cast<DisplayModeRequest*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(DisplayModeRequest)));
        if(!ready||!release||!request||!completed||!modeRequest){error="Cannot create display recovery events";return false;}
        const auto executable=QDir::toNativeSeparators(QCoreApplication::applicationDirPath()+"/deskport-display-recovery.exe");
        const auto arguments=QStringLiteral("lease %1 %2").arg(GetCurrentProcessId()).arg(nonce);
        const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        struct ComScope { HRESULT result; ~ComScope(){if(SUCCEEDED(result))CoUninitialize();} } comScope{com};
        SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;
        info.lpVerb=L"runas";info.lpFile=reinterpret_cast<LPCWSTR>(executable.utf16());
        info.lpParameters=reinterpret_cast<LPCWSTR>(arguments.utf16());info.nShow=SW_HIDE;
        if(!ShellExecuteExW(&info)||!info.hProcess){const auto code=GetLastError();error=QString("Windows display authorization was canceled or unavailable (error %1)").arg(code);return false;}
        process=info.hProcess;
        std::cerr << "Display lease: authorization returned; waiting for guard" << std::endl;
        HANDLE waiting[]{ready,process};
        if(WaitForMultipleObjects(2,waiting,FALSE,15000)!=WAIT_OBJECT_0){error="Independent display recovery guard could not prepare the owned device";return false;}
        std::cerr << "Display lease: guard ready" << std::endl;
        return true;
    }
    bool resize(unsigned width, unsigned height) {
        ResetEvent(completed);
        modeRequest->width=width;modeRequest->height=height;modeRequest->result=ERROR_IO_PENDING;
        MemoryBarrier();SetEvent(request);
        HANDLE waiting[]{completed,process};
        if(WaitForMultipleObjects(2,waiting,FALSE,20000)!=WAIT_OBJECT_0)return false;
        MemoryBarrier();
        if(modeRequest->result)std::cerr<<"Dynamic display preparation failed: "<<modeRequest->result<<std::endl;
        return modeRequest->result==ERROR_SUCCESS;
    }
    bool finish() {
        if(!process)return true;
        SetEvent(release);
        const auto waited=WaitForSingleObject(process,30000);DWORD result=ERROR_TIMEOUT;
        if(waited==WAIT_OBJECT_0)GetExitCodeProcess(process,&result);
        if(waited==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;}
        if(result){std::cerr<<"Full display recovery failed: "<<result<<"; durable snapshot retained"<<std::endl;return false;}
        return true;
    }
    ~DisplayLease(){finish();if(modeRequest)UnmapViewOfFile(modeRequest);if(mapping)CloseHandle(mapping);if(request)CloseHandle(request);if(completed)CloseHandle(completed);if(process)CloseHandle(process);if(ready)CloseHandle(ready);if(release)CloseHandle(release);}
};
QString path;
QString output;
QString captureOutput;
DEVMODEW original{};
bool changed = false;
bool privateVirtual = false;
bool headlessVirtual = false;
bool current(DEVMODEW& mode) {
    mode = {}; mode.dmSize = sizeof(mode);
    return EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), ENUM_CURRENT_SETTINGS, &mode, 0);
}
bool writeState(bool pending) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(QJsonObject{
        {"version", 1}, {"output", output}, {"pending", pending}, {"virtual", privateVirtual},
        {"original", QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(&original), sizeof(original)).toBase64())}
    }).toJson();
    if(file.write(bytes)!=bytes.size()||!file.commit())return false;
    QSaveFile capture(QFileInfo(path).absolutePath()+"/windows-capture-output");
    const auto name=(captureOutput.isEmpty()?output:captureOutput).toUtf8()+'\n';
    return capture.open(QIODevice::WriteOnly)&&capture.write(name)==name.size()&&capture.commit();
}
// GDI mode changes may reload unrelated registry modes. Preserve the full
// active CCD mode set and reapply it without saving any display defaults.
struct ActiveTopology {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    bool read() {
        for (int attempt=0; attempt<5; ++attempt) {
            UINT32 np=0,nm=0;
            if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,&nm)||np>64||nm>512)return false;
            paths.resize(np);modes.resize(nm);
            const auto rc=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,paths.data(),&nm,modes.data(),nullptr);
            if(rc==ERROR_INSUFFICIENT_BUFFER)continue;
            if(rc)return false;
            paths.resize(np);modes.resize(nm);return true;
        }
        return false;
    }
};
ActiveTopology sessionTopology;
ActiveTopology idleTopology;
bool policyChanged = false;
bool preservesPhysicalSources(const ActiveTopology& before, const ActiveTopology& after) {
    const auto sameAdapter=[](LUID a,LUID b){return a.LowPart==b.LowPart&&a.HighPart==b.HighPart;};
    for(const auto& old:before.modes) {
        if(old.infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)continue;
        const auto mode=std::find_if(after.modes.begin(),after.modes.end(),[&](const DISPLAYCONFIG_MODE_INFO& m){return m.infoType==old.infoType&&m.id==old.id&&sameAdapter(m.adapterId,old.adapterId);});
        if(mode==after.modes.end())return false;
        const auto& a=old.sourceMode;const auto& b=mode->sourceMode;
        if(a.width!=b.width||a.height!=b.height||a.position.x!=b.position.x||a.position.y!=b.position.y||a.pixelFormat!=b.pixelFormat)return false;
    }
    for(const auto& old:before.paths) {
        const auto path=std::find_if(after.paths.begin(),after.paths.end(),[&](const DISPLAYCONFIG_PATH_INFO& p){return sameAdapter(p.sourceInfo.adapterId,old.sourceInfo.adapterId)&&p.sourceInfo.id==old.sourceInfo.id&&sameAdapter(p.targetInfo.adapterId,old.targetInfo.adapterId)&&p.targetInfo.id==old.targetInfo.id;});
        if(path==after.paths.end())return false;
        const auto& a=old.targetInfo;const auto& b=path->targetInfo;
        if(a.rotation!=b.rotation||a.scaling!=b.scaling||a.refreshRate.Numerator!=b.refreshRate.Numerator||a.refreshRate.Denominator!=b.refreshRate.Denominator||a.scanLineOrdering!=b.scanLineOrdering)return false;
    }
    return true;
}
bool applyMode(DEVMODEW target) {
    if(!privateVirtual)return ChangeDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),&target,nullptr,0,nullptr)==DISP_CHANGE_SUCCESSFUL;
    ActiveTopology after;
    const auto& before=sessionTopology;
    if(before.paths.empty()&&!headlessVirtual)return false;
    const auto changeResult=ChangeDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),&target,nullptr,0,nullptr);
    if(changeResult!=DISP_CHANGE_SUCCESSFUL)
        std::cerr<<"GDI display change rejected ("<<changeResult<<"); trying the owned CCD path"<<std::endl;
    // An enabled indirect display can reject its initial GDI positioning even
    // though CCD exposes a valid active path. Re-query after either result and
    // let the topology-preserving CCD update below place the owned output.
    if(!after.read())return false;
    // Most drivers preserve the existing outputs themselves. Re-submitting
    // their complete CCD configuration is unnecessary and can be rejected by
    // physical GPUs while the display is locked or powered down. Only repair
    // the baseline or position the owned output when GDI did not establish it.
    if(preservesPhysicalSources(before,after)) {
        DEVMODEW actual{};
        if(current(actual)&&actual.dmPelsWidth==target.dmPelsWidth&&actual.dmPelsHeight==target.dmPelsHeight&&
           actual.dmPosition.x==target.dmPosition.x&&actual.dmPosition.y==target.dmPosition.y)return true;
    }
    auto sameAdapter=[](LUID a,LUID b){return a.LowPart==b.LowPart&&a.HighPart==b.HighPart;};
    bool found=false;
    for(auto& path:after.paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
        name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        name.header.size=sizeof(name);name.header.adapterId=path.sourceInfo.adapterId;name.header.id=path.sourceInfo.id;
        if(DisplayConfigGetDeviceInfo(&name.header))return false;
        const bool owned=output.compare(QString::fromWCharArray(name.viewGdiDeviceName),Qt::CaseInsensitive)==0;
        if(owned) {
            const auto index=(path.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE)?path.sourceInfo.sourceModeInfoIdx:path.sourceInfo.modeInfoIdx;
            if(index>=after.modes.size()||after.modes[index].infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)return false;
            auto& source=after.modes[index].sourceMode;
            source.position=target.dmPosition;source.width=target.dmPelsWidth;source.height=target.dmPelsHeight;
            // The queried desktop-image rectangle describes the old source
            // geometry. Let CCD derive it again after moving/resizing our VDD;
            // retaining it can make SetDisplayConfig reject the path with 87.
            if(path.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE)
                path.targetInfo.desktopModeInfoIdx=DISPLAYCONFIG_PATH_DESKTOP_IMAGE_IDX_INVALID;
            found=true;
        } else {
            const auto oldPath=std::find_if(before.paths.begin(),before.paths.end(),[&](const DISPLAYCONFIG_PATH_INFO& p){return sameAdapter(p.sourceInfo.adapterId,path.sourceInfo.adapterId)&&p.sourceInfo.id==path.sourceInfo.id&&sameAdapter(p.targetInfo.adapterId,path.targetInfo.adapterId)&&p.targetInfo.id==path.targetInfo.id;});
            if(oldPath==before.paths.end())return false;
            path.targetInfo.rotation=oldPath->targetInfo.rotation;
            path.targetInfo.scaling=oldPath->targetInfo.scaling;
            path.targetInfo.refreshRate=oldPath->targetInfo.refreshRate;
            path.targetInfo.scanLineOrdering=oldPath->targetInfo.scanLineOrdering;
            // Copy mode payloads by stable adapter/type/id, retaining the new
            // array indices referenced by the queried paths.
            for(auto& mode:after.modes) {
                if(!sameAdapter(mode.adapterId,path.sourceInfo.adapterId)&&!sameAdapter(mode.adapterId,path.targetInfo.adapterId))continue;
                auto old=std::find_if(before.modes.begin(),before.modes.end(),[&](const DISPLAYCONFIG_MODE_INFO& m){return sameAdapter(m.adapterId,mode.adapterId)&&m.id==mode.id&&m.infoType==mode.infoType;});
                if(old!=before.modes.end())mode=*old;
            }
        }
    }
    if(!found)return false;
    const auto flags=SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_VIRTUAL_MODE_AWARE|SDC_ALLOW_CHANGES;
    auto rc=SetDisplayConfig(UINT32(after.paths.size()),after.paths.data(),UINT32(after.modes.size()),after.modes.data(),SDC_VALIDATE|flags);
    if(!rc)rc=SetDisplayConfig(UINT32(after.paths.size()),after.paths.data(),UINT32(after.modes.size()),after.modes.data(),SDC_APPLY|flags);
    if(rc)std::cerr<<"Preserving display topology failed: "<<rc<<std::endl;
    if(rc)return false;
    ActiveTopology verified;
    if(!verified.read())return false;
    DEVMODEW actual{};
    return preservesPhysicalSources(before,verified) && current(actual) &&
        actual.dmPelsWidth==target.dmPelsWidth && actual.dmPelsHeight==target.dmPelsHeight &&
        actual.dmPosition.x==target.dmPosition.x && actual.dmPosition.y==target.dmPosition.y;
}
// Build every policy from the immutable sharing baseline, never from the
// previous session's clone/exclusive topology. The elevated guardian retains
// the pre-sharing snapshot independently for crashes and final release.
bool applyPolicy(DEVMODEW target, int policy) {
    if (!privateVirtual) return policy == 0 && applyMode(target);
    captureOutput.clear();
    ActiveTopology desired = idleTopology;
    size_t ownedIndex = desired.paths.size();
    for (size_t i=0; i<desired.paths.size(); ++i) {
        const auto& p=desired.paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
        name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        name.header.size=sizeof(name);name.header.adapterId=p.sourceInfo.adapterId;name.header.id=p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&name.header)) return false;
        if (output.compare(QString::fromWCharArray(name.viewGdiDeviceName),Qt::CaseInsensitive)==0) ownedIndex=i;
    }
    if (ownedIndex==desired.paths.size()) return false;
    auto sourceIndex=[](const DISPLAYCONFIG_PATH_INFO& p) -> UINT32 {
        return (p.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) ? p.sourceInfo.sourceModeInfoIdx : p.sourceInfo.modeInfoIdx;
    };
    const auto ownedMode=sourceIndex(desired.paths[ownedIndex]);
    if (ownedMode>=desired.modes.size()) return false;
    auto& ownedSource=desired.modes[ownedMode].sourceMode;
    ownedSource.width=target.dmPelsWidth;ownedSource.height=target.dmPelsHeight;
    ownedSource.position=policy==2 ? original.dmPosition : POINTL{0,0};
    for (size_t i=0; i<desired.paths.size(); ++i) {
        auto& p=desired.paths[i];
        if (i!=ownedIndex && policy!=0) continue;
        const auto index=sourceIndex(p);
        if(index>=desired.modes.size())return false;
        if(policy==0) {
            // Virtual clone paths share source geometry across adapters.
            // Never silently substitute extension if a driver cannot clone.
            if(!(p.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE))return false;
            desired.modes[index].sourceMode=ownedSource;
            p.sourceInfo.cloneGroupId=DISPLAYCONFIG_PATH_CLONE_GROUP_INVALID;
            p.targetInfo.rotation=DISPLAYCONFIG_ROTATION_IDENTITY;
            p.targetInfo.targetModeInfoIdx=DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID;
        }
        if(p.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) {
            p.targetInfo.desktopModeInfoIdx=DISPLAYCONFIG_PATH_DESKTOP_IMAGE_IDX_INVALID;
            // Retain the supported signal mode; vary the virtual desktop source.
        } else p.targetInfo.modeInfoIdx=DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    }
    if(policy==1) {
        const auto ownedPath=desired.paths[ownedIndex];
        desired.paths.assign(1,ownedPath);
    }
    const auto flags=SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_VIRTUAL_MODE_AWARE|SDC_ALLOW_CHANGES;
    auto rc=SetDisplayConfig(UINT32(desired.paths.size()),desired.paths.data(),UINT32(desired.modes.size()),desired.modes.data(),SDC_VALIDATE|flags);
    if(!rc)rc=SetDisplayConfig(UINT32(desired.paths.size()),desired.paths.data(),UINT32(desired.modes.size()),desired.modes.data(),SDC_APPLY|flags);
    if(rc){std::cerr<<"Display policy "<<policy<<" failed: "<<rc<<std::endl;return false;}
    ActiveTopology verified;
    DEVMODEW actual{};
    if(!verified.read())return false;
    const auto ownedTarget=idleTopology.paths[ownedIndex].targetInfo;
    for(const auto& p:verified.paths) {
        if(p.targetInfo.id!=ownedTarget.id||p.targetInfo.adapterId.LowPart!=ownedTarget.adapterId.LowPart||p.targetInfo.adapterId.HighPart!=ownedTarget.adapterId.HighPart)continue;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;name.header.size=sizeof(name);name.header.adapterId=p.sourceInfo.adapterId;name.header.id=p.sourceInfo.id;
        if(!DisplayConfigGetDeviceInfo(&name.header)){output=QString::fromWCharArray(name.viewGdiDeviceName);std::cerr<<"Capture output after policy: "<<output.toStdString()<<std::endl;}
    }
    if(policy==0) {
        for(const auto& p:verified.paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;name.header.size=sizeof(name);name.header.adapterId=p.sourceInfo.adapterId;name.header.id=p.sourceInfo.id;
            if(DisplayConfigGetDeviceInfo(&name.header))continue;
            DEVMODEW mode{};mode.dmSize=sizeof(mode);
            if(EnumDisplaySettingsExW(name.viewGdiDeviceName,ENUM_CURRENT_SETTINGS,&mode,0)) {captureOutput=QString::fromWCharArray(name.viewGdiDeviceName);break;}
        }
    }
    actual.dmSize=sizeof(actual);
    const bool haveCurrent=captureOutput.isEmpty()?current(actual):EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(captureOutput.utf16()),ENUM_CURRENT_SETTINGS,&actual,0);
    std::cerr<<"Policy verification "<<policy<<" output="<<output.toStdString()<<" readable="<<haveCurrent<<" requested="<<target.dmPelsWidth<<"x"<<target.dmPelsHeight<<" actual="<<actual.dmPelsWidth<<"x"<<actual.dmPelsHeight<<" paths="<<verified.paths.size()<<std::endl;
    for(const auto& p:verified.paths) {
        const auto n=sourceIndex(p);
        if(n<verified.modes.size())std::cerr<<"source "<<p.sourceInfo.id<<" flags="<<p.flags<<" size="<<verified.modes[n].sourceMode.width<<"x"<<verified.modes[n].sourceMode.height<<" pos="<<verified.modes[n].sourceMode.position.x<<","<<verified.modes[n].sourceMode.position.y<<std::endl;
    }
    if(!haveCurrent||actual.dmPelsWidth!=target.dmPelsWidth||actual.dmPelsHeight!=target.dmPelsHeight)return false;
    if(policy==2)return preservesPhysicalSources(sessionTopology,verified)&&actual.dmPosition.x==original.dmPosition.x&&actual.dmPosition.y==original.dmPosition.y&&writeState(true);
    if(actual.dmPosition.x||actual.dmPosition.y)return false;
    if(policy==1)return verified.paths.size()==1&&writeState(true);
    if(verified.paths.size()!=idleTopology.paths.size())return false;
    for(const auto& p:verified.paths) {
        const auto index=sourceIndex(p);
        if(index>=verified.modes.size())return false;
        const auto& m=verified.modes[index].sourceMode;
        const bool rotated=p.targetInfo.rotation==DISPLAYCONFIG_ROTATION_ROTATE90||p.targetInfo.rotation==DISPLAYCONFIG_ROTATION_ROTATE270;
        if(m.position.x||m.position.y||
           !((m.width==target.dmPelsWidth&&m.height==target.dmPelsHeight)||
             (rotated&&m.height==target.dmPelsWidth&&m.width==target.dmPelsHeight)))return false;
    }
    return writeState(true);
}
bool restore() {
    if (!changed) return true;
    // Apply only the display that this helper changed, without modifying saved defaults.
    if (privateVirtual && policyChanged) {
        auto baseline=idleTopology;
        const auto rc=SetDisplayConfig(UINT32(baseline.paths.size()),baseline.paths.data(),UINT32(baseline.modes.size()),baseline.modes.data(),SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_VIRTUAL_MODE_AWARE);
        ActiveTopology verified;
        if(rc||!verified.read()||!preservesPhysicalSources(baseline,verified))return false;
        policyChanged=false;
    } else if (!applyMode(original)) return false;
    changed = false;
    captureOutput.clear();
    return writeState(false);
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (app.arguments().value(1)=="--text-caret") return runTextCaretProbe(app.arguments());
    DisplayLease recovery;
    const auto directory = qEnvironmentVariable("DESKPORT_DISPLAY_STATE_DIR");
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        send({{"error", "Missing private display state directory"}}); return 1;
    }
    QLockFile lock(directory + "/windows-display.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock()) { send({{"error", "Another display owner is running"}}); return 1; }
    path = directory + "/windows-display.json";
    const auto lease = CreateMutexW(nullptr, FALSE, L"Global\\DeskPort.WindowsDisplay.Owner");
    if (!lease || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (lease) CloseHandle(lease);
        send({{"error", "Another DeskPort display owner is active in this Windows session"}}); return 1;
    }
    QSettings machine("HKEY_LOCAL_MACHINE\\Software\\DeskPort", QSettings::NativeFormat);
    const auto ownedInstance = machine.value("VirtualDisplayDevice").toString();
    QFile previous(path);
    if (previous.exists()) {
        if (!previous.open(QIODevice::ReadOnly)) { send({{"error", "Cannot read display recovery state"}}); return 1; }
        const auto saved = QJsonDocument::fromJson(previous.readAll()).object();
        previous.close(); // Windows cannot atomically replace an open recovery file.
        if (saved["pending"].toBool()) {
            const auto bytes = QByteArray::fromBase64(saved["original"].toString().toLatin1());
            output = saved["output"].toString();
            if (saved["version"].toInt() != 1 || bytes.size() != sizeof(original) || !output.startsWith("\\\\.\\DISPLAY")) {
                send({{"error", "Invalid display recovery record; original record retained"}}); return 1;
            }
            memcpy(&original, bytes.constData(), sizeof(original));
            if (original.dmSize != sizeof(original) || original.dmDriverExtra != 0) {
                send({{"error", "Invalid saved display mode"}}); return 1;
            }
            if (saved["virtual"].toBool()) {
                // The independent guardian restores the physical topology and
                // disables this device even when the helper is killed. Replaying
                // a stale GDI mode here would re-enable it outside that guard.
                // The new lease below must verify the disabled device baseline.
                if (ownedInstance.isEmpty()) { send({{"error", "The virtual display recovery owner is missing"}}); return 1; }
            } else {
                changed = true;
                if (!restore()) { send({{"error", "Previous display restoration failed; reconnect the display and retry"}}); return 1; }
            }
        }
    }
    // Bind only the device instance created by DeskPort, never an independently
    // installed VDD adapter with the same vendor or friendly name.
    if(!ownedInstance.isEmpty()&&!sessionTopology.read()){send({{"error", "Cannot capture the pre-sharing display topology"}});return 1;}
    if (!ownedInstance.isEmpty() && !recovery.start()) { send({{"error", recovery.error}}); return 1; }
    const auto owned = ownedAdapterHardware(ownedInstance);
    if (!ownedInstance.isEmpty() && owned.isEmpty()) {
        send({{"error", "The owned virtual display is missing or ambiguous; no other display was selected"}}); return 1;
    }
    output.clear();
    QString physical;
    // PnP reports DN_STARTED before the indirect display publishes its output.
    // Reconnects must wait for that output instead of treating the short gap as
    // a missing adapter. Never fall back to a different adapter during the wait.
    for (int attempt = 0; attempt < 50; ++attempt) {
      output.clear(); physical.clear();
      DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
      for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i) {
        if (!owned.isEmpty() && QString::fromWCharArray(device.DeviceID).compare(owned, Qt::CaseInsensitive) == 0) {
            output = QString::fromWCharArray(device.DeviceName);
            privateVirtual = true;
        }
        if ((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) && (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            physical = QString::fromWCharArray(device.DeviceName);
        device = {}; device.cb = sizeof(device);
      }
      DEVMODEW readyMode{}; readyMode.dmSize = sizeof(readyMode);
      if (owned.isEmpty() || (!output.isEmpty() &&
          EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), ENUM_CURRENT_SETTINGS, &readyMode, 0))) break;
      Sleep(100);
    }
    if (!owned.isEmpty() && output.isEmpty()) {
        send({{"error", "The owned virtual adapter has no available display output"}}); return 1;
    }
    if (output.isEmpty()) output = physical;
    if (output.isEmpty() || !current(original)) {
        original = {}; original.dmSize = sizeof(original);
        if (output.isEmpty() || !EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), ENUM_REGISTRY_SETTINGS, &original, 0)) {
            send({{"error", "Cannot read the selected Windows display mode"}}); return 1;
        }
    }
    if (privateVirtual) {
        std::cerr << "Display lease: positioning owned output" << std::endl;
        // Enabling an indirect display can make Windows retire the closed
        // laptop panel. A sole remaining source must be placed at (0, 0), not
        // to the right of the no-longer-active panel. The independent guardian
        // still owns the complete pre-enable snapshot for final restoration.
        QString activePhysical;
        DISPLAY_DEVICEW active{};active.cb=sizeof(active);
        for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&active,0);++i) {
            const auto name=QString::fromWCharArray(active.DeviceName);
            if((active.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)&&name!=output&&
               (activePhysical.isEmpty()||(active.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)))activePhysical=name;
            active={};active.cb=sizeof(active);
        }
        headlessVirtual=activePhysical.isEmpty();
        if(headlessVirtual) {
            original.dmPosition={0,0};
            sessionTopology.paths.clear();sessionTopology.modes.clear();
            std::cerr<<"Display lease: owned output is the only active display"<<std::endl;
        } else {
            physical=activePhysical;
            DEVMODEW primary{}; primary.dmSize = sizeof(primary);
            if (!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(physical.utf16()), ENUM_CURRENT_SETTINGS, &primary, 0)) {
                send({{"error", "Cannot determine the physical desktop layout"}}); return 1;
            }
            LONG right = primary.dmPosition.x + LONG(primary.dmPelsWidth);
            DISPLAY_DEVICEW other{}; other.cb = sizeof(other);
            for (DWORD i=0; EnumDisplayDevicesW(nullptr,i,&other,0); ++i) {
                if ((other.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
                    output != QString::fromWCharArray(other.DeviceName)) {
                    DEVMODEW mode{}; mode.dmSize=sizeof(mode);
                    if (EnumDisplaySettingsExW(other.DeviceName,ENUM_CURRENT_SETTINGS,&mode,0))
                        right=qMax(right,mode.dmPosition.x+LONG(mode.dmPelsWidth));
                }
                other={}; other.cb=sizeof(other);
            }
            for(const auto& mode:sessionTopology.modes) {
                if(mode.infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
                    right=qMax(right,mode.sourceMode.position.x+LONG(mode.sourceMode.width));
            }
            // Windows may reactivate a remembered placement above the primary.
            // Always place this session's owned output to the right of all existing
            // outputs; the external lease restores the actual pre-session topology.
            original.dmPosition.x = right;
            original.dmPosition.y = primary.dmPosition.y;
        }
        original.dmFields |= DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
        if (!writeState(true) || !applyMode(original)) {
            send({{"error", "Cannot activate the DeskPort virtual display"}}); return 1;
        }
        std::cerr << "Display lease: owned output positioned" << std::endl;
    }
    if (!writeState(false)) {
        send({{"error", "Cannot snapshot the current Windows display"}}); return 1;
    }
    if(privateVirtual&&!idleTopology.read()){send({{"error","Cannot snapshot the idle display topology"}});return 1;}
    QJsonArray modes; QSet<QString> seenModes;
    for(DWORD index=0; index<4096; ++index) {
        DEVMODEW mode{}; mode.dmSize=sizeof(mode);
        if(!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),index,&mode,0))break;
        const int w=int(mode.dmPelsWidth),h=int(mode.dmPelsHeight);
        const auto key=QString::number(w)+"x"+QString::number(h);
        if(w>=640&&w<=7680&&h>=360&&h<=4320&&w%4==0&&h%4==0&&mode.dmBitsPerPel==original.dmBitsPerPel&&!seenModes.contains(key)&&modes.size()<96) {
            seenModes.insert(key);modes.append(QJsonObject{{"width",w},{"height",h}});
        }
    }
    send({{"displayId", 1}, {"width", int(original.dmPelsWidth)}, {"height", int(original.dmPelsHeight)}, {"scale", 1}, {"virtual", privateVirtual}, {"displayModes",privateVirtual ? QJsonArray() : modes}});
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.size() > 4096) break;
        const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const int seq = request["seq"].toInt();
        QJsonObject response{{"seq", seq}, {"scale", 1}};
        QString error;
        bool fatalTopologyError=false;
        const int width = request["width"].toInt(), height = request["height"].toInt();
        if (!seq) error = "Missing request sequence";
        else if (!request["session"].toBool()) {
            if (!restore()) error = "Windows rejected restoration; recovery record retained";
        } else if (request["displayPolicy"].toInt() < 0 || request["displayPolicy"].toInt() > 2 || (!privateVirtual && request["displayPolicy"].toInt() != 0)) {
            error = "The selected display policy requires the owned virtual display";
        } else if (width < 640 || width > 7680 || height < 360 || height > 4320 || width % 4 || height % 4) {
            error = "Invalid display size";
        } else {
            DEVMODEW target{}; bool found = false;
            for (DWORD index = 0; ; ++index) {
                target = {}; target.dmSize = sizeof(target);
                if (!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), index, &target, 0)) break;
                if (int(target.dmPelsWidth) == width && int(target.dmPelsHeight) == height && target.dmBitsPerPel == original.dmBitsPerPel) {
                    found = true; break;
                }
            }
            if (!found && privateVirtual) {
                // The signed driver reads its mode list when its owned adapter
                // starts. The independent guardian validates and performs that
                // transition; it remains responsible for physical recovery.
                if(ownedAdapterHardware(ownedInstance).compare(owned,Qt::CaseInsensitive)!=0) {send({{"seq",seq},{"error","The owned virtual adapter became ambiguous"}});break;}
                if(!restore()) {send({{"seq",seq},{"error","Cannot restore before display recreation"}});break;}
                QFile::remove(QFileInfo(path).absolutePath()+"/windows-capture-output");
                if(!recovery.resize(unsigned(width),unsigned(height))) {
                    send({{"seq",seq},{"error","Cannot prepare the requested virtual display mode"}});break;
                }
                output.clear();
                for(int attempt=0;attempt<50;++attempt) {
                    DISPLAY_DEVICEW device{};device.cb=sizeof(device);
                    QCoreApplication::processEvents();
                    for(DWORD i=0;EnumDisplayDevicesW(nullptr,i,&device,0);++i) {
                        if(QString::fromWCharArray(device.DeviceID).compare(owned,Qt::CaseInsensitive)==0) {
                            DEVMODEW ready{};ready.dmSize=sizeof(ready);
                            output=QString::fromWCharArray(device.DeviceName);
                        }
                        device={};device.cb=sizeof(device);
                    }
                    DEVMODEW available{};
                    if(!output.isEmpty()&&(current(available)||EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),ENUM_REGISTRY_SETTINGS,&available,0)))break;
                    Sleep(100);
                }
                DEVMODEW baseline{};baseline.dmSize=sizeof(baseline);
                if(output.isEmpty()||(!current(baseline)&&!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),ENUM_REGISTRY_SETTINGS,&baseline,0))) {send({{"seq",seq},{"error","The recreated virtual display is unavailable"}});break;}
                baseline.dmPosition=original.dmPosition;
                baseline.dmFields|=DM_POSITION;
                bool positioned=false;
                for(int attempt=0;attempt<30&&!positioned;++attempt) {
                    QCoreApplication::processEvents();
                    positioned=applyMode(baseline);
                    if(!positioned)Sleep(100);
                }
                if(!positioned||!idleTopology.read()) {send({{"seq",seq},{"error","Cannot position the recreated virtual display"}});break;}
                original=baseline;
                if(!writeState(false)){send({{"seq",seq},{"error","Cannot publish the recreated display"}});break;}
                for(DWORD i=0;;++i) {
                    target={};target.dmSize=sizeof(target);
                    if(!EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()),i,&target,0))break;
                    if(int(target.dmPelsWidth)==width && int(target.dmPelsHeight)==height && target.dmBitsPerPel==original.dmBitsPerPel){found=true;break;}
                }
                if(!found){send({{"seq",seq},{"error","The driver did not expose the requested display mode"}});break;}
            }
            if (!found) {
                target = original;
                target.dmPelsWidth = DWORD(width); target.dmPelsHeight = DWORD(height);
                target.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
            }
            if (!privateVirtual && ChangeDisplaySettingsExW(reinterpret_cast<LPCWSTR>(output.utf16()), &target, nullptr, CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL)
                error = "Windows rejected the requested display mode";
            else if (!writeState(true)) error = "Cannot persist display recovery state";
            else {
                changed = true;
                target.dmPosition=original.dmPosition;
                target.dmFields|=DM_POSITION;
                policyChanged=privateVirtual;
                if (!applyPolicy(target,request["displayPolicy"].toInt())) {
                    error = "Display mode application failed; ending the display lease to restore the desktop";
                    fatalTopologyError=true;
                }
            }
        }
        DEVMODEW actual{};actual.dmSize=sizeof(actual);
        const bool readable=captureOutput.isEmpty()?current(actual):EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(captureOutput.utf16()),ENUM_CURRENT_SETTINGS,&actual,0);
        if (readable) { response["width"] = int(actual.dmPelsWidth); response["height"] = int(actual.dmPelsHeight); }
        else if (error.isEmpty()) error = "Cannot verify the resulting display mode";
        if (!error.isEmpty()) response["error"] = error;
        send(response);
        if(fatalTopologyError)break;
    }
    if (!restore()) { std::cerr << "Display restoration failed; recovery record retained" << std::endl; return 2; }
    if (!recovery.finish()) return 3;
    CloseHandle(lease);
    return 0;
}
