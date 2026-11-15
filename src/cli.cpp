#include "core.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <chrono>

static std::string json(const std::wstring& value) {
    std::string out="\"";
    for(unsigned char c:lu::utf8(value)) {
        if(c=='"'||c=='\\') {out+='\\';out+=char(c);}
        else if(c=='\n') out+="\\n";
        else if(c=='\r') out+="\\r";
        else if(c=='\t') out+="\\t";
        else if(c>=32) out+=char(c);
    }
    return out+'"';
}
int wmain(int argc,wchar_t** argv) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int code=1;
    try {
        if(argc<3) {
            std::cout<<"Usage: unpack_cli inspect|extract|restore FILE... [--out DIR] [--keep] [--no-nested] [--shallow] [--book FILE] [--password-file UTF8_TXT] [--progress]\n       unpack_cli scan DIRECTORY [--flat] [--shallow] [--book FILE] [--progress]\n       unpack_cli create7z|createzip OUTPUT INPUT...\n";
            CoUninitialize();return 2;
        }
        lu::Options options;
        bool showProgress=false;
        lu::fs::path book;
        std::vector<lu::fs::path> files;
        std::wstring command=argv[1];
        for(int i=2;i<argc;++i) {
            std::wstring arg=argv[i];
            if(arg==L"--out" && i+1<argc) options.output=argv[++i];
            else if(arg==L"--book" && i+1<argc) book=argv[++i];
            else if(arg==L"--password-file" && i+1<argc) {
                std::ifstream in(lu::fs::path(argv[++i]),std::ios::binary);
                if(!in) throw std::runtime_error("Cannot read password input");
                std::string data((std::istreambuf_iterator<char>(in)),{});
                if(data.starts_with("\xEF\xBB\xBF")) data.erase(0,3);
                auto passwords=lu::splitLines(lu::wide(data));
                if(!passwords.empty()) options.password=passwords.front();
            }
            else if(arg==L"--keep") options.recycle=false;
            else if(arg==L"--container") options.allowContainer=true;
            else if(arg==L"--no-nested") options.recursive=false;
            else if(arg==L"--shallow") options.deep=false;
            else if(arg==L"--progress") showProgress=true;
            else if(arg==L"--flat") options.scanRecursive=false;
            else if(arg==L"--verify") options.verifyData=true;
            else if(arg.starts_with(L"--")) throw std::runtime_error("Unknown option");
            else files.emplace_back(lu::fs::absolute(arg));
        }
        if(files.empty()) throw std::runtime_error("No files specified");
        lu::PasswordBook passwords(book);
        lu::Engine engine(passwords);
        auto start=std::chrono::steady_clock::now();
        lu::ProgressGate progressGate(std::chrono::seconds(1));
        lu::Progress progress;
        if(showProgress)progress=[&](std::wstring phase,uint64_t done,uint64_t total,lu::ProgressUnit unit){
            auto now=std::chrono::steady_clock::now();
            if(progressGate.accept(phase,done,total,unit,now)){
                std::cerr<<"{\"phase\":"<<json(phase)<<",\"done\":"<<done<<",\"total\":"<<total<<",\"unit\":\""<<(unit==lu::ProgressUnit::Bytes?"bytes":unit==lu::ProgressUnit::Files?"files":unit==lu::ProgressUnit::Candidates?"candidates":unit==lu::ProgressUnit::Items?"items":"none")<<"\"}\n";
            }
            return true;
        };
        lu::Result r;
        std::vector<std::pair<std::vector<lu::fs::path>,lu::Result>> entries;
        if(command==L"import-passwords") {passwords.importText(files.front());r.ok=true;r.note=L"已导入密码";r.bytes=passwords.count();}
        else if(command==L"inspect") r=engine.inspect(files,options,progress);
        else if(command==L"scan")r=engine.scan(files.front(),options,progress,[&](const auto& paths,const auto& result){entries.emplace_back(paths,result);});
        else if(command==L"extract") r=engine.extract(files,options,progress);
        else if(command==L"restore") r=engine.restore(files,options,progress);
        else if(command==L"create7z"||command==L"createzip") {
            if(files.size()<2) throw std::runtime_error("Create requires output and input");
            auto target=files.front(); files.erase(files.begin());
            target.replace_extension(command==L"createzip"?L".zip":L".7z");
            r=engine.compress(files,target,options.password);
        } else throw std::runtime_error("Unknown command");
        auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"{\"ok\":"<<(r.ok?"true":"false")<<",\"type\":"<<json(r.type)<<",\"output\":"<<json(r.output.wstring())
            <<",\"note\":"<<json(r.note)<<",\"archives\":"<<r.archives<<",\"attempts\":"<<r.attempts<<",\"bytes\":"<<r.bytes
            <<",\"offset\":"<<r.offset<<",\"apate\":"<<(r.apate?"true":"false")<<",\"encrypted\":"<<(r.encrypted?"true":"false")
            <<",\"recovered\":"<<(r.recovered?"true":"false")<<",\"password_checked\":"<<(r.passwordVerified?"true":"false")<<",\"status\":"<<json(r.status)<<",\"engine_error\":"<<json(r.engineError)<<",\"failed_file\":"<<json(r.failedFile.wstring())<<",\"ms\":"<<ms<<",\"sources\":[";
        for(size_t i=0;i<r.sources.size();++i){if(i)std::cout<<',';std::cout<<json(r.sources[i].wstring());}
        std::cout<<"],\"scanned_files\":"<<r.scannedFiles<<",\"skipped_files\":"<<r.skippedFiles<<",\"entries\":[";
        for(size_t i=0;i<entries.size();++i){if(i)std::cout<<',';const auto& [paths,item]=entries[i];
            std::cout<<"{\"files\":[";for(size_t j=0;j<paths.size();++j){if(j)std::cout<<',';std::cout<<json(paths[j].wstring());}
            std::cout<<"],\"ok\":"<<(item.ok?"true":"false")<<",\"type\":"<<json(item.type)<<",\"status\":"<<json(item.status)<<",\"note\":"<<json(item.note)<<",\"bytes\":"<<item.bytes<<",\"offset\":"<<item.offset<<",\"recovered\":"<<(item.recovered?"true":"false")<<",\"encrypted\":"<<(item.encrypted?"true":"false")<<"}";
        }
        std::cout<<"]}\n";
        code=r.ok?0:1;
    } catch(const std::exception& e) {std::cout<<"{\"ok\":false,\"note\":"<<json(lu::wide(e.what()))<<"}\n";}
    CoUninitialize();return code;
}
