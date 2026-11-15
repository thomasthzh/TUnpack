#include <windows.h>
#include <array>
#include <iostream>
#include <vector>
#include <string>

int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(!module){std::cout<<"Resource module could not be read\n";return 1;}
    bool ok=true;
    auto group=FindResourceW(module,MAKEINTRESOURCEW(101),RT_GROUP_ICON);
    if(!group)ok=false;
    else {
        auto resource=LoadResource(module,group);auto bytes=static_cast<const WORD*>(LockResource(resource));
        if(!bytes||SizeofResource(module,group)<6||bytes[2]!=9)ok=false;
    }
    for(auto size:std::array<int,9>{16,20,24,32,40,48,64,128,256}){
        auto icon=static_cast<HICON>(LoadImageW(module,MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));
        ICONINFO info{};BITMAP bitmap{};
        if(!icon||!GetIconInfo(icon,&info)||!GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap)||bitmap.bmWidth!=size||bitmap.bmHeight!=size)ok=false;
        if(info.hbmMask)DeleteObject(info.hbmMask);if(info.hbmColor)DeleteObject(info.hbmColor);if(icon)DestroyIcon(icon);
    }
    FreeLibrary(module);
    DWORD handle=0;auto count=GetFileVersionInfoSizeW(argv[1],&handle);std::vector<unsigned char> version(count);
    if(!count||!GetFileVersionInfoW(argv[1],0,count,version.data()))ok=false;
    else for(const auto* key:{L"ProductName",L"FileDescription",L"OriginalFilename"}){
        std::wstring query=L"\\StringFileInfo\\040904B0\\"+std::wstring(key);void* data=nullptr;UINT chars=0;
        std::wstring expected=std::wstring(key)==L"OriginalFilename"?L"T解压.exe":L"T解压";
        if(!VerQueryValueW(version.data(),query.c_str(),&data,&chars)||!data||std::wstring(static_cast<wchar_t*>(data))!=expected)ok=false;
    }
    std::cout<<(ok?"PASS":"FAIL")<<" TUnpack brand metadata and nine native ICO sizes; no window created\n";
    return ok?0:1;
}
