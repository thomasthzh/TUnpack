#include "file_drop.hpp"
#include <atomic>
#include <stdexcept>

namespace lu::ui {
std::vector<fs::path> pathsFromDrop(HDROP drop){
    std::vector<fs::path> paths;
    auto count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);
    paths.reserve(count);
    for(UINT i=0;i<count;++i){auto n=DragQueryFileW(drop,i,nullptr,0);std::wstring name(n+1,L'\0');DragQueryFileW(drop,i,name.data(),n+1);name.resize(n);paths.emplace_back(name);}
    return paths;
}
class FileDropTarget final:public IDropTarget {
    std::atomic_ulong references_=1;
    FileDropCallbacks callbacks_;
    std::vector<fs::path> paths_;
    bool entered_=false;
    struct Medium {
        STGMEDIUM value{};
        ~Medium(){if(value.tymed!=TYMED_NULL)ReleaseStgMedium(&value);}
    };
    void reset(){entered_=false;paths_.clear();callbacks_.hover(false,0);}
    bool accepts(DWORD effect) const {return entered_&&(effect&DROPEFFECT_COPY)&&!paths_.empty()&&callbacks_.accepts(paths_);}
    void report(const std::exception& error) noexcept {try{callbacks_.error(wide(error.what()));}catch(...){} }
public:
    explicit FileDropTarget(FileDropCallbacks callbacks):callbacks_(std::move(callbacks)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        if(iid==IID_IUnknown||iid==IID_IDropTarget){*out=static_cast<IDropTarget*>(this);AddRef();return S_OK;}*out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {auto count=--references_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data,DWORD,POINTL,DWORD* effect) override {
        auto allowed=*effect;*effect=DROPEFFECT_NONE;
        try{
            reset();FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
            if(!(allowed&DROPEFFECT_COPY)||data->QueryGetData(&format)!=S_OK)return S_OK;
            Medium medium;auto hr=data->GetData(&format,&medium.value);if(FAILED(hr))return hr;
            paths_=pathsFromDrop(static_cast<HDROP>(medium.value.hGlobal));entered_=true;
            bool accepted=accepts(allowed);callbacks_.hover(accepted,unsigned(paths_.size()));*effect=accepted?DROPEFFECT_COPY:DROPEFFECT_NONE;return S_OK;
        }catch(const std::exception& error){report(error);entered_=false;return E_FAIL;}catch(...){entered_=false;return E_FAIL;}
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD* effect) override {
        auto allowed=*effect;*effect=DROPEFFECT_NONE;
        try{bool accepted=accepts(allowed);callbacks_.hover(accepted,unsigned(paths_.size()));*effect=accepted?DROPEFFECT_COPY:DROPEFFECT_NONE;return S_OK;}
        catch(const std::exception& error){report(error);return E_FAIL;}catch(...){return E_FAIL;}
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {try{reset();return S_OK;}catch(...){return E_FAIL;}}
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data,DWORD,POINTL,DWORD* effect) override {
        auto allowed=*effect;*effect=DROPEFFECT_NONE;
        try{
            bool accepted=accepts(allowed);reset();if(!accepted)return S_OK;
            FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};Medium medium;
            auto hr=data->GetData(&format,&medium.value);if(FAILED(hr))return hr;
            auto paths=pathsFromDrop(static_cast<HDROP>(medium.value.hGlobal));
            if(!paths.empty()&&callbacks_.accepts(paths)&&callbacks_.receive(paths))*effect=DROPEFFECT_COPY;
            return S_OK;
        }catch(const std::exception& error){report(error);entered_=false;return E_FAIL;}catch(...){entered_=false;return E_FAIL;}
    }
};
IDropTarget* createFileDropTarget(FileDropCallbacks callbacks){return new FileDropTarget(std::move(callbacks));}
}
