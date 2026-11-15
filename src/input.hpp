#pragma once
#include <windows.h>
#include <filesystem>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <array>
#include <map>
#include <stdexcept>
#include <chrono>

namespace lu {
namespace fs = std::filesystem;
enum class ProgressUnit {None,Bytes,Files,Candidates,Items};
using Progress = std::function<bool(std::wstring,uint64_t,uint64_t,ProgressUnit)>;
struct Cancelled final:std::runtime_error {Cancelled():std::runtime_error("Cancelled") {}};
class ProgressGate {
    bool initialized_=false;
    std::wstring phase_;
    ProgressUnit unit_=ProgressUnit::None;
    uint64_t done_=0,total_=0;
    std::chrono::steady_clock::time_point published_{};
    std::chrono::milliseconds interval_;
public:
    explicit ProgressGate(std::chrono::milliseconds interval=std::chrono::milliseconds(90)):interval_(interval){}
    bool accept(const std::wstring& phase,uint64_t done,uint64_t total,ProgressUnit unit,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now());
};

std::string utf8(const std::wstring& s);
std::wstring wide(const std::string& s);
std::wstring lower(std::wstring s);
fs::path executableDir();
fs::path uniquePath(const fs::path& path);

class MappedFile {
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
public:
    const unsigned char* data = nullptr;
    uint64_t size = 0;
    fs::path path;
    explicit MappedFile(const fs::path& file);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
};

// All transformations share one read-only source. No whole-file copy or hash.
struct Input {
    std::vector<std::shared_ptr<MappedFile>> files;
    std::vector<uint64_t> starts;
    uint64_t physicalSize = 0;
    uint64_t size = 0;
    uint64_t offset = 0;
    uint32_t mask = 0;
    bool apate = false;
    bool nativeVolumes = false;
    std::wstring formatHint;
    uint64_t recoveryPrefix=0,recoveryBytes=0;
    std::array<unsigned char,32> recoveryHeader{};
    std::vector<unsigned char> recoveryTail;
    std::vector<unsigned char> recoveryProbe;
    uint32_t recoveryCrc=0,recoveryCrcMask=0;
    bool recoveryReverse=true,missingVolume=false;
    std::optional<uint64_t> zipPasswordOffset;
    bool headerPatched=false;
    uint64_t headerPatchAt=0;
    fs::path path;
    explicit Input(const std::vector<fs::path>& paths);
    size_t read(uint64_t pos, void* out, size_t count) const;
    unsigned char byte(uint64_t pos) const;
    Input slice(uint64_t start) const;
    std::vector<fs::path> paths() const;
    bool canUseNative() const;
private:
    size_t physicalRead(uint64_t pos, void* out, size_t count) const;
};

class InputBuffer : public std::streambuf {
    Input input_;
    uint64_t position_ = 0;
protected:
    std::streamsize xsgetn(char* s, std::streamsize n) override;
    int_type underflow() override;
    int_type uflow() override;
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode mode) override;
    pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override;
public:
    explicit InputBuffer(Input input) : input_(std::move(input)) {}
};

struct VolumeDirectory;
// A batch shares directory metadata; the snapshot dies with this resolver.
class VolumeResolver {
    std::map<std::wstring,std::shared_ptr<VolumeDirectory>> directories_;
    const VolumeDirectory& directory(const fs::path& path,const Progress& progress);
    std::optional<std::vector<fs::path>> findRenamed(const fs::path& requested,const Progress& progress);
public:
    std::vector<fs::path> resolve(const fs::path& file,const Progress& progress={});
};
std::vector<fs::path> resolveVolumes(const fs::path& file,const Progress& progress={});
std::vector<Input> identifyInputs(const Input& raw, const Progress& progress, bool deep);
bool findEmbedded(const Input& raw,const Progress& progress,const std::function<bool(const Input&)>& accept);
std::wstring signature(const Input& input);
uint32_t crc32(const void* data,size_t size);
struct SevenZipLayout { uint64_t end,headerOffset,headerSize;uint32_t headerCrc; };
std::optional<SevenZipLayout> sevenZipLayout(const Input& input);
bool archiveExtension(const fs::path& file);
}
