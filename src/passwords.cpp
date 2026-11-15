#include "passwords.hpp"
#include "storage.hpp"
#include <wincrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <set>

namespace lu {
std::vector<std::wstring> splitLines(const std::wstring& text) {
    std::vector<std::wstring> out;
    size_t at=0;
    while(at<text.size()) {
        auto end=text.find(L'\n',at); if(end==std::wstring::npos) end=text.size();
        auto line=text.substr(at,end-at); if(!line.empty() && line.back()==L'\r') line.pop_back();
        if(!line.empty()) out.push_back(std::move(line));
        at=end+1;
    }
    return out;
}
using storage::put64;using storage::get64;using storage::putString;using storage::getString;
PasswordBook::PasswordBook(fs::path file) : file_(std::move(file)) {
    if(file_.empty())file_=storage::dataPath(L"passwords.dpapi");
    if(!fs::exists(file_)) return;
    std::ifstream input(file_,std::ios::binary);
    std::vector<char> encrypted((std::istreambuf_iterator<char>(input)),{});
    DATA_BLOB source{DWORD(encrypted.size()),reinterpret_cast<BYTE*>(encrypted.data())}, clear{};
    if(!CryptUnprotectData(&source,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&clear))
        throw std::runtime_error("Cannot unlock password database with this Windows account");
    std::string data(reinterpret_cast<char*>(clear.pbData),clear.cbData); LocalFree(clear.pbData);
    size_t pos=0; auto count=get64(data,pos);
    for(uint64_t i=0;i<count;++i) {
        Password p; p.value=getString(data,pos); p.uses=get64(data,pos); p.last=get64(data,pos); p.directory=getString(data,pos);
        entries_.push_back(std::move(p));
    }
}
void PasswordBook::saveLocked() {
    std::string data; put64(data,entries_.size());
    for(const auto& p:entries_) { putString(data,p.value); put64(data,p.uses); put64(data,p.last); putString(data,p.directory); }
    DATA_BLOB source{DWORD(data.size()),reinterpret_cast<BYTE*>(data.data())}, encrypted{};
    if(!CryptProtectData(&source,L"T解压密码本",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted))
        throw std::runtime_error("Cannot save password database");
    std::string bytes(reinterpret_cast<char*>(encrypted.pbData),encrypted.cbData);LocalFree(encrypted.pbData);
    storage::atomicWrite(file_,bytes);
}
size_t PasswordBook::count() const { std::scoped_lock lock(mutex_); return entries_.size(); }
std::vector<Password> PasswordBook::snapshot() const {std::scoped_lock lock(mutex_);return entries_;}
struct PasswordChanges {PasswordImport counts;std::vector<std::wstring> additions;};
static std::wstring readPasswordText(const fs::path& file,uint64_t limit){
    auto size=fs::file_size(file);
    if(size>limit)throw std::runtime_error(utf8(limit==65536?L"自动密码提示超过 64 KiB":L"密码文本超过 16 MiB，请拆分后导入；不要将压缩分卷作为密码文本。"));
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read password text file");
    std::string text(size_t(size),'\0');input.read(text.data(),std::streamsize(text.size()));
    if(input.gcount()!=std::streamsize(text.size())||input.peek()!=std::char_traits<char>::eof())throw std::runtime_error(utf8(L"文件未完整读取或正在变化，请稍后重新导入。"));
    if(text.starts_with("\xEF\xBB\xBF"))text.erase(0,3);
    if(std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32&&c!='\r'&&c!='\n'&&c!='\t';}))throw std::runtime_error(utf8(L"文件含有二进制数据，请使用 UTF-8 密码文本。"));
    if(text.empty())return {};
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(!length)throw std::runtime_error(utf8(L"文件不是有效的 UTF-8 文本，请从原软件重新导出。"));
    std::wstring decoded(size_t(length),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),decoded.data(),length);return decoded;
}
static std::wstring filenameHint(const fs::path& file){
    auto name=file.stem();
    auto digits=[](const std::wstring& text,size_t from){return text.size()>from&&std::all_of(text.begin()+from,text.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});};
    bool rar=lower(file.extension().wstring())==L".rar";
    for(unsigned i=0;i<8;++i){auto extension=lower(name.extension().wstring());
        if(archiveExtension(name)&&extension!=L".001"){rar|=extension==L".rar";name=name.stem();}
        else if((extension.size()==4&&digits(extension,1)&&archiveExtension(name.stem()))||(rar&&extension.size()<=11&&extension.starts_with(L".part")&&digits(extension,5))||(extension.size()==4&&extension[1]>=L'r'&&extension[1]<=L'z'&&digits(extension,2)))name=name.stem();
        else break;
    }return name.wstring();
}
static PasswordChanges classify(const std::vector<Password>& entries,const std::vector<std::wstring>& values){
    PasswordChanges changes;std::set<std::wstring> existing;for(const auto& p:entries)existing.insert(p.value);
    for(const auto& value:values)if(!value.empty()){if(existing.insert(value).second){changes.additions.push_back(value);++changes.counts.added;}else ++changes.counts.duplicates;}
    return changes;
}
PasswordImport PasswordBook::preview(const std::vector<std::wstring>& values) const {std::scoped_lock lock(mutex_);return classify(entries_,values).counts;}
PasswordImport PasswordBook::add(const std::vector<std::wstring>& values) {
    std::scoped_lock lock(mutex_);
    auto previous=entries_.size();
    auto changes=classify(entries_,values);
    for(auto& value:changes.additions)entries_.push_back({std::move(value),0,0,{}});
    if(changes.counts.added){try{saveLocked();}catch(...){entries_.resize(previous);throw;}}
    return changes.counts;
}
void PasswordBook::success(const std::wstring& value,const fs::path& file) {
    if(value.empty()) return;
    std::scoped_lock lock(mutex_);
    auto it=std::find_if(entries_.begin(),entries_.end(),[&](const auto& p){return p.value==value;});
    if(it==entries_.end()) {entries_.push_back({value,0,0,{}});it=entries_.end()-1;}
    ++it->uses; it->last=uint64_t(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    it->directory=lower(fs::absolute(file).parent_path().wstring()); saveLocked();
}
std::vector<std::wstring> PasswordBook::ranked(const fs::path& file,const std::wstring& preferred) const {
    std::vector<std::wstring> out;std::set<std::wstring> seen;
    auto append=[&](const std::wstring& value){if(!value.empty()&&seen.insert(value).second)out.push_back(value);};
    append(preferred);
    auto hint=[&](const std::wstring& text) {
        static const std::wregex re(L"(?:密码|解压码|password|passwd|pwd|pass)[\\s:：=._-]*([^\\s\\[\\]【】()（）,，;；]+)",std::regex::icase);
        for(auto it=std::wsregex_iterator(text.begin(),text.end(),re);it!=std::wsregex_iterator();++it) append((*it)[1].str());
    };
    hint(filenameHint(file));hint(file.stem().wstring());
    for(const auto* name:{L"password.txt",L"密码.txt",L"解压密码.txt",L"说明.txt"}) {
        auto p=file.parent_path()/name;
        std::error_code error;if(!fs::is_regular_file(p,error))continue;
        try{
            auto w=readPasswordText(p,65536);hint(w);
            if(std::wstring(name)!=L"说明.txt") for(const auto& line:splitLines(w)) append(line);
        }catch(const std::exception&){/* Optional local hints must not block the archive. */}
    }
    auto items=snapshot();
    auto directory=lower(fs::absolute(file).parent_path().wstring());
    std::stable_sort(items.begin(),items.end(),[&](const auto& a,const auto& b){
        if((a.directory==directory)!=(b.directory==directory)) return a.directory==directory;
        if(a.last!=b.last) return a.last>b.last;
        return a.uses>b.uses;
    });
    for(const auto& p:items) append(p.value);
    out.insert(out.begin(),L"");
    return out;
}
PasswordImport PasswordBook::importText(const fs::path& file) {
    return add(splitLines(readPasswordText(file,16*1024*1024)));
}
PasswordImportBatch PasswordBook::importTexts(const std::vector<fs::path>& files){
    PasswordImportBatch batch;
    for(const auto& file:files){try{auto result=importText(file);batch.totals.added+=result.added;batch.totals.duplicates+=result.duplicates;++batch.files;}
        catch(const std::exception& error){batch.failures.push_back({file,wide(error.what())});}}
    return batch;
}
}
