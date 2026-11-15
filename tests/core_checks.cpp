#include "core.hpp"
#include <iostream>

int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int code=1;
    try{
        lu::fs::path source=lu::fs::absolute(argv[1]),carrier=lu::fs::absolute(argv[2]);
        lu::PasswordBook book(source.parent_path()/L"checks.dpapi");lu::Engine engine(book);
        lu::Options options;options.recycle=false;options.output=source.parent_path()/L"cancel-output";
        auto early=engine.inspect({carrier},options,[](std::wstring,uint64_t,uint64_t,lu::ProgressUnit){return false;});
        if(early.ok||!lu::fs::exists(carrier))throw std::runtime_error("Scan cancellation failed");
        bool finalReached=false;
        auto final=engine.extract({source},options,[&](std::wstring phase,uint64_t,uint64_t,lu::ProgressUnit){if(phase==L"准备完成任务"){finalReached=true;return false;}return true;});
        if(final.ok||!finalReached||!lu::fs::exists(source)||!lu::fs::is_directory(final.output))throw std::runtime_error("Cancellation at completion boundary failed");
        auto headerArchive=lu::uniquePath(source.parent_path()/L"metadata-only.7z");
        const std::wstring samplePassword=L"public-regression-password";
        book.add({samplePassword});
        if(!engine.compress({source},headerArchive,samplePassword).ok)throw std::runtime_error("Cannot create header encrypted fixture");
        auto metadata=engine.inspect({headerArchive},options,[](std::wstring phase,uint64_t,uint64_t,lu::ProgressUnit){return phase!=L"快速验证密码";});
        if(!metadata.ok||!metadata.encrypted||metadata.attempts!=1)throw std::runtime_error("Encrypted header metadata triggered redundant body decoding");
        auto scanFolder=lu::uniquePath(source.parent_path()/L"cancel-last-scan");lu::fs::create_directory(scanFolder);
        lu::fs::copy_file(source,scanFolder/L"last.mp4");bool scanCancelled=false;
        auto scanned=engine.scan(scanFolder,options,[&](std::wstring phase,uint64_t,uint64_t,lu::ProgressUnit){if(phase.starts_with(L"验证密码"))scanCancelled=true;return !scanCancelled;},{});
        if(scanned.ok||!scanCancelled)throw std::runtime_error("Scan cancellation on the final input was reported as success");
        std::cout<<"Cancellation during scan and before completion passed; all original fixtures retained.\n";code=0;
    }catch(const std::exception& e){std::cout<<e.what()<<'\n';}
    CoUninitialize();return code;
}
