#pragma once
#include "core.hpp"
#include <set>
#include <chrono>

namespace lu::ui {
enum class Action {Extract,Inspect,Restore,Create7z,CreateZip,Scan};
enum class State {Waiting,Running,Done,Attention,Cancelled};
enum class Page {Queue,Passwords,Volumes,TaskSettings};
constexpr size_t PageCount=4;
enum Id {Add=101,AddFolder,Volumes,Tools,Create,Remove,Up,Down,Start,Cancel,Choose,Reset,OpenOutput,QueueTab,PasswordTab,
 SavePasswords,ImportPasswords,NewPassword,Directory,BatchPassword,Nested,Recycle,Deep,List,Status,Retry,Extract,Inspect,Restore,Container,
 DirectoryLabel,BatchLabel,DropZone,TaskDetails,PasswordLabel,PasswordSearch,PasswordList,ShowPasswords,PasswordFeedback,
 VolumeList,VolumeAdd,VolumeRemove,VolumeUp,VolumeDown,AcceptVolumes,CancelVolumes,VolumeFeedback,ClearDone,QueueSearch,QueueFilter,SearchClear,PasteFiles,VolumePaste,VolumeSort,NextStep,ProgressBar,UsePassword,QueuePhase,
 ConfigureTask,HoldTask,TaskCustom,TaskDirectoryLabel,TaskDirectory,TaskChoose,TaskSourceDirectory,TaskNested,TaskRecycle,TaskDeep,TaskFeedback,TaskBack,TaskSave,TaskApply,PauseBatch,RetryVisible,First,Last,LocateSource,LocateProblem,RetryInner};
enum class TaskFilter {All,Waiting,Running,Attention,Done,Cancelled,Paused,NeedsPassword,VolumeProblem,SourceProblem,VerificationProblem};
inline constexpr std::array<const wchar_t*,11> TaskFilterNames{L"全部任务",L"待开始",L"处理中",L"需处理",L"已完成",L"已取消",L"已暂缓",L"需要密码",L"分卷问题",L"来源问题",L"校验未过"};
static_assert(TaskFilterNames.size()==size_t(TaskFilter::VerificationProblem)+1);
enum class QueueMove {Up,Down,First,Last};
struct TaskSettings {
    fs::path output;
    bool recursive=true,recycle=true,deep=true;
    bool operator==(const TaskSettings&) const = default;
};
struct Task {
    uint64_t id=0;
    std::vector<uint64_t> mergedFrom;
    std::vector<fs::path> files;
    Action action=Action::Extract;
    State state=State::Waiting;
    fs::path target;
    bool allowContainer=false,manualVolumes=false,refreshVolumes=false;
    bool paused=false;
    std::optional<TaskSettings> settings;
    std::wstring phase=L"等待开始";
    int percent=-1;
    uint64_t progressDone=0,progressTotal=0;
    ProgressUnit progressUnit=ProgressUnit::None;
    std::chrono::steady_clock::time_point started{};
    Result result;
};
struct Counts {
    unsigned waiting=0,running=0,done=0,attention=0,cancelled=0,paused=0;
    unsigned total() const {return waiting+running+done+attention+cancelled+paused;}
};
class Queue {
    mutable std::mutex mutex_;
    std::vector<Task> tasks_;
    std::map<std::wstring,unsigned> active_;
    std::map<std::wstring,unsigned> readers_;
    std::set<std::wstring> runningSources_;
    uint64_t nextId_=1;
    uint64_t revision_=0;
    void index(const Task& task,int delta);
public:
    uint64_t add(Task task);
    std::vector<Task> snapshot() const;
    std::optional<Task> get(uint64_t id) const;
    std::optional<Task> takeNext();
    unsigned resolve(uint64_t id,const std::vector<fs::path>& files);
    bool retainSources(uint64_t id,const std::vector<fs::path>& files);
    void progress(uint64_t id,const std::wstring& phase,uint64_t done,uint64_t total,ProgressUnit unit=ProgressUnit::None);
    void outputReady(uint64_t id,const fs::path& path);
    void finish(uint64_t id,Result result,bool cancelled);
    unsigned remove(const std::vector<uint64_t>& ids);
    unsigned reorder(const std::vector<uint64_t>& ids,QueueMove direction);
    unsigned retry(const std::vector<uint64_t>& ids);
    unsigned setAction(const std::vector<uint64_t>& ids,Action action,bool container=false);
    bool setVolumes(uint64_t id,const std::vector<fs::path>& files);
    bool replaceSource(uint64_t id,const fs::path& previous,const fs::path& replacement);
    unsigned configure(const std::vector<uint64_t>& ids,std::optional<TaskSettings> settings);
    unsigned setPaused(const std::vector<uint64_t>& ids,bool paused);
    uint64_t revision() const;
    void restore(std::vector<Task> tasks);
    Counts counts() const;
};
struct TaskView {
    std::wstring query;
    TaskFilter filter=TaskFilter::All;
    bool matches(const Task& task) const;
    std::vector<uint64_t> retryIds(const std::vector<Task>& tasks) const;
    bool unfiltered() const {return query.empty()&&filter==TaskFilter::All;}
};
class UpdateBatch {
    std::mutex mutex_;
    std::set<uint64_t> pending_;
public:
    bool add(uint64_t id);
    std::vector<uint64_t> take();
};
std::wstring pathKey(const fs::path& file);
std::wstring taskName(const Task& task);
std::wstring actionName(Action action);
std::wstring taskActionName(const Task& task);
std::wstring taskStatus(const Task& task);
std::wstring taskProgressCaption(const Task& task,std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now());
std::wstring nextStep(const Result& result);
std::wstring nextStep(const Task& task);
std::wstring taskDetails(const Task& task);
std::optional<Task> nestedTask(const Task& parent,const TaskSettings& defaults);
std::wstring settingsDescription(const std::optional<TaskSettings>& settings);
bool settingsAllowed(const Task& task);
TaskSettings settingsFrom(const Options& options);
Options applySettings(const Options& batch,const std::optional<TaskSettings>& settings);
struct TaskSettingsEditor {
    bool active=false,custom=false,mixed=false;
    std::vector<uint64_t> ids;
    TaskSettings value;
    void open(const std::vector<Task>& tasks,const TaskSettings& defaults);
};
std::wstring formatBytes(uint64_t bytes);
std::wstring countSummary(const Counts& counts);
std::wstring phaseCaption(const Counts& counts,bool running,bool stopping,const std::wstring& phase,bool pauseAfterCurrent=false);
bool retryable(const Task& task);
std::wstring retrySummary(unsigned selected,unsigned eligible,unsigned changed);
bool fileActionAllowed(const Task& task);
bool acceptsDrop(Page page,const std::vector<fs::path>& paths,bool busy);
struct Selection {std::vector<uint64_t> ids;uint64_t focus=0,anchor=0;};
Selection restoreSelection(const std::vector<Task>& after,const Selection& selection);
int findListItem(size_t count,const std::wstring& query,int start,bool prefix,bool wrap,const std::function<std::wstring(size_t)>& label);
size_t appendVolumes(std::vector<fs::path>& draft,const std::vector<fs::path>& paths);
std::vector<int> moveVolumes(std::vector<fs::path>& draft,std::vector<int> selected,int delta);
void sortVolumes(std::vector<fs::path>& draft);
std::vector<fs::path> parseFilePaths(const std::wstring& text);
struct VolumeEditor {
    uint64_t taskId=0,originId=0;
    bool active=false;
    fs::path originFile;
    std::optional<TaskSettings> settings;
    std::vector<fs::path> files;
    void open(uint64_t id,const std::vector<fs::path>& initial){if(!active||id!=taskId){files=initial;taskId=id;originId=id;originFile.clear();settings.reset();}active=true;}
    void open(const Task& task,const TaskSettings& defaults={});
};
struct Box {
    int x=0,y=0,width=0,height=0;
    int right() const {return x+width;}
    int bottom() const {return y+height;}
};
struct Layout {
    int width,height,listTop,listBottom,detailTop,detailHeight;
    bool showDrop;
    int volumeBottom,volumeFeedback,volumeActionsTop,volumeActionsBottom;
    std::array<Box,TaskApply-Add+1> boxes{};
    const Box& at(Id id) const {return boxes[size_t(id-Add)];}
};
Layout makeLayout(int width,int height);
struct Viewport {int width,height;bool horizontal,vertical;};
Viewport fitViewport(int availableWidth,int availableHeight,int barWidth,int barHeight,unsigned dpi);
Box pixelBox(const Box& box,unsigned dpi,int scrollX=0,int scrollY=0);
int revealOffset(int current,int viewport,int start,int end,int content);
std::vector<int> tableColumns(Page page,int clientWidth,unsigned dpi);
struct TableSizing {
    int clientWidth=-1;
    unsigned dpi=0,customDpi=96;
    std::vector<int> custom;
    std::vector<int> fit(Page page,int width,unsigned scale);
    void remember(std::vector<int> widths,unsigned scale){custom=std::move(widths);customDpi=scale;}
};
const std::vector<Id>& navigationOrder();
HFONT makeUiFont(unsigned dpi,int size=14,int weight=FW_NORMAL);
}
