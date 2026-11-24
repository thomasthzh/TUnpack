#include "ui_model.hpp"
#include <algorithm>
#include <cwchar>
#include <shlwapi.h>
#include <numeric>

namespace lu::ui {
std::wstring pathKey(const fs::path& file){return lower(fs::absolute(file).lexically_normal().wstring());}
static bool sameFiles(const std::vector<fs::path>& a,const std::vector<fs::path>& b){
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){return pathKey(x)==pathKey(y);});
}
static bool contains(const std::vector<fs::path>& group,const std::vector<fs::path>& files){
    return std::all_of(files.begin(),files.end(),[&](const auto& file){return std::any_of(group.begin(),group.end(),[&](const auto& member){return pathKey(file)==pathKey(member);});});
}
static std::wstring settingsKey(const std::optional<TaskSettings>& settings){
    if(!settings)return L"batch\n";
    return L"custom\n"+(settings->output.empty()?L"":pathKey(settings->output))+L"\n"+(settings->recursive?L"1":L"0")+(settings->recycle?L"1":L"0")+(settings->deep?L"1":L"0")+L"\n";
}
static std::wstring workPrefix(const Task& task){return std::to_wstring(int(task.action))+L"\n"+(task.allowContainer?L"1":L"0")+L"\n"+(task.target.empty()?L"":pathKey(task.target))+L"\n"+settingsKey(task.settings);}
static std::vector<std::wstring> taskKeys(const Task& task){
    auto prefix=workPrefix(task);
    std::vector<std::wstring> keys;std::wstring group=prefix;
    for(const auto& file:task.files){auto key=pathKey(file);group+=key+L"\n";keys.push_back(prefix+key+L"\n");}
    if(task.files.size()>1)keys.insert(keys.begin(),std::move(group));
    return keys;
}
void Queue::addRunningSource(uint64_t id,const std::wstring& key){
    if(runningByTask_[id].insert(key).second)++runningSources_[key];
}
void Queue::clearRunningTask(uint64_t id){
    auto it=runningByTask_.find(id);
    if(it==runningByTask_.end())return;
    for(const auto& key:it->second){
        auto src=runningSources_.find(key);
        if(src!=runningSources_.end()&&!--src->second)runningSources_.erase(src);
    }
    runningByTask_.erase(it);
}
void Queue::index(const Task& task,int delta){
    if(task.state!=State::Waiting&&task.state!=State::Running)return;
    for(const auto& key:taskKeys(task))if(delta>0)++active_[key];else {auto it=active_.find(key);if(it!=active_.end()&&!--it->second)active_.erase(it);}
    if(task.state==State::Running&&delta<0)clearRunningTask(task.id);
    for(const auto& file:task.files){auto key=pathKey(file);if(delta>0)++readers_[key];else {auto it=readers_.find(key);if(it!=readers_.end()&&!--it->second)readers_.erase(it);}if(task.state==State::Running&&delta>0)addRunningSource(task.id,key);}
}
uint64_t Queue::add(Task task){
    if(task.files.empty())return 0;
    for(auto& file:task.files)file=fs::absolute(file).lexically_normal();
    std::scoped_lock lock(mutex_);
    for(const auto& file:task.files)if(runningSources_.contains(pathKey(file)))return 0;
    if(active_.contains(taskKeys(task).front()))return 0;
    task.id=nextId_++;index(task,1);tasks_.push_back(std::move(task));++revision_;return tasks_.back().id;
}
std::vector<Task> Queue::snapshot() const {std::scoped_lock lock(mutex_);return tasks_;}
std::optional<Task> Queue::get(uint64_t id) const {std::scoped_lock lock(mutex_);for(const auto& task:tasks_)if(task.id==id)return task;return {};}
std::optional<Task> Queue::takeNext(bool allowConcurrent){
    std::scoped_lock lock(mutex_);
    if(!allowConcurrent&&!runningByTask_.empty())return {};
    for(auto& task:tasks_){
        if(task.state!=State::Waiting||task.paused)continue;
        if(allowConcurrent&&std::any_of(task.files.begin(),task.files.end(),[&](const auto& file){return runningSources_.contains(pathKey(file));}))continue;
        task.state=State::Running;
        for(const auto& file:task.files)addRunningSource(task.id,pathKey(file));
        task.phase=L"正在识别";task.started=std::chrono::steady_clock::now();task.progressDone=task.progressTotal=0;task.progressUnit=ProgressUnit::None;++revision_;return task;
    }
    return {};
}
unsigned Queue::resolve(uint64_t id,const std::vector<fs::path>& files){
    std::scoped_lock lock(mutex_);auto selected=std::find_if(tasks_.begin(),tasks_.end(),[&](const auto& t){return t.id==id;});
    if(selected==tasks_.end())return 0;
    index(*selected,-1);selected->files=files;index(*selected,1);auto work=workPrefix(*selected);++revision_;
    // Existing member-key counts tell us whether another pending task can match.
    // The common independent-archive case does not need a full queue traversal.
    if(selected->paused)return 0;
    auto keys=taskKeys(*selected);
    if(std::none_of(keys.begin(),keys.end(),[&](const auto& key){auto it=active_.find(key);return it!=active_.end()&&it->second>1;}))return 0;
    auto before=tasks_.size();std::vector<uint64_t> mergedIds;
    std::erase_if(tasks_,[&](const auto& t){bool merged=t.id!=id&&t.state==State::Waiting&&!t.paused&&workPrefix(t)==work&&
        (t.manualVolumes?sameFiles(t.files,files):contains(files,t.files));if(merged){index(t,-1);mergedIds.push_back(t.id);mergedIds.insert(mergedIds.end(),t.mergedFrom.begin(),t.mergedFrom.end());}return merged;});
    selected=std::find_if(tasks_.begin(),tasks_.end(),[&](const auto& t){return t.id==id;});selected->mergedFrom.insert(selected->mergedFrom.end(),mergedIds.begin(),mergedIds.end());
    
    return unsigned(before-tasks_.size());
}
bool Queue::retainSources(uint64_t id,const std::vector<fs::path>& files){
    std::scoped_lock lock(mutex_);auto task=std::find_if(tasks_.begin(),tasks_.end(),[&](const auto& t){return t.id==id&&t.state==State::Running;});
    if(task==tasks_.end())return true;
    bool retain=false;for(const auto& file:files){auto key=pathKey(file);addRunningSource(id,key);auto readers=readers_.find(key);auto own=contains(task->files,{file})?1u:0u;if(readers!=readers_.end()&&readers->second>own)retain=true;}
    return retain;
}
void Queue::progress(uint64_t id,const std::wstring& phase,uint64_t done,uint64_t total,ProgressUnit unit){
    std::scoped_lock lock(mutex_);for(auto& task:tasks_)if(task.id==id&&task.state==State::Running){task.phase=phase;task.progressDone=done;task.progressTotal=total;task.progressUnit=unit;task.percent=total?std::min(99,int(std::min(1.0L,static_cast<long double>(done)/total)*100)):-1;return;}
}
void Queue::finish(uint64_t id,Result result,bool cancelled){
    std::scoped_lock lock(mutex_);for(auto& task:tasks_)if(task.id==id){
        bool wasRunning=task.state==State::Running;index(task,-1);if(wasRunning)clearRunningTask(id);task.state=result.ok?State::Done:cancelled?State::Cancelled:State::Attention;
        if(result.output.empty())result.output=task.result.output;
        task.percent=result.ok?100:-1;task.result=std::move(result);task.phase=taskStatus(task);++revision_;return;
    }
}
void Queue::outputReady(uint64_t id,const fs::path& path){
    if(path.empty())return;std::scoped_lock lock(mutex_);for(auto& task:tasks_)if(task.id==id&&task.state==State::Running){if(task.result.output!=path){task.result.output=path;++revision_;}return;}
}
unsigned Queue::remove(const std::vector<uint64_t>& ids){
 std::set<uint64_t> selected(ids.begin(),ids.end());std::scoped_lock lock(mutex_);auto removed=unsigned(std::erase_if(tasks_,[&](const auto& t){bool remove=selected.contains(t.id)&&t.state!=State::Running;if(remove)index(t,-1);return remove;}));if(removed)++revision_;return removed;
}
unsigned Queue::reorder(const std::vector<uint64_t>& ids,QueueMove direction){
    if(ids.empty())return 0;std::set<uint64_t> selected(ids.begin(),ids.end());std::scoped_lock lock(mutex_);unsigned changed=0;std::vector<size_t> slots;
    auto arrange=[&]{
        if(slots.size()<2){slots.clear();return;}
        std::vector<size_t> order(slots.size());std::iota(order.begin(),order.end(),0);
        auto chosen=[&](size_t index){return selected.contains(tasks_[slots[index]].id);};
        switch(direction){
        case QueueMove::Up:for(size_t i=1;i<order.size();++i)if(chosen(order[i])&&!chosen(order[i-1]))std::swap(order[i],order[i-1]);break;
        case QueueMove::Down:for(size_t i=order.size()-1;i>0;--i)if(chosen(order[i-1])&&!chosen(order[i]))std::swap(order[i],order[i-1]);break;
        case QueueMove::First:std::stable_partition(order.begin(),order.end(),chosen);break;
        case QueueMove::Last:std::stable_partition(order.begin(),order.end(),[&](size_t index){return !chosen(index);});break;
        }
        unsigned moved=0;for(size_t i=0;i<order.size();++i)if(order[i]!=i&&chosen(order[i]))++moved;
        if(moved){std::vector<Task> arranged;arranged.reserve(slots.size());for(auto index:order)arranged.push_back(std::move(tasks_[slots[index]]));for(size_t i=0;i<slots.size();++i)tasks_[slots[i]]=std::move(arranged[i]);changed+=moved;}
        slots.clear();
    };
    for(size_t i=0;i<tasks_.size();++i){if(tasks_[i].state==State::Running)arrange();else if(tasks_[i].state==State::Waiting)slots.push_back(i);}arrange();if(changed)++revision_;return changed;
}
unsigned Queue::retry(const std::vector<uint64_t>& ids){
 std::set<uint64_t> selected(ids.begin(),ids.end());std::scoped_lock lock(mutex_);unsigned changed=0;
 for(auto& t:tasks_)if(selected.contains(t.id)&&t.state!=State::Running){if(std::any_of(t.files.begin(),t.files.end(),[&](const auto& f){return runningSources_.contains(pathKey(f));}))continue;index(t,-1);t.state=State::Waiting;t.paused=false;t.refreshVolumes=true;t.phase=L"等待重试";t.percent=-1;t.result={};index(t,1);++changed;}if(changed)++revision_;return changed;
}
unsigned Queue::setAction(const std::vector<uint64_t>& ids,Action action,bool container){
 std::set<uint64_t> selected(ids.begin(),ids.end());std::scoped_lock lock(mutex_);unsigned changed=0;
 for(auto& t:tasks_)if(selected.contains(t.id)&&t.state!=State::Running&&t.action!=Action::Scan&&t.action!=Action::Create7z&&t.action!=Action::CreateZip){
  if(std::any_of(t.files.begin(),t.files.end(),[&](const auto& f){return runningSources_.contains(pathKey(f));}))continue;
  index(t,-1);t.action=action;t.allowContainer=container;t.state=State::Waiting;t.refreshVolumes=true;t.phase=L"等待开始";t.percent=-1;t.result={};index(t,1);++changed;
 }if(changed)++revision_;return changed;
}
bool Queue::setVolumes(uint64_t id,const std::vector<fs::path>& files){
    std::scoped_lock lock(mutex_);for(auto& t:tasks_)if(t.id==id&&t.state!=State::Running&&t.action!=Action::Scan&&t.action!=Action::Create7z&&t.action!=Action::CreateZip){
        for(const auto& f:files)if(runningSources_.contains(pathKey(f)))return false;
        index(t,-1);t.files=files;t.manualVolumes=true;t.state=State::Waiting;t.phase=L"等待开始";t.percent=-1;t.result={};index(t,1);++revision_;return true;
    }return false;
}
unsigned Queue::configure(const std::vector<uint64_t>& ids,std::optional<TaskSettings> settings){
    if(settings&&!settings->output.empty())settings->output=fs::absolute(settings->output).lexically_normal();
    std::set<uint64_t> selected(ids.begin(),ids.end());std::scoped_lock lock(mutex_);unsigned changed=0;
    for(auto& task:tasks_)if(selected.contains(task.id)&&settingsAllowed(task)){if(task.settings!=settings){index(task,-1);task.settings=settings;index(task,1);++revision_;}++changed;}
    return changed;
}
bool Queue::replaceSource(uint64_t id,const fs::path& previous,const fs::path& replacement){
    if(previous.empty()||replacement.empty())return false;auto normalized=fs::absolute(replacement).lexically_normal();auto oldKey=pathKey(previous),newKey=pathKey(normalized);
    std::scoped_lock lock(mutex_);for(auto& task:tasks_)if(task.id==id&&task.state!=State::Running){
        auto at=std::find_if(task.files.begin(),task.files.end(),[&](const auto& file){return pathKey(file)==oldKey;});
        if(at==task.files.end())return false;
        if(oldKey!=newKey&&std::any_of(task.files.begin(),task.files.end(),[&](const auto& file){return pathKey(file)==newKey;}))return false;
        auto files=task.files;files[size_t(at-task.files.begin())]=normalized;
        if(!task.manualVolumes&&task.action!=Action::Scan&&task.action!=Action::Create7z&&task.action!=Action::CreateZip)files={normalized};
        if(std::any_of(files.begin(),files.end(),[&](const auto& file){return runningSources_.contains(pathKey(file));}))return false;
        index(task,-1);task.files=std::move(files);task.state=State::Waiting;task.refreshVolumes=true;task.phase=L"等待开始";task.percent=-1;task.result={};index(task,1);++revision_;return true;
    }return false;
}
unsigned Queue::setPaused(const std::vector<uint64_t>& ids,bool paused){
    std::set<uint64_t> selected(ids.begin(),ids.end());std::scoped_lock lock(mutex_);unsigned changed=0;
    for(auto& task:tasks_)if(selected.contains(task.id)&&task.state==State::Waiting&&task.paused!=paused){task.paused=paused;++changed;}
    if(changed)++revision_;return changed;
}
uint64_t Queue::revision() const {std::scoped_lock lock(mutex_);return revision_;}
void Queue::restore(std::vector<Task> tasks){
    std::scoped_lock lock(mutex_);if(!runningSources_.empty())throw std::runtime_error("Cannot restore an active queue");
    std::set<uint64_t> ids;uint64_t next=1;
    for(auto& task:tasks){if(!task.id||task.id==UINT64_MAX||!ids.insert(task.id).second||task.files.empty())throw std::runtime_error("Invalid saved task identity");next=std::max(next,task.id+1);for(auto merged:task.mergedFrom){if(!merged||merged==UINT64_MAX)throw std::runtime_error("Invalid merged task identity");next=std::max(next,merged+1);}for(auto& file:task.files)file=fs::absolute(file).lexically_normal();
        if(task.state==State::Running){task.state=State::Cancelled;task.paused=false;task.result.ok=false;task.result.note=L"上次处理被中断。可查看保留的输出，重试会从头处理并使用新目录。";task.result.status=L"interrupted";}
        task.started={};task.progressDone=task.progressTotal=0;task.progressUnit=ProgressUnit::None;if(!task.manualVolumes)task.refreshVolumes=true;task.percent=task.state==State::Done?100:-1;
    }
    tasks_=std::move(tasks);active_.clear();readers_.clear();nextId_=next;
    for(auto& task:tasks_){task.phase=task.state==State::Waiting?L"等待开始":taskStatus(task);index(task,1);}++revision_;
}
Counts Queue::counts() const {Counts c;std::scoped_lock lock(mutex_);for(const auto& t:tasks_)switch(t.state){case State::Waiting:if(t.paused)++c.paused;else ++c.waiting;break;case State::Running:++c.running;break;case State::Done:++c.done;break;case State::Attention:++c.attention;break;case State::Cancelled:++c.cancelled;break;}return c;}
bool TaskView::matches(const Task& task) const {
    if(filter==TaskFilter::Waiting&&(task.state!=State::Waiting||task.paused))return false;
    if(filter==TaskFilter::Paused&&(task.state!=State::Waiting||!task.paused))return false;
    if(filter==TaskFilter::Running&&task.state!=State::Running)return false;
    if(filter==TaskFilter::Attention&&task.state!=State::Attention)return false;
    if(filter==TaskFilter::Done&&task.state!=State::Done)return false;
    if(filter==TaskFilter::Cancelled&&task.state!=State::Cancelled)return false;
    if(filter==TaskFilter::NeedsPassword&&(task.state!=State::Attention||task.result.status!=L"password_required"))return false;
    if(filter==TaskFilter::VolumeProblem&&(task.state!=State::Attention||(task.result.status!=L"missing_volume"&&task.result.status!=L"ambiguous_volume")))return false;
    if(filter==TaskFilter::SourceProblem&&(task.state!=State::Attention||task.result.status!=L"source_unavailable"))return false;
    if(filter==TaskFilter::VerificationProblem&&(task.state!=State::Attention||task.result.status!=L"verification_failed"))return false;
    if(query.empty())return true;
    auto needle=lower(query);auto pathMatches=[&](const auto& path){return !path.empty()&&lower(path.wstring()).find(needle)!=std::wstring::npos;};
    if(pathMatches(task.target)||pathMatches(task.result.failedFile)||pathMatches(task.result.output))return true;
    return std::any_of(task.files.begin(),task.files.end(),pathMatches);
}
std::vector<uint64_t> TaskView::retryIds(const std::vector<Task>& tasks) const {std::vector<uint64_t> ids;for(const auto& task:tasks)if(retryable(task)&&matches(task))ids.push_back(task.id);return ids;}
bool UpdateBatch::add(uint64_t id){std::scoped_lock lock(mutex_);bool first=pending_.empty();pending_.insert(id);return first;}
std::vector<uint64_t> UpdateBatch::take(){std::scoped_lock lock(mutex_);std::vector<uint64_t> ids(pending_.begin(),pending_.end());pending_.clear();return ids;}
std::wstring actionName(Action action){switch(action){case Action::Extract:return L"解压";case Action::Inspect:return L"识别";case Action::Restore:return L"另存原包";case Action::Create7z:return L"创建 7z";case Action::CreateZip:return L"创建 ZIP";case Action::Scan:return L"扫描文件夹";}return {};}
std::wstring taskActionName(const Task& task){return task.action==Action::Extract&&task.allowContainer?L"容器解压":actionName(task.action);}
std::wstring taskName(const Task& task){auto name=(task.target.empty()?task.files.front():task.target).filename().wstring();if(task.files.size()>1&&task.action!=Action::Create7z&&task.action!=Action::CreateZip)name+=L" · "+std::to_wstring(task.files.size())+L" 卷";return name;}
std::wstring taskStatus(const Task& task){
    if(task.state==State::Waiting&&task.paused)return L"已暂缓";
    if(task.state==State::Waiting||task.state==State::Running)return task.phase;
    if(task.state==State::Cancelled)return L"已取消";
    if(task.state==State::Done)return task.action==Action::Inspect?L"已识别":task.action==Action::Scan?L"已扫描":task.action==Action::Restore?L"已另存":task.action==Action::Extract?L"已解压":L"已创建";
    if(task.result.status==L"password_required")return L"需要密码";
    if(task.result.status==L"missing_volume")return L"缺少分卷";
    if(task.result.status==L"ambiguous_volume")return L"确认分卷";
    if(task.result.status==L"verification_failed")return L"校验未通过";
    if(task.result.status==L"source_unavailable")return L"来源无法访问";
    return L"需要处理";
}
std::wstring taskProgressCaption(const Task& task,std::chrono::steady_clock::time_point now){
    auto text=task.phase;if(task.state!=State::Running)return text;
    auto number=[&](uint64_t value){return task.progressUnit==ProgressUnit::Bytes?formatBytes(value):std::to_wstring(value);};
    if(task.progressUnit!=ProgressUnit::None){
        auto suffix=task.progressUnit==ProgressUnit::Files?L" 个文件":task.progressUnit==ProgressUnit::Candidates?L" 个候选":task.progressUnit==ProgressUnit::Items?L" 项":L"";
        text+=L" · "+number(task.progressDone)+(task.progressTotal?L" / "+number(task.progressTotal):L"")+suffix;
    }
    if(task.started!=std::chrono::steady_clock::time_point{}){
        auto seconds=std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::seconds>(now-task.started).count());wchar_t duration[48];swprintf(duration,std::size(duration),L"%lld:%02lld",static_cast<long long>(seconds/60),static_cast<long long>(seconds%60));text+=L" · 已用时 "+std::wstring(duration);
    }return text;
}
std::wstring nextStep(const Result& result){
    if(result.status==L"source_unavailable")return L"来源可能已移动、改名或断开连接。可重新定位源文件，也可恢复原路径后重试。";
    if(result.status==L"password_required")return L"在下方补充本批密码，或到密码本导入，再重试此任务。";
    if(result.status==L"missing_volume"||result.status==L"ambiguous_volume")return result.failedFile.empty()?L"点击“补充分卷”，补齐文件并确认首卷在第一行，再开始。":L"点击“补充分卷”，为下方失败的内层文件建立单独任务；外层结果和原包保留。";
    if(result.status==L"verification_failed")return L"当前数据未通过完整校验。请检查分卷或获取完整副本；原包已保留。";
    return {};
}
std::wstring nextStep(const Task& task){
    if(nestedTask(task,{}))return (task.result.status==L"password_required"?std::wstring(L"先填写本批密码或导入密码本，再从任务操作选择“只重试内层包”。"):std::wstring(L"问题处理好后，可从任务操作选择“只重试内层包”；缺卷请用“补充分卷”。"))+L"只准备该内层任务，不重复解压外层；原任务仍标记未完成，已有结果保留。";
    if(task.state==State::Cancelled)return L"任务已取消。点击“重试”后再开始，未开始的任务仍在队列。";
    return task.state==State::Attention?nextStep(task.result):L"";
}
std::wstring taskDetails(const Task& task){
    auto text=taskActionName(task)+L" · "+taskName(task)+L" · "+taskStatus(task)+L"\r\n";
    text+=L"下次处理设置："+settingsDescription(task.settings)+L"\r\n";
    if(!task.result.type.empty())text+=L"识别格式："+task.result.type+L"\r\n";
    if(!task.result.note.empty())text+=task.result.note+L"\r\n";
    auto next=nextStep(task);if(!next.empty())text+=next+L"\r\n";
    if(!task.result.failedFile.empty())text+=(task.result.status==L"source_unavailable"?L"无法访问的来源：":task.action==Action::Scan?L"未读取的文件或目录：":L"失败的内层文件：")+task.result.failedFile.wstring()+L"\r\n";
    if(!task.result.output.empty())text+=(task.state==State::Running?L"正在写入：":task.state==State::Done?L"输出：":L"保留输出：")+task.result.output.wstring()+L"\r\n";
    text+=L"来源：";for(size_t i=0;i<task.files.size();++i)text+=(i?L"\r\n":L"")+task.files[i].wstring();
    return text;
}
std::optional<Task> nestedTask(const Task& parent,const TaskSettings& defaults){
    if(parent.action!=Action::Extract||(parent.state!=State::Attention&&parent.state!=State::Cancelled)||parent.result.status==L"source_unavailable"||parent.result.output.empty()||parent.result.failedFile.empty())return {};
    auto file=fs::path(pathKey(parent.result.failedFile)),root=fs::path(pathKey(parent.result.output));
    auto relative=file.lexically_relative(root);
    if(relative.empty()||relative==L"."||*relative.begin()==L".."||std::any_of(parent.files.begin(),parent.files.end(),[&](const auto& source){return pathKey(source)==file.wstring();}))return {};
    Task child;child.files={parent.result.failedFile};child.settings=parent.settings.value_or(defaults);child.settings->output=parent.result.failedFile.parent_path();return child;
}
void VolumeEditor::open(const Task& task,const TaskSettings& defaults){
    auto child=nestedTask(task,defaults);
    if(child&&(task.result.status==L"missing_volume"||task.result.status==L"ambiguous_volume")){
        if(!active||originId!=task.id||taskId!=0||pathKey(originFile)!=pathKey(task.result.failedFile)){files={task.result.failedFile};taskId=0;originId=task.id;originFile=task.result.failedFile;}
        active=true;
        settings=child->settings;
    }else {open(task.id,task.files);settings=task.settings;}
}
TaskSettings settingsFrom(const Options& options){return {options.output,options.recursive,options.recycle,options.deep};}
Options applySettings(const Options& batch,const std::optional<TaskSettings>& settings){
    auto result=batch;if(settings){result.output=settings->output;result.recursive=settings->recursive;result.recycle=settings->recycle;result.deep=settings->deep;}return result;
}
bool settingsAllowed(const Task& task){return task.state!=State::Running&&task.action!=Action::Create7z&&task.action!=Action::CreateZip;}
std::wstring settingsDescription(const std::optional<TaskSettings>& settings){
    if(!settings)return L"跟随本批设置";
    return (settings->output.empty()?std::wstring(L"原包目录"):settings->output.wstring())+L" · "+(settings->recursive?L"展开嵌套":L"不展开嵌套")+L" · "+(settings->recycle?L"解压后回收源":L"保留源文件")+L" · "+(settings->deep?L"深度识别":L"快速识别");
}
void TaskSettingsEditor::open(const std::vector<Task>& tasks,const TaskSettings& defaults){
    std::vector<uint64_t> selected;for(const auto& task:tasks)if(settingsAllowed(task))selected.push_back(task.id);
    if(active&&selected==ids)return;
    ids=std::move(selected);active=!ids.empty();mixed=false;if(!active)return;
    auto first=std::find_if(tasks.begin(),tasks.end(),[](const auto& task){return settingsAllowed(task);});custom=first->settings.has_value();value=first->settings.value_or(defaults);
    for(const auto& task:tasks)if(settingsAllowed(task)&&settingsKey(task.settings)!=settingsKey(first->settings))mixed=true;
}
std::wstring formatBytes(uint64_t bytes){
    if(bytes<1024)return std::to_wstring(bytes)+L" B";
    const wchar_t* units[]={L"KiB",L"MiB",L"GiB",L"TiB"};double value=double(bytes)/1024;unsigned unit=0;while(value>=1024&&unit<3){value/=1024;++unit;}
    wchar_t out[64];swprintf(out,64,L"%.1f %ls",value,units[unit]);return out;
}
std::wstring countSummary(const Counts& c){auto text=L"待开始 "+std::to_wstring(c.waiting)+L"   ·   处理中 "+std::to_wstring(c.running)+L"   ·   完成 "+std::to_wstring(c.done)+L"   ·   需处理 "+std::to_wstring(c.attention)+L"   ·   已取消 "+std::to_wstring(c.cancelled);if(c.paused)text+=L"   ·   暂缓 "+std::to_wstring(c.paused);return text;}
std::wstring phaseCaption(const Counts& c,bool running,bool stopping,const std::wstring& phase,bool pauseAfterCurrent){
    if(running&&stopping)return L"正在停止当前操作…";
    if(running&&pauseAfterCurrent)return L"当前项完成后暂停 · "+(phase.empty()?std::wstring(L"正在完成当前项…"):phase);
    if(running)return c.running&&!phase.empty()?phase:L"正在准备下一个任务…";
    if(c.waiting&&pauseAfterCurrent)return L"批次已暂停 · 待开始 "+std::to_wstring(c.waiting)+L" 项 · 点击继续全部";
    if(c.waiting)return L"待开始 "+std::to_wstring(c.waiting)+L" 项 · 检查设置后开始全部";
    if(c.paused)return L"暂缓 "+std::to_wstring(c.paused)+L" 项 · 选中后可继续";
    if(c.attention)return L"有 "+std::to_wstring(c.attention)+L" 项需处理 · 选中查看下一步";
    if(c.cancelled)return L"取消的任务可重试 · 可以继续添加文件";
    return L"准备就绪 · 可以继续添加文件";
}
bool retryable(const Task& task){return task.state==State::Attention||task.state==State::Cancelled;}
std::wstring retrySummary(unsigned selected,unsigned eligible,unsigned changed){
    auto note=L"已加入待开始队列 "+std::to_wstring(changed)+L" 项。";
    if(selected>eligible)note+=L" 跳过 "+std::to_wstring(selected-eligible)+L" 项（仅重试需处理或已取消的任务）。";
    if(eligible>changed)note+=L" "+std::to_wstring(eligible-changed)+L" 项仍在使用原包，本轮结束后可重试。";
    if(changed)note+=L" 开始全部后处理。";
    return note;
}
bool fileActionAllowed(const Task& task){return task.state!=State::Running&&task.action!=Action::Scan&&task.action!=Action::Create7z&&task.action!=Action::CreateZip;}
bool acceptsDrop(Page page,const std::vector<fs::path>& paths,bool busy){
    if(page==Page::TaskSettings)return false;
    if(paths.empty())return false;
    if(page!=Page::Passwords)return true;
    return !busy&&std::any_of(paths.begin(),paths.end(),[](const auto& p){return lower(p.extension().wstring())==L".txt";});
}
Selection restoreSelection(const std::vector<Task>& after,const Selection& selection){
    std::map<uint64_t,uint64_t> surviving;for(const auto& task:after){surviving[task.id]=task.id;for(auto id:task.mergedFrom)surviving[id]=task.id;}
    auto survivor=[&](uint64_t id){auto it=surviving.find(id);return it==surviving.end()?uint64_t(0):it->second;};
    Selection restored;std::set<uint64_t> seen;for(auto id:selection.ids)if(auto mapped=survivor(id);mapped&&seen.insert(mapped).second)restored.ids.push_back(mapped);
    restored.focus=survivor(selection.focus);restored.anchor=survivor(selection.anchor);if(!restored.focus&&!restored.ids.empty())restored.focus=restored.ids.front();return restored;
}
int findListItem(size_t count,const std::wstring& query,int start,bool prefix,bool wrap,const std::function<std::wstring(size_t)>& label){
    if(!count||start<0)return -1;
    if(size_t(start)>=count){if(!wrap)return -1;start=int(size_t(start)%count);}
    auto needle=lower(query);size_t limit=wrap?count:count-size_t(start);
    for(size_t step=0;step<limit;++step){auto index=(size_t(start)+step)%count;auto candidate=lower(label(index));if(prefix?candidate.starts_with(needle):candidate==needle)return int(index);}
    return -1;
}
size_t appendVolumes(std::vector<fs::path>& draft,const std::vector<fs::path>& paths){size_t added=0;for(const auto& p:paths)if(!contains(draft,{p})){draft.push_back(fs::absolute(p).lexically_normal());++added;}return added;}
std::vector<int> moveVolumes(std::vector<fs::path>& draft,std::vector<int> selected,int delta){
    std::sort(selected.begin(),selected.end());selected.erase(std::unique(selected.begin(),selected.end()),selected.end());
    if(selected.empty()||selected.front()<0||selected.back()>=int(draft.size()))return selected;
    if(delta<0){if(!selected.front())return selected;for(auto& index:selected){std::swap(draft[size_t(index)],draft[size_t(index-1)]);--index;}}
    else if(delta>0){if(selected.back()+1==int(draft.size()))return selected;for(auto it=selected.rbegin();it!=selected.rend();++it){std::swap(draft[size_t(*it)],draft[size_t(*it+1)]);++*it;}}
    return selected;
}
std::vector<fs::path> parseFilePaths(const std::wstring& text){
    std::vector<fs::path> paths;for(auto line:splitLines(text)){auto first=line.find_first_not_of(L" \t");if(first==std::wstring::npos)continue;auto last=line.find_last_not_of(L" \t");line=line.substr(first,last-first+1);if(line.size()>1&&line.front()==L'"'&&line.back()==L'"')line=line.substr(1,line.size()-2);if(!line.empty())paths.emplace_back(line);}return paths;
}
void sortVolumes(std::vector<fs::path>& draft){std::stable_sort(draft.begin(),draft.end(),[](const auto& a,const auto& b){return StrCmpLogicalW(a.filename().c_str(),b.filename().c_str())<0;});}
}
