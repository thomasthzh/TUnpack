#pragma once
#include "input.hpp"
#include <shlobj.h>

namespace lu::ui {
enum class LocationAction {OpenDirectory,SelectItem};
class ShellLocation {
    fs::path path_;
    LocationAction action_;
    std::unique_ptr<ITEMIDLIST,decltype(&CoTaskMemFree)> item_{nullptr,CoTaskMemFree};
public:
    explicit ShellLocation(const fs::path& path,bool locate=false);
    const fs::path& path() const {return path_;}
    LocationAction action() const {return action_;}
    void open(HWND owner) const;
};
constexpr ULONG_PTR OpenFilesMessage=0x54554E50;
constexpr size_t MaxOpenPacket=2*1024*1024;
std::string encodeOpenFiles(const std::vector<fs::path>& paths);
std::vector<fs::path> decodeOpenFiles(const void* data,size_t size);
std::wstring workspaceInstanceName();
class InstanceGate {
    HANDLE handle_=nullptr;
    bool primary_=false;
public:
    explicit InstanceGate(const std::wstring& name);
    ~InstanceGate();
    InstanceGate(const InstanceGate&)=delete;
    InstanceGate& operator=(const InstanceGate&)=delete;
    bool primary() const {return primary_;}
};
enum class ForwardResult {Received,Unavailable,Unconfirmed,Rejected};
ForwardResult forwardOpenFiles(const std::wstring& marker,const std::vector<fs::path>& paths);
void activateWorkspace(HWND window);
}
