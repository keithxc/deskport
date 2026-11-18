// SPDX-License-Identifier: GPL-3.0-or-later
// API doubles around the production installer implementation. No SCM or files.
#include <cassert>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>
using DWORD=std::uint32_t; using BYTE=unsigned char; using SC_HANDLE=void*;
using HKEY=void*; using PSECURITY_DESCRIPTOR=void*;
constexpr DWORD ERROR_SUCCESS=0, ERROR_ACCESS_DENIED=5, ERROR_INVALID_PARAMETER=87,
 ERROR_SERVICE_DOES_NOT_EXIST=1060, ERROR_SERVICE_NOT_ACTIVE=1062,
 ERROR_SERVICE_ALREADY_RUNNING=1056, ERROR_SERVICE_CANNOT_ACCEPT_CTRL=1061,
 ERROR_TIMEOUT=1460, SERVICE_STOPPED=1, SERVICE_RUNNING=4, SERVICE_STOP_PENDING=3,
 SC_STATUS_PROCESS_INFO=0, SERVICE_CONTROL_STOP=1, SERVICE_WIN32_OWN_PROCESS=16,
 SERVICE_AUTO_START=2, SERVICE_ERROR_NORMAL=1, SERVICE_NO_CHANGE=0xffffffff,
 SC_MANAGER_CONNECT=1, SC_MANAGER_CREATE_SERVICE=2, SERVICE_ALL_ACCESS=0xf01ff,
 RRF_RT_REG_DWORD=1, KEY_SET_VALUE=2, REG_DWORD=4, SDDL_REVISION_1=1,
 DACL_SECURITY_INFORMATION=4, SC_ACTION_RESTART=1, SC_ACTION_NONE=0,
 SERVICE_CONFIG_FAILURE_ACTIONS=2;
