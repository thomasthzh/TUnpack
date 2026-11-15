#include "shell_open.hpp"
#include "storage.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>

using namespace lu;using namespace lu::ui;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::vector<fs::path> decode(const std::string& packet){return decodeOpenFiles(packet.data(),packet.size());}
static bool rejected(const std::string& packet){try{decode(packet);return false;}catch(const std::exception&){return true;}}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 2;
    struct Uninitialize{~Uninitialize(){CoUninitialize();}} uninitialize;
    auto root=uniquePath(fs::path(argv[1])/L"shell-locations");fs::create_directories(root);
    unsigned passed=0,total=0;auto check=[&](const char* name,const auto& test){++total;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    check("Empty open request represents activation without adding files",[]{require(decode(encodeOpenFiles({})).empty(),"activation request added a path");});
    check("Unicode, spaces and relative paths preserve order as absolute paths",[]{std::vector<fs::path> paths={L"generated/卷 三.docx",L"C:\\generated\\有 空格.mp4",L"\\\\server\\share\\last.003"};auto restored=decode(encodeOpenFiles(paths));require(restored.size()==paths.size(),"path count changed");for(size_t i=0;i<paths.size();++i)require(restored[i]==fs::absolute(paths[i]).lexically_normal(),"path/order changed");});
    check("Unknown version, truncation and extra data cannot admit partial requests",[]{auto good=encodeOpenFiles({L"C:\\generated\\first.zip"});auto wrong=good;wrong[0]='?';auto shortPacket=good;shortPacket.pop_back();require(rejected(wrong)&&rejected(shortPacket)&&rejected(good+"extra"),"malformed request partially accepted");});
    check("Relative, embedded-null and invalid UTF-8 payload paths are rejected",[]{auto frame=[](const std::wstring& path){std::string out="TUNPKO1\n";storage::put64(out,1);storage::putString(out,path);return out;};auto invalid=frame(L"C:\\invalid.zip");invalid.back()=char(0xff);require(rejected(frame(L"relative.zip"))&&rejected(frame(std::wstring(L"C:\\ok\0hidden.zip",16)))&&rejected(invalid),"invalid path accepted");});
    check("Oversized/count-mismatched requests fail before path admission",[]{std::string huge="TUNPKO1\n";storage::put64(huge,32769);auto valid=encodeOpenFiles({});require(rejected(huge)&&rejected(std::string(MaxOpenPacket+1,'x')),"request limit ignored");valid[8]=1;require(rejected(valid),"declared missing path accepted");});
    check("Independent mutex handles serialize the same isolated workspace",[]{auto name=L"Local\\TUnpack.Tests."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());{InstanceGate first(name);InstanceGate second(name);require(first.primary()&&!second.primary(),"two primary workspaces admitted");}InstanceGate reopened(name);require(reopened.primary(),"closed workspace left a stale gate");});
    check("Production instance identity is user scoped within the login session",[]{auto name=workspaceInstanceName();require(name.starts_with(L"Local\\TUnpack.Workspace.S-1-")&&name==workspaceInstanceName(),"user-scoped instance identity unstable");});
    check("Large file batch preserves every ordered path within a compact packet",[]{std::vector<fs::path> paths;for(unsigned i=0;i<5000;++i)paths.emplace_back(L"C:\\generated\\"+std::to_wstring(i)+L".mp4");auto packet=encodeOpenFiles(paths);require(packet.size()<MaxOpenPacket&&decode(packet)==paths,"large batch changed");std::cout<<"5000 file-open paths bytes: "<<packet.size()<<'\n';});
    check("Native result target prepares exact Unicode file selection without launching it",[&]{auto file=root/L"有 空格.exe";{std::ofstream out(file,std::ios::binary);out<<"public nonexecutable fixture";}ShellLocation location(file);require(location.action()==LocationAction::SelectItem&&location.path()==file,"result file became parent directory or executable action");require(fs::file_size(file)==28,"file content changed");});
    check("Output folder opens contents while explicit source location selects the folder",[&]{auto folder=root/L"结果 文件夹";fs::create_directories(folder);ShellLocation output(folder),source(folder,true);require(output.action()==LocationAction::OpenDirectory&&source.action()==LocationAction::SelectItem&&output.path()==source.path()&&source.path()==folder,"folder intent changed");});
    check("Missing exact item never falls back to an existing parent",[&]{auto missing=root/L"已经移动.mp4";for(bool locate:{false,true}){try{ShellLocation location(missing,locate);throw std::runtime_error("missing item accepted");}catch(const std::exception& error){require(std::string(error.what()).find(utf8(fs::absolute(missing).lexically_normal().wstring()))!=std::string::npos,"missing path silently substituted parent or lost diagnostic");}}require(fs::is_directory(root),"parent fixture missing");});
    check("Empty navigation cannot accidentally open the working directory",[]{try{ShellLocation location(fs::path{});throw std::runtime_error("empty target accepted");}catch(const std::exception& error){require(std::string(error.what())==utf8(L"没有可定位的路径。"),"empty path resolved to current directory");}});
    std::cout<<passed<<'/'<<total<<" file-open protocol, native location preparation and kernel gate checks passed; no Explorer execution, windows or GUI messages.\n";return passed==total?0:1;
}
