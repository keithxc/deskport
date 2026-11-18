// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <setupapi.h>
#include <string>
#include <vector>
#include "display-dpi-policy.h"
namespace deskport::display_dpi {
// These per-source DPI packet types are used by Windows Settings but are not a
// public SDK contract. Keep them isolated, size-checked and fail closed on any
// unsupported packet/range. Never substitute global DPI/registry changes.
struct GetPacket { DISPLAYCONFIG_DEVICE_INFO_HEADER header; int minimum,current,maximum; };
struct SetPacket { DISPLAYCONFIG_DEVICE_INFO_HEADER header; int relative; };
static_assert(sizeof(GetPacket)==32&&sizeof(SetPacket)==24,"Unexpected CCD DPI packet layout");
struct Source { LUID adapter{}; UINT32 id=0; };
inline bool same(LUID a,LUID b) {return a.LowPart==b.LowPart&&a.HighPart==b.HighPart;}
inline bool ownedAdapter(LUID adapter,const std::wstring& instance) {
    DISPLAYCONFIG_ADAPTER_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
    name.header.size=sizeof(name);name.header.adapterId=adapter;
    if(DisplayConfigGetDeviceInfo(&name.header))return false;
    auto devices=SetupDiCreateDeviceInfoList(nullptr,nullptr);if(devices==INVALID_HANDLE_VALUE)return false;
    SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);DWORD bytes=0;
    bool result=false;
    if(SetupDiOpenDeviceInterfaceW(devices,name.adapterDevicePath,0,&iface)) {
        SetupDiGetDeviceInterfaceDetailW(devices,&iface,nullptr,0,&bytes,nullptr);
        if(bytes>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)&&bytes<=65536) {
            std::vector<BYTE> buffer(bytes);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
            wchar_t actual[1024]{},hardware[4096]{};
            result=SetupDiGetDeviceInterfaceDetailW(devices,&iface,detail,bytes,nullptr,&device)&&
                SetupDiGetDeviceInstanceIdW(devices,&device,actual,1024,nullptr)&&!_wcsicmp(instance.c_str(),actual)&&
                SetupDiGetDeviceRegistryPropertyW(devices,&device,SPDRP_HARDWAREID,nullptr,reinterpret_cast<PBYTE>(hardware),sizeof(hardware),nullptr)&&
                !_wcsicmp(hardware,L"Root\\MttVDD");
        }
    }
    SetupDiDestroyDeviceInfoList(devices);return result;
}
inline DWORD ownedSource(const std::wstring& instance,Source& source) {
    if(instance.rfind(L"ROOT\\DESKPORTVIRTUALDISPLAY\\",0)!=0)return ERROR_ACCESS_DENIED;
    for(int attempt=0;attempt<5;++attempt) {
        UINT32 np=0,nm=0;LONG rc=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,&nm);
        if(rc)return DWORD(rc);if(!np||np>64||nm>512)return ERROR_INVALID_DATA;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
        rc=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE,&np,paths.data(),&nm,modes.data(),nullptr);
        if(rc==ERROR_INSUFFICIENT_BUFFER)continue;if(rc)return DWORD(rc);paths.resize(np);
        unsigned found=0;
        for(const auto& path:paths) {
            if(!ownedAdapter(path.targetInfo.adapterId,instance))continue;
            // A cloned physical source must never receive an owned-target write.
            if(!same(path.sourceInfo.adapterId,path.targetInfo.adapterId))return ERROR_ACCESS_DENIED;
            source={path.sourceInfo.adapterId,path.sourceInfo.id};++found;
        }
        if(found!=1)return ERROR_NOT_FOUND;
        for(const auto& path:paths)
            if(same(path.sourceInfo.adapterId,source.adapter)&&path.sourceInfo.id==source.id&&
               !ownedAdapter(path.targetInfo.adapterId,instance))return ERROR_ACCESS_DENIED;
        return ERROR_SUCCESS;
    }
    return ERROR_RETRY;
}
inline DWORD query(Source source,Range& range) {
    GetPacket packet{};packet.header.type=static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-3);
    packet.header.size=sizeof(packet);packet.header.adapterId=source.adapter;packet.header.id=source.id;
    const auto rc=DisplayConfigGetDeviceInfo(&packet.header);if(rc)return DWORD(rc);
    range={packet.minimum,packet.current,packet.maximum};return valid(range)?ERROR_SUCCESS:ERROR_NOT_SUPPORTED;
}
inline DWORD effectiveDpi(Source source,UINT& dpi) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    name.header.size=sizeof(name);name.header.adapterId=source.adapter;name.header.id=source.id;
    auto rc=DisplayConfigGetDeviceInfo(&name.header);if(rc)return DWORD(rc);
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(!EnumDisplaySettingsExW(name.viewGdiDeviceName,ENUM_CURRENT_SETTINGS,&mode,0)||!mode.dmPelsWidth||!mode.dmPelsHeight)return ERROR_NOT_READY;
    auto window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"DeskPort DPI observation",WS_POPUP,
        mode.dmPosition.x+10,mode.dmPosition.y+10,20,20,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return GetLastError();dpi=GetDpiForWindow(window);DestroyWindow(window);
    return dpi?ERROR_SUCCESS:ERROR_INVALID_STATE;
}
inline DWORD set(Source source,int value) {
    Range before{};auto rc=query(source,before);if(rc)return rc;
    if(value<before.minimum||value>before.maximum)return ERROR_NOT_SUPPORTED;
    if(before.current!=value) {
        SetPacket packet{};packet.header.type=static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-4);
        packet.header.size=sizeof(packet);packet.header.adapterId=source.adapter;packet.header.id=source.id;packet.relative=value;
        rc=DWORD(DisplayConfigSetDeviceInfo(&packet.header));if(rc)return rc;
    }
    // CCD can report the requested step even when Windows clamps the effective
    // UI DPI to a lower value. A real per-monitor-aware window must agree too.
    for(int i=0;i<30;++i) {Range actual{};UINT dpi=0;rc=query(source,actual);
        if(!rc&&actual.current==value&&!effectiveDpi(source,dpi)&&dpi==UINT(percent(actual)*96/100))return 0;
        Sleep(50);
    }
    return rc?rc:ERROR_INVALID_STATE;
}
}
