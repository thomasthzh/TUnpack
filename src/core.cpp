#include "core.hpp"
#include "password_probe.hpp"
#include <bit7z/bitarchivereader.hpp>
#include <bit7z/bitfilecompressor.hpp>
#include <bit7z/biterror.hpp>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwctype>
#include <exception>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace lu {
using namespace bit7z;
bool isCancelled(const std::exception& error){if(dynamic_cast<const Cancelled*>(&error))return true;if(auto engine=dynamic_cast<const BitException*>(&error))return engine->nativeCode()==E_ABORT;return std::string(error.what())=="Cancelled";}
std::wstring errorText(const std::exception& error){
    std::string message=error.what();
    if(message=="Cancelled")return L"已取消，未开始清理原包";
    if(message.starts_with("Cannot open archive:"))return L"未能打开：密码未匹配、分卷不完整，或文件损坏。改名的分卷请用“手工分卷”指定顺序。";
    if(message.starts_with("Missing first"))return L"找不到首卷，请补齐首卷，或用“手工分卷”按顺序加入全部文件。";
    if(message.starts_with("Missing RAR volume"))return L"RAR 分卷中间有缺卷，请补齐后再解压。";
    if(message.starts_with("Missing ZIP volume"))return L"ZIP 分卷不完整，请补齐 "+wide(message.substr(19))+L"；.z01 起的各卷与末卷 .zip 应放在同一目录，原文件保留。";
    if(message.starts_with("Missing middle volume"))return L"分卷不连续，请补齐缺失卷，或用“手工分卷”确认文件和顺序。";
    if(message.starts_with("Ambiguous RAR volumes"))return L"同一卷号存在多个 RAR 文件，请用“手工分卷”确认文件和顺序。";
    if(message.starts_with("Ambiguous split volume"))return L"找到多个可能的分卷，请用“手工分卷”确认顺序；原文件保留。";
    if(message.starts_with("Selected file is outside"))return L"所选文件不属于这个压缩包的有效分卷范围，请单独确认文件；原文件保留。";
    if(message.starts_with("This is a normal document"))return L"这是正常文档或应用容器。若需要拆开它，请右键选择“明确按压缩容器解压”。";
    if(message.starts_with("Archive contains an absolute")||message.starts_with("Archive output path"))return L"包内存在越界路径，已停止解压并保留原包。";
    if(message.starts_with("Archive contains duplicate"))return L"包内存在重名文件（包括大小写冲突），已停止并保留原包。";
    if(message.starts_with("Archive contains a link"))return L"包内包含文件链接，已停止自动解压并保留原包。";
    if(message.starts_with("Cannot confirm all participating"))return L"已解压，但无法确认完整分卷列表；全部原包已保留。";
    if(message.starts_with("Cannot read input")||message.starts_with("Cannot map input"))return L"无法读取文件，请检查文件是否存在或被其他程序占用。";
    if(message.starts_with("Cannot write restored")||message.starts_with("Cannot finish restored"))return L"无法写入还原结果，请检查磁盘空间和输出目录权限。";
    if(message.starts_with("Nested archive depth"))return L"嵌套层数超过 32，剩余压缩包和全部原包已保留。";
    if(message.starts_with("Cannot unlock password"))return L"当前 Windows 账户无法解密这个密码本。请使用原账户，或重新导入密码文本。";
    if(auto engine=dynamic_cast<const BitException*>(&error)){
        if(engine->code()==BitError::InvalidZipPassword)return L"创建加密 ZIP 需要 ASCII 密码。中文密码请创建 7z 压缩包。";
        if(engine->code()==BitFailureSource::WrongPassword)return L"密码未匹配，原包已保留。";
        if(engine->code()==BitFailureSource::CRCError||engine->code()==BitFailureSource::DataError)return L"压缩数据校验失败，文件可能损坏或密码不匹配；原包已保留。";
        if(engine->nativeCode()==E_ABORT)return L"操作已停止，原包保留。";
        return L"解压引擎操作失败，请检查文件完整性、磁盘空间与目录权限（"+wide(engine->code().category().name())+L" "+std::to_wstring(engine->code().value())+L"）。";
    }
    return wide(message);
}
static const BitInFormat Lz4{0x0f},Lz5{0x10},Lizard{0x11};
static bool parseDigits(const std::wstring& s,size_t from,size_t to,unsigned long long& value){
    if(from>=to||to>s.size())return false;
    unsigned long long acc=0;
    for(size_t i=from;i<to;++i){
        if(s[i]<L'0'||s[i]>L'9')return false;
        if(acc>(UINT64_MAX-9)/10)return false;
        acc=acc*10+unsigned(s[i]-L'0');
    }
    value=acc;
    return true;
}
static std::optional<size_t> parseManualRarVolumeIndex(const std::wstring& rawName){
    auto name=lower(rawName);
    if(name.starts_with(L"archive.part")&&name.ends_with(L".rar")&&name.size()>16){
        unsigned long long number=0;
        if(parseDigits(name,12,name.size()-4,number)&&number>0)return size_t(number-1);
        return {};
    }
    size_t extPos=0;
    if(name.starts_with(L"archive.part1."))extPos=14;
    else if(name.starts_with(L"archive."))extPos=8;
    else return {};
    if(name.size()==extPos+3&&name[extPos]>=L'r'&&name[extPos]<=L'z'){
        unsigned long long num=0;
        if(parseDigits(name,extPos+1,extPos+3,num))return size_t(name[extPos]-L'r')*100+size_t(num)+1;
    }
    return {};
}
static std::optional<unsigned long long> parseZipVolumeNumber(const std::wstring& rawName){
    auto name=lower(rawName);
    if(!name.starts_with(L"archive.z")||name.size()<=9)return {};
    unsigned long long number=0;
    if(!parseDigits(name,9,name.size(),number))return {};
    return number;
}
struct Opened {
    Bit7zLibrary& library;
    Input source;
    InputBuffer buffer;
    std::istream stream;
    std::wstring enginePassword;
    std::unique_ptr<BitArchiveReader> reader;
    bool manualRar=false;
    std::shared_ptr<std::vector<fs::path>> usedVolumes=std::make_shared<std::vector<fs::path>>();
    explicit Opened(Bit7zLibrary& lib,Input in,const std::wstring& password)
        :library(lib),source(std::move(in)),buffer(source),stream(&buffer),enginePassword(password) {
        library.setVolumeResolver({});
        usedVolumes->push_back(source.path);
        library.setVolumeObserver([used=usedVolumes](const std::wstring& path){fs::path p(path);if(std::find(used->begin(),used->end(),p)==used->end())used->push_back(p);});
        const auto kind=signature(source);
        const BitInFormat& format=source.formatHint==L"zip"?BitFormat::Zip:source.formatHint==L"7z"?BitFormat::SevenZip:kind==L"lz4"?Lz4:kind==L"lz5"?Lz5:kind==L"liz"?Lizard:BitFormat::Auto;
        manualRar=source.files.size()>1&&kind==L"rar"&&std::all_of(source.files.begin(),source.files.end(),[](const auto& file){return file->size>=6&&!std::memcmp(file->data,"Rar!\x1a\x07",6);});
        if(manualRar){
            auto paths=source.paths();
            library.setVolumeResolver([paths](const std::wstring& name)->std::wstring{
                size_t index=0;
                if(!name.empty()){
                    auto parsed=parseManualRarVolumeIndex(name);
                    if(!parsed||*parsed>=paths.size())return{};
                    index=*parsed;
                }
                return paths[index].wstring();
            },L"archive.part1.rar");
        }
        if(source.nativeVolumes){
            auto paths=source.paths();
            library.setVolumeResolver([paths](const std::wstring& name)->std::wstring{
                if(name.empty()||lower(name)==L"archive.zip")return paths.back().wstring();
                auto number=parseZipVolumeNumber(name);
                return number&&*number&&*number<paths.size()?paths[size_t(*number-1)].wstring():std::wstring{};
            },L"archive.zip");
        }
        try {
            if(source.canUseNative()||manualRar)reader=std::make_unique<BitArchiveReader>(lib,source.path.wstring(),format,password);
            else reader=std::make_unique<BitArchiveReader>(lib,stream,format,password);
        }catch(...){library.setVolumeResolver({});library.setVolumeObserver({});throw;}
    }
    ~Opened(){reader.reset();library.setVolumeResolver({});library.setVolumeObserver({});}
};
static std::wstring formatName(const BitInFormat& f) {
    if(f==Lz4)return L"lz4";
    if(f==Lz5)return L"lz5";
    if(f==Lizard)return L"liz";
    if(f==BitFormat::SevenZip) return L"7z";
    if(f==BitFormat::Zip) return L"zip";
    if(f==BitFormat::Rar || f==BitFormat::Rar5) return L"rar";
    if(f==BitFormat::GZip) return L"gz";
    if(f==BitFormat::BZip2) return L"bz2";
    if(f==BitFormat::Xz) return L"xz";
    if(f==BitFormat::Zstd) return L"zst";
    if(f==BitFormat::Tar) return L"tar";
    if(f==BitFormat::Iso) return L"iso";
    if(f==BitFormat::Cab) return L"cab";
    if(f==BitFormat::Wim) return L"wim";
    if(f==BitFormat::Lzma) return L"lzma";
    if(f==BitFormat::Arj) return L"arj";
    if(f==BitFormat::Lzh) return L"lzh";
    if(f==BitFormat::Cpio) return L"cpio";
    if(f==BitFormat::Deb) return L"deb";
    if(f==BitFormat::Rpm) return L"rpm";
    if(f==BitFormat::Udf) return L"udf";
    if(f==BitFormat::Dmg) return L"dmg";
    if(f==BitFormat::Xar) return L"xar";
    return {};
}
static bool documentContainer(const BitArchiveReader& reader) {
    if(reader.detectedFormat()!=BitFormat::Zip) return false;
    bool content=false,office=false,apk=false,epub=false,jar=false;
    for(uint32_t i=0;i<reader.itemsCount();++i) {
        auto p=lower(reader.itemProperty(i,BitProperty::Path).getString());
        std::replace(p.begin(),p.end(),L'\\',L'/');
        content|=p==L"[content_types].xml";
        office|=p.starts_with(L"word/")||p.starts_with(L"xl/")||p.starts_with(L"ppt/");
        apk|=p==L"androidmanifest.xml";
        epub|=p==L"meta-inf/container.xml";
        jar|=p==L"meta-inf/manifest.mf";
    }
    return (content && office)||apk||epub||jar;
}
static void setCallbacks(BitAbstractArchiveHandler& reader,const Progress& progress,const std::wstring& phase) {
    if(progress&&!progress(phase,0,0,ProgressUnit::Bytes))throw Cancelled();
    auto total=std::make_shared<uint64_t>(0);
    reader.setTotalCallback([total](uint64_t n){*total=n;});
    reader.setProgressCallback([progress,total,phase](uint64_t n){return !progress||progress(phase,n,*total,ProgressUnit::Bytes);});
}
static uint64_t propertyUInt(const BitPropVariant& v) {
    if(v.isUInt64()) return v.getUInt64();
    if(v.isUInt32()) return v.getUInt32();
    if(v.isUInt16()) return v.getUInt16();
    return 0;
}
static std::optional<uint32_t> encryptedProbe(const BitArchiveReader& reader) {
    std::optional<uint32_t> best,empty;
    uint64_t cost=UINT64_MAX;
    bool solid=false; auto solidProp=reader.archiveProperty(BitProperty::Solid);
    if(solidProp.isBool()) solid=solidProp.getBool();
    for(uint32_t i=0;i<reader.itemsCount();++i) {
        if(reader.isItemFolder(i)||!reader.isItemEncrypted(i)) continue;
        auto size=propertyUInt(reader.itemProperty(i,BitProperty::Size));
        if(!size){if(!empty)empty=i;continue;} // Prefer actual data; keep an empty-only archive verifiable.
        if(solid) return i;
        if(!best||size<cost) {best=i;cost=size;}
    }
    return best?best:empty;
}
static uint64_t missingSevenZipBytes(const Input& input) {
    auto layout=sevenZipLayout(input);
    return layout&&layout->end>input.size?layout->end-input.size:0;
}
static std::unique_ptr<Opened> selectArchive(Bit7zLibrary& lib,const Input& raw,const std::vector<std::wstring>& passwords,
                                          const Progress& progress,bool deep,Result& result,std::wstring& used,bool metadataOnly=false) {
    std::string lastError;
    std::string incomplete;
    bool encodingIssue=false;
    auto tryInputs=[&](const std::vector<Input>& inputs)->std::unique_ptr<Opened> {
        for(const auto& input:inputs) {
            auto kind=signature(input);
            if(!input.recoveryPrefix&&!input.headerPatched&&(sevenZipLayout(input).has_value()||input.formatHint==L"zip")){result.type=kind;result.offset=input.offset;result.apate=input.apate;}
            if(auto missing=missingSevenZipBytes(input)) {
                result.type=L"7z";result.offset=input.offset;result.apate=input.apate;
                result.status=L"missing_volume";
                incomplete=utf8(L"已识别为 7z，但当前文件或卷组还缺 "+std::to_wstring(missing)+L" 字节。请补齐后续分卷；原文件已保留。");
                continue;
            }
            bool encryptedHeader=!input.recoveryProbe.empty();
            if(auto layout=sevenZipLayout(input);layout&&layout->end<=input.size&&layout->headerSize<=4096){
                std::vector<unsigned char> header(size_t(layout->headerSize));
                input.read(layout->headerOffset,header.data(),header.size());
                const unsigned char aes[]{6,0xf1,7,1};
                encryptedHeader|=!header.empty()&&header[0]==0x17&&crc32(header.data(),header.size())==layout->headerCrc&&std::search(header.begin(),header.end(),std::begin(aes),std::end(aes))!=header.end();
            }
            if(encryptedHeader){result.encrypted=true;result.status=L"password_required";}
            bool stopInput=false,priorityAllowed=false;
            auto tryCandidates=[&](const std::vector<PasswordCandidate>& filtered,bool forceBodyVerify)->std::unique_ptr<Opened>{
                auto usablePasswords=std::count_if(filtered.begin(),filtered.end(),[](const auto& p){return !p.value.empty()&&p.usable;});
                for(size_t p=0;p<filtered.size();++p) {
                    bool openedMetadata=false,counted=false;
                    bool verifiedHeader=false,verifiedBody=false;
                    const auto& candidate=filtered[p];
                    if(!candidate.usable){encodingIssue=true;continue;}
                    if(progress && !progress(L"验证密码",p,filtered.size(),ProgressUnit::Candidates)) throw Cancelled();
                    try {
                        if(!input.recoveryProbe.empty()&&!candidate.value.empty()){
                            ++result.attempts;counted=true;
                            std::string bytes(reinterpret_cast<const char*>(input.recoveryProbe.data()),input.recoveryProbe.size());
                            std::istringstream stream(bytes);BitArchiveReader probeReader(lib,stream,BitFormat::SevenZip,candidate.engineValue);
                            if(probeReader.itemsCount()!=1||propertyUInt(probeReader.itemProperty(0,BitProperty::Size))>64*1024*1024)continue;
                            std::ostringstream decoded;probeReader.extractTo(decoded,0);auto header=decoded.str();
                            if((crc32(header.data(),header.size())&input.recoveryCrcMask)!=input.recoveryCrc)continue;
                            verifiedHeader=true;
                        }
                        auto opened=std::make_unique<Opened>(lib,input,candidate.engineValue);
                        openedMetadata=true;
                        auto& reader=*opened->reader;
                        auto type=formatName(reader.detectedFormat());
                        if(type.empty()) {stopInput=true;break;} // Do not unpack arbitrary PE/media structures as archives.
                        if(propertyUInt(reader.archiveProperty(BitProperty::ErrorFlags))){if(encryptedHeader){if(!counted&&!candidate.value.empty())++result.attempts;continue;}stopInput=true;break;}
                        if((input.recoveryPrefix||input.headerPatched)&&!reader.itemsCount())continue;
                        result.type=type;result.offset=input.offset;result.apate=input.apate;
                        if(input.missingVolume){result.type=type;result.recovered=true;result.encrypted=encryptedProbe(reader).has_value();result.status=L"missing_volume";used.clear();return opened;}
                        auto probe=encryptedProbe(reader);
                        result.encrypted=probe.has_value()||encryptedHeader;
                        if(encryptedHeader&&!candidate.value.empty()&&!counted){++result.attempts;counted=true;}
                        if(probe&&!encryptedHeader&&!metadataOnly) {
                            if(candidate.value.empty()){result.status=L"password_required";auto size=reader.itemProperty(*probe,BitProperty::Size);priorityAllowed=kind==L"zip"&&(size.isUInt32()||size.isUInt64())&&propertyUInt(size)<=1024*1024;continue;}
                            if(!counted){++result.attempts;counted=true;}
                            setCallbacks(reader,progress,L"快速验证密码");
                            if(usablePasswords>1||forceBodyVerify){reader.testItem(*probe);verifiedBody=true;}
                        }
                        result.encrypted=probe.has_value()||encryptedHeader;
                        result.type=type; result.offset=input.offset; result.apate=input.apate;
                        result.recovered=input.recoveryPrefix!=0||input.headerPatched;result.passwordVerified=verifiedHeader||verifiedBody||encryptedHeader;
                        result.status=L"identified";
                        result.engineError.clear();
                        used=candidate.value;
                        return opened;
                    } catch(const BitException& e) {
                        if(isCancelled(e))throw Cancelled();
                        lastError=e.code().category().name()+std::string(" ")+std::to_string(e.code().value());
                        result.engineError=wide(lastError);
                        if(e.code()==BitFailureSource::WrongPassword) {if(!openedMetadata){encryptedHeader=true;result.type=kind;result.encrypted=true;result.status=L"password_required";}if(!counted&&!candidate.value.empty())++result.attempts;continue;}
                        if(counted&&openedMetadata&&(e.code()==BitFailureSource::CRCError||e.code()==BitFailureSource::DataError)){result.status=L"password_required";continue;}
                        if(forceBodyVerify&&openedMetadata){result.status=L"verification_failed";throw;}
                        if(progress && !progress(L"识别文件",0,0,ProgressUnit::None)) throw Cancelled();
                        // Some encrypted headers report a generic header error before a key is accepted.
                        if(encryptedHeader && p+1<filtered.size() &&
                           (e.code()==BitFailureSource::HeadersError || e.code()==BitFailureSource::DataError)) {++result.attempts;continue;}
                        stopInput=true;break;
                    }
                }
                return {};
            };
            if(kind!=L"zip"){
                if(auto opened=tryCandidates(zipPasswordCandidates(input,passwords,progress),false))return opened;
                continue;
            }
            // Opening ZIP metadata needs no key. Keep expensive prechecks lazy.
            if(auto opened=tryCandidates({{L"",L""}},false))return opened;
            if(stopInput)continue;
            std::vector<std::wstring> remaining;for(const auto& value:passwords)if(!value.empty())remaining.push_back(value);
            size_t prefix=priorityAllowed?std::min<size_t>(4,remaining.size()):0;
            if(prefix){
                std::vector<std::wstring> preferred(remaining.begin(),remaining.begin()+ptrdiff_t(prefix));
                if(auto opened=tryCandidates(zipPasswordCandidates(input,preferred,progress),true))return opened;
            }
            if(!stopInput&&prefix<remaining.size()){
                std::vector<std::wstring> rest(remaining.begin()+ptrdiff_t(prefix),remaining.end());
                if(auto opened=tryCandidates(zipPasswordCandidates(input,rest,progress),false))return opened;
            }

        }
        return {};
    };
    if(auto opened=tryInputs(identifyInputs(raw,progress,false))) return opened;
    if(deep && signature(raw).empty()) {
        std::unique_ptr<Opened> found;
        findEmbedded(raw,progress,[&](const Input& candidate){found=tryInputs({candidate});return bool(found);});
        if(found) return found;
    }
    if(encodingIssue)throw std::runtime_error(utf8(L"ZIP 密码校验已匹配，但当前系统代码页无法无损传给解压引擎。原文件已保留。"));
    if(!incomplete.empty())throw std::runtime_error(incomplete);
    if(result.status==L"password_required")throw std::runtime_error(utf8(L"已识别为 "+result.type+L"，现有密码未通过验证。可在密码库补充密码；也不能排除数据损坏。"));
    if(result.status.empty())result.status=result.type.empty()?L"not_archive":L"verification_failed";
    throw std::runtime_error("Cannot open archive: "+lastError+". Password not matched, missing volume, damaged file or unsupported disguise. For renamed split files, add all volumes in order.");
}
static void validatePaths(const BitArchiveReader& reader) {
    std::unordered_set<std::wstring> paths;
    paths.reserve(reader.itemsCount());
    for(const auto& item:reader) {
        auto name=item.path(); std::replace(name.begin(),name.end(),L'/',L'\\');
        fs::path p(name);
        if(p.is_absolute()||p.has_root_name()||name.find(L':')!=std::wstring::npos||(!name.empty()&&name.front()==L'\\'))
            throw std::runtime_error("Archive contains an absolute output path");
        for(const auto& part:p) if(part==L"..") throw std::runtime_error("Archive output path leaves destination");
        auto hard=item.itemProperty(BitProperty::HardLink);
        if(item.isSymLink()||(hard.isString()&&!hard.getString().empty())) throw std::runtime_error("Archive contains a link; automatic extraction refused");
        if(!name.empty() && !paths.insert(lower(p.lexically_normal().wstring())).second)
            throw std::runtime_error("Archive contains duplicate output names");
    }
}
Engine::Engine(PasswordBook& p,fs::path lib):library_(lib.wstring()),passwords_(p) {}
static std::vector<fs::path> participatingVolumes(const Opened& opened);
static void archiveFailure(Result& result,const std::exception& error){
    const std::string message=error.what();result.note=errorText(error);if(isCancelled(error)){result.status=L"cancelled";return;}
    if(message.starts_with("Missing first")||message.starts_with("Missing middle")||message.starts_with("Missing RAR"))result.status=L"missing_volume";
    if(message.starts_with("Missing ZIP")){result.type=L"zip";result.status=L"missing_volume";}
    if(message.starts_with("Ambiguous split")){result.type=L"7z";result.status=L"ambiguous_volume";}
    if(message.starts_with("Ambiguous RAR")){result.type=L"rar";result.status=L"ambiguous_volume";}
    if(auto engine=dynamic_cast<const BitException*>(&error)){
        result.engineError=wide(engine->code().category().name())+L" "+std::to_wstring(engine->code().value());
        bool password=engine->code()==BitFailureSource::WrongPassword;
        bool data=engine->code()==BitFailureSource::CRCError||engine->code()==BitFailureSource::DataError;
        if(password||(data&&result.encrypted)){
            result.status=result.passwordVerified?L"verification_failed":L"password_required";
            result.note=result.passwordVerified?L"密码验证已通过，但正文校验失败。可能存在数据损坏或正文使用了不同密码；原包和已生成内容均保留。":L"现有密码未通过正文验证。请补充密码后重试；也不能排除数据损坏，原包和已生成内容均保留。";
        }else if(data)result.status=L"verification_failed";
    }
}
Result Engine::inspect(const std::vector<fs::path>& files,const Options& options,const Progress& progress) {
    return inspectInput(files,options,progress,options.volumesResolved);
}
Result Engine::inspectInput(const std::vector<fs::path>& files,const Options& options,const Progress& progress,bool volumesResolved) {
    Result result;bool missingFirst=false;
    try {
        std::vector<fs::path> paths=files;
        if(!volumesResolved&&files.size()==1){try{paths=resolveVolumes(files.front(),progress);}catch(const std::exception& e){if(!std::string(e.what()).starts_with("Missing first"))throw;missingFirst=true;}}
        Input raw(paths);
        std::wstring used; auto opened=selectArchive(library_,raw,passwords_.ranked(raw.path,options.password),progress,options.deep,result,used,!options.verifyData);
        if(opened->source.missingVolume){result.status=L"missing_volume";result.note=L"已识别为 7z 续卷，缺少前部 "+std::to_wstring(opened->source.recoveryPrefix)+L" 字节。请补齐首卷，原文件保留。";return result;}
        result.archives=1;
        for(const auto& item:*opened->reader) result.bytes+=item.size();
        if(options.verifyData){
            setCallbacks(*opened->reader,progress,L"完整校验");opened->reader->test();
            result.passwordVerified=result.encrypted;result.status=L"verified";result.sources=participatingVolumes(*opened);
        }
        result.ok=true;
        result.note=documentContainer(*opened->reader)?L"正常文档或应用容器":result.recovered?(opened->source.recoveryReverse?L"倒序与头部已重建，解压时校验正文":L"头部已重建，解压时校验正文"):result.apate?L"apate 头部伪装":result.offset?L"发现内嵌压缩包":lower(raw.path.extension().wstring())!=L"."+result.type?L"内容格式与后缀不同":L"标准压缩包";
        if(raw.files.size()>1)result.note+=L"；已自动组合 "+std::to_wstring(raw.files.size())+L" 卷";
        if(options.verifyData)result.note=L"完整校验通过，未生成解压文件";
    } catch(const std::exception& e) {
        result.note=errorText(e);if(isCancelled(e)){result.status=L"cancelled";return result;}auto message=std::string(e.what());
        if(missingFirst){result.status=L"missing_volume";result.note=L"找不到首卷，请补齐首卷，或手工确认改名分卷的顺序。";}
        if(message.starts_with("Ambiguous split volume")){result.type=L"7z";result.status=L"ambiguous_volume";}
        if(message.starts_with("Ambiguous RAR")){result.type=L"rar";result.status=L"ambiguous_volume";}
        if(message.starts_with("Missing RAR")){result.type=L"rar";result.status=L"missing_volume";}
        if(message.starts_with("Missing ZIP")){result.type=L"zip";result.status=L"missing_volume";}
        if(message.starts_with("Missing middle volume"))result.status=L"missing_volume";
        if(options.verifyData&&!result.type.empty()&&result.status!=L"password_required"&&result.status!=L"missing_volume"&&result.status!=L"ambiguous_volume"){
            result.status=L"verification_failed";result.note=L"完整校验未通过，原文件保留。";
        }
    }
    return result;
}
static std::mutex gOutputDirMutex;
static fs::path createUniqueOutputFolder(const fs::path& source,const fs::path& parent) {
    auto name=source.stem().wstring();
    if(lower(source.extension().wstring())==L".001") name=source.stem().stem().wstring();
    std::scoped_lock lock(gOutputDirMutex);
    auto target=uniquePath((parent.empty()?source.parent_path():parent)/name);
    fs::create_directories(target);
    return target;
}
static bool extractParallelZip(Bit7zLibrary& lib,const Opened& opened,const fs::path& output,const Progress& progress,const std::wstring& phase,unsigned requestedThreads) {
    const auto& reader=*opened.reader;
    if(reader.detectedFormat()!=BitFormat::Zip||opened.source.nativeVolumes)return false;
    unsigned hw=std::max(1u,std::thread::hardware_concurrency());
    unsigned maxWorkers=requestedThreads==1?1u:(requestedThreads>1?std::min(requestedThreads,16u):std::min(8u,hw));
    if(maxWorkers<2)return false;
    uint32_t count=reader.itemsCount();
    if(count<16)return false;
    struct Entry{uint32_t index;uint64_t size;};
    std::vector<Entry> files;
    files.reserve(count);
    std::vector<fs::path> dirs;
    uint64_t totalBytes=0;
    for(uint32_t i=0;i<count;++i){
        if(reader.isItemFolder(i)){
            auto p=reader.itemProperty(i,BitProperty::Path).getString();
            if(!p.empty())dirs.push_back(output/fs::path(p));
        }else{
            uint64_t sz=propertyUInt(reader.itemProperty(i,BitProperty::Size));
            totalBytes+=sz;
            files.push_back({i,sz});
        }
    }
    if(files.size()<16||totalBytes<512ull*1024)return false;
    unsigned workers=std::min<unsigned>(maxWorkers,unsigned(files.size()/4));
    if(workers<2)return false;

    // Pre-create all directories on the calling thread to avoid concurrent CreateDirectory races.
    std::unordered_set<std::wstring> createdDirs;
    auto ensureDir=[&](const fs::path& dir){
        if(dir.empty())return;
        auto key=lower(dir.wstring());
        if(createdDirs.insert(key).second)fs::create_directories(dir);
    };
    for(const auto& d:dirs)ensureDir(d);
    for(const auto& entry:files){
        auto p=reader.itemProperty(entry.index,BitProperty::Path).getString();
        if(!p.empty())ensureDir((output/fs::path(p)).parent_path());
    }

    // Greedy LPT load balancing by uncompressed size, then sort each bucket ascending for sequential I/O.
    std::sort(files.begin(),files.end(),[](const Entry& a,const Entry& b){return a.size>b.size;});
    std::vector<std::vector<uint32_t>> buckets(workers);
    std::vector<uint64_t> loads(workers,0);
    for(const auto& entry:files){
        size_t best=size_t(std::min_element(loads.begin(),loads.end())-loads.begin());
        buckets[best].push_back(entry.index);
        loads[best]+=entry.size+4096;
    }
    for(auto& b:buckets)std::sort(b.begin(),b.end());

    if(progress&&!progress(phase,0,totalBytes,ProgressUnit::Bytes))throw Cancelled();

    std::atomic<uint64_t> doneBytes{0};
    std::atomic<bool> cancel{false};
    std::mutex progressMutex,errorMutex;
    std::exception_ptr firstError;

    std::vector<std::jthread> threads;
    threads.reserve(workers);
    for(unsigned w=0;w<workers;++w){
        if(buckets[w].empty())continue;
        threads.emplace_back([&,w]{
            try{
                Opened worker(lib,opened.source,opened.enginePassword);
                auto& wr=*worker.reader;
                wr.setOverwriteMode(OverwriteMode::None);
                uint64_t prev=0;
                wr.setProgressCallback([&](uint64_t n)->bool{
                    if(cancel.load(std::memory_order_relaxed))return false;
                    uint64_t delta=n>prev?n-prev:0;
                    prev=n;
                    uint64_t agg=doneBytes.fetch_add(delta,std::memory_order_relaxed)+delta;
                    if(progress){
                        std::unique_lock lock(progressMutex,std::try_to_lock);
                        if(lock.owns_lock()){
                            if(!progress(phase,std::min(agg,totalBytes),totalBytes,ProgressUnit::Bytes)){
                                cancel.store(true,std::memory_order_relaxed);
                                return false;
                            }
                        }
                    }
                    return !cancel.load(std::memory_order_relaxed);
                });
                wr.extractTo(output.wstring(),buckets[w]);
            }catch(...){
                cancel.store(true,std::memory_order_relaxed);
                std::scoped_lock lock(errorMutex);
                if(!firstError)firstError=std::current_exception();
            }
        });
    }
    for(auto& t:threads)t.join();
    if(firstError)std::rethrow_exception(firstError);
    if(cancel)throw Cancelled();
    if(progress&&!progress(phase,totalBytes,totalBytes,ProgressUnit::Bytes))throw Cancelled();
    return true;
}
Result Engine::scan(const fs::path& directory,const Options& options,const Progress& progress,
                    const std::function<void(const std::vector<fs::path>&,const Result&)>& found){
    Result summary;std::unordered_set<std::wstring> handled;VolumeResolver volumes;unsigned unreadable=0;
    try{
        auto skipProblem=[&](const fs::path& path){++unreadable;++summary.skippedFiles;if(summary.failedFile.empty())summary.failedFile=path;};
        auto visit=[&](const fs::directory_entry& entry){
            std::error_code error;auto status=entry.symlink_status(error);if(error){skipProblem(entry.path());return;}if(fs::is_symlink(status)||!fs::is_regular_file(status))return;
            if(progress&&!progress(L"扫描目录",summary.scannedFiles,0,ProgressUnit::Files))throw Cancelled();
            ++summary.scannedFiles;auto size=entry.file_size(error);if(error){skipProblem(entry.path());return;}summary.bytes+=size;
            std::vector<fs::path> files{entry.path()};
            bool resolved=false;
            try{files=volumes.resolve(entry.path(),progress);resolved=true;}catch(const Cancelled&){throw;}catch(const std::exception&){}
            if(!handled.insert(lower(files.front().wstring())).second){++summary.skippedFiles;return;}
            if(resolved&&files.size()==1&&!archiveExtension(entry.path())){
                try{
                    Input probe(files);
                    if(!hasArchiveCandidate(probe,progress,options.deep)){++summary.skippedFiles;return;}
                }catch(const Cancelled&){throw;}catch(const std::exception&){}
            }
            auto check=inspectInput(files,options,progress,resolved);if(check.status==L"cancelled")throw Cancelled();
            if(progress&&!progress(L"扫描目录",summary.scannedFiles,0,ProgressUnit::Files))throw Cancelled();
            bool document=check.ok&&check.note==L"正常文档或应用容器";
            if(!document&&(check.ok||!check.type.empty()||check.status==L"missing_volume"||archiveExtension(entry.path()))){
                if(found)found(files,check);
                ++summary.archives;
            }else ++summary.skippedFiles;
        };
        std::error_code error;fs::directory_iterator root(directory,error);if(error)throw fs::filesystem_error("Cannot open scan directory",directory,error);
        if(options.scanRecursive){
            for(auto it=fs::recursive_directory_iterator(directory,fs::directory_options::skip_permission_denied);it!=fs::recursive_directory_iterator();it.increment(error)){
                if(error)throw fs::filesystem_error("Cannot continue scan directory",directory,error);
                visit(*it);
            }
            if(error)throw fs::filesystem_error("Cannot continue scan directory",directory,error);
        }else {for(;root!=fs::directory_iterator();root.increment(error)){if(error)throw fs::filesystem_error("Cannot continue scan directory",directory,error);visit(*root);}if(error)throw fs::filesystem_error("Cannot continue scan directory",directory,error);}
        summary.ok=true;summary.status=unreadable?L"scanned_with_skips":L"scanned";summary.note=L"扫描 "+std::to_wstring(summary.scannedFiles)+L" 个文件，找到 "+std::to_wstring(summary.archives)+L" 组压缩包或需处理的分卷";if(unreadable)summary.note+=L"；有 "+std::to_wstring(unreadable)+L" 项变化或无法访问，已跳过，其余继续";
    }catch(const std::exception& e){summary.status=isCancelled(e)?L"cancelled":L"scan_incomplete";summary.note=errorText(e);}
    return summary;
}
static std::vector<fs::path> participatingVolumes(const Opened& opened) {
    const auto& raw=opened.source;
    const auto& reader=*opened.reader;
    if(raw.nativeVolumes){
        auto paths=raw.paths();
        if(reader.volumesCount()!=paths.size()||std::any_of(paths.begin(),paths.end(),[&](const auto& path){return std::find(opened.usedVolumes->begin(),opened.usedVolumes->end(),path)==opened.usedVolumes->end();}))throw std::runtime_error("Cannot confirm all participating volumes; original set kept");
        return paths;
    }
    if(raw.files.size()>1&&!opened.manualRar){
        auto offset=propertyUInt(reader.archiveProperty(BitProperty::Offset));
        if(offset>raw.size)throw std::runtime_error("Cannot confirm all participating volumes");
        auto length=propertyUInt(reader.archiveProperty(BitProperty::PhySize));
        if(!length||length>raw.size-offset)length=raw.size-offset;
        auto begin=raw.offset+offset,end=begin+length;
        std::vector<fs::path> paths;
        for(size_t i=0;i<raw.files.size();++i){
            if(raw.starts[i]<end&&raw.starts[i]+raw.files[i]->size>begin&&std::find(paths.begin(),paths.end(),raw.files[i]->path)==paths.end())paths.push_back(raw.files[i]->path);
        }
        if(paths.empty())throw std::runtime_error("Cannot confirm all participating volumes");
        return paths;
    }
    auto paths=*opened.usedVolumes;
    auto expected=reader.isMultiVolume()?reader.volumesCount():1;
    if(expected>paths.size())throw std::runtime_error("Cannot confirm all participating volumes; original set kept");
    paths.resize(expected);
    return paths;
}
void Engine::extractOne(const std::vector<fs::path>& files,const Options& options,const Progress& progress,
                        const std::wstring& preferred,unsigned depth,Result& result,std::vector<fs::path>& cleanup,std::vector<fs::path>& rootVolumes) {
    if(depth>32) throw std::runtime_error("Nested archive depth exceeds 32; remaining archive kept");
    Result current;std::wstring used;std::unique_ptr<Opened> opened;fs::path output;
    // Only this level's identification/extraction errors are classified here.
    // Recursive failures already carry the failing inner archive's context.
    try{
        Input raw(files.size()==1&&!options.volumesResolved?resolveVolumes(files.front(),progress):files);
        auto ranked=passwords_.ranked(raw.path,!options.password.empty()?options.password:preferred);
        opened=selectArchive(library_,raw,ranked,progress,options.deep,current,used);
        if(opened->source.missingVolume)throw std::runtime_error("Missing first volume; only continuation metadata is present");
        if(documentContainer(*opened->reader)) {
            if(depth)return;
            if(!options.allowContainer)throw std::runtime_error("This is a normal document or application container. Use explicit container extraction to unpack it; original kept.");
        }
        validatePaths(*opened->reader);
        output=createUniqueOutputFolder(raw.path,options.output);
    }catch(const std::exception& error){
        archiveFailure(current,error);
        if(!depth)result=current;
        else {result.status=current.status;result.engineError=current.engineError;result.note=current.note;result.attempts+=current.attempts;}
        throw;
    }
    auto& reader=*opened->reader;const auto& raw=opened->source;
    if(!depth){result.output=output;result.type=current.type;result.offset=current.offset;result.apate=current.apate;result.encrypted=current.encrypted;result.recovered=current.recovered;result.status=current.status;result.passwordVerified=current.passwordVerified;}
    if(!depth&&options.outputReady)options.outputReady(output);
    result.recovered|=current.recovered;result.attempts+=current.attempts;
    const auto extractPhase=L"解压 "+raw.path.filename().wstring();
    try{
        if(!extractParallelZip(library_,*opened,output,progress,extractPhase,options.threads)){
            setCallbacks(reader,progress,extractPhase);reader.setOverwriteMode(OverwriteMode::None);
            reader.extractTo(output.wstring()); // Full CRC/MAC validation belongs to the mature engine.
        }
    }
    catch(const std::exception& error){
        archiveFailure(current,error);result.status=current.status;result.engineError=current.engineError;result.note=current.note;throw;
    }
    ++result.archives;
    if(!used.empty()) {passwords_.success(used,raw.path);std::scoped_lock lock(batchMutex_);batchPassword_=used;}
    std::vector<fs::path> children;
    for(const auto& item:reader) {
        if(!item.isDir()) {
            auto relative=fs::path(item.path());
            if(relative.empty()) relative=L"[Content]";
            auto p=output/relative; result.bytes+=item.size();
            if(options.recursive && fs::is_regular_file(p)) children.push_back(p);
        }
    }
    // Only inspect files created by this extraction, never rescan an existing destination tree.
    std::unordered_set<std::wstring> handled;VolumeResolver childVolumes;
    for(const auto& child:children) {
        if(progress && !progress(L"检查嵌套压缩包",0,0,ProgressUnit::None)) throw Cancelled();
        std::vector<fs::path> volumes{child};bool resolved=false;
        try{volumes=childVolumes.resolve(child,progress);resolved=true;}catch(const Cancelled&){throw;}catch(const std::exception&){}
        Options nested=options;nested.output=child.parent_path();nested.volumesResolved=resolved;
        auto volumeKey=lower(volumes.front().wstring());
        if(handled.contains(volumeKey))continue;
        bool likely=archiveExtension(child)||volumes.size()>1;
        if(!likely){
            Input probe({child});
            likely=!signature(probe).empty();
            if(!likely&&resolved){
                if(!hasArchiveCandidate(probe,progress,options.deep))continue;
            }
            if(!likely){
                auto check=inspect(volumes,nested,progress);if(check.status==L"cancelled")throw Cancelled();
                likely=check.ok||(!check.type.empty()&&check.status==L"password_required")||check.status==L"missing_volume";
            }
        }
        if(!likely) continue;
        handled.insert(volumeKey);
        result.failedFile=child;
        try{extractOne(volumes,nested,progress,used,depth+1,result,cleanup,rootVolumes);}
        catch(...){if(result.failedFile.empty())result.failedFile=child;throw;}
        result.failedFile.clear();
    }
    auto paths=participatingVolumes(*opened);
    if(!depth)rootVolumes=paths;
    cleanup.insert(cleanup.end(),paths.begin(),paths.end());
}
Result Engine::extract(const std::vector<fs::path>& files,const Options& options,const Progress& progress) {
    Result result;
    try {
        std::wstring preferred;
        {std::scoped_lock lock(batchMutex_);preferred=batchPassword_;}
        std::vector<fs::path> cleanup,rootVolumes;
        extractOne(files,options,progress,preferred,0,result,cleanup,rootVolumes);
        if(progress&&!progress(L"准备完成任务",result.bytes,result.bytes,ProgressUnit::Bytes))throw Cancelled();
        // All mappings/readers are closed, all nested outputs succeeded before any source recycling.
        result.sources=cleanup;
        bool retained=options.retainSources&&options.retainSources(cleanup,rootVolumes);
        result.ok=true;
        result.status=L"extracted";result.passwordVerified=result.encrypted;
        if(options.recycle&&retained)result.note=L"解压完成；后续任务仍需读取这些原包，已保留。";
        else if(options.recycle && !recycleFiles(cleanup,progress)) result.note=L"解压完成；部分原包未能移入回收站";
        else result.note=options.recycle?L"解压完成，原包已移入回收站":L"解压完成，已保留原包";
        if(result.recovered)result.note+=L"；已重建压缩包头";
    } catch(const std::exception& e) {
        if(result.failedFile.empty()&&std::string(e.what()).starts_with("Missing first")){
            auto detail=inspect(files,options,progress);if(detail.status==L"cancelled"){result.status=detail.status;result.note=detail.note;return result;}if(detail.status==L"missing_volume")return detail;
        }
        if(isCancelled(e))result.status=L"cancelled";
        if(result.note.empty())result.note=errorText(e);
        if(!result.failedFile.empty())result.note=L"嵌套文件 "+result.failedFile.filename().wstring()+L"："+result.note;
    }
    return result;
}
Result Engine::restore(const std::vector<fs::path>& files,const Options& options,const Progress& progress) {
    Result result;
    try {
        Input raw(files.size()==1&&!options.volumesResolved?resolveVolumes(files.front(),progress):files);
        std::wstring used; auto opened=selectArchive(library_,raw,passwords_.ranked(raw.path,options.password),progress,options.deep,result,used,true);
        if(opened->source.missingVolume)throw std::runtime_error("Missing first volume; cannot restore incomplete data");
        auto parent=options.output.empty()?raw.path.parent_path():options.output;
        fs::create_directories(parent);
        std::vector<char> buffer(4*1024*1024);
        auto copy=[&](const Input& input,const fs::path& target){
            std::ofstream out(target,std::ios::binary);
            for(uint64_t at=0;at<input.size;){
                if(progress&&!progress(L"另存原始压缩包",at,input.size,ProgressUnit::Bytes))throw Cancelled();
                if(input.recoveryPrefix&&at==32&&at<input.recoveryPrefix){out.seekp(std::streamoff(input.recoveryPrefix));at=input.recoveryPrefix;continue;}
                auto n=input.read(at,buffer.data(),buffer.size());if(!n)throw std::runtime_error("Unexpected input end");
                out.write(buffer.data(),std::streamsize(n));if(!out)throw std::runtime_error("Cannot write restored archive");at+=n;
            }
            out.close();if(!out)throw std::runtime_error("Cannot finish restored archive");result.bytes+=input.size;
        };
        if(opened->source.nativeVolumes||(opened->reader->isMultiVolume()&&result.type==L"rar")){
            auto parts=participatingVolumes(*opened);
            result.output=uniquePath(parent/(raw.path.stem().wstring()+L".restored"));fs::create_directories(result.output);
            if(options.outputReady)options.outputReady(result.output);
            unsigned char head[12]{};raw.read(0,head,12);bool legacy=head[6]==0&&!(head[10]&0x10);
            for(size_t i=0;i<parts.size();++i){
                std::wstring name=L"archive.part"+std::to_wstring(i+1)+L".rar";
                if(opened->source.nativeVolumes){auto number=std::to_wstring(i+1);if(number.size()<2)number.insert(0,2-number.size(),L'0');name=i+1==parts.size()?L"archive.zip":L"archive.z"+number;}
                else if(legacy){wchar_t ext[12];if(i==0)name=L"archive.rar";else{swprintf(ext,12,L".%lc%02u",wchar_t(L'r'+(i-1)/100),unsigned((i-1)%100));name=L"archive"+std::wstring(ext);}}
                copy(Input({parts[i]}),result.output/name);
            }
        }else{
            auto input=opened->source;auto physical=propertyUInt(opened->reader->archiveProperty(BitProperty::PhySize));if(physical&&physical<input.size)input.size=physical;
            result.output=uniquePath(parent/(raw.path.stem().wstring()+L".restored."+result.type));if(options.outputReady)options.outputReady(result.output);copy(input,result.output);
        }
        result.ok=true;result.note=result.recovered?L"已另存重建的压缩包；缺失的非正文前缀留空，源文件保留":L"已另存原始压缩包，源文件保留";
    } catch(const std::exception& e) {archiveFailure(result,e);}
    return result;
}
Result Engine::compress(const std::vector<fs::path>& files,const fs::path& output,const std::wstring& password,const Progress& progress,const std::function<void(const fs::path&)>& outputReady) {
    Result result;
    try {
        auto target=uniquePath(output); fs::create_directories(target.parent_path());
        const auto& format=lower(target.extension().wstring())==L".zip"?BitFormat::Zip:BitFormat::SevenZip;
        BitFileCompressor compressor(library_,format);
        compressor.setCompressionLevel(BitCompressionLevel::Normal);
        compressor.setPassword(password,format==BitFormat::SevenZip);
        if(format==BitFormat::Zip&&!password.empty())compressor.setFormatProperty(L"em",L"AES256");
        compressor.setThreadsCount(0);
        setCallbacks(compressor,progress,L"创建压缩包");
        std::vector<std::wstring> paths; for(const auto& p:files) paths.push_back(fs::absolute(p).wstring());
        result.output=target;if(outputReady)outputReady(target);
        compressor.compress(paths,target.wstring());
        result.ok=true;result.output=target;result.note=L"压缩完成";result.bytes=fs::file_size(target);result.archives=1;
    } catch(const std::exception& e) {result.note=errorText(e);if(isCancelled(e))result.status=L"cancelled";}
    return result;
}
bool recycleFiles(const std::vector<fs::path>& files,const Progress& progress) {
    IFileOperation* op=nullptr;
    if(FAILED(CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&op)))) return false;
    if(FAILED(op->SetOperationFlags(FOF_ALLOWUNDO|FOFX_RECYCLEONDELETE|FOF_NO_CONNECTED_ELEMENTS|FOFX_EARLYFAILURE|FOF_WANTNUKEWARNING|FOF_NOCONFIRMATION|FOF_SILENT|FOF_NOERRORUI))){op->Release();return false;}
    bool ok=true;
    for(const auto& file:files) {
        if(progress&&!progress(L"准备回收原包",0,files.size(),ProgressUnit::Items)){ok=false;break;}
        IShellItem* item=nullptr;
        if(FAILED(SHCreateItemFromParsingName(file.c_str(),nullptr,IID_PPV_ARGS(&item)))) {ok=false;break;}
        auto hr=op->DeleteItem(item,nullptr); item->Release();
        if(FAILED(hr)) {ok=false;break;}
    }
    if(ok&&progress&&!progress(L"原包移入回收站",0,files.size(),ProgressUnit::Items))ok=false;
    if(ok) {BOOL aborted=FALSE;ok=SUCCEEDED(op->PerformOperations())&&SUCCEEDED(op->GetAnyOperationsAborted(&aborted))&&!aborted;}
    op->Release();return ok;
}
}