const HKEY HKEY_LOCAL_MACHINE=reinterpret_cast<HKEY>(1);
struct SERVICE_STATUS_PROCESS { DWORD dwCurrentState=SERVICE_STOPPED,dwWin32ExitCode=0; };
struct SERVICE_STATUS {};
struct QUERY_SERVICE_CONFIGW { DWORD dwServiceType; const wchar_t* lpBinaryPathName; const wchar_t* lpServiceStartName; };
struct SC_ACTION { DWORD Type,Delay; };
struct SERVICE_FAILURE_ACTIONSW { DWORD dwResetPeriod,cActions; SC_ACTION* lpsaActions; };
struct Model {
 bool exists=false,owned=false,protectedPath=true,stuck=false;
 DWORD state=SERVICE_STOPPED,exit=0,error=ERROR_ACCESS_DENIED,kind=SERVICE_WIN32_OWN_PROCESS;
 std::wstring command=L"\"C:\\Program Files\\DeskPort\\host\\deskport-display-service.exe\" --service",user=L"LocalSystem";
 std::string failure;
 std::vector<std::string> calls;
 std::uint64_t now=0;
 int handles=0;
} m;
bool api(const char* name) { m.calls.push_back(name); return m.failure!=name; }
DWORD GetLastError(){return m.error;}
auto GetTickCount64(){return m.now;}
void Sleep(DWORD ms){m.now+=ms;}
void CloseServiceHandle(SC_HANDLE){--m.handles;}
SC_HANDLE OpenSCManagerW(void*,void*,DWORD){if(!api("manager"))return nullptr;++m.handles;return reinterpret_cast<SC_HANDLE>(1);}
SC_HANDLE OpenServiceW(SC_HANDLE,const wchar_t*,DWORD){
 if(!api("open"))return 0;
 if(!m.exists){m.error=ERROR_SERVICE_DOES_NOT_EXIST;return 0;}
 ++m.handles;return reinterpret_cast<SC_HANDLE>(2);
}
bool QueryServiceStatusEx(SC_HANDLE,DWORD,BYTE* out,DWORD,DWORD*){
 if(!api("status"))return false;
 auto p=reinterpret_cast<SERVICE_STATUS_PROCESS*>(out);p->dwCurrentState=m.state;p->dwWin32ExitCode=m.exit;return true;
}
bool ControlService(SC_HANDLE,DWORD,SERVICE_STATUS*){
 if(!api("stop"))return false;
 m.state=m.stuck?SERVICE_STOP_PENDING:SERVICE_STOPPED;return true;
}
DWORD RegGetValueW(HKEY,const wchar_t*,const wchar_t*,DWORD,void*,DWORD* out,DWORD*){
 if(!api("ownership-read"))return ERROR_ACCESS_DENIED;
 *out=m.owned?1:0;return 0;
}
bool QueryServiceConfigW(SC_HANDLE,QUERY_SERVICE_CONFIGW* out,DWORD,DWORD* size){
 if(!api("config-read"))return false;
 *size=sizeof(*out);if(!out)return false;
 *out={m.kind,m.command.c_str(),m.user.c_str()};return true;
}
bool DeleteService(SC_HANDLE){if(!api("delete"))return false;m.exists=false;return true;}
DWORD RegOpenKeyExW(HKEY,const wchar_t*,DWORD,DWORD,HKEY* key){*key=reinterpret_cast<HKEY>(2);return 0;}
void RegDeleteValueW(HKEY,const wchar_t*){m.owned=false;}
void RegCloseKey(HKEY){}
SC_HANDLE CreateServiceW(SC_HANDLE,const wchar_t*,const wchar_t*,DWORD,DWORD,DWORD start,DWORD,
 const wchar_t* path,void*,void*,void*,const wchar_t* user,void*){
 if(!api("create"))return 0;
 assert(start==SERVICE_AUTO_START);m.exists=true;m.command=path;m.user=user;++m.handles;return reinterpret_cast<SC_HANDLE>(2);
}
bool ChangeServiceConfigW(SC_HANDLE,DWORD,DWORD start,DWORD,const wchar_t*,void*,void*,void*,void*,void*,void*){
 assert(start==SERVICE_AUTO_START);return api("config-write");
}
bool ConvertStringSecurityDescriptorToSecurityDescriptorW(const wchar_t* s,DWORD,PSECURITY_DESCRIPTOR* out,void*){
 assert(std::wstring(s)==L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00020085;;;AU)");
 if(!api("descriptor"))return false;
*out=reinterpret_cast<void*>(1);return true;
}
bool SetServiceObjectSecurity(SC_HANDLE,DWORD,void*){return api("dacl");}
void LocalFree(void*){}
bool ChangeServiceConfig2W(SC_HANDLE,DWORD,SERVICE_FAILURE_ACTIONSW* r){
 assert(r->dwResetPeriod==86400&&r->cActions==3);
 assert(r->lpsaActions[0].Type==SC_ACTION_RESTART&&r->lpsaActions[0].Delay==5000);
 assert(r->lpsaActions[1].Type==SC_ACTION_RESTART&&r->lpsaActions[1].Delay==30000);
 assert(r->lpsaActions[2].Type==SC_ACTION_NONE);
 return api("recovery");
}
DWORD RegCreateKeyExW(HKEY,const wchar_t*,DWORD,void*,DWORD,DWORD,void*,HKEY* key,void*){
 *key=reinterpret_cast<HKEY>(2);return api("key-create")?0:ERROR_ACCESS_DENIED;
}
DWORD RegSetValueExW(HKEY,const wchar_t*,DWORD,DWORD,const BYTE*,DWORD){
 if(!api("ownership-write"))return ERROR_ACCESS_DENIED;
 m.owned=true;return 0;
}
bool StartServiceW(SC_HANDLE,DWORD,void*){if(!api("start"))return false;m.state=m.stuck?2:SERVICE_RUNNING;return true;}
int _wcsicmp(const wchar_t* a,const wchar_t* b){return wcscasecmp(a,b);}
namespace deskport::display_service {
constexpr wchar_t ServiceName[]=L"DeskPortDisplayService",OwnershipValue[]=L"DisplayServiceOwned";
bool protectedExecutable(const std::wstring&){return m.protectedPath;}
std::wstring serviceCommand(const std::wstring& p){return L"\""+p+L"\" --service";}
}
// The runner generates this header from display-service-install.h, removing
// only its Windows-specific includes. Every installer function remains intact.
#include "production-service-install.h"
bool called(const std::string& name){for(auto& c:m.calls)if(c==name)return true;return false;}
void reset(bool installed=false){assert(m.handles==0);m=Model{};m.exists=m.owned=installed;m.state=installed?SERVICE_RUNNING:SERVICE_STOPPED;}
DWORD run(const std::wstring& op){auto r=deskport::display_service::installService(L"C:\\Program Files\\DeskPort",op);assert(m.handles==0);return r;}
int main(){
 reset();assert(!run(L"register"));assert(m.exists&&m.owned&&m.state==SERVICE_RUNNING&&called("dacl")&&called("recovery"));
 // Upgrade and rollback use the same ownership-checked path. No delete/recreate.
 for(int round=0;round<2;++round){m.calls.clear();assert(!run(L"register"));assert(called("stop")&&called("config-write")&&!called("create")&&!called("delete"));}
 assert(!run(L"stop"));assert(m.exists&&m.owned&&m.state==SERVICE_STOPPED);
 assert(!run(L"remove"));assert(!m.exists&&!m.owned);assert(!run(L"register"));assert(m.exists&&m.owned);
 reset();assert(!run(L"stop")&&!run(L"remove"));assert(!called("create")&&!called("stop")&&!called("delete"));
 for(int fact=0;fact<4;++fact){reset(true);if(fact==0)m.owned=false;if(fact==1)m.command+=L" foreign";if(fact==2)m.user=L"User";if(fact==3)m.kind=32;
  assert(run(L"remove")==ERROR_ACCESS_DENIED);assert(!called("stop")&&!called("delete")&&m.exists);}
 reset();m.protectedPath=false;assert(run(L"register")==ERROR_ACCESS_DENIED);assert(!called("manager"));
 reset(true);m.stuck=true;assert(run(L"register")==ERROR_TIMEOUT);assert(m.now==65000&&!called("config-write"));
 reset(true);m.exit=31;assert(run(L"register")==31);assert(!called("config-write"));
 for(const auto* failure:{"manager","open","config-read","status","stop","config-write"}){
  reset(true);m.failure=failure;assert(run(L"register")!=0);assert(m.exists&&!called("create")&&!called("delete"));
 }
 for(const auto* failure:{"create","descriptor","dacl","recovery","key-create","ownership-write","start"}){
  reset();m.failure=failure;assert(run(L"register")!=0);
  if(std::string(failure)=="start")assert(m.exists&&m.owned); // retained repair path
  else assert(!m.exists); // only our newly created service can be removed
 }
 reset();m.stuck=true;assert(run(L"register")==ERROR_TIMEOUT);assert(m.exists&&m.owned&&m.now==15000);
 reset(true);m.failure="delete";assert(run(L"remove")!=0);assert(m.exists&&m.owned);
 reset();assert(run(L"unknown")==ERROR_INVALID_PARAMETER);assert(m.calls.empty());
 std::cout<<"PASS production SCM lifecycle: install/upgrade/rollback/reinstall, foreign ownership, barriers, finite recovery, API failures and handle cleanup\n";
}
