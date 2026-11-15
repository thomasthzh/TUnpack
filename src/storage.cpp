#include "storage.hpp"
#include <shlobj.h>
#include <fstream>
#include <stdexcept>
#include <climits>

namespace lu::storage {
void put64(std::string& out,uint64_t n){for(unsigned i=0;i<8;++i)out.push_back(char(n>>(i*8)));}
uint64_t get64(const std::string& in,size_t& pos){
    if(pos>in.size()||in.size()-pos<8)throw std::runtime_error("Invalid saved data");
    uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(static_cast<unsigned char>(in[pos++]))<<(i*8);return n;
}
void putString(std::string& out,const std::wstring& value){auto bytes=utf8(value);put64(out,bytes.size());out+=bytes;}
std::wstring getString(const std::string& in,size_t& pos){
    auto n=get64(in,pos);if(n>in.size()-pos)throw std::runtime_error("Invalid saved data");
    if(n>INT_MAX)throw std::runtime_error("Saved text is too large");
    if(!n)return {};
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,in.data()+pos,int(n),nullptr,0);if(!length)throw std::runtime_error("Invalid saved UTF-8 text");
    std::wstring value(size_t(length),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,in.data()+pos,int(n),value.data(),length);pos+=size_t(n);return value;
}
fs::path dataPath(const wchar_t* name){
    PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))throw std::runtime_error("Cannot locate application data");
    auto result=fs::path(local)/L"T解压"/name;CoTaskMemFree(local);return result;
}
void atomicWrite(const fs::path& file,const std::string& data){
    if(!file.parent_path().empty())fs::create_directories(file.parent_path());
    auto temp=file;temp+=L".tmp";std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(data.data(),std::streamsize(data.size()));out.close();
    if(!out||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit application data");
}
}
