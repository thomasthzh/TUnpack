#include "shell_open.hpp"
#include "storage.hpp"
#include <sddl.h>
#include <shellapi.h>
#include <stdexcept>

namespace lu::ui {
ShellLocation::ShellLocation(const fs::path& path,bool locate){
    if(path.empty())throw std::runtime_error(utf8(L"没有可定位的路径。"));
    path_=fs::absolute(path).lexically_normal();
    auto attributes=GetFileAttributesW(path_.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES)throw std::runtime_error(utf8(L"路径不存在或无法访问："+path_.wstring()));
    action_=!locate&&(attributes&FILE_ATTRIBUTE_DIRECTORY)?LocationAction::OpenDirectory:LocationAction::SelectItem;
    if(action_==LocationAction::SelectItem){
        PIDLIST_ABSOLUTE item=nullptr;auto status=SHParseDisplayName(path_.c_str(),nullptr,&item,0,nullptr);item_.reset(item);
        if(FAILED(status)||!item_)throw std::runtime_error(utf8(L"无法在文件管理器中定位："+path_.wstring()));
    }
}
void ShellLocation::open(HWND owner) const {
    if(action_==LocationAction::OpenDirectory){
        auto opened=reinterpret_cast<INT_PTR>(ShellExecuteW(owner,L"open",path_.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
        if(opened<=32)throw std::runtime_error(utf8(L"无法打开目录，请检查权限或路径："+path_.wstring()));
    }else if(FAILED(SHOpenFolderAndSelectItems(item_.get(),0,nullptr,0)))throw std::runtime_error(utf8(L"无法在文件管理器中定位："+path_.wstring()));
}
namespace {
constexpr char Magic[]="TUNPKO1\n";
void checkPath(const fs::path& path){if(path.empty()||!path.is_absolute()||path.native().find(L'\0')!=std::wstring::npos)throw std::runtime_error("Invalid file-open path");}
}
std::string encodeOpenFiles(const std::vector<fs::path>& paths){
    if(paths.size()>32768)throw std::runtime_error("Too many file-open paths");std::string packet=Magic;storage::put64(packet,paths.size());
    for(const auto& path:paths){if(path.empty())throw std::runtime_error("Empty file-open path");auto absolute=fs::absolute(path).lexically_normal();checkPath(absolute);storage::putString(packet,absolute.wstring());if(packet.size()>MaxOpenPacket)throw std::runtime_error("File-open request exceeds 2 MiB");}return packet;
}
std::vector<fs::path> decodeOpenFiles(const void* data,size_t size){
    if(!data||size>MaxOpenPacket||size<sizeof(Magic)-1)throw std::runtime_error("Invalid file-open request");std::string packet(static_cast<const char*>(data),size);
    if(packet.compare(0,sizeof(Magic)-1,Magic)!=0)throw std::runtime_error("Unsupported file-open request");size_t position=sizeof(Magic)-1;auto count=storage::get64(packet,position);if(count>32768)throw std::runtime_error("Too many file-open paths");
    std::vector<fs::path> paths;paths.reserve(size_t(count));for(uint64_t i=0;i<count;++i){fs::path path=storage::getString(packet,position);checkPath(path);paths.push_back(path.lexically_normal());}if(position!=packet.size())throw std::runtime_error("Trailing file-open data");return paths;
}
std::wstring workspaceInstanceName(){
    HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot identify workspace user");
    struct Close {HANDLE handle;~Close(){CloseHandle(handle);}} close{token};DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<unsigned char> buffer(size);
    if(!GetTokenInformation(token,TokenUser,buffer.data(),size,&size))throw std::runtime_error("Cannot identify workspace user");LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid))throw std::runtime_error("Cannot identify workspace user");std::wstring name=L"Local\\TUnpack.Workspace.";name+=sid;LocalFree(sid);return name;
}
InstanceGate::InstanceGate(const std::wstring& name){handle_=CreateMutexW(nullptr,FALSE,name.c_str());auto error=GetLastError();if(!handle_)throw std::runtime_error("Cannot lock workspace; check existing window permissions");primary_=error!=ERROR_ALREADY_EXISTS;}
InstanceGate::~InstanceGate(){if(handle_)CloseHandle(handle_);}
void activateWorkspace(HWND window){
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);auto popup=GetLastActivePopup(window);SetForegroundWindow(IsWindowVisible(popup)?popup:window);
}
ForwardResult forwardOpenFiles(const std::wstring& marker,const std::vector<fs::path>& paths){
    struct Search {const wchar_t* marker;HWND window=nullptr;} search{marker.c_str()};
    auto deadline=GetTickCount64()+2000;
    do {EnumWindows([](HWND window,LPARAM parameter)->BOOL{auto& found=*reinterpret_cast<Search*>(parameter);if(GetPropW(window,found.marker)){found.window=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));if(search.window)break;Sleep(25);}while(GetTickCount64()<deadline);
    if(!search.window)return ForwardResult::Unavailable;DWORD process=0;GetWindowThreadProcessId(search.window,&process);AllowSetForegroundWindow(process);
    auto packet=encodeOpenFiles(paths);COPYDATASTRUCT payload{OpenFilesMessage,DWORD(packet.size()),packet.data()};DWORD_PTR accepted=0;
    if(!SendMessageTimeoutW(search.window,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&payload),SMTO_ABORTIFHUNG|SMTO_BLOCK|SMTO_ERRORONEXIT,2000,&accepted))return ForwardResult::Unconfirmed;
    return accepted?ForwardResult::Received:ForwardResult::Rejected;
}
}
