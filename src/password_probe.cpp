#include "password_probe.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace lu {
namespace {
uint16_t le16(const unsigned char* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t le32(const unsigned char* p){return le16(p)|(uint32_t(le16(p+2))<<16);}

struct Sha1State {
    uint32_t h[5];
};

inline uint32_t rol32(uint32_t v,unsigned bits){return (v<<bits)|(v>>(32-bits));}

void sha1Transform(uint32_t h[5],const unsigned char block[64]){
    uint32_t w[80];
    for(unsigned i=0;i<16;++i){
        w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|(uint32_t(block[i*4+2])<<8)|uint32_t(block[i*4+3]);
    }
    for(unsigned i=16;i<80;++i){
        w[i]=rol32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
    for(unsigned i=0;i<20;++i){
        uint32_t t=rol32(a,5)+((b&c)|((~b)&d))+e+0x5a827999u+w[i];
        e=d;d=c;c=rol32(b,30);b=a;a=t;
    }
    for(unsigned i=20;i<40;++i){
        uint32_t t=rol32(a,5)+(b^c^d)+e+0x6ed9eba1u+w[i];
        e=d;d=c;c=rol32(b,30);b=a;a=t;
    }
    for(unsigned i=40;i<60;++i){
        uint32_t t=rol32(a,5)+((b&c)|(b&d)|(c&d))+e+0x8f1bbcdcu+w[i];
        e=d;d=c;c=rol32(b,30);b=a;a=t;
    }
    for(unsigned i=60;i<80;++i){
        uint32_t t=rol32(a,5)+(b^c^d)+e+0xca62c1d6u+w[i];
        e=d;d=c;c=rol32(b,30);b=a;a=t;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
}

void sha1Init(uint32_t h[5]){
    h[0]=0x67452301u;h[1]=0xefcdab89u;h[2]=0x98badcfeu;h[3]=0x10325476u;h[4]=0xc3d2e1f0u;
}

void sha1Compute(const unsigned char* data,size_t len,unsigned char out[20]){
    uint32_t h[5];sha1Init(h);
    size_t i=0;
    for(;i+64<=len;i+=64)sha1Transform(h,data+i);
    unsigned char tail[128]{};
    size_t rem=len-i;
    if(rem)std::memcpy(tail,data+i,rem);
    tail[rem]=0x80;
    size_t totalBlocks=(rem+9<=64)?64:128;
    uint64_t bits=uint64_t(len)*8;
    for(unsigned b=0;b<8;++b)tail[totalBlocks-1-b]=static_cast<unsigned char>(bits>>(b*8));
    sha1Transform(h,tail);
    if(totalBlocks==128)sha1Transform(h,tail+64);
    for(unsigned k=0;k<5;++k){
        out[k*4]=static_cast<unsigned char>(h[k]>>24);
        out[k*4+1]=static_cast<unsigned char>(h[k]>>16);
        out[k*4+2]=static_cast<unsigned char>(h[k]>>8);
        out[k*4+3]=static_cast<unsigned char>(h[k]);
    }
}

// Compute a single 20-byte PBKDF2-HMAC-SHA1 block (1-based blockIndex) with precomputed ipad/opad midstates.
void pbkdf2Sha1Block(const std::string& password,const unsigned char* salt,size_t saltLen,uint32_t iterations,uint32_t blockIndex,unsigned char out[20]){
    unsigned char keyBlock[64]{};
    if(password.size()>64){
        sha1Compute(reinterpret_cast<const unsigned char*>(password.data()),password.size(),keyBlock);
    }else if(!password.empty()){
        std::memcpy(keyBlock,password.data(),password.size());
    }
    unsigned char ipad[64],opad[64];
    for(unsigned i=0;i<64;++i){ipad[i]=keyBlock[i]^0x36u;opad[i]=keyBlock[i]^0x5cu;}
    uint32_t innerInit[5],outerInit[5];
    sha1Init(innerInit);sha1Transform(innerInit,ipad);
    sha1Init(outerInit);sha1Transform(outerInit,opad);

    // U_1 = HMAC(P, salt || BE32(blockIndex))
    uint32_t h[5];
    std::memcpy(h,innerInit,sizeof(h));
    unsigned char msg[64]{};
    // WinZip AES salts are 8, 12, or 16 bytes (+ 4 byte index <= 20 bytes).
    std::memcpy(msg,salt,saltLen);
    msg[saltLen]=static_cast<unsigned char>(blockIndex>>24);
    msg[saltLen+1]=static_cast<unsigned char>(blockIndex>>16);
    msg[saltLen+2]=static_cast<unsigned char>(blockIndex>>8);
    msg[saltLen+3]=static_cast<unsigned char>(blockIndex);
    size_t msgLen=saltLen+4;
    msg[msgLen]=0x80;
    uint64_t innerBits=uint64_t(64+msgLen)*8;
    for(unsigned b=0;b<8;++b)msg[63-b]=static_cast<unsigned char>(innerBits>>(b*8));
    sha1Transform(h,msg);

    unsigned char block[64]{};
    block[20]=0x80;
    // Outer hash always hashes 64 + 20 = 84 bytes = 672 bits = 0x02a0.
    block[62]=0x02;block[63]=0xa0;
    for(unsigned k=0;k<5;++k){
        block[k*4]=static_cast<unsigned char>(h[k]>>24);
        block[k*4+1]=static_cast<unsigned char>(h[k]>>16);
        block[k*4+2]=static_cast<unsigned char>(h[k]>>8);
        block[k*4+3]=static_cast<unsigned char>(h[k]);
    }
    std::memcpy(h,outerInit,sizeof(h));
    sha1Transform(h,block);

    uint32_t acc[5];
    std::memcpy(acc,h,sizeof(acc));

    // Remaining iterations: inner and outer both hash exactly 84 bytes (64-byte pad + 20-byte digest).
    for(uint32_t iter=1;iter<iterations;++iter){
        for(unsigned k=0;k<5;++k){
            block[k*4]=static_cast<unsigned char>(h[k]>>24);
            block[k*4+1]=static_cast<unsigned char>(h[k]>>16);
            block[k*4+2]=static_cast<unsigned char>(h[k]>>8);
            block[k*4+3]=static_cast<unsigned char>(h[k]);
        }
        std::memcpy(h,innerInit,sizeof(h));
        sha1Transform(h,block);
        for(unsigned k=0;k<5;++k){
            block[k*4]=static_cast<unsigned char>(h[k]>>24);
            block[k*4+1]=static_cast<unsigned char>(h[k]>>16);
            block[k*4+2]=static_cast<unsigned char>(h[k]>>8);
            block[k*4+3]=static_cast<unsigned char>(h[k]);
        }
        std::memcpy(h,outerInit,sizeof(h));
        sha1Transform(h,block);
        for(unsigned k=0;k<5;++k)acc[k]^=h[k];
    }

    for(unsigned k=0;k<5;++k){
        out[k*4]=static_cast<unsigned char>(acc[k]>>24);
        out[k*4+1]=static_cast<unsigned char>(acc[k]>>16);
        out[k*4+2]=static_cast<unsigned char>(acc[k]>>8);
        out[k*4+3]=static_cast<unsigned char>(acc[k]);
    }
}

struct ZipCryptoCheck {
    std::array<unsigned char,12> header{};
    unsigned char check=0;
};
struct Check {
    bool aes=false;
    std::vector<unsigned char> salt;
    std::array<unsigned char,2> aesVerify{};
    unsigned keyLength=0;
    std::vector<ZipCryptoCheck> cryptoChecks;
};

std::optional<Check> getCheck(const Input& input){
    uint64_t offset=input.zipPasswordOffset.value_or(0);
    std::optional<Check> result;
    unsigned scanned=0;
    while(offset+30<=input.size&&scanned<16){
        unsigned char h[30];if(input.read(offset,h,sizeof(h))!=sizeof(h))break;
        if(le32(h)!=0x04034b50)break;
        ++scanned;
        auto flags=le16(h+6),method=le16(h+8);auto compressed=le32(h+18);
        uint64_t data=offset+30+le16(h+26)+le16(h+28);
        if(data>input.size)break;
        if(flags&1){
            if(method==99){
                if(result)break;
                std::vector<unsigned char> extra(le16(h+28));input.read(offset+30+le16(h+26),extra.data(),extra.size());
                for(size_t p=0;p+4<=extra.size();){
                    auto size=le16(extra.data()+p+2);
                    if(p+4+size>extra.size())return {};
                    if(le16(extra.data()+p)==0x9901 && size>=7 && extra[p+6]=='A'&&extra[p+7]=='E'){
                        unsigned strength=extra[p+8];if(strength<1||strength>3)return {};
                        Check aes;
                        aes.aes=true;aes.keyLength=8+strength*8;
                        aes.salt.resize(aes.keyLength/2);
                        if(input.read(data,aes.salt.data(),aes.salt.size())!=aes.salt.size())return {};
                        if(input.read(data+aes.salt.size(),aes.aesVerify.data(),2)!=2)return {};
                        return aes;
                    }
                    p+=4+size;
                }
                return {};
            }
            // Strong encryption and unknown methods need the engine's own verifier.
            if(flags&0x40)return {};
            ZipCryptoCheck zc;
            if(input.read(data,zc.header.data(),12)!=12)break;
            zc.check=(flags&8)?h[11]:h[17];
            if(!result)result=Check{};
            result->cryptoChecks.push_back(zc);
            // Two independent ZipCrypto headers reduce false positives from 1/256 to 1/65536.
            if(result->cryptoChecks.size()>=2)return result;
        }
        if((flags&8)||compressed==0xffffffff)break;
        uint64_t next=data+compressed;if(next<=offset||next>input.size)break;offset=next;
    }
    return result;
}

uint32_t crcByte(uint32_t crc,unsigned char b){
    static const auto table=[] {std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;++i){auto n=i;for(unsigned j=0;j<8;++j)n=(n>>1)^((n&1)?0xedb88320u:0);t[i]=n;}return t;}();
    return (crc>>8)^table[(crc^b)&255];
}
bool zipCrypto(const std::vector<ZipCryptoCheck>& checks,const std::string& password){
    uint32_t a0=0x12345678,b0=0x23456789,c0=0x34567890;
    auto update=[](uint32_t& a,uint32_t& b,uint32_t& c,unsigned char ch){a=crcByte(a,ch);b=(b+(a&255))*134775813u+1;c=crcByte(c,static_cast<unsigned char>(b>>24));};
    for(unsigned char ch:password)update(a0,b0,c0,ch);
    for(const auto& check:checks){
        uint32_t a=a0,b=b0,c=c0;
        unsigned char plain=0;
        for(auto encrypted:check.header){uint32_t t=c|2;plain=encrypted^static_cast<unsigned char>((t*(t^1))>>8);update(a,b,c,plain);}
        if(plain!=check.check)return false;
    }
    return true;
}
}

std::vector<PasswordCandidate> zipPasswordCandidates(const Input& input,const std::vector<std::wstring>& candidates,const Progress& progress){
    if(signature(input)!=L"zip"){std::vector<PasswordCandidate> out;out.reserve(candidates.size());for(const auto& value:candidates)out.push_back({value,value});return out;}
    auto check=getCheck(input);
    struct Variant {PasswordCandidate password;std::string bytes;};
    std::vector<Variant> variants;
    variants.reserve(candidates.size());
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
    if(!check){std::vector<PasswordCandidate> out;out.reserve(variants.size());for(auto& variant:variants)out.push_back(std::move(variant.password));return out;}

    const uint32_t aesOffset=check->keyLength*2;
    const uint32_t aesBlockIndex=aesOffset/20+1;
    const uint32_t aesByteInBlock=aesOffset%20;

    std::vector<unsigned char> accepted(variants.size(),0);
    std::atomic_size_t next=0,finished=0;
    std::atomic_bool cancel=false;
    std::mutex doneMutex;
    std::condition_variable doneCv;

    unsigned hw=std::max(1u,std::thread::hardware_concurrency());
    unsigned workers=check->aes?std::min(16u,hw):(variants.size()>256?std::min(8u,hw):1u);
    workers=std::min(workers,unsigned(variants.size()));
    bool serial=variants.size()<=8||workers<=1;
    if(!serial&&progress&&!progress(L"密码头部预检",0,variants.size(),ProgressUnit::Candidates))throw Cancelled();

    auto testOne=[&](size_t index){
        const auto& variant=variants[index];
        if(variant.password.value.empty())accepted[index]=1;
        else if(check->aes){
            unsigned char block[20];
            pbkdf2Sha1Block(variant.bytes,check->salt.data(),check->salt.size(),1000,aesBlockIndex,block);
            accepted[index]=block[aesByteInBlock]==check->aesVerify[0]&&block[aesByteInBlock+1]==check->aesVerify[1];
        }else{
            accepted[index]=zipCrypto(check->cryptoChecks,variant.bytes);
        }
    };

    if(serial){
        for(size_t i=0;i<variants.size();++i){
            if(progress&&!progress(L"密码头部预检",i,variants.size(),ProgressUnit::Candidates))throw Cancelled();
            testOne(i);
        }
    }else{
        constexpr size_t chunk=4;
        std::vector<std::jthread> threads;
        threads.reserve(workers);
        for(unsigned w=0;w<workers;++w){
            threads.emplace_back([&]{
                while(!cancel.load(std::memory_order_relaxed)){
                    size_t begin=next.fetch_add(chunk,std::memory_order_relaxed);
                    if(begin>=variants.size())break;
                    size_t end=std::min(begin+chunk,variants.size());
                    for(size_t i=begin;i<end;++i){
                        if(cancel.load(std::memory_order_relaxed))return;
                        testOne(i);
                    }
                    if(finished.fetch_add(end-begin,std::memory_order_release)+(end-begin)>=variants.size()){
                        std::scoped_lock lock(doneMutex);
                        doneCv.notify_one();
                    }
                }
            });
        }
        {
            std::unique_lock lock(doneMutex);
            while(finished.load(std::memory_order_acquire)<variants.size()&&!cancel.load(std::memory_order_relaxed)){
                if(progress&&!progress(L"密码头部预检",finished.load(std::memory_order_relaxed),variants.size(),ProgressUnit::Candidates)){
                    cancel.store(true,std::memory_order_relaxed);
                    break;
                }
                doneCv.wait_for(lock,std::chrono::milliseconds(10),[&]{return finished.load(std::memory_order_acquire)>=variants.size()||cancel.load(std::memory_order_relaxed);});
            }
        }
        for(auto& thread:threads)thread.join();
        if(cancel)throw Cancelled();
    }

    std::vector<PasswordCandidate> out;
    for(size_t i=0;i<variants.size();++i)if(accepted[i])out.push_back(std::move(variants[i].password));
    return out;
}
}
