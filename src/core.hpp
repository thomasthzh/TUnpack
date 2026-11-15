#pragma once
#include "input.hpp"
#include "passwords.hpp"
#include <bit7z/bit7zlibrary.hpp>

namespace lu {
std::wstring errorText(const std::exception& error);
bool isCancelled(const std::exception& error);
struct Options {
    fs::path output;
    bool recursive=true;
    bool recycle=true;
    bool deep=true;
    bool allowContainer=false;
    bool scanRecursive=true;
    bool verifyData=false;
    // Only the current input set has been resolved; nested inputs set their own value.
    bool volumesResolved=false;
    std::wstring password;
    // Reports the actual unique root target before writing; never a nested target.
    std::function<void(const fs::path&)> outputReady;
    // All SDK-confirmed sources and the root archive's volumes, after readers close.
    std::function<bool(const std::vector<fs::path>&,const std::vector<fs::path>&)> retainSources;
};
struct Result {
    bool ok=false;
    fs::path output;
    fs::path failedFile;
    std::wstring type, note,status,engineError;
    std::vector<fs::path> sources;
    unsigned archives=0, attempts=0;
    uint64_t bytes=0, offset=0;
    uint64_t scannedFiles=0,skippedFiles=0;
    bool apate=false, encrypted=false,recovered=false,passwordVerified=false;
};
class Engine {
    bit7z::Bit7zLibrary library_;
    PasswordBook& passwords_;
    std::wstring batchPassword_;
    Result inspectInput(const std::vector<fs::path>& files,const Options& options,const Progress& progress,bool volumesResolved);
    void extractOne(const std::vector<fs::path>& files,const Options& options,const Progress& progress,
                    const std::wstring& preferred,unsigned depth,Result& result,std::vector<fs::path>& cleanup,std::vector<fs::path>& rootVolumes);
public:
    explicit Engine(PasswordBook& passwords,fs::path library=executableDir()/L"7z.dll");
    Result extract(const std::vector<fs::path>& files,const Options& options,const Progress& progress={});
    Result inspect(const std::vector<fs::path>& files,const Options& options,const Progress& progress={});
    Result scan(const fs::path& directory,const Options& options,const Progress& progress,
                const std::function<void(const std::vector<fs::path>&,const Result&)>& found);
    Result restore(const std::vector<fs::path>& files,const Options& options,const Progress& progress={});
    Result compress(const std::vector<fs::path>& files,const fs::path& output,const std::wstring& password,const Progress& progress={},const std::function<void(const fs::path&)>& outputReady={});
};
bool recycleFiles(const std::vector<fs::path>& files,const Progress& progress={});
}
