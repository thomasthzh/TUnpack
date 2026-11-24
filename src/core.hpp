#pragma once
#include "input.hpp"
#include "passwords.hpp"
#include <bit7z/bit7zlibrary.hpp>
#include <mutex>

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
    // 0 = auto parallel threads for eligible archives, 1 = serial, >1 = explicit cap.
    unsigned threads=0;
    // Smart extraction: avoid double-nested folder when archive contains a single root folder.
    bool smartFolder=false;
    std::vector<std::wstring> includePatterns;
    std::vector<std::wstring> excludePatterns;
    std::wstring password;
    // Reports the actual unique root target before writing; never a nested target.
    std::function<void(const fs::path&)> outputReady;
    // All SDK-confirmed sources and the root archive's volumes, after readers close.
    std::function<bool(const std::vector<fs::path>&,const std::vector<fs::path>&)> retainSources;
};
struct EntryInfo {
    std::wstring path;
    uint64_t size=0;
    uint64_t packedSize=0;
    uint32_t crc=0;
    bool isDir=false;
    bool encrypted=false;
};
struct CompressOptions {
    unsigned level=5; // 0=Store, 1=Fastest, 3=Fast, 5=Normal, 7=Max, 9=Ultra
    std::wstring method; // empty=default, or L"zstd", L"lzma2", L"deflate", L"store"
    bool solid=true;
    unsigned threads=0; // 0=auto
};
struct Result {
    bool ok=false;
    fs::path output;
    fs::path failedFile;
    std::wstring type, note,status,engineError;
    std::vector<fs::path> sources;
    std::vector<EntryInfo> entries;
    unsigned archives=0, attempts=0;
    uint64_t bytes=0, offset=0;
    uint64_t scannedFiles=0,skippedFiles=0;
    bool apate=false, encrypted=false,recovered=false,passwordVerified=false;
};
class Engine {
    bit7z::Bit7zLibrary library_;
    PasswordBook& passwords_;
    mutable std::mutex batchMutex_;
    std::wstring batchPassword_;
    Result inspectInput(const std::vector<fs::path>& files,const Options& options,const Progress& progress,bool volumesResolved);
    void extractOne(const std::vector<fs::path>& files,const Options& options,const Progress& progress,
                    const std::wstring& preferred,unsigned depth,Result& result,std::vector<fs::path>& cleanup,std::vector<fs::path>& rootVolumes);
public:
    explicit Engine(PasswordBook& passwords,fs::path library=executableDir()/L"7z.dll");
    Result extract(const std::vector<fs::path>& files,const Options& options,const Progress& progress={});
    Result inspect(const std::vector<fs::path>& files,const Options& options,const Progress& progress={});
    Result list(const std::vector<fs::path>& files,const Options& options,const Progress& progress={});
    Result scan(const fs::path& directory,const Options& options,const Progress& progress,
                const std::function<void(const std::vector<fs::path>&,const Result&)>& found);
    Result restore(const std::vector<fs::path>& files,const Options& options,const Progress& progress={});
    Result compress(const std::vector<fs::path>& files,const fs::path& output,const std::wstring& password,const Progress& progress={},const std::function<void(const fs::path&)>& outputReady={},const CompressOptions& compressOptions={});
};
bool recycleFiles(const std::vector<fs::path>& files,const Progress& progress={});
}
