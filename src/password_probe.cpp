#include "password_probe.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <optional>
#include <stdexcept>

namespace lu {
namespace {
uint16_t le16(const unsigned char* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t le32(const unsigned char* p){return le16(p)|(uint32_t(le16(p+2))<<16);}
struct Check {bool aes=false;std::vector<unsigned char> salt;std::array<unsigned char,12> header{};unsigned keyLength=0;unsigned char check=0;};
std::optional<Check> getCheck(const Input& input){
    uint64_t offset=input.zipPasswordOffset.value_or(0);
    while(offset+30<=input.size){
        unsigned char h[30];input.read(offset,h,sizeof(h));
        if(le32(h)!=0x04034b50)return{};
        auto flags=le16(h+6),method=le16(h+8);auto compressed=le32(h+18);
        uint64_t data=offset+30+le16(h+26)+le16(h+28);
        if(data>input.size)return{};
        if(flags&1){
            Check result;
            if(method==99){
                std::vector<unsigned char> extra(le16(h+28));input.read(offset+30+le16(h+26),extra.data(),extra.size());
                for(size_t p=0;p+4<=extra.size();){
                    auto size=le16(extra.data()+p+2);
                    if(p+4+size>extra.size())return{};
                    if(le16(extra.data()+p)==0x9901 && size>=7 && extra[p+6]=='A'&&extra[p+7]=='E'){
                        unsigned strength=extra[p+8];if(strength<1||strength>3)return{};
                        result.aes=true;result.keyLength=8+strength*8;
                        result.salt.resize(result.keyLength/2);
                        if(input.read(data,result.salt.data(),result.salt.size())!=result.salt.size())return{};
                        if(input.read(data+result.salt.size(),result.header.data(),2)!=2)return{};
                        return result;
                    }p+=4+size;
                }return{};
            }
            // Strong encryption and unknown methods need the engine's own verifier.
            if(flags&0x40)return{};
            if(input.read(data,result.header.data(),12)!=12)return{};
            result.check=(flags&8)?h[11]:h[17];return result;
        }
        if((flags&8)||compressed==0xffffffff)return{};
        uint64_t next=data+compressed;if(next<=offset||next>input.size)return{};offset=next;
    }return{};
}
uint32_t crcByte(uint32_t crc,unsigned char b){
    static const auto table=[] {std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;++i){auto n=i;for(unsigned j=0;j<8;++j)n=(n>>1)^((n&1)?0xedb88320u:0);t[i]=n;}return t;}();
    return (crc>>8)^table[(crc^b)&255];
}
bool zipCrypto(const Check& check,const std::string& password){
    uint32_t a=0x12345678,b=0x23456789,c=0x34567890;
    auto update=[&](unsigned char ch){a=crcByte(a,ch);b=(b+(a&255))*134775813u+1;c=crcByte(c,static_cast<unsigned char>(b>>24));};
    for(unsigned char ch:password)update(ch);
    unsigned char plain=0;
    for(auto encrypted:check.header){uint32_t t=c|2;plain=encrypted^static_cast<unsigned char>((t*(t^1))>>8);update(plain);}
    return plain==check.check;
}
}
std::vector<PasswordCandidate> zipPasswordCandidates(const Input& input,const std::vector<std::wstring>& candidates,const Progress& progress){
    if(signature(input)!=L"zip"){std::vector<PasswordCandidate> out;for(const auto& value:candidates)out.push_back({value,value});return out;}
    auto check=getCheck(input);
    struct Variant {PasswordCandidate password;std::string bytes;};
    std::vector<Variant> variants;
    auto ansi=[](const std::wstring& value){
        auto n=WideCharToMultiByte(CP_ACP,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
        std::string bytes(size_t(n),'\0');if(n)WideCharToMultiByte(CP_ACP,0,value.data(),int(value.size()),bytes.data(),n,nullptr,nullptr);return bytes;
    };
    for(const auto& value:candidates){
        if(value.empty()){variants.push_back({{value,value},{}});continue;}
        auto bytes=utf8(value),legacy=ansi(value);
        auto n=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);
        std::wstring adapted(size_t(n),L'\0');
        if(n)MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),adapted.data(),n);
        bool roundtrip=n&&ansi(adapted)==bytes;
        variants.push_back({{value,roundtrip?adapted:value,roundtrip},bytes});
        if(legacy!=bytes)variants.push_back({{value,value},legacy});
    }
    if(!check){std::vector<PasswordCandidate> out;for(auto& variant:variants)out.push_back(std::move(variant.password));return out;}
    std::vector<unsigned char> accepted(variants.size(),0);
    std::atomic_size_t next=0,finished=0;
    std::atomic_bool cancel=false,cryptoFailed=false;
    unsigned workers=check->aes?std::min(4u,std::max(1u,std::thread::hardware_concurrency())):1;
    workers=std::min(workers,unsigned(variants.size()));
    std::vector<std::jthread> threads;
    bool serial=variants.size()<=8;
    if(!serial&&progress&&!progress(L"密码头部预检",0,variants.size(),ProgressUnit::Candidates))throw Cancelled();
    auto verify=[&]{
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        if(check->aes&&BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA1_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0){cryptoFailed=true;cancel=true;return;}
        struct CloseAlgorithm{BCRYPT_ALG_HANDLE handle;~CloseAlgorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}} closeAlgorithm{algorithm};
        while(!cancel){
            auto index=next.fetch_add(1);if(index>=variants.size())break;
            if(serial&&progress&&!progress(L"密码头部预检",index,variants.size(),ProgressUnit::Candidates)){cancel=true;break;}
            const auto& variant=variants[index];
            if(variant.password.value.empty())accepted[index]=1;
            else if(check->aes){
                std::array<unsigned char,66> derived{};
                auto status=BCryptDeriveKeyPBKDF2(algorithm,reinterpret_cast<PUCHAR>(const_cast<char*>(variant.bytes.data())),ULONG(variant.bytes.size()),check->salt.data(),ULONG(check->salt.size()),1000,derived.data(),check->keyLength*2+2,0);
                if(status<0){cryptoFailed=true;cancel=true;break;}
                accepted[index]=derived[check->keyLength*2]==check->header[0]&&derived[check->keyLength*2+1]==check->header[1];
            }else accepted[index]=zipCrypto(*check,variant.bytes);
            ++finished;
        }
    };
    if(serial)verify();else for(unsigned i=0;i<workers;++i)threads.emplace_back(verify);
    while(finished<variants.size()&&!cancel){
        if(progress&&!progress(L"密码头部预检",finished,variants.size(),ProgressUnit::Candidates))cancel=true;
        if(!cancel)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for(auto& thread:threads)thread.join();
    if(cryptoFailed)throw std::runtime_error("Windows password verifier failed");
    if(cancel)throw Cancelled();
    std::vector<PasswordCandidate> out;for(size_t i=0;i<variants.size();++i)if(accepted[i])out.push_back(std::move(variants[i].password));
    return out;
}
}
