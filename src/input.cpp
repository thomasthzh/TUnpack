#include "input.hpp"
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace lu {
bool ProgressGate::accept(const std::wstring& phase,uint64_t done,uint64_t total,ProgressUnit unit,std::chrono::steady_clock::time_point now){
    bool changed=!initialized_||phase!=phase_||unit!=unit_||done<done_||total!=total_;
    bool publish=changed||now-published_>=interval_;
    initialized_=true;if(phase_!=phase)phase_=phase;unit_=unit;done_=done;total_=total;if(publish)published_=now;return publish;
}

std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);
    return out;
}
std::wstring lower(std::wstring s) { std::transform(s.begin(),s.end(),s.begin(),towlower); return s; }
fs::path executableDir() {
    std::wstring s(32768,L'\0');
    s.resize(GetModuleFileNameW(nullptr,s.data(),DWORD(s.size())));
    return fs::path(s).parent_path();
}
fs::path uniquePath(const fs::path& path) {
    if (!fs::exists(path)) return path;
    for (unsigned i=2;;++i) {
        auto p=path.parent_path()/(path.stem().wstring()+L" ("+std::to_wstring(i)+L")"+path.extension().wstring());
        if (!fs::exists(p)) return p;
    }
}
MappedFile::MappedFile(const fs::path& p) : path(fs::absolute(p)) {
    file_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file_==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot read input file (locked or unavailable)");
    LARGE_INTEGER n{};
    if (!GetFileSizeEx(file_,&n)) { CloseHandle(file_); throw std::runtime_error("Cannot read input length"); }
    size=uint64_t(n.QuadPart);
    if (!size) return;
    mapping_=CreateFileMappingW(file_,nullptr,PAGE_READONLY,0,0,nullptr);
    if (mapping_) data=static_cast<const unsigned char*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,0));
    if (!data) {
        if (mapping_) CloseHandle(mapping_);
        CloseHandle(file_);
        throw std::runtime_error("Cannot map input file");
    }
}
MappedFile::~MappedFile() {
    if (data) UnmapViewOfFile(data);
    if (mapping_) CloseHandle(mapping_);
    if (file_!=INVALID_HANDLE_VALUE) CloseHandle(file_);
}
static std::optional<uint32_t> splitZipDisks(const MappedFile& file);
Input::Input(const std::vector<fs::path>& paths) {
    if (paths.empty()) throw std::runtime_error("No input volumes");
    path=fs::absolute(paths.front());
    for (const auto& p:paths) {
        starts.push_back(physicalSize);
        auto m=std::make_shared<MappedFile>(p);
        physicalSize+=m->size;
        files.push_back(std::move(m));
    }
    size=physicalSize;
    if(files.size()>1){
        auto disks=splitZipDisks(*files.back());
        if(disks&&*disks==files.size()){
            // Native ZIP offsets are relative to a disk. Open the directory
            // volume, while retaining physical order for the actual source set.
            nativeVolumes=true;formatHint=L"zip";path=files.back()->path;
        }
    }
}
size_t Input::physicalRead(uint64_t pos,void* out,size_t count) const {
    if (pos>=physicalSize) return 0;
    size_t done=0;
    while (done<count && pos<physicalSize) {
        size_t i=size_t(std::upper_bound(starts.begin(),starts.end(),pos)-starts.begin()-1);
        auto available=files[i]->size-(pos-starts[i]);
        auto n=size_t(std::min<uint64_t>(count-done,available));
        if (!n) break;
        std::memcpy(static_cast<char*>(out)+done,files[i]->data+(pos-starts[i]),n);
        done+=n; pos+=n;
    }
    return done;
}
size_t Input::read(uint64_t pos,void* out,size_t count) const {
    if (pos>=size) return 0;
    count=size_t(std::min<uint64_t>(count,size-pos));
    pos+=offset;
    if(headerPatched){
        size_t done=0;
        if(pos<headerPatchAt){auto n=size_t(std::min<uint64_t>(count,headerPatchAt-pos));done=physicalRead(pos,out,n);pos+=done;}
        if(done<count&&pos>=headerPatchAt&&pos<headerPatchAt+32){auto n=size_t(std::min<uint64_t>(count-done,headerPatchAt+32-pos));std::memcpy(static_cast<unsigned char*>(out)+done,recoveryHeader.data()+pos-headerPatchAt,n);done+=n;pos+=n;}
        return done+physicalRead(pos,static_cast<unsigned char*>(out)+done,count-done);
    }
    if(recoveryPrefix) {
        size_t done=0;
        while(done<count) {
            auto destination=static_cast<unsigned char*>(out)+done;size_t n=0;
            if(pos<32){n=size_t(std::min<uint64_t>(count-done,32-pos));std::memcpy(destination,recoveryHeader.data()+pos,n);}
            else if(pos<recoveryPrefix)break; // Missing bytes are never invented while decoding file data.
            else if(pos-recoveryPrefix<recoveryBytes){
                auto at=pos-recoveryPrefix;n=size_t(std::min<uint64_t>(count-done,recoveryBytes-at));
                if(recoveryReverse)std::reverse_copy(files[0]->data+physicalSize-at-n,files[0]->data+physicalSize-at,destination);
                else std::memcpy(destination,files[0]->data+at,n);
            }else {
                auto at=pos-recoveryPrefix-recoveryBytes;
                if(at>=recoveryTail.size())break;
                n=std::min(count-done,recoveryTail.size()-size_t(at));std::memcpy(destination,recoveryTail.data()+at,n);
            }
            done+=n;pos+=n;
        }
        return done;
    }
    size_t done=0;
    if (apate) {
        uint64_t original=physicalSize-mask-4;
        uint64_t saved=std::min<uint64_t>(mask,original);
        uint64_t at=std::max<uint64_t>(mask,original);
        if (pos<saved) {
            done=size_t(std::min<uint64_t>(count,saved-pos));
            const auto* src=files[0]->data+at+saved-pos-done;
            std::reverse_copy(src,src+done,static_cast<unsigned char*>(out));
            pos+=done;
        }
    }
    return done+physicalRead(pos,static_cast<char*>(out)+done,count-done);
}
unsigned char Input::byte(uint64_t pos) const { unsigned char c=0; read(pos,&c,1); return c; }
Input Input::slice(uint64_t start) const {
    Input result=*this;
    result.offset+=start;
    result.size-=start;
    if(result.zipPasswordOffset){if(*result.zipPasswordOffset>=start)*result.zipPasswordOffset-=start;else result.zipPasswordOffset.reset();}
    return result;
}
std::vector<fs::path> Input::paths() const { std::vector<fs::path> p; for(const auto& f:files) p.push_back(f->path); return p; }
bool Input::canUseNative() const { return !apate && !offset && !recoveryPrefix && !headerPatched && (files.size()==1||nativeVolumes); }
std::streamsize InputBuffer::xsgetn(char* s,std::streamsize n) {
    if(n<=0) return 0;
    auto count=input_.read(position_,s,size_t(n)); position_+=count; return std::streamsize(count);
}
InputBuffer::int_type InputBuffer::underflow() { unsigned char value=0;return input_.read(position_,&value,1)==1?int_type(value):traits_type::eof(); }
InputBuffer::int_type InputBuffer::uflow() { auto c=underflow(); if(c!=traits_type::eof()) ++position_; return c; }
InputBuffer::pos_type InputBuffer::seekoff(off_type off,std::ios_base::seekdir dir,std::ios_base::openmode mode) {
    if (!(mode & std::ios_base::in)) return pos_type(off_type(-1));
    int64_t base=dir==std::ios_base::end ? int64_t(input_.size) : dir==std::ios_base::cur ? int64_t(position_) : 0;
    if ((off<0 && off < -base) || (off>=0 && uint64_t(off)>input_.size-uint64_t(base))) return pos_type(off_type(-1));
    position_=uint64_t(base+off); return pos_type(position_);
}
InputBuffer::pos_type InputBuffer::seekpos(pos_type pos,std::ios_base::openmode mode) { return seekoff(off_type(pos),std::ios_base::beg,mode); }

