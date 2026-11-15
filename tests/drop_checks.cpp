#include "file_drop.hpp"
#include "queue_runner.hpp"
#include <shlobj.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <fstream>

using namespace lu;
using namespace lu::ui;
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct TransferStats {unsigned reads=0,releases=0;};
class MediumOwner final:public IUnknown {
    ULONG references_=1;HGLOBAL memory_;TransferStats& stats_;
public:
    MediumOwner(HGLOBAL memory,TransferStats& stats):memory_(memory),stats_(stats){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(iid==IID_IUnknown){*out=this;AddRef();return S_OK;}*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {auto n=--references_;if(!n){GlobalFree(memory_);++stats_.releases;delete this;}return n;}
};
class FileData final:public IDataObject {
    ULONG references_=1;
public:
    std::vector<fs::path> paths{L"C:\\drop-fixture\\测试.mp4",L"C:\\drop-fixture\\set.003.docx"};
    TransferStats stats;
    bool supported=true,fail=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {if(iid==IID_IUnknown||iid==IID_IDataObject){*out=this;AddRef();return S_OK;}*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {auto n=--references_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {return supported&&format->cfFormat==CF_HDROP&&(format->tymed&TYMED_HGLOBAL)?S_OK:DV_E_FORMATETC;}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format,STGMEDIUM* medium) override {
        *medium={};if(QueryGetData(format)!=S_OK)return DV_E_FORMATETC;if(fail)return E_FAIL;
        std::wstring names;for(const auto& path:paths){names+=path.wstring();names.push_back(L'\0');}names.push_back(L'\0');if(paths.empty())names.push_back(L'\0');
        auto bytes=sizeof(DROPFILES)+names.size()*sizeof(wchar_t);auto memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes);if(!memory)return E_OUTOFMEMORY;
        auto data=static_cast<unsigned char*>(GlobalLock(memory));auto header=reinterpret_cast<DROPFILES*>(data);header->pFiles=sizeof(DROPFILES);header->fWide=TRUE;std::memcpy(data+sizeof(DROPFILES),names.data(),names.size()*sizeof(wchar_t));GlobalUnlock(memory);
        medium->tymed=TYMED_HGLOBAL;medium->hGlobal=memory;medium->pUnkForRelease=new MediumOwner(memory,stats);++stats.reads;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC* out) override{out->ptd=nullptr;return DATA_S_SAMEFORMATETC;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD,IEnumFORMATETC**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*) override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override{return OLE_E_ADVISENOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override{return OLE_E_ADVISENOTSUPPORTED;}
};
struct Target {
    bool enabled=true,received=true,throwReceive=false,hover=false;
    unsigned count=0,calls=0,errors=0;
    std::vector<fs::path> files;
    IDropTarget* native;
    Target():native(createFileDropTarget({[&](const auto&){return enabled;},[&](const auto& paths){++calls;if(throwReceive)throw std::runtime_error("public synthetic receive failure");files=paths;return received;},[&](bool active,unsigned items){hover=active;count=items;},[&](const auto&){++errors;}})){}
    ~Target(){native->Release();}
    HRESULT enter(FileData& source,DWORD& effect){return native->DragEnter(&source,0,{},&effect);}
    HRESULT drop(FileData& source,DWORD& effect){return native->Drop(&source,0,{},&effect);}
};
int wmain(int argc,wchar_t** argv){
    unsigned passed=0,total=0;auto check=[&](const char* name,const auto& test){++total;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    check("COM identity and reference ownership",[]{Target t;IUnknown* identity=nullptr;require(t.native->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity))==S_OK&&identity==t.native,"identity mismatch");identity->Release();void* unsupported=reinterpret_cast<void*>(1);require(t.native->QueryInterface(IID_IDataObject,&unsupported)==E_NOINTERFACE&&!unsupported,"unsupported interface exposed");require(t.native->QueryInterface(IID_IUnknown,nullptr)==E_POINTER,"null out accepted");});
    check("Unicode HDROP enter/drop uses native path decoding",[]{FileData source;Target t;DWORD effect=DROPEFFECT_COPY|DROPEFFECT_MOVE;require(t.enter(source,effect)==S_OK&&effect==DROPEFFECT_COPY&&t.hover&&t.count==2,"enter rejected");effect=DROPEFFECT_COPY;require(t.drop(source,effect)==S_OK&&effect==DROPEFFECT_COPY&&t.calls==1&&t.files==source.paths&&!t.hover,"drop mismatch");require(source.stats.reads==2&&source.stats.releases==2,"medium ownership leak");});
    check("Long paths are not truncated",[]{FileData source;source.paths={fs::path(L"C:\\drop-fixture")/(std::wstring(2000,L'长')+L".mp4")};Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);effect=DROPEFFECT_COPY;t.drop(source,effect);require(t.files==source.paths,"long path truncated");});
    check("Borrowed HDROP decoding leaves ownership with the caller",[]{FileData source;FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};require(source.GetData(&format,&medium)==S_OK,"cannot create borrowed payload");auto first=pathsFromDrop(static_cast<HDROP>(medium.hGlobal));auto second=pathsFromDrop(static_cast<HDROP>(medium.hGlobal));bool valid=first==source.paths&&second==source.paths&&source.stats.releases==0;ReleaseStgMedium(&medium);require(valid&&source.stats.releases==1,"borrowed payload was released by decoder");});
    check("DragOver reuses data without repeated GetData",[]{FileData source;Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);for(unsigned i=0;i<1000;++i){effect=DROPEFFECT_COPY;require(t.native->DragOver(0,{},&effect)==S_OK&&effect==DROPEFFECT_COPY,"over rejected");}require(source.stats.reads==1&&source.stats.releases==1,"over repeatedly decoded data");});
    check("Move-only source is never moved",[]{FileData source;Target t;DWORD effect=DROPEFFECT_MOVE;t.enter(source,effect);require(effect==DROPEFFECT_NONE&&source.stats.reads==0&&!t.hover,"move-only accepted");});
    check("Changed allowed effects are honored",[]{FileData source;Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);effect=DROPEFFECT_NONE;t.native->DragOver(0,{},&effect);require(effect==DROPEFFECT_NONE&&!t.hover,"over ignored permission change");effect=DROPEFFECT_NONE;t.drop(source,effect);require(effect==DROPEFFECT_NONE&&t.calls==0&&source.stats.reads==1,"drop ignored permission change");});
    check("Page or closing policy is rechecked before drop",[]{FileData source;Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);t.enabled=false;effect=DROPEFFECT_COPY;t.native->DragOver(0,{},&effect);require(effect==DROPEFFECT_NONE,"over kept stale policy");effect=DROPEFFECT_COPY;t.drop(source,effect);require(effect==DROPEFFECT_NONE&&t.calls==0,"drop kept stale policy");});
    check("Leave clears prior payload and hover",[]{FileData source;Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);t.native->DragLeave();effect=DROPEFFECT_COPY;t.drop(source,effect);require(!t.hover&&!t.count&&!t.calls&&effect==DROPEFFECT_NONE,"left drag still received");});
    check("Unsupported data is refused",[]{FileData source;source.supported=false;Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);require(effect==DROPEFFECT_NONE&&!t.hover&&!source.stats.reads,"unsupported data accepted");});
    check("Empty HDROP is refused and released",[]{FileData source;source.paths.clear();Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);require(effect==DROPEFFECT_NONE&&!t.hover&&source.stats.releases==1,"empty data accepted");});
    check("Failed GetData cannot call receiver",[]{FileData source;source.fail=true;Target t;DWORD effect=DROPEFFECT_COPY;require(FAILED(t.enter(source,effect))&&effect==DROPEFFECT_NONE&&!t.calls&&source.stats.releases==0,"failed enter received");});
    check("Failed second GetData preserves ownership",[]{FileData source;Target t;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);source.fail=true;effect=DROPEFFECT_COPY;require(FAILED(t.drop(source,effect))&&effect==DROPEFFECT_NONE&&!t.calls&&source.stats.releases==1,"failed drop received or leaked");});
    check("Receiver rejection is not reported as copy success",[]{FileData source;Target t;t.received=false;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);effect=DROPEFFECT_COPY;t.drop(source,effect);require(effect==DROPEFFECT_NONE&&t.calls==1&&source.stats.releases==2,"rejection reported as copy");});
    check("Receiver exception stays inside COM and releases data",[]{FileData source;Target t;t.throwReceive=true;DWORD effect=DROPEFFECT_COPY;t.enter(source,effect);effect=DROPEFFECT_COPY;require(FAILED(t.drop(source,effect))&&effect==DROPEFFECT_NONE&&t.errors==1&&source.stats.releases==2,"exception escaped or leaked");});
    check("Changed payload is validated again",[]{FileData source;Target t;auto target=createFileDropTarget({[](const auto& paths){return !paths.empty()&&paths.front().extension()==L".mp4";},[&](const auto&){++t.calls;return true;},[](bool,unsigned){},[](const auto&) {}});source.paths.resize(1);DWORD effect=DROPEFFECT_COPY;target->DragEnter(&source,0,{},&effect);source.paths={L"C:\\drop-fixture\\wrong.bin"};effect=DROPEFFECT_COPY;target->Drop(&source,0,{},&effect);target->Release();require(effect==DROPEFFECT_NONE&&!t.calls&&source.stats.releases==2,"changed payload bypassed policy");});
    if(argc>1)check("Native HDROP -> production queue -> real extraction result",[&]{
        auto folder=uniquePath(fs::path(argv[1])/L"drop-full-flow");fs::create_directories(folder);auto payload=folder/L"payload.txt";
        {std::ofstream out(payload,std::ios::binary);out<<"public native drop payload";}
        PasswordBook book(folder/L"isolated.dpapi");Engine engine(book);auto created=engine.compress({payload},folder/L"video.zip",L"");require(created.ok,"cannot create drop fixture");
        auto disguised=folder/L"video.mp4";fs::copy_file(created.output,disguised);Queue q;
        auto target=createFileDropTarget({[](const auto& paths){return !paths.empty();},[&](const auto& paths){bool added=false;for(const auto& path:paths){Task item;item.files={path};added=(q.add(item)!=0)||added;}return added;},[](bool,unsigned){},[](const auto&) {}});
        FileData source;source.paths={disguised};DWORD effect=DROPEFFECT_COPY;target->DragEnter(&source,0,{},&effect);effect=DROPEFFECT_COPY;target->Drop(&source,0,{},&effect);target->Release();
        require(effect==DROPEFFECT_COPY&&q.counts().waiting==1&&source.stats.reads==source.stats.releases,"drop admission failed");
        Options options;options.recycle=false;options.output=folder/L"output";runQueue(q,engine,options,[]{return false;},[](uint64_t){});auto item=q.snapshot().front();
        auto relative=item.result.output.lexically_relative(options.output);require(!relative.empty()&&relative!=L"."&&!relative.is_absolute()&&*relative.begin()!=L"..","drop output points outside the selected destination");
        std::ifstream decoded(item.result.output/L"payload.txt",std::ios::binary);std::string text{std::istreambuf_iterator<char>(decoded),{}};
        require(item.state==State::Done&&item.result.type==L"zip"&&text=="public native drop payload"&&fs::exists(disguised),"native drop extraction did not complete");
    });
    std::cout<<passed<<'/'<<total<<" native drop checks passed; no windows created.\n";return passed==total?0:1;
}
