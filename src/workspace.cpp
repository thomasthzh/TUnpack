#include "workspace.hpp"
#include "storage.hpp"
#include <fstream>
#include <set>
#include <stdexcept>

namespace lu::ui {
namespace {
constexpr size_t MaxWorkspace=32*1024*1024;
constexpr char Magic[]="TUNPKW1\n";
struct Writer {
    std::string data=Magic;
    void number(uint64_t value){storage::put64(data,value);}
    void text(const std::wstring& value){storage::putString(data,value);}
    void path(const fs::path& value){text(value.wstring());}
    void settings(const TaskSettings& value){path(value.output);number(value.recursive);number(value.recycle);number(value.deep);}
    void policy(const std::optional<TaskSettings>& value){number(value.has_value());if(value)settings(*value);}
    void paths(const std::vector<fs::path>& values){if(values.size()>100000)throw std::runtime_error("Workspace source count exceeds 100000");number(values.size());for(const auto& value:values)path(value);}
    void ids(const std::vector<uint64_t>& values){if(values.size()>100000)throw std::runtime_error("Workspace selection count exceeds 100000");number(values.size());for(auto value:values)number(value);}
};
struct Reader {
    const std::string& data;size_t position=sizeof(Magic)-1;
    uint64_t number(uint64_t maximum=UINT64_MAX){auto value=storage::get64(data,position);if(value>maximum)throw std::runtime_error("Invalid workspace value");return value;}
    bool boolean(){return number(1)!=0;}
    size_t count(){auto value=number(100000);if(value>(data.size()-position)/8)throw std::runtime_error("Invalid workspace collection");return size_t(value);}
    std::wstring text(){return storage::getString(data,position);}
    fs::path path(){return fs::path(text());}
    TaskSettings settings(){TaskSettings value;value.output=path();value.recursive=boolean();value.recycle=boolean();value.deep=boolean();return value;}
    std::optional<TaskSettings> policy(){if(!boolean())return {};return settings();}
    std::vector<fs::path> paths(){std::vector<fs::path> values;auto n=count();values.reserve(n);while(n--)values.push_back(path());return values;}
    std::vector<uint64_t> ids(){std::vector<uint64_t> values;auto n=count();values.reserve(n);while(n--)values.push_back(number());return values;}
};
void writeResult(Writer& w,const Result& result){
    w.number(result.ok);w.path(result.output);w.path(result.failedFile);w.text(result.type);w.text(result.note);w.text(result.status);w.text(result.engineError);
    w.number(result.archives);w.number(result.attempts);w.number(result.bytes);w.number(result.offset);w.number(result.scannedFiles);w.number(result.skippedFiles);
    w.number(result.apate);w.number(result.encrypted);w.number(result.recovered);w.number(result.passwordVerified);
}
Result readResult(Reader& r){
    Result value;value.ok=r.boolean();value.output=r.path();value.failedFile=r.path();value.type=r.text();value.note=r.text();value.status=r.text();value.engineError=r.text();
    value.archives=unsigned(r.number(UINT32_MAX));value.attempts=unsigned(r.number(UINT32_MAX));value.bytes=r.number();value.offset=r.number();value.scannedFiles=r.number();value.skippedFiles=r.number();
    value.apate=r.boolean();value.encrypted=r.boolean();value.recovered=r.boolean();value.passwordVerified=r.boolean();return value;
}
}
WorkspaceStore::WorkspaceStore(fs::path file):file_(file.empty()?storage::dataPath(L"workspace.dat"):std::move(file)){}
void WorkspaceStore::save(const Workspace& workspace) const {
    if(workspace.tasks.size()>100000)throw std::runtime_error("Workspace task count exceeds 100000");
    Writer w;w.settings(workspace.defaults);w.number(workspace.width);w.number(workspace.height);w.number(workspace.maximized);w.number(unsigned(workspace.page));w.text(workspace.view.query);w.number(unsigned(workspace.view.filter));
    w.ids(workspace.selection.ids);w.number(workspace.selection.focus);w.number(workspace.selection.anchor);
    w.number(workspace.tasks.size());for(const auto& task:workspace.tasks){
        w.number(task.id);w.ids(task.mergedFrom);w.paths(task.files);w.number(unsigned(task.action));w.number(unsigned(task.state));w.path(task.target);w.number(task.allowContainer);w.number(task.manualVolumes);w.number(task.paused);w.policy(task.settings);writeResult(w,task.result);
    }
    const auto& v=workspace.volumes;w.number(v.active);w.number(v.taskId);w.number(v.originId);w.path(v.originFile);w.paths(v.files);w.policy(v.settings);
    const auto& editor=workspace.editor;w.number(editor.active);w.number(editor.custom);w.number(editor.mixed);w.ids(editor.ids);w.settings(editor.value);
    for(size_t i=0;i<PageCount;++i){w.number(workspace.columns[i].customDpi);w.number(workspace.columns[i].custom.size());for(auto width:workspace.columns[i].custom)w.number(width);w.number(workspace.scroll[i].x);w.number(workspace.scroll[i].y);w.number(workspace.focus[i]);}
    if(w.data.size()>MaxWorkspace)throw std::runtime_error("Workspace exceeds 32 MiB");storage::atomicWrite(file_,w.data);
}
std::optional<Workspace> WorkspaceStore::load() const {
    if(!fs::exists(file_))return {};
    auto size=fs::file_size(file_);if(size>MaxWorkspace||size<sizeof(Magic)-1)throw std::runtime_error("Invalid workspace size");
    std::ifstream input(file_,std::ios::binary);std::string data(size,'\0');if(!input.read(data.data(),std::streamsize(size))||input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Cannot read workspace");
    if(data.compare(0,sizeof(Magic)-1,Magic)!=0)throw std::runtime_error("Unsupported workspace version");Reader r{data};Workspace value;
    value.defaults=r.settings();value.width=int(r.number(20000));value.height=int(r.number(20000));if(value.width<320||value.height<200)throw std::runtime_error("Invalid workspace window size");value.maximized=r.boolean();value.page=Page(r.number(unsigned(Page::TaskSettings)));value.view.query=r.text();value.view.filter=TaskFilter(r.number(unsigned(TaskFilter::VerificationProblem)));
    value.selection.ids=r.ids();value.selection.focus=r.number();value.selection.anchor=r.number();auto n=r.count();std::set<uint64_t> ids;
    while(n--){Task task;task.id=r.number(UINT64_MAX-1);if(!task.id||!ids.insert(task.id).second)throw std::runtime_error("Invalid workspace task identity");task.mergedFrom=r.ids();task.files=r.paths();if(task.files.empty())throw std::runtime_error("Empty workspace source");task.action=Action(r.number(unsigned(Action::Scan)));task.state=State(r.number(unsigned(State::Cancelled)));task.target=r.path();task.allowContainer=r.boolean();task.manualVolumes=r.boolean();task.paused=r.boolean();task.settings=r.policy();task.result=readResult(r);value.tasks.push_back(std::move(task));}
    auto& v=value.volumes;v.active=r.boolean();v.taskId=r.number();v.originId=r.number();v.originFile=r.path();v.files=r.paths();v.settings=r.policy();if(!ids.contains(v.taskId))v.taskId=0;
    auto& editor=value.editor;editor.active=r.boolean();editor.custom=r.boolean();editor.mixed=r.boolean();editor.ids=r.ids();editor.value=r.settings();std::erase_if(editor.ids,[&](auto id){return !ids.contains(id);});if(editor.ids.empty())editor.active=false;
    for(size_t i=0;i<PageCount;++i){auto& column=value.columns[i];column.customDpi=unsigned(r.number(10000));if(!column.customDpi)throw std::runtime_error("Invalid workspace DPI");auto count=r.number(8);while(count--)column.custom.push_back(int(r.number(200000)));value.scroll[i].x=LONG(r.number(200000));value.scroll[i].y=LONG(r.number(200000));value.focus[i]=int(r.number(10000));}
    if(r.position!=data.size())throw std::runtime_error("Unexpected workspace data");return value;
}
fs::path WorkspaceStore::preserveUnreadable() const {auto backup=file_;backup+=L".unreadable";backup=uniquePath(backup);fs::copy_file(file_,backup);return backup;}
WorkspaceWriter::WorkspaceWriter(fs::path file):store_(std::move(file)),thread_([this]{run();}){}
WorkspaceWriter::~WorkspaceWriter(){flush();{std::scoped_lock lock(mutex_);stopping_=true;}condition_.notify_all();thread_.join();}
void WorkspaceWriter::submit(Workspace workspace){{std::scoped_lock lock(mutex_);pending_=std::move(workspace);}condition_.notify_all();}
bool WorkspaceWriter::flush(){std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return !pending_&&!busy_;});return error_.empty();}
std::wstring WorkspaceWriter::error() const {std::scoped_lock lock(mutex_);return error_;}
void WorkspaceWriter::run(){
    for(;;){std::unique_lock lock(mutex_);condition_.wait(lock,[&]{return stopping_||pending_.has_value();});if(stopping_&&!pending_)return;auto workspace=std::move(*pending_);pending_.reset();busy_=true;lock.unlock();
        std::wstring error;try{store_.save(workspace);}catch(const std::exception& e){error=errorText(e);}
        lock.lock();error_=std::move(error);busy_=false;condition_.notify_all();
    }
}
}