static uint32_t u32(const unsigned char* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
static std::optional<uint32_t> splitZipDisks(const MappedFile& file){
    if(file.size<22)return {};
    auto length=size_t(std::min<uint64_t>(file.size,65557+20));
    auto tail=file.data+file.size-length;
    auto u16=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
    for(size_t at=length-22+1;at-->0;){
        const auto* end=tail+at;
        if(u32(end)!=0x06054b50||at+22+u16(end+20)!=length)continue;
        auto disk=u16(end+4),directoryDisk=u16(end+6);
        if(!disk||directoryDisk>disk)continue;
        if(at>=20&&u32(end-20)==0x07064b50){
            auto count=u32(end-4),recordDisk=u32(end-16);
            if(count>1&&recordDisk<count&&(disk==0xffff||disk==count-1))return count;
        }else if(disk!=0xffff)return disk+1;
    }
    return {};
}
static uint32_t crcUpdate(uint32_t crc,const unsigned char* data,size_t size) {
    static const auto table=[]{
        std::array<uint32_t,256> t{};
        for(uint32_t i=0;i<256;++i){
            uint32_t c=i;
            for(unsigned bit=0;bit<8;++bit)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));
            t[i]=c;
        }
        return t;
    }();
    for(size_t i=0;i<size;++i)crc=(crc>>8)^table[(crc^data[i])&0xffu];
    return crc;
}
uint32_t crc32(const void* data,size_t size){return crcUpdate(0xffffffffu,static_cast<const unsigned char*>(data),size)^0xffffffffu;}
static void sevenZipHeader(unsigned char* header,uint64_t offset,const std::vector<unsigned char>& tail){
    std::memcpy(header,"7z\xbc\xaf\x27\x1c\0\x04",8);
    auto put=[&](size_t at,uint64_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)header[at+i]=static_cast<unsigned char>(value>>(i*8));};
    put(12,offset,8);put(20,tail.size(),8);put(28,crc32(tail.data(),tail.size()),4);put(8,crc32(header+12,20),4);
}
static bool contiguousSingleFile(const Input& raw){
    return raw.files.size()==1&&!raw.apate&&!raw.recoveryPrefix&&!raw.headerPatched&&raw.files[0]->data;
}
static std::optional<Input> recoverSevenZipContinuation(const Input& raw){
    if(raw.files.size()!=1||raw.offset||raw.apate||raw.size<32)return {};
    auto length=size_t(std::min<uint64_t>(1024*1024,raw.size));auto start=raw.size-length;
    std::vector<unsigned char> owned;
    const unsigned char* tail=nullptr;
    if(contiguousSingleFile(raw)){
        tail=raw.files[0]->data+start;
    }else{
        owned.resize(length);raw.read(start,owned.data(),length);tail=owned.data();
    }
    if(tail[length-1]!=0||tail[length-2]!=0)return {};
    for(size_t index=length-3;index-->0;){
        bool plain=tail[index]==1&&tail[index+1]==4&&tail[index+2]==6;
        bool encoded=tail[index]==0x17&&tail[index+1]==6;
        if(!plain&&!encoded)continue;
        size_t at=index+(plain?3:2);bool valid=true;
        auto byte=[&]()->unsigned{if(at>=length){valid=false;return 0;}return tail[at++];};
        auto number=[&](){auto first=byte();uint64_t value=0;unsigned mask=128;for(unsigned i=0;i<8;++i,mask>>=1){if(!(first&mask))return value|(uint64_t(first&(mask-1))<<(i*8));value|=uint64_t(byte())<<(i*8);}return value;};
        auto position=number(),streams=number();if(!streams||streams>32||byte()!=9)continue;
        uint64_t packed=0;
        for(uint64_t stream=0;stream<streams;++stream){auto size=number();if(size>UINT64_MAX-packed){valid=false;break;}packed+=size;}
        if(!valid||position>UINT64_MAX-32||packed>UINT64_MAX-32-position)continue;
        auto logical=32+position+packed,physical=start+index;
        if(logical<32)continue;
        if(logical<=physical){
            if(sevenZipLayout(raw))continue;
            auto base=physical-logical;auto input=raw.slice(base);
            input.headerPatched=true;input.headerPatchAt=base;input.recoveryReverse=false;input.formatHint=L"7z";
            std::vector<unsigned char> header(tail+index,tail+length);
            sevenZipHeader(input.recoveryHeader.data(),logical-32,header);
            return input;
        }
        auto input=raw;input.recoveryPrefix=logical-physical;input.recoveryBytes=physical;
        input.recoveryReverse=false;input.missingVolume=input.recoveryPrefix>32;input.formatHint=L"7z";
        input.recoveryTail.assign(tail+index,tail+length);
        input.size=logical+input.recoveryTail.size();sevenZipHeader(input.recoveryHeader.data(),logical-32,input.recoveryTail);
        return input;
    }
    return {};
}
static std::optional<Input> recoverReversedSevenZip(const Input& raw) {
    if(raw.files.size()!=1||raw.offset||raw.apate||raw.size<64)return {};
    auto length=size_t(std::min<uint64_t>(4096,raw.size));std::vector<unsigned char> first(length);
    raw.read(0,first.data(),length);
    for(size_t end=16;end<=length;++end) {
        if(first[end-1]!=0x17||first[end-2]!=0x06)continue;
        std::vector<unsigned char> h(first.rbegin()+ptrdiff_t(length-end),first.rend());
        size_t at=0;bool valid=true;
        auto byte=[&]()->unsigned {if(at>=h.size()){valid=false;return 0;}return h[at++];};
        auto expect=[&](unsigned value){if(byte()!=value)valid=false;};
        auto number=[&](){auto firstByte=byte();uint64_t value=0;unsigned mask=0x80;for(unsigned i=0;i<8;++i,mask>>=1){if(!(firstByte&mask))return value|(uint64_t(firstByte&(mask-1))<<(i*8));value|=uint64_t(byte())<<(i*8);}return value;};
        expect(0x17);expect(0x06);auto packPosition=number();auto afterPosition=at;if(number()!=1)valid=false;
        expect(0x09);auto packedSize=number();expect(0);expect(0x07);expect(0x0b);
        if(number()!=1)valid=false;
        expect(0);auto coders=number();
        if(!valid||!coders||coders>32)continue;
        uint64_t inputs=0,outputs=0;bool aes=false;
        for(uint64_t coder=0;coder<coders&&valid;++coder) {
            auto flags=byte(),idBytes=flags&15;
            if(!idBytes||(flags&0x80)||at+idBytes>h.size()){valid=false;break;}
            aes|=idBytes==4&&std::memcmp(h.data()+at,"\x06\xf1\x07\x01",4)==0;at+=idBytes;
            auto in=(flags&0x10)?number():1,out=(flags&0x10)?number():1;
            if(!in||!out||in>32||out>32){valid=false;break;}inputs+=in;outputs+=out;
            if(flags&0x20){auto properties=number();if(properties>h.size()-at){valid=false;break;}at+=size_t(properties);}
        }
        if(!valid||!aes||!outputs||outputs>32||inputs<outputs||inputs-outputs+1!=1)continue;
        for(uint64_t bind=1;bind<outputs;++bind){if(number()>=inputs||number()>=outputs)valid=false;}
        expect(0x0c);for(uint64_t output=0;output<outputs;++output)if(!number())valid=false;
        if(!valid||at>=h.size()||(h[at]!=0x0a&&h[at]!=0))continue;
        uint32_t retainedCrc=0,retainedMask=0;
        if(h[at]==0x0a&&at+2<=h.size()&&h[at+1]==1){
            auto available=std::min<size_t>(4,h.size()-at-2);
            for(size_t i=0;i<available;++i){retainedCrc|=uint32_t(h[at+2+i])<<(i*8);retainedMask|=0xffu<<(i*8);}
        }
        // Optional unpack CRC may be the missing trailer; SDK validates decoded metadata and file CRCs.
        h.resize(at);h.push_back(0);h.push_back(0);
        if(packPosition>UINT64_MAX-32||packedSize>UINT64_MAX-32-packPosition)continue;
        auto headerAt=32+packPosition+packedSize,body=raw.size-end;
        if(headerAt<body||headerAt-body<32||h.size()>UINT64_MAX-headerAt)continue;
        auto recovered=raw;recovered.recoveryPrefix=headerAt-body;recovered.recoveryBytes=body;
        recovered.recoveryTail=std::move(h);recovered.size=headerAt+recovered.recoveryTail.size();recovered.formatHint=L"7z";
        sevenZipHeader(recovered.recoveryHeader.data(),headerAt-32,recovered.recoveryTail);
        if(!retainedMask||packedSize>64*1024*1024)continue;
        std::vector<unsigned char> probeHeader{1,4,6,0};
        probeHeader.insert(probeHeader.end(),recovered.recoveryTail.begin()+ptrdiff_t(afterPosition),recovered.recoveryTail.end());
        probeHeader.insert(probeHeader.end(),{5,1,0,0});
        recovered.recoveryProbe.resize(32+size_t(packedSize)+probeHeader.size());
        auto& probe=recovered.recoveryProbe;sevenZipHeader(probe.data(),packedSize,probeHeader);
        if(recovered.read(32+packPosition,probe.data()+32,size_t(packedSize))!=packedSize)continue;
        std::copy(probeHeader.begin(),probeHeader.end(),probe.begin()+ptrdiff_t(32+packedSize));
        recovered.recoveryCrc=retainedCrc;recovered.recoveryCrcMask=retainedMask;
        return recovered;
    }
    return {};
}
std::optional<SevenZipLayout> sevenZipLayout(const Input& input) {
    unsigned char h[32]{};
    if(input.read(0,h,sizeof(h))!=sizeof(h)||std::memcmp(h,"7z\xBC\xAF\x27\x1C",6))return {};
    if((crcUpdate(0xffffffffu,h+12,20)^0xffffffffu)!=u32(h+8))return {};
    auto u64=[](const unsigned char* p){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(i*8);return n;};
    auto offset=u64(h+12),size=u64(h+20);
    if(offset>UINT64_MAX-32||size>UINT64_MAX-32-offset)return {};
    return SevenZipLayout{32+offset+size,32+offset,size,u32(h+28)};
}
static bool validNextHeader(const Input& input,const SevenZipLayout& layout) {
    if(layout.end>input.size||!layout.headerSize)return false;
    unsigned char buffer[65536];uint32_t crc=0xffffffffu;
    for(uint64_t done=0;done<layout.headerSize;){
        auto n=input.read(layout.headerOffset+done,buffer,size_t(std::min<uint64_t>(sizeof(buffer),layout.headerSize-done)));
        if(!n)return false;
        crc=crcUpdate(crc,buffer,n);done+=n;
    }
    return (crc^0xffffffffu)==layout.headerCrc;
}
std::wstring signature(const Input& in) {
    unsigned char h[560]{}; auto n=in.read(0,h,sizeof(h));
    uint64_t prefix=0;
    while(n>=8 && (u32(h)&0xfffffff0u)==0x184d2a50u) {
        uint64_t skip=uint64_t(u32(h+4))+8;
        if(skip>in.size-prefix)return{};
        prefix+=skip;n=in.read(prefix,h,sizeof(h));
    }
    auto match=[&](const char* s,size_t len){return n>=len && !std::memcmp(h,s,len);};
    if (match("7z\xBC\xAF\x27\x1C",6)) return L"7z";
    if (match("Rar!\x1A\x07",6)) return L"rar";
    if (match("PK\x03\x04",4)||match("PK\x05\x06",4)||match("PK\x07\x08",4)) return L"zip";
    if (match("\x1F\x8B",2)) return L"gz";
    if (match("BZh",3)) return L"bz2";
    if (match("\xFD" "7zXZ\0",6)) return L"xz";
    if (match("\x28\xB5\x2F\xFD",4)) return L"zst";
    if (match("\x04\x22\x4D\x18",4)) return L"lz4";
    if (match("\x05\x22\x4D\x18",4)) return L"lz5";
    if (match("\x06\x22\x4D\x18",4)) return L"liz";
    if (match("MSCF",4)) return L"cab";
    if (match("MSWIM\0\0\0",8)) return L"wim";
    if (n>262 && !std::memcmp(h+257,"ustar",5)) return L"tar";
    return {};
}
bool archiveExtension(const fs::path& p) {
    auto ext=lower(p.extension().wstring());
    const std::wstring all=L"|.zip|.zipx|.7z|.rar|.gz|.gzip|.tgz|.tar|.bz2|.tbz2|.xz|.txz|.zst|.lz4|.lz5|.liz|.cab|.iso|.wim|.arj|.lzh|.lzma|.001|";
    return all.find(L"|"+ext+L"|")!=std::wstring::npos;
}
struct VolumeNumber {std::wstring prefix;unsigned index;};
static std::wstring pathKey(const fs::path& path){return lower(path.lexically_normal().wstring());}
static std::optional<VolumeNumber> volumeNumber(const fs::path& file){
    auto ext=file.extension().wstring();
    bool numeric=ext.size()==4&&std::all_of(ext.begin()+1,ext.end(),iswdigit);
    auto token=numeric?file.filename().wstring():file.stem().wstring();auto at=token.size();
    while(at&&iswdigit(token[at-1]))--at;
    if(at==token.size()||token.size()-at>6)return {};
    return VolumeNumber{lower(token.substr(0,at)),unsigned(std::stoul(token.substr(at)))};
}
static void volumeProgress(const Progress& progress,const wchar_t* phase,uint64_t done,uint64_t total=0,ProgressUnit unit=ProgressUnit::Files){if(progress&&!progress(phase,done,total,unit))throw Cancelled();}
static size_t readFileHead(const fs::path& file,unsigned char* out,size_t maxBytes){
    HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return 0;
    DWORD read=0;
    BOOL ok=ReadFile(h,out,DWORD(maxBytes),&read,nullptr);
    CloseHandle(h);
    return ok?size_t(read):0;
}
static std::optional<SevenZipLayout> sevenZipLayoutFromBytes(const unsigned char* h,size_t n){
    if(n<32||std::memcmp(h,"7z\xBC\xAF\x27\x1C",6))return {};
    if((crcUpdate(0xffffffffu,h+12,20)^0xffffffffu)!=u32(h+8))return {};
    auto u64=[](const unsigned char* p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(i*8);return v;};
    auto offset=u64(h+12),size=u64(h+20);
    if(offset>UINT64_MAX-32||size>UINT64_MAX-32-offset)return {};
    return SevenZipLayout{32+offset+size,32+offset,size,u32(h+28)};
}
struct VolumeDirectory {
    std::unordered_map<std::wstring,std::map<unsigned,std::vector<fs::path>>> numbered;
    std::unordered_map<uint64_t,std::vector<fs::path>> bySize;
    std::unordered_map<std::wstring,uint64_t> lengths;
    std::vector<fs::path> firsts;
    explicit VolumeDirectory(const fs::path& directory,const Progress& progress){
        uint64_t visited=0;volumeProgress(progress,L"读取同目录分卷",0,0,ProgressUnit::Items);
        for(const auto& entry:fs::directory_iterator(directory)){
            volumeProgress(progress,L"读取同目录分卷",++visited,0,ProgressUnit::Items);std::error_code error;auto status=entry.symlink_status(error);
            if(error||fs::is_symlink(status)||!fs::is_regular_file(status))continue;
            auto file=entry.path();auto bytes=entry.file_size(error);if(error)continue;
            lengths.emplace(pathKey(file),bytes);bySize[bytes].push_back(file);
            if(auto number=volumeNumber(file)){
                numbered[number->prefix][number->index].push_back(file);
                if(number->index==1&&bytes>=32){
                    unsigned char h[32];
                    if(readFileHead(file,h,sizeof(h))==sizeof(h)){
                        auto layout=sevenZipLayoutFromBytes(h,sizeof(h));
                        if(layout&&layout->end>bytes)firsts.push_back(file);
                    }
                }
            }
        }
    }
};
const VolumeDirectory& VolumeResolver::directory(const fs::path& path,const Progress& progress){
    auto& snapshot=directories_[pathKey(path)];
    if(!snapshot)snapshot=std::make_shared<VolumeDirectory>(path,progress);
    return *snapshot;
}
static std::optional<std::vector<fs::path>> completeSevenZipGroup(const fs::path& first,const fs::path& requested,const VolumeDirectory& directory,const Progress& progress){
    volumeProgress(progress,L"核对分卷顺序",0);
    Input raw({first});auto layout=sevenZipLayout(raw);
    if(!layout||layout->end<=raw.size)return {};
    auto requestedKey=pathKey(requested);
    auto contains=[&](const auto& paths){return std::any_of(paths.begin(),paths.end(),[&](const auto& p){return pathKey(p)==requestedKey;});};
    auto finish=[&](const std::vector<fs::path>& parts,uint64_t total,std::optional<size_t> missingAt)->std::optional<std::vector<fs::path>>{
        bool includesRequested=contains(parts);
        if(total==layout->end&&includesRequested&&validNextHeader(Input(parts),*layout))return parts;
        if(total>=layout->end)return {};
        auto sized=directory.bySize.find(layout->end-total);if(sized==directory.bySize.end())return {};
        std::optional<std::vector<fs::path>> found;
        uint64_t checked=0;for(const auto& file:sized->second){
            volumeProgress(progress,L"匹配分卷候选",checked++,sized->second.size());
            if(std::any_of(parts.begin(),parts.end(),[&](const auto& p){return pathKey(p)==pathKey(file);})||(!includesRequested&&pathKey(file)!=requestedKey))continue;
            auto proposed=parts;proposed.insert(missingAt?proposed.begin()+ptrdiff_t(*missingAt):proposed.end(),file);
            if(!validNextHeader(Input(proposed),*layout))continue;
            if(found)throw std::runtime_error("Ambiguous split volume candidates");
            found=std::move(proposed);
        }
        return found;
    };
    std::vector<fs::path> parts{first};uint64_t total=raw.size;
    if(auto found=finish(parts,total,{}))return found;
    auto family=volumeNumber(first);
    if(!family||family->index!=1)return {};
    auto familyIndex=directory.numbered.find(family->prefix);if(familyIndex==directory.numbered.end())return {};
    const auto& indexes=familyIndex->second;
    auto choose=[&](const std::vector<fs::path>& matches)->std::optional<fs::path>{
        auto selected=std::find_if(matches.begin(),matches.end(),[&](const auto& p){return pathKey(p)==requestedKey;});
        if(matches.size()>1&&selected==matches.end()){
            auto requestNumber=volumeNumber(requested);
            if(requestedKey==pathKey(first)||(requestNumber&&requestNumber->prefix==family->prefix))throw std::runtime_error("Ambiguous split volume candidates");
            return {};
        }
        return selected!=matches.end()?*selected:matches.front();
    };
    // Prefer a closed consecutive prefix; unrelated higher numbers must not obstruct it.
    unsigned expected=2;
    for(auto item=indexes.upper_bound(1);item!=indexes.end()&&total<layout->end&&item->first==expected;++item,++expected){
        volumeProgress(progress,L"核对分卷顺序",parts.size(),indexes.size());
        auto next=choose(item->second);if(!next)return {};
        parts.push_back(*next);total+=directory.lengths.at(pathKey(*next));
    }
    if(auto found=finish(parts,total,{}))return found;
    if(total>=layout->end)return {};
    // Exactly one absent ordinal can be inferred from a unique remaining size and final header CRC.
    parts={first};total=raw.size;expected=2;unsigned gaps=0;size_t missingAt=0;
    for(auto item=indexes.upper_bound(1);item!=indexes.end()&&total<layout->end;++item){
        volumeProgress(progress,L"核对分卷顺序",parts.size(),indexes.size());
        if(item->first>expected){if(!gaps)missingAt=parts.size();gaps+=item->first-expected;}
        auto next=choose(item->second);if(!next)return {};
        parts.push_back(*next);total+=directory.lengths.at(pathKey(*next));expected=item->first+1;
    }
    if(total==layout->end)return finish(parts,total,{});
    if(gaps!=1)return {};
    return finish(parts,total,missingAt);
}
static bool isAsciiDigits(const std::wstring& s,size_t from,size_t to){
    if(from>=to||to>s.size())return false;
    for(size_t i=from;i<to;++i)if(s[i]<L'0'||s[i]>L'9')return false;
    return true;
}
static bool isNumberedZipExt(const std::wstring& ext){
    return ext.size()>=3&&ext[0]==L'.'&&(ext[1]==L'z'||ext[1]==L'Z')&&isAsciiDigits(ext,2,ext.size());
}
static bool isOldRarExt(const std::wstring& ext){
    if(ext.size()!=4||ext[0]!=L'.')return false;
    wchar_t c=towlower(ext[1]);
    return c>=L'r'&&c<=L'z'&&isAsciiDigits(ext,2,4);
}
static bool parsePartRar(const std::wstring& filename,std::wstring& prefixOut,std::wstring& digitsOut){
    auto dot=filename.rfind(L'.');
    if(dot==std::wstring::npos||dot<6)return false;
    size_t numStart=dot;
    while(numStart>0&&filename[numStart-1]>=L'0'&&filename[numStart-1]<=L'9')--numStart;
    if(numStart==dot||numStart<5)return false;
    if(filename[numStart-5]!=L'.'||towlower(filename[numStart-4])!=L'p'||towlower(filename[numStart-3])!=L'a'||towlower(filename[numStart-2])!=L'r'||towlower(filename[numStart-1])!=L't')return false;
    prefixOut=lower(filename.substr(0,numStart));
    digitsOut=filename.substr(numStart,dot-numStart);
    return true;
}
static bool splitTrailingDigits(const std::wstring& stem,std::wstring& prefixOut,size_t& widthOut){
    size_t at=stem.size();
    while(at>0&&stem[at-1]>=L'0'&&stem[at-1]<=L'9')--at;
    if(at==stem.size())return false;
    prefixOut=stem.substr(0,at);
    widthOut=stem.size()-at;
    return true;
}
static bool signatureInBytes(const unsigned char* h,size_t n){
    auto match=[&](const char* s,size_t len){return n>=len&&!std::memcmp(h,s,len);};
    return match("7z\xBC\xAF\x27\x1C",6)||match("Rar!\x1A\x07",6)
        ||match("PK\x03\x04",4)||match("PK\x05\x06",4)||match("PK\x07\x08",4)
        ||match("\x1F\x8B",2)||match("BZh",3)||match("\xFD" "7zXZ\0",6)
        ||match("\x28\xB5\x2F\xFD",4)||match("\x04\x22\x4D\x18",4)
        ||match("\x05\x22\x4D\x18",4)||match("\x06\x22\x4D\x18",4)
        ||match("MSCF",4)||match("MSWIM\0\0\0",8)
        ||(n>262&&!std::memcmp(h+257,"ustar",5));
}
std::optional<std::vector<fs::path>> VolumeResolver::findRenamed(const fs::path& requested,const Progress& progress){
    volumeProgress(progress,L"分析分卷",0);
    unsigned char h[560]{};auto count=readFileHead(requested,h,sizeof(h));auto ext=lower(requested.extension().wstring());
    bool executable=(ext==L".exe"||ext==L".dll"||ext==L".mui")&&count>=2&&h[0]=='M'&&h[1]=='Z';
    bool image=(ext==L".png"&&count>=8&&!std::memcmp(h,"\x89PNG\r\n\x1a\n",8))
        ||((ext==L".jpg"||ext==L".jpeg")&&count>=12&&h[0]==0xff&&h[1]==0xd8&&h[2]==0xff)
        ||(ext==L".webp"&&count>=12&&!std::memcmp(h,"RIFF",4)&&!std::memcmp(h+8,"WEBP",4));
    if(executable||image||(count>=8&&!std::memcmp(h,"UnityFS",7)))return {};
    if(signatureInBytes(h,count))return {};
    if(count>=8&&(u32(h)&0xfffffff0u)==0x184d2a50u){
        Input input({requested});
        if(!signature(input).empty())return {};
    }
    const auto& snapshot=directory(requested.parent_path(),progress);
    if(snapshot.firsts.empty())return {};
    std::optional<std::vector<fs::path>> found;
    uint64_t checked=0;for(const auto& first:snapshot.firsts){
        volumeProgress(progress,L"匹配分卷候选",checked++,snapshot.firsts.size());
        if(pathKey(first)==pathKey(requested))continue;
        if(auto group=completeSevenZipGroup(first,requested,snapshot,progress)){
            if(found)throw std::runtime_error("Ambiguous split volume candidates");
            found=std::move(group);
        }
    }
    return found;
}
static std::optional<Input> zipFromTail(const Input& raw);
std::vector<fs::path> resolveVolumes(const fs::path& file,const Progress& progress) {return VolumeResolver().resolve(file,progress);}
std::vector<fs::path> VolumeResolver::resolve(const fs::path& file,const Progress& progress) {
    volumeProgress(progress,L"分析分卷",0);
    auto path=fs::absolute(file);
    auto ext=path.extension().wstring();
    bool numberedZip=isNumberedZipExt(ext);
    if(lower(ext)==L".zip"||numberedZip){
        auto last=path;last.replace_extension(L".zip");
        if(fs::is_regular_file(last)){
            Input end({last});auto disks=splitZipDisks(*end.files.front());
            if(disks){
                std::vector<fs::path> parts;
                for(uint32_t disk=1;disk<*disks;++disk){
                    volumeProgress(progress,L"核对 ZIP 分卷",disk-1,*disks);
                    auto suffix=std::to_wstring(disk);if(suffix.size()<2)suffix.insert(0,2-suffix.size(),L'0');
                    auto part=last;part.replace_extension(L".z"+suffix);
                    if(!fs::is_regular_file(part))throw std::runtime_error("Missing ZIP volume ("+utf8(part.filename().wstring())+")");
                    parts.push_back(part);
                }
                parts.push_back(last);
                if(std::none_of(parts.begin(),parts.end(),[&](const auto& p){return pathKey(p)==pathKey(path);}))throw std::runtime_error("Selected file is outside the available volume group");
                return parts;
            }
        }else if(numberedZip){
            auto firstPath=path;firstPath.replace_extension(L".z01");
            unsigned char magic[4]{};
            if(readFileHead(fs::is_regular_file(firstPath)?firstPath:path,magic,4)==4&&!std::memcmp(magic,"PK\x07\x08",4))
                throw std::runtime_error("Missing ZIP volume ("+utf8(last.filename().wstring())+")");
        }
    }
    {
        unsigned char head[32]{};
        if(readFileHead(path,head,sizeof(head))==sizeof(head)){
            if(auto layout=sevenZipLayoutFromBytes(head,sizeof(head))){
                std::error_code ec;auto fsize=fs::file_size(path,ec);
                if(!ec&&layout->end<=fsize)return {path};
                if(auto group=completeSevenZipGroup(path,path,directory(path.parent_path(),progress),progress))return *group;
            }
        }
    }
    if (ext.size()==4 && isAsciiDigits(ext,1,4)) {
        auto first=path; first.replace_extension(L".001");
        if (!fs::exists(first)){if(zipFromTail(Input({path})))return {path};if(auto group=findRenamed(path,progress))return *group;throw std::runtime_error("Missing first volume (.001). Add all volumes in order.");}
        if(auto group=completeSevenZipGroup(first,path,directory(path.parent_path(),progress),progress))return *group;
        if(zipFromTail(Input({path})))return {path};
        Input firstInput({first});auto firstLayout=sevenZipLayout(firstInput);
        if(pathKey(path)!=pathKey(first)){
            bool complete=firstLayout&&firstLayout->end<=firstInput.size;
            if(!firstLayout&&signature(firstInput)==L"zip")complete=zipFromTail(firstInput).has_value();
            if(complete)throw std::runtime_error("Selected file is outside the complete archive");
        }
        std::vector<fs::path> out;
        uint64_t total=0;
        for(unsigned i=1;i<=999;++i) {
            volumeProgress(progress,L"核对分卷顺序",i-1);
            wchar_t suffix[8]; swprintf(suffix,8,L".%03u",i);
            auto p=first; p.replace_extension(suffix);
            if(!fs::exists(p)) break;
            out.push_back(p);
            total+=fs::file_size(p);
            if(firstLayout&&total>=firstLayout->end)break;
        }
        if(std::none_of(out.begin(),out.end(),[&](const auto& p){return pathKey(p)==pathKey(path);})){
            if(!firstLayout||total<firstLayout->end)throw std::runtime_error("Missing middle volume");
            throw std::runtime_error("Selected file is outside the available volume group");
        }
        return out;
    }
    if(isOldRarExt(ext)) {
        auto first=path;first.replace_extension(L".rar");
        if(!fs::exists(first))throw std::runtime_error("Missing first RAR volume (.rar)");
        return {first};
    }
    auto filename=path.filename().wstring();
    std::wstring partPrefix,partDigits;
    if (parsePartRar(filename,partPrefix,partDigits)) {
        unsigned char magic[6]{};
        if(readFileHead(path,magic,6)==6&&!std::memcmp(magic,"Rar!\x1A\x07",6)){
            std::vector<std::pair<unsigned,fs::path>> parts;
            uint64_t checked=0;for(const auto& entry:fs::directory_iterator(path.parent_path())) {
                volumeProgress(progress,L"读取同目录分卷",checked++,0,ProgressUnit::Items);
                std::wstring otherPrefix,otherDigits;auto name=entry.path().filename().wstring();
                std::error_code error;if(!entry.is_regular_file(error)||error||!parsePartRar(name,otherPrefix,otherDigits)||otherPrefix!=partPrefix)continue;
                unsigned char otherMagic[6]{};
                if(readFileHead(entry.path(),otherMagic,6)!=6||std::memcmp(otherMagic,"Rar!\x1A\x07",6))continue;
                unsigned long index=0;
                try{index=std::stoul(otherDigits);}catch(...){throw std::runtime_error("Invalid RAR volume number");}
                if(index==0||index>999999)throw std::runtime_error("Invalid RAR volume number");
                parts.emplace_back(unsigned(index),entry.path());
            }
            std::sort(parts.begin(),parts.end());
            if(parts.empty()||parts.front().first!=1)throw std::runtime_error("Missing first RAR volume");
            std::vector<fs::path> out;
            for(const auto& [index,p]:parts) {
                volumeProgress(progress,L"核对分卷顺序",out.size(),parts.size());
                if(index<out.size()+1)throw std::runtime_error("Ambiguous RAR volumes: multiple files have the same part number");
                if(index!=out.size()+1)throw std::runtime_error("Missing RAR volume "+std::to_string(out.size()+1));
                out.push_back(p);
            }
            return out; // Native volume resolver maps the real files without renaming or copying.
        }
    }
    // A renamed raw 7z split can keep a numeric stem (scene01.mp4, scene02.mp4).
    // Accept it only when both 7z metadata CRCs validate across the assembled view.
    auto stem=path.stem().wstring();
    std::wstring stemPrefix;size_t stemWidth=0;
    if(splitTrailingDigits(stem,stemPrefix,stemWidth)) {
        auto numbered=[&](unsigned index){auto digits=std::to_wstring(index);if(digits.size()<stemWidth)digits.insert(0,stemWidth-digits.size(),L'0');return path.parent_path()/(stemPrefix+digits+ext);};
        auto first=numbered(1);
        if(fs::is_regular_file(first)) {
            unsigned char firstHead[32]{};
            if(readFileHead(first,firstHead,sizeof(firstHead))==sizeof(firstHead)){
                auto layout=sevenZipLayoutFromBytes(firstHead,sizeof(firstHead));
                if(layout&&layout->end>fs::file_size(first)) {
                    std::vector<fs::path> parts;uint64_t total=0;
                    for(unsigned index=1;index<=999999&&total<layout->end;++index){volumeProgress(progress,L"核对分卷顺序",parts.size());auto next=numbered(index);if(!fs::is_regular_file(next))break;parts.push_back(next);total+=fs::file_size(next);}
                    if(total==layout->end&&std::find(parts.begin(),parts.end(),path)!=parts.end()&&validNextHeader(Input(parts),*layout))return parts;
                }
            }
        }
    }
    if(auto group=findRenamed(path,progress))return *group;
    return {path};
}
static std::optional<Input> zipFromTail(const Input& raw) {
    if(raw.size<22)return {};
    const auto length=size_t(std::min<uint64_t>(raw.size,1024*1024));
    auto start=raw.size-length;
    std::vector<unsigned char> owned;
    const unsigned char* tail=nullptr;
    if(contiguousSingleFile(raw)){
        tail=raw.files[0]->data+raw.offset+start;
    }else{
        owned.resize(length);raw.read(start,owned.data(),length);tail=owned.data();
    }
    auto u16=[](const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
    auto u64=[](const unsigned char* p){return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);};
    for(size_t index=length-22+1;index-->0;) {
        auto h=tail+index;
        if(h[0]!='P'||u32(h)!=0x06054b50||u16(h+4)||u16(h+6)||u16(h+8)!=u16(h+10))continue;
        auto end=start+index+22+u16(h+20);
        if(end>raw.size)continue;
        uint64_t directorySize=u32(h+12),directoryOffset=u32(h+16),directoryEnd=start+index,base=0;
        uint64_t entries=u16(h+10);
        bool zip64=index>=20&&u32(h-20)==0x07064b50;
        if(zip64) {
            auto locator=h-20;bool found=false;
            if(u32(locator+4)||u32(locator+16)!=1)continue;
            for(size_t z=index-20;z-->0;) {
                if(z+56>length||u32(tail+z)!=0x06064b50)continue;
                auto record=tail+z;auto bytes=u64(record+4);
                if(bytes<44||bytes>index-20-z||z+12+bytes!=index-20||u32(record+16)||u32(record+20))continue;
                if(start+z<u64(locator+8))continue;
                base=start+z-u64(locator+8);entries=u64(record+32);
                directorySize=u64(record+40);directoryOffset=u64(record+48);directoryEnd=start+z;found=true;break;
            }
            if(!found||base>directoryEnd||directoryOffset>directoryEnd-base||directorySize!=directoryEnd-base-directoryOffset)continue;
        }else {
            if(directorySize>directoryEnd||directoryOffset>directoryEnd-directorySize)continue;
            base=directoryEnd-directorySize-directoryOffset;
        }
        unsigned char magic[4]{};
        if(entries) {
            if(directorySize<46||raw.read(base+directoryOffset,magic,4)!=4||u32(magic)!=0x02014b50)continue;
            if(raw.read(base,magic,4)!=4||u32(magic)!=0x04034b50)continue;
        }else if(directorySize||directoryOffset)continue;
        auto input=raw.slice(base);input.size=end-base;input.formatHint=L"zip";input.zipPasswordOffset.reset();
        std::optional<uint64_t> fallbackOffset;
        for(uint64_t at=base+directoryOffset;at+46<=directoryEnd;){
            unsigned char record[46];if(raw.read(at,record,sizeof(record))!=sizeof(record)||u32(record)!=0x02014b50)break;
            auto name=u16(record+28),extra=u16(record+30),comment=u16(record+32);
            if(uint64_t(46)+name+extra+comment>directoryEnd-at)break;
            if(u16(record+8)&1){
                uint64_t offset=u32(record+42);
                uint64_t uncomp=u32(record+24);
                if(offset==0xffffffffu||uncomp==0xffffffffu){
                    std::vector<unsigned char> fields(extra);raw.read(at+46+name,fields.data(),fields.size());
                    for(size_t p=0;p+4<=fields.size();){
                        auto bytes=u16(fields.data()+p+2);if(bytes>fields.size()-p-4)break;
                        if(u16(fields.data()+p)==1){
                            auto idx=p+4;
                            if(u32(record+24)==0xffffffffu){if(idx+8<=p+4+bytes)uncomp=u64(fields.data()+idx);idx+=8;}
                            if(u32(record+20)==0xffffffffu)idx+=8;
                            if(offset==0xffffffffu&&idx+8<=p+4+bytes)offset=u64(fields.data()+idx);
                            break;
                        }
                        p+=4+bytes;
                    }
                }
                if(offset<input.size){
                    if(uncomp>0){input.zipPasswordOffset=offset;break;}
                    if(!fallbackOffset)fallbackOffset=offset;
                }
            }
            at+=46+name+extra+comment;
        }
        if(!input.zipPasswordOffset&&fallbackOffset)input.zipPasswordOffset=fallbackOffset;
        return input; // Directory offsets locate the archive without reading a multi-GB carrier.
    }
    return {};
}
static bool hasApateSignature(const Input& raw){
    if(raw.files.size()!=1||raw.size<12)return false;
    unsigned char footer[4];raw.read(raw.size-4,footer,4);
    uint32_t mask=u32(footer);
    if(!mask||uint64_t(mask)+4>=raw.size)return false;
    auto restored=raw;restored.apate=true;restored.mask=mask;restored.size=raw.size-mask-4;
    return !signature(restored).empty();
}
std::vector<Input> identifyInputs(const Input& raw,const Progress& progress,bool deep) {
    if(raw.nativeVolumes)return {raw};
    std::vector<Input> inputs;
    if(auto recovered=recoverReversedSevenZip(raw))inputs.push_back(std::move(*recovered));
    // Cheap apate footer probe. The engine must still validate the recovered archive.
    if(raw.files.size()==1 && raw.size>=12) {
        unsigned char footer[4]; raw.read(raw.size-4,footer,4);
        uint32_t mask=u32(footer);
        if(mask && uint64_t(mask)+4<raw.size) {
            auto restored=raw; restored.apate=true; restored.mask=mask; restored.size=raw.size-mask-4;
            if(!signature(restored).empty()) inputs.push_back(std::move(restored));
        }
    }
    inputs.push_back(raw);
    if(auto continuation=recoverSevenZipContinuation(raw))inputs.push_back(std::move(*continuation));
    std::vector<Input> located;
    located.reserve(inputs.size()*2);
    for(const auto& input:inputs) {
        if(auto zip=zipFromTail(input))located.push_back(std::move(*zip));
        located.push_back(input);
    }
    inputs=std::move(located);
    if(!deep || !signature(raw).empty()) return inputs;
    findEmbedded(raw,progress,[&](const Input& candidate){inputs.push_back(candidate);return false;});
    return inputs;
}
bool hasArchiveCandidate(const Input& raw,const Progress& progress,bool deep) {
    if(raw.nativeVolumes||!signature(raw).empty())return true;
    if(raw.size>=0x8006){
        unsigned char iso[5];
        if(raw.read(0x8001,iso,5)==5&&!std::memcmp(iso,"CD001",5))return true;
    }
    if(raw.size>=8){
        unsigned char h[8];
        if(raw.read(0,h,8)==8&&(!std::memcmp(h,"!<arch>\n",8)||!std::memcmp(h,"xar!",4)||(h[0]==0x60&&h[1]==0xea)||(h[0]==0xed&&h[1]==0xab&&h[2]==0xee&&h[3]==0xdb)))return true;
    }
    if(hasApateSignature(raw))return true;
    if(zipFromTail(raw).has_value())return true;
    if(recoverReversedSevenZip(raw).has_value())return true;
    if(recoverSevenZipContinuation(raw).has_value())return true;
    if(!deep)return false;
    return findEmbedded(raw,progress,[](const Input&){return true;});
}
bool findEmbedded(const Input& raw,const Progress& progress,const std::function<bool(const Input&)>& accept) {
    // Scan sequentially, including archives beyond bit7z's SFX window. No content hashes.
    constexpr size_t block=4*1024*1024;
    auto scanRange=[&](const unsigned char* data,size_t n,size_t limit,uint64_t start)->bool{
        for(size_t i=(start==0?1:0);i<limit;++i){
            unsigned char b=data[i];
            if(b!='7'&&b!='R'&&b!='P')continue;
            bool hit=(b=='7' && i+6<=n && !std::memcmp(data+i,"7z\xBC\xAF\x27\x1C",6))
                ||(b=='R' && i+7<=n && !std::memcmp(data+i,"Rar!\x1A\x07",6))
                ||(b=='P' && i+4<=n && !std::memcmp(data+i,"PK\x03\x04",4));
            if(hit && accept(raw.slice(start+i))) return true;
        }
        return false;
    };
    if(contiguousSingleFile(raw)){
        const unsigned char* base=raw.files[0]->data+raw.offset;
        for(uint64_t start=0;start<raw.size;start+=block){
            if(progress && !progress(L"深度识别",start,raw.size,ProgressUnit::Bytes)) throw Cancelled();
            size_t avail=size_t(raw.size-start);
            size_t limit=std::min(avail,block);
            if(scanRange(base+start,avail,limit,start))return true;
        }
        return false;
    }
    std::vector<unsigned char> buffer(std::min<uint64_t>(raw.size,block+8));
    for(uint64_t start=0;start<raw.size;start+=block) {
        if(progress && !progress(L"深度识别",start,raw.size,ProgressUnit::Bytes)) throw Cancelled();
        size_t n=raw.read(start,buffer.data(),buffer.size());
        if(scanRange(buffer.data(),n,std::min(n,block),start))return true;
    }
    return false;
}
}
