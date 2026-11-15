#include "queue_runner.hpp"
#include "file_drop.hpp"
#include "workspace.hpp"
#include "shell_open.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <array>

namespace {
using namespace lu;
using namespace lu::ui;
constexpr UINT WM_QUEUE_UPDATE=WM_APP+1,WM_FINISHED=WM_APP+2;
constexpr UINT_PTR QueueRefreshTimer=7001,PasswordPreviewTimer=7002,WorkspaceTimer=7003;
HWND tips,phaseStatus;
std::wstring currentPhase,tooltipText;
uint64_t activeTaskId=0;
HWND window,list,progress,status,passwordEdit,directoryEdit,batchEdit,passwordList,volumeList;
std::vector<HWND> queueControls,passwordControls,volumeControls,taskControls,dropWindows;
std::array<std::vector<HWND>*,PageCount> pageGroups{&queueControls,&passwordControls,&volumeControls,&taskControls};
TaskSettingsEditor taskEditor;
Queue queue;
TaskView taskView;
UpdateBatch backgroundUpdates;
struct Row {Task task;std::array<std::wstring,5> cells;};
std::vector<Row> rows;
std::vector<Password> passwordRows;
std::vector<std::array<std::wstring,3>> passwordCells;
VolumeEditor volumeEditor;
std::vector<std::array<std::wstring,4>> volumeCells;
std::unique_ptr<PasswordBook> book;
fs::path selectedOutput;
std::jthread worker;
std::atomic_bool stopped=false,pauseAfterCurrent=false;
bool running=false,closing=false,refreshing=false,dragHover=false,marquee=false,taskMenuOpen=false;
unsigned dragCount=0;
int scrollX=0,scrollY=0;
bool layingOut=false;
Page currentPage=Page::Queue,dropPage=Page::Queue;
std::array<POINT,PageCount> pageScroll{};
std::array<HWND,PageCount> pageFocus{};
std::array<TableSizing,PageCount> columnSizes{};

Workspace startupWorkspace;
std::unique_ptr<WorkspaceWriter> workspaceWriter;
fs::path workspaceFile;
std::wstring startupNotice,workspaceError,lastWorkspaceErrorNotice;
std::wstring instanceMarker;
std::optional<Selection> restoredSelection;
bool workspaceReady=false,workspaceLoaded=false;
uint64_t uiRevision=0,lastRequestedUi=UINT64_MAX,lastRequestedQueue=UINT64_MAX;
void workspaceChanged(){if(workspaceReady)++uiRevision;}
UINT dpi=96;
HFONT font=nullptr,titleFont=nullptr,smallFont=nullptr;
HICON iconLarge=nullptr,iconSmall=nullptr,iconHero=nullptr;
HBRUSH background=CreateSolidBrush(RGB(247,249,250));
int px(int n){return MulDiv(n,int(dpi),96);}
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
void message(const std::wstring& value){MessageBoxW(window,value.c_str(),L"T解压",MB_OK|MB_ICONINFORMATION);}
void feedback(const std::wstring& value){if(tips)SendMessageW(tips,TTM_POP,0,0);SetWindowTextW(status,value.c_str());}
void refresh(uint64_t select=0);
void updateControls();
void showPage(Page page);
void refreshPasswords();
void updatePasswordControls();
void refreshVolumes(const std::vector<fs::path>& selected={},bool reloadMetadata=false);
void updateVolumeControls();
TaskSettings batchSettings();
void updateTaskControls();
void requestWorkspaceSave(bool force=false);
bool saveBeforeExit();
void loadTaskEditorControls();
void updatePasswordPreview();
void revealFocus();
void rememberFocus(){auto focus=GetFocus();const auto& controls=*pageGroups[size_t(currentPage)];if(std::find(controls.begin(),controls.end(),focus)!=controls.end())if(pageFocus[size_t(currentPage)]!=focus){pageFocus[size_t(currentPage)]=focus;workspaceChanged();}}
void focusAndReveal(HWND target){SetFocus(target);rememberFocus();revealFocus();}
bool receiveFiles(const std::vector<fs::path>& files);
void updateIcons(){
 auto load=[&](int size){return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));};
 auto big=load(GetSystemMetricsForDpi(SM_CXICON,dpi)),small=load(GetSystemMetricsForDpi(SM_CXSMICON,dpi)),hero=load(px(42));
 SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(big));SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(small));
 for(auto icon:{iconLarge,iconSmall,iconHero})if(icon)DestroyIcon(icon);iconLarge=big;iconSmall=small;iconHero=hero;
}
HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,Id id){
 auto h=CreateWindowExW(wcscmp(cls,WC_EDITW)==0?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|(wcscmp(cls,WC_STATICW)?WS_TABSTOP:0)|style,0,0,0,0,window,reinterpret_cast<HMENU>(id),GetModuleHandleW(nullptr),nullptr);
 SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;
}
struct TableRedraw {
 HWND target;
 explicit TableRedraw(HWND table):target(IsWindowVisible(table)?table:nullptr){if(target)SendMessageW(target,WM_SETREDRAW,FALSE,0);}
 ~TableRedraw(){if(target){SendMessageW(target,WM_SETREDRAW,TRUE,0);RedrawWindow(target,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_ERASE|RDW_ALLCHILDREN);}}
};
HWND button(const wchar_t* label,Id id){return control(WC_BUTTONW,label,id==Start||id==TaskApply||id==QueueTab||id==PasswordTab||id==DropZone?BS_OWNERDRAW:BS_PUSHBUTTON,id);}
void updateFonts(){
 auto old=std::array{font,titleFont,smallFont};
 font=makeUiFont(dpi,14,FW_NORMAL);
 titleFont=makeUiFont(dpi,25,FW_SEMIBOLD);
 smallFont=makeUiFont(dpi,12,FW_NORMAL);
 if(window)EnumChildWindows(window,[](HWND h,LPARAM){SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return TRUE;},0);
 for(auto h:{status,phaseStatus})if(h)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
 if(tips){SendMessageW(tips,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(tips,TTM_SETMAXTIPWIDTH,0,px(560));}
 auto filter=GetDlgItem(window,QueueFilter);if(filter){SendMessageW(filter,CB_SETITEMHEIGHT,0,px(22));SendMessageW(filter,CB_SETITEMHEIGHT,WPARAM(-1),px(22));}
 for(auto f:old)if(f)DeleteObject(f);
}
Layout currentLayout(){RECT r;GetClientRect(window,&r);return makeLayout(r.right*96/int(dpi),r.bottom*96/int(dpi));}
void fitTable(HWND table,Page page){
 for(int pass=0;pass<3;++pass){RECT client{};GetClientRect(table,&client);auto widths=columnSizes[size_t(page)].fit(page,client.right,dpi);
  for(size_t i=0;i<widths.size();++i)if(ListView_GetColumnWidth(table,int(i))!=widths[i])ListView_SetColumnWidth(table,int(i),widths[i]);
  RECT after{};GetClientRect(table,&after);if(after.right==client.right)break;
 }
}
void layout(){
 if(layingOut)return;layingOut=true;if(tips)SendMessageW(tips,TTM_POP,0,0);
 RECT client{};GetClientRect(window,&client);
 auto extent=[&](LONG object)->int{SCROLLBARINFO info{sizeof(info)};if(!GetScrollBarInfo(window,object,&info)||(info.rgstate[0]&STATE_SYSTEM_INVISIBLE))return 0;return object==OBJID_VSCROLL?info.rcScrollBar.right-info.rcScrollBar.left:info.rcScrollBar.bottom-info.rcScrollBar.top;};
 auto view=fitViewport(client.right+extent(OBJID_VSCROLL),client.bottom+extent(OBJID_HSCROLL),GetSystemMetricsForDpi(SM_CXVSCROLL,dpi),GetSystemMetricsForDpi(SM_CYHSCROLL,dpi),dpi);
 int viewW=std::max(1,view.width*96/int(dpi)),viewH=std::max(1,view.height*96/int(dpi));auto canvas=makeLayout(viewW,viewH);
 SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE|SIF_POS,0,canvas.width-1,UINT(viewW),scrollX,0};SetScrollInfo(window,SB_HORZ,&horizontal,TRUE);
 SCROLLINFO vertical{sizeof(vertical),SIF_RANGE|SIF_PAGE|SIF_POS,0,canvas.height-1,UINT(viewH),scrollY,0};SetScrollInfo(window,SB_VERT,&vertical,TRUE);
 scrollX=GetScrollPos(window,SB_HORZ);scrollY=GetScrollPos(window,SB_VERT);
 auto l=currentLayout();auto batch=BeginDeferWindowPos(int(l.boxes.size()));
 auto place=[&](auto move){for(size_t i=0;i<l.boxes.size();++i){const auto& box=l.boxes[i];auto id=Id(int(Add)+int(i));auto target=GetDlgItem(window,id);if(!target||!box.width)continue;auto pixels=pixelBox(box,dpi,scrollX,scrollY);if(id==QueueFilter)pixels.height=px(220);if(!move(target,pixels))return false;}return true;};
 bool queued=batch&&place([&](HWND target,const Box& box){batch=DeferWindowPos(batch,target,nullptr,box.x,box.y,box.width,box.height,SWP_NOZORDER|SWP_NOACTIVATE);return batch!=nullptr;});
 if(!queued||!EndDeferWindowPos(batch))place([](HWND target,const Box& box){SetWindowPos(target,nullptr,box.x,box.y,box.width,box.height,SWP_NOZORDER|SWP_NOACTIVATE);return true;});
 ShowWindow(GetDlgItem(window,DropZone),currentPage==Page::Queue&&l.showDrop?SW_SHOW:SW_HIDE);
 fitTable(list,Page::Queue);fitTable(passwordList,Page::Passwords);fitTable(volumeList,Page::Volumes);
 InvalidateRect(window,nullptr,FALSE);layingOut=false;
}
void scrollView(int bar,int value){workspaceChanged();SCROLLINFO info{sizeof(info),SIF_POS};info.nPos=value;SetScrollInfo(window,bar,&info,TRUE);if(bar==SB_VERT)scrollY=GetScrollPos(window,bar);else scrollX=GetScrollPos(window,bar);layout();InvalidateRect(window,nullptr,FALSE);}
void revealFocus(){
 if(layingOut)return;auto focus=GetFocus();if(!focus||!IsChild(window,focus))return;
 RECT target{},client{};GetClientRect(window,&client);if(client.right<=0||client.bottom<=0||IsIconic(window))return;
 if(focus==list||focus==passwordList||focus==volumeList){
  int row=ListView_GetNextItem(focus,-1,LVNI_FOCUSED);if(row<0&&ListView_GetItemCount(focus))row=0;
  if(row>=0){ListView_EnsureVisible(focus,row,FALSE);ListView_GetItemRect(focus,row,&target,LVIR_LABEL);MapWindowPoints(focus,window,reinterpret_cast<POINT*>(&target),2);}
  else {GetWindowRect(focus,&target);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&target),2);target.right=target.left+px(180);target.bottom=target.top+px(52);}
 }else {
  GUITHREADINFO gui{sizeof(gui)};wchar_t type[32]{};GetClassNameW(focus,type,int(std::size(type)));
  if(_wcsicmp(type,L"Edit")==0&&GetGUIThreadInfo(GetCurrentThreadId(),&gui)&&gui.hwndCaret==focus){target=gui.rcCaret;InflateRect(&target,px(8),px(4));MapWindowPoints(focus,window,reinterpret_cast<POINT*>(&target),2);}
  else {GetWindowRect(focus,&target);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&target),2);}
 }
 auto l=currentLayout();int viewW=client.right*96/int(dpi),viewH=client.bottom*96/int(dpi);
 int left=MulDiv(target.left,96,int(dpi))+scrollX,top=MulDiv(target.top,96,int(dpi))+scrollY;
 int right=MulDiv(target.right,96,int(dpi))+scrollX,bottom=MulDiv(target.bottom,96,int(dpi))+scrollY;
 auto x=revealOffset(scrollX,viewW,left,right,l.width),y=revealOffset(scrollY,viewH,top,bottom,l.height);
 if(x!=scrollX||y!=scrollY){scrollX=x;scrollY=y;layout();}
}
void invokeCommand(Id id){auto target=GetDlgItem(window,id);if(target&&IsWindowEnabled(target)&&IsWindowVisible(target))SendMessageW(window,WM_COMMAND,id,0);revealFocus();}
void activateList(HWND table){if(table==passwordList)invokeCommand(UsePassword);else {SendMessageW(window,WM_COMMAND,OpenOutput,0);revealFocus();}}
std::vector<int> selectedIndices(HWND table,size_t count){std::vector<int> indices;for(int i=-1;(i=ListView_GetNextItem(table,i,LVNI_SELECTED))>=0;)if(size_t(i)<count)indices.push_back(i);return indices;}
std::vector<uint64_t> selectedTasks(){std::vector<uint64_t> ids;for(auto i:selectedIndices(list,rows.size()))ids.push_back(rows[size_t(i)].task.id);return ids;}
std::optional<Task> selectedTask(){auto ids=selectedTasks();return ids.size()==1?queue.get(ids.front()):std::optional<Task>{};}
Row makeRow(Task task){Row r;r.task=std::move(task);r.cells={taskName(r.task),taskActionName(r.task),taskStatus(r.task),r.task.percent<0?L"—":std::to_wstring(r.task.percent)+L"%",r.task.result.note.empty()?(r.task.settings?settingsDescription(r.task.settings):r.task.manualVolumes?L"手工顺序 · 首卷在第一行":r.task.files.front().parent_path().wstring()):r.task.result.note};return r;}
void setProgress(int percent,bool active){
 bool unknown=active&&percent<0;
 if(unknown!=marquee){
  if(!unknown)SendMessageW(progress,PBM_SETMARQUEE,FALSE,0);
  auto style=GetWindowLongPtrW(progress,GWL_STYLE);SetWindowLongPtrW(progress,GWL_STYLE,unknown?style|PBS_MARQUEE:style&~LONG_PTR(PBS_MARQUEE));
  if(unknown)SendMessageW(progress,PBM_SETMARQUEE,TRUE,30);
  marquee=unknown;
 }
 if(!unknown)SendMessageW(progress,PBM_SETPOS,percent<0?0:percent,0);
}
void updatePhaseCaption(){auto counts=queue.counts();auto task=activeTaskId?queue.get(activeTaskId):std::optional<Task>{};auto detail=task&&task->state==State::Running?taskName(*task)+L" · "+taskProgressCaption(*task):currentPhase;auto phase=phaseCaption(counts,running,stopped,detail,pauseAfterCurrent);if(text(phaseStatus)!=phase)SetWindowTextW(phaseStatus,phase.c_str());}
void updateControls(){
 auto selected=selectedIndices(list,rows.size());std::optional<Task> task=selected.size()==1?queue.get(rows[size_t(selected.front())].task.id):std::optional<Task>{};auto c=queue.counts();bool hasWaiting=std::any_of(selected.begin(),selected.end(),[](auto i){return rows[size_t(i)].task.state==State::Waiting;});auto enable=[&](Id id,bool value){EnableWindow(GetDlgItem(window,id),value);};
 enable(Start,!running&&c.waiting);enable(Cancel,running&&!stopped);std::wstring startLabel=running?L"正在处理…":c.waiting?(pauseAfterCurrent?L"继续全部 ":L"开始全部 ")+std::to_wstring(c.waiting)+L" 项":L"开始队列";SetWindowTextW(GetDlgItem(window,Start),startLabel.c_str());
 enable(Remove,std::any_of(selected.begin(),selected.end(),[](auto i){return rows[size_t(i)].task.state!=State::Running;}));enable(Up,hasWaiting&&taskView.unfiltered()&&!closing);enable(Down,hasWaiting&&taskView.unfiltered()&&!closing);
 bool repair=task&&task->state==State::Attention&&(task->result.status==L"password_required"||task->result.status==L"missing_volume"||task->result.status==L"ambiguous_volume"||task->result.status==L"source_unavailable");enable(NextStep,repair&&!running);ShowWindow(GetDlgItem(window,NextStep),currentPage==Page::Queue&&repair?SW_SHOW:SW_HIDE);if(repair)SetWindowTextW(GetDlgItem(window,NextStep),task->result.status==L"password_required"?L"填写本批密码":task->result.status==L"source_unavailable"?L"重新定位源…":L"补充分卷…");
 enable(Retry,std::any_of(selected.begin(),selected.end(),[](auto i){return retryable(rows[size_t(i)].task);}));enable(OpenOutput,task&&!task->result.output.empty());SetWindowTextW(GetDlgItem(window,OpenOutput),task&&task->state==State::Running?L"查看当前输出":task&&task->state!=State::Done&&!task->result.output.empty()?L"查看保留结果":L"打开结果");enable(Tools,c.total()>0&&!closing);enable(Volumes,!running);enable(Create,!running);
 enable(ConfigureTask,!running&&std::any_of(selected.begin(),selected.end(),[](auto i){return settingsAllowed(rows[size_t(i)].task);}));
 bool resume=std::any_of(selected.begin(),selected.end(),[](auto i){return rows[size_t(i)].task.state==State::Waiting&&rows[size_t(i)].task.paused;});SetWindowTextW(GetDlgItem(window,HoldTask),resume?L"继续暂缓项":L"暂缓所选");enable(HoldTask,std::any_of(selected.begin(),selected.end(),[](auto i){return rows[size_t(i)].task.state==State::Waiting;}));
 for(auto id:{Choose,Reset,Nested,Recycle,Deep,BatchPassword,SavePasswords,ImportPasswords})enable(id,!running);updatePasswordControls();
 std::wstring details;if(task)details=taskDetails(*task);
 else details=selected.empty()?L"选中任务查看完整路径、识别结果和处理建议。\r\n开始按钮处理全部等待项；查找和筛选只改变显示。":L"已选中 "+std::to_wstring(selected.size())+L" 项，可批量重试、暂缓或整理等待项顺序。";
 updatePhaseCaption();
 auto detail=GetDlgItem(window,TaskDetails);if(text(detail)!=details)SetWindowTextW(detail,details.c_str());InvalidateRect(window,nullptr,FALSE);
}
void refresh(uint64_t select){
 Selection selection;selection.ids=selectedTasks();int focused=ListView_GetNextItem(list,-1,LVNI_FOCUSED);if(focused>=0&&size_t(focused)<rows.size())selection.focus=rows[size_t(focused)].task.id;
 int mark=ListView_GetSelectionMark(list);if(mark>=0&&size_t(mark)<rows.size())selection.anchor=rows[size_t(mark)].task.id;
 if(restoredSelection){selection=std::move(*restoredSelection);restoredSelection.reset();}
 uint64_t top=0;auto topIndex=ListView_GetTopIndex(list);if(topIndex>=0&&size_t(topIndex)<rows.size())top=rows[size_t(topIndex)].task.id;
 auto snapshot=queue.snapshot();if(select){auto target=std::find_if(snapshot.begin(),snapshot.end(),[&](const auto& t){return t.id==select;});if(target!=snapshot.end()&&taskView.matches(*target)){selection.ids={select};selection.focus=selection.anchor=select;}else select=0;}
 selection=restoreSelection(snapshot,selection);std::vector<size_t> visible;std::wstring activeCaption;int activePercent=-1;activeTaskId=0;
 for(size_t i=0;i<snapshot.size();++i){const auto& task=snapshot[i];if(taskView.matches(task))visible.push_back(i);if(task.state==State::Running){activeTaskId=task.id;activeCaption=taskName(task)+L" · "+task.phase;activePercent=task.percent;}}
 bool unchanged=visible.size()==rows.size();if(unchanged)for(size_t i=0;i<visible.size();++i)if(rows[i].task.id!=snapshot[visible[i]].id){unchanged=false;break;}
 if(unchanged&&!select){
  for(size_t i=0;i<visible.size();++i){auto& current=snapshot[visible[i]];const auto& old=rows[i].task;
   if(old.state!=current.state||old.phase!=current.phase||old.percent!=current.percent||old.action!=current.action||old.files!=current.files||old.manualVolumes!=current.manualVolumes||old.allowContainer!=current.allowContainer||old.paused!=current.paused||old.settings!=current.settings||old.result.output!=current.result.output||old.result.note!=current.result.note||old.result.status!=current.result.status){rows[i]=makeRow(std::move(current));ListView_RedrawItems(list,int(i),int(i));}
  }
 }else{
  rows.clear();rows.reserve(visible.size());for(auto index:visible)rows.push_back(makeRow(std::move(snapshot[index])));
  refreshing=true;{TableRedraw redraw(list);ListView_SetItemCountEx(list,int(rows.size()),LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
  std::set<uint64_t> selected(selection.ids.begin(),selection.ids.end());int focusRow=-1,topRow=-1,anchorRow=-1;
  for(size_t i=0;i<rows.size();++i){auto id=rows[i].task.id;if(id==top)topRow=int(i);if(id==selection.focus)focusRow=int(i);if(id==selection.anchor)anchorRow=int(i);if(selected.contains(id))ListView_SetItemState(list,int(i),LVIS_SELECTED,LVIS_SELECTED);}
  if(focusRow>=0){ListView_SetItemState(list,focusRow,LVIS_FOCUSED,LVIS_FOCUSED);}ListView_SetSelectionMark(list,anchorRow);
  if(select&&focusRow>=0){ListView_EnsureVisible(list,focusRow,FALSE);}else if(topRow>=0){ListView_EnsureVisible(list,topRow,FALSE);}
  }refreshing=false;InvalidateRect(list,nullptr,FALSE);
 }
 fitTable(list,Page::Queue);currentPhase=std::move(activeCaption);if(!currentPhase.empty())setProgress(activePercent,true);updateControls();if(select&&GetFocus()==list)revealFocus();
}
void flushUpdates(){KillTimer(window,QueueRefreshTimer);if(!backgroundUpdates.take().empty())refresh();}

std::vector<fs::path> chooseFiles(const wchar_t* title=L"添加文件",const wchar_t* filter=L"所有文件\0*.*\0",bool multiple=true){
 std::vector<wchar_t> buffer(65536);OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=window;ofn.lpstrTitle=title;ofn.lpstrFilter=filter;ofn.lpstrFile=buffer.data();ofn.nMaxFile=DWORD(buffer.size());ofn.Flags=OFN_EXPLORER|(multiple?OFN_ALLOWMULTISELECT:0)|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
 if(!GetOpenFileNameW(&ofn))return {};fs::path first(buffer.data());auto next=buffer.data()+wcslen(buffer.data())+1;if(!*next)return {first};std::vector<fs::path> result;while(*next){result.push_back(first/next);next+=wcslen(next)+1;}return result;
}
fs::path chooseDirectory(const wchar_t* title=L"选择文件夹"){
 IFileOpenDialog* dialog=nullptr;fs::path path;if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetTitle(title);if(SUCCEEDED(dialog->Show(window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR name=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))){path=name;CoTaskMemFree(name);}item->Release();}}dialog->Release();}return path;
}
void relocateSource(const Task& task){
 auto title=L"重新定位："+task.result.failedFile.filename().wstring();fs::path replacement;
 if(task.action==Action::Scan)replacement=chooseDirectory(title.c_str());else {auto files=chooseFiles(title.c_str(),L"所有文件\0*.*\0",false);if(!files.empty())replacement=files.front();}
 if(replacement.empty())return;
 if(!queue.replaceSource(task.id,task.result.failedFile,replacement)){feedback(L"来源未替换：任务已改变、该文件重复或正在使用，请重新选择任务后重试。");return;}
 refresh(task.id);feedback(L"已更新来源并保留处理设置。"+(task.paused?std::wstring(L"此任务仍暂缓。") :std::wstring(L"开始全部后处理；当前筛选可能隐藏待开始项。")));
}
unsigned addFiles(const std::vector<fs::path>& files,bool navigate=true){
 unsigned added=0,duplicates=0,unavailable=0;uint64_t first=0;
 for(const auto& file:files){Task task;task.files={file};auto attributes=GetFileAttributesW(file.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){++unavailable;continue;}if(attributes&FILE_ATTRIBUTE_DIRECTORY)task.action=Action::Scan;auto id=queue.add(std::move(task));if(id){++added;if(!first)first=id;}else ++duplicates;}
 if(files.empty())return 0;if(navigate)showPage(Page::Queue);refresh(navigate?first:0);if(navigate)focusAndReveal(list);feedback(L"已加入 "+std::to_wstring(added)+L" 项"+(duplicates?L"，跳过 "+std::to_wstring(duplicates)+L" 个重复项":L"")+(unavailable?L"，无法访问 "+std::to_wstring(unavailable)+L" 项":L"")+(taskView.unfiltered()?L"。分卷在后台识别，等待队列处理。":L"。当前查找或筛选可能隐藏新任务；开始按钮处理全部等待项。"));return added;
}
void postUpdate(uint64_t id){if(backgroundUpdates.add(id))PostMessageW(window,WM_QUEUE_UPDATE,0,0);}
void start(){
 if(running||!queue.counts().waiting)return;auto options=applySettings(Options{},batchSettings());options.password=text(batchEdit);
 stopped=false;pauseAfterCurrent=false;running=true;currentPhase.clear();updateControls();feedback(L"按队列顺序处理，运行中仍可添加文件。");focusAndReveal(list);worker=std::jthread([options]{
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  try{Engine engine(*book);runQueue(queue,engine,options,[]{return stopped.load();},postUpdate,[]{return pauseAfterCurrent.load();});
  }catch(const std::exception& e){auto error=new std::wstring(errorText(e));if(!PostMessageW(window,WM_FINISHED,1,reinterpret_cast<LPARAM>(error)))delete error;CoUninitialize();return;}
  CoUninitialize();PostMessageW(window,WM_FINISHED,0,0);
 });
}
void updatePasswordPreview(){
 auto values=splitLines(text(passwordEdit));auto preview=book->preview(values);auto label=L"新增密码 · "+std::to_wstring(values.size())+L" 条；预计新增 "+std::to_wstring(preview.added)+L"，重复 "+std::to_wstring(preview.duplicates)+L" · Enter 换行";SetWindowTextW(GetDlgItem(window,PasswordLabel),label.c_str());
}
void updatePasswordControls(){
 int selected=ListView_GetNextItem(passwordList,-1,LVNI_SELECTED);EnableWindow(GetDlgItem(window,UsePassword),!running&&selected>=0&&size_t(selected)<passwordRows.size());
}
void refreshPasswords(){
 auto oldValue=[&](int row){return row>=0&&size_t(row)<passwordRows.size()?passwordRows[size_t(row)].value:std::wstring();};
 auto selected=oldValue(ListView_GetNextItem(passwordList,-1,LVNI_SELECTED)),focused=oldValue(ListView_GetNextItem(passwordList,-1,LVNI_FOCUSED)),top=oldValue(ListView_GetTopIndex(passwordList));
 auto query=lower(text(GetDlgItem(window,PasswordSearch)));auto values=book->snapshot();passwordRows.clear();passwordCells.clear();bool visible=IsDlgButtonChecked(window,ShowPasswords)==BST_CHECKED;
 for(auto& p:values)if(query.empty()||lower(p.value).find(query)!=std::wstring::npos||p.directory.find(query)!=std::wstring::npos){passwordCells.push_back({visible?p.value:L"••••••••",std::to_wstring(p.uses),p.directory.empty()?L"尚无使用记录":p.directory});passwordRows.push_back(std::move(p));}
 refreshing=true;{TableRedraw redraw(passwordList);ListView_SetItemCountEx(passwordList,int(passwordRows.size()),LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);ListView_SetItemState(passwordList,-1,0,LVIS_SELECTED|LVIS_FOCUSED);int selectedRow=-1,topRow=-1;
  for(size_t i=0;i<passwordRows.size();++i){auto& value=passwordRows[i].value;if(value==selected){selectedRow=int(i);ListView_SetItemState(passwordList,int(i),LVIS_SELECTED,LVIS_SELECTED);}if(value==focused)ListView_SetItemState(passwordList,int(i),LVIS_FOCUSED,LVIS_FOCUSED);if(value==top)topRow=int(i);}
  ListView_SetSelectionMark(passwordList,selectedRow);if(topRow>=0)ListView_EnsureVisible(passwordList,topRow,FALSE);
 }refreshing=false;fitTable(passwordList,Page::Passwords);updatePasswordControls();InvalidateRect(window,nullptr,FALSE);
}
void useSavedPassword(){
 if(running)return;int selected=ListView_GetNextItem(passwordList,-1,LVNI_SELECTED);if(selected<0||size_t(selected)>=passwordRows.size())return;
 SetWindowTextW(batchEdit,passwordRows[size_t(selected)].value.c_str());showPage(Page::Queue);focusAndReveal(batchEdit);feedback(L"已设为本批优先密码；需要密码的任务可先重试，再开始队列。");
}
std::wstring importReport(const PasswordImport& r){return L"新增 "+std::to_wstring(r.added)+L" 条，已存在 "+std::to_wstring(r.duplicates)+L" 条。";}
bool importPasswords(const std::vector<fs::path>& files,unsigned skipped=0){
 if(running){SetWindowTextW(GetDlgItem(window,PasswordFeedback),L"当前队列运行中，结束后可导入。草稿会保留。");return false;}
 if(files.empty()&&!skipped)return false;
 auto batch=book->importTexts(files);std::wstring note=L"读取 "+std::to_wstring(batch.files)+L" 个文本；"+importReport(batch.totals);
 if(skipped)note+=L" 跳过 "+std::to_wstring(skipped)+L" 个非TXT文件。";
 for(const auto& failure:batch.failures)note+=L"\r\n"+failure.file.filename().wstring()+L"："+failure.message;
 SetWindowTextW(GetDlgItem(window,PasswordFeedback),note.c_str());refreshPasswords();focusAndReveal(passwordEdit);updatePasswordPreview();return batch.files!=0;
}
void showPage(Page page){
 workspaceChanged();
 if(page!=currentPage){auto index=size_t(currentPage);pageScroll[index]={scrollX,scrollY};rememberFocus();scrollX=pageScroll[size_t(page)].x;scrollY=pageScroll[size_t(page)].y;}
 currentPage=page;if(tips)SendMessageW(tips,TTM_ACTIVATE,page==Page::Queue,0);
 for(size_t i=0;i<PageCount;++i)for(auto h:*pageGroups[i])ShowWindow(h,i==size_t(page)?SW_SHOW:SW_HIDE);
 if(page==Page::Passwords){refreshPasswords();updatePasswordPreview();}
 layout();updateControls();auto target=pageFocus[size_t(page)];if(!target||!IsWindowEnabled(target)||!IsWindowVisible(target)){
  switch(page){case Page::Queue:target=list;break;case Page::Passwords:target=GetDlgItem(window,PasswordSearch);break;case Page::Volumes:target=volumeList;break;case Page::TaskSettings:target=GetDlgItem(window,TaskCustom);break;}
 }
 focusAndReveal(target);InvalidateRect(GetDlgItem(window,QueueTab),nullptr,TRUE);InvalidateRect(GetDlgItem(window,PasswordTab),nullptr,TRUE);
}
TaskSettings batchSettings(){return {selectedOutput,IsDlgButtonChecked(window,Nested)==BST_CHECKED,IsDlgButtonChecked(window,Recycle)==BST_CHECKED,IsDlgButtonChecked(window,Deep)==BST_CHECKED};}
void updateTaskControls(){
 bool custom=IsDlgButtonChecked(window,TaskCustom)==BST_CHECKED;for(auto id:{TaskChoose,TaskSourceDirectory,TaskNested,TaskRecycle,TaskDeep,TaskDirectory})EnableWindow(GetDlgItem(window,id),custom);
}
void readTaskEditor(){workspaceChanged();taskEditor.custom=IsDlgButtonChecked(window,TaskCustom)==BST_CHECKED;taskEditor.value.recursive=IsDlgButtonChecked(window,TaskNested)==BST_CHECKED;taskEditor.value.recycle=IsDlgButtonChecked(window,TaskRecycle)==BST_CHECKED;taskEditor.value.deep=IsDlgButtonChecked(window,TaskDeep)==BST_CHECKED;}
void openTaskSettings(){
 if(running)return;std::vector<Task> selected;for(auto row:selectedIndices(list,rows.size()))if(settingsAllowed(rows[size_t(row)].task))selected.push_back(rows[size_t(row)].task);if(selected.empty()){feedback(L"请选择未运行的解压、识别、扫描或另存任务。");return;}
 taskEditor.open(selected,batchSettings());loadTaskEditorControls();showPage(Page::TaskSettings);
}
void loadTaskEditorControls(){
 CheckDlgButton(window,TaskCustom,taskEditor.custom?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(window,TaskNested,taskEditor.value.recursive?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(window,TaskRecycle,taskEditor.value.recycle?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(window,TaskDeep,taskEditor.value.deep?BST_CHECKED:BST_UNCHECKED);
 SetWindowTextW(GetDlgItem(window,TaskDirectory),taskEditor.value.output.empty()?L"原包所在目录":taskEditor.value.output.c_str());SetWindowTextW(GetDlgItem(window,TaskFeedback),taskEditor.mixed?L"所选任务的设置不同。保存后统一采用当前选项；创建任务的目标文件不在此修改。":L"未勾选专用设置时，跟随队列底部的本批设置。保存不自动执行；可保存并加入待开始队列。");
 updateTaskControls();
}
void saveTaskSettings(bool enqueue){
 readTaskEditor();std::vector<uint64_t> ids;std::set<uint64_t> selected(taskEditor.ids.begin(),taskEditor.ids.end());for(const auto& task:queue.snapshot())if(selected.contains(task.id)&&settingsAllowed(task))ids.push_back(task.id);
 auto saved=queue.configure(ids,taskEditor.custom?std::optional<TaskSettings>(taskEditor.value):std::nullopt);unsigned prepared=enqueue?queue.retry(ids):0;taskEditor.active=false;showPage(Page::Queue);refresh();
 feedback(L"已保存 "+std::to_wstring(saved)+L" 项设置。"+(enqueue?L" 已加入待开始 "+std::to_wstring(prepared)+L" 项，开始全部后处理。":L" 原有任务状态保持不变。")+(taskView.unfiltered()?L"":L" 当前筛选可能隐藏这些任务。"));
}
std::vector<int> selectedVolumes(){return selectedIndices(volumeList,volumeCells.size());}
fs::path volumeCellPath(int index){return index>=0&&size_t(index)<volumeCells.size()?fs::path(volumeCells[size_t(index)][3])/volumeCells[size_t(index)][1]:fs::path();}
void updateVolumeControls(){
 auto selected=selectedVolumes();EnableWindow(GetDlgItem(window,VolumeRemove),!selected.empty());EnableWindow(GetDlgItem(window,VolumeUp),!selected.empty()&&selected.front()>0);EnableWindow(GetDlgItem(window,VolumeDown),!selected.empty()&&size_t(selected.back()+1)<volumeEditor.files.size());EnableWindow(GetDlgItem(window,AcceptVolumes),!volumeEditor.files.empty());EnableWindow(GetDlgItem(window,VolumeSort),volumeEditor.files.size()>1);InvalidateRect(window,nullptr,FALSE);
}
void refreshVolumes(const std::vector<fs::path>& select,bool reloadMetadata){
 std::set<std::wstring> selected;for(auto index:selectedVolumes())selected.insert(pathKey(volumeCellPath(index)));
 auto focus=volumeCellPath(ListView_GetNextItem(volumeList,-1,LVNI_FOCUSED));auto anchor=volumeCellPath(ListView_GetSelectionMark(volumeList));
 if(!select.empty()){selected.clear();for(const auto& path:select)selected.insert(pathKey(path));focus=anchor=select.front();}
 auto focusKey=focus.empty()?L"":pathKey(focus),anchorKey=anchor.empty()?L"":pathKey(anchor);
 std::map<std::wstring,std::wstring> sizes;if(!reloadMetadata)for(size_t i=0;i<volumeCells.size();++i)sizes[pathKey(volumeCellPath(int(i)))]=volumeCells[i][2];
 volumeCells.clear();for(size_t i=0;i<volumeEditor.files.size();++i){auto& path=volumeEditor.files[i];auto cached=sizes.find(pathKey(path));std::wstring sizeText;
  if(cached!=sizes.end())sizeText=cached->second;else {std::error_code error;auto size=fs::file_size(path,error);sizeText=error?L"无法读取":formatBytes(size);}
  volumeCells.push_back({i?std::to_wstring(i+1):L"1 · 首卷",path.filename().wstring(),sizeText,path.parent_path().wstring()});
 }
 refreshing=true;{TableRedraw redraw(volumeList);ListView_SetItemCountEx(volumeList,int(volumeCells.size()),LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);ListView_SetItemState(volumeList,-1,0,LVIS_SELECTED|LVIS_FOCUSED);int focusRow=-1,anchorRow=-1;
 for(size_t i=0;i<volumeEditor.files.size();++i){auto key=pathKey(volumeEditor.files[i]);if(selected.contains(key))ListView_SetItemState(volumeList,int(i),LVIS_SELECTED,LVIS_SELECTED);if(key==focusKey)focusRow=int(i);if(key==anchorKey)anchorRow=int(i);}
 if(focusRow>=0){ListView_SetItemState(volumeList,focusRow,LVIS_FOCUSED,LVIS_FOCUSED);ListView_EnsureVisible(volumeList,focusRow,FALSE);}ListView_SetSelectionMark(volumeList,anchorRow);}fitTable(volumeList,Page::Volumes);refreshing=false;InvalidateRect(volumeList,nullptr,FALSE);updateVolumeControls();if(GetFocus()==volumeList)revealFocus();
}
size_t addVolumes(std::vector<fs::path> paths){workspaceChanged();
 unsigned skipped=0;std::vector<fs::path> files;for(const auto& p:paths){std::error_code e;if(fs::is_regular_file(p,e))files.push_back(p);else ++skipped;}
 auto previous=volumeEditor.files.size();auto added=appendVolumes(volumeEditor.files,files);skipped+=unsigned(files.size()-added);std::vector<fs::path> selected;if(added)selected.assign(volumeEditor.files.begin()+std::ptrdiff_t(previous),volumeEditor.files.end());refreshVolumes(selected,true);
 auto note=L"已加入 "+std::to_wstring(added)+L" 卷"+(skipped?L"，跳过 "+std::to_wstring(skipped)+L" 个重复项或文件夹":L"")+L"。请检查首卷和顺序。";SetWindowTextW(GetDlgItem(window,VolumeFeedback),note.c_str());focusAndReveal(volumeList);return added;
}
void orderedVolumes(){
 workspaceChanged();
 if(running)return;auto selected=selectedTask();bool inner=false;
 if(selected&&fileActionAllowed(*selected)){volumeEditor.open(*selected,batchSettings());inner=volumeEditor.taskId!=selected->id;}
 else if(volumeEditor.active){if(volumeEditor.taskId&&!queue.get(volumeEditor.taskId))volumeEditor.taskId=0;}
 else volumeEditor.open(0,{});
 showPage(Page::Volumes);refreshVolumes(volumeEditor.files.empty()?std::vector<fs::path>{}:std::vector<fs::path>{volumeEditor.files.front()},true);
 SetWindowTextW(GetDlgItem(window,VolumeFeedback),inner?L"正在补充失败内层文件的分卷；保存为单独任务，外层结果和原包保留。":L"可拖入补充分卷；首卷放在第一行，多选后可整组调整。返回队列会保留草稿。");
 if(volumeEditor.files.empty())addVolumes(chooseFiles(L"选择全部分卷"));
}
void acceptVolumes(){
 if(volumeEditor.files.empty())return;for(size_t i=0;i<volumeEditor.files.size();++i){std::error_code error;if(!fs::is_regular_file(volumeEditor.files[i],error)){refreshVolumes({volumeEditor.files[i]},true);auto note=L"第 "+std::to_wstring(i+1)+L" 卷不存在或无法读取，请重新添加该文件。";SetWindowTextW(GetDlgItem(window,VolumeFeedback),note.c_str());return;}}
 uint64_t id=volumeEditor.taskId;if(id){if(!queue.setVolumes(id,volumeEditor.files)){SetWindowTextW(GetDlgItem(window,VolumeFeedback),L"原任务已经开始或已移除，请返回队列后重新整理。");return;}}
 else {Task task;task.files=volumeEditor.files;task.manualVolumes=true;task.settings=volumeEditor.settings;id=queue.add(std::move(task));if(!id){SetWindowTextW(GetDlgItem(window,VolumeFeedback),L"该卷组已在队列中，或原包正在使用。请返回队列查看；当前顺序草稿会保留。");return;}}
 volumeEditor.active=false;showPage(Page::Queue);refresh(id);feedback(L"已保存 "+std::to_wstring(volumeEditor.files.size())+L" 卷的读取顺序。"+(queue.get(id)->paused?L"此任务仍暂缓，继续后再处理。":L"已回到待开始队列；开始全部后处理。"));
}
bool receiveFiles(const std::vector<fs::path>& files){
 if(currentPage==Page::TaskSettings)return false;
 if(currentPage==Page::Volumes)return addVolumes(files)!=0;
 if(currentPage==Page::Passwords){std::vector<fs::path> texts;for(const auto& path:files)if(lower(path.extension().wstring())==L".txt")texts.push_back(path);return importPasswords(texts,unsigned(files.size()-texts.size()));}
 return addFiles(files)!=0;
}
std::vector<fs::path> readClipboardFiles(){
 if(!OpenClipboard(window))throw std::runtime_error(utf8(L"剪贴板暂时无法读取，请稍后再试。"));
 struct Close {~Close(){CloseClipboard();}} close;
 if(auto files=GetClipboardData(CF_HDROP))return pathsFromDrop(static_cast<HDROP>(files));
 auto memory=GetClipboardData(CF_UNICODETEXT);if(!memory)return {};
 auto data=static_cast<const wchar_t*>(GlobalLock(memory));if(!data)return {};
 struct Unlock {HGLOBAL handle;~Unlock(){GlobalUnlock(handle);}} unlock{memory};
 size_t length=0,limit=GlobalSize(memory)/sizeof(wchar_t);while(length<limit&&data[length])++length;
 return parseFilePaths(std::wstring(data,length));
}
void pasteFiles(bool volumes){
 auto files=readClipboardFiles();if(files.empty()){auto note=L"剪贴板没有文件或逐行路径。可在资源管理器复制文件，或使用“复制为路径”。";if(volumes)SetWindowTextW(GetDlgItem(window,VolumeFeedback),note);else feedback(note);return;}
 if(volumes)addVolumes(std::move(files));else addFiles(files);
}

Workspace captureWorkspace(){
 rememberFocus();Workspace result;result.defaults=batchSettings();result.tasks=queue.snapshot();result.volumes=volumeEditor;result.editor=taskEditor;result.columns=columnSizes;result.scroll=pageScroll;result.scroll[size_t(currentPage)]={scrollX,scrollY};result.view=taskView;result.page=currentPage;
 for(size_t i=0;i<PageCount;++i)result.focus[i]=pageFocus[i]?GetDlgCtrlID(pageFocus[i]):0;
 result.selection.ids=selectedTasks();int focus=ListView_GetNextItem(list,-1,LVNI_FOCUSED),anchor=ListView_GetSelectionMark(list);if(focus>=0&&size_t(focus)<rows.size())result.selection.focus=rows[size_t(focus)].task.id;if(anchor>=0&&size_t(anchor)<rows.size())result.selection.anchor=rows[size_t(anchor)].task.id;
 WINDOWPLACEMENT place{sizeof(place)};if(GetWindowPlacement(window,&place)){result.width=std::max(320,MulDiv(place.rcNormalPosition.right-place.rcNormalPosition.left,96,int(dpi)));result.height=std::max(200,MulDiv(place.rcNormalPosition.bottom-place.rcNormalPosition.top,96,int(dpi)));result.maximized=place.showCmd==SW_SHOWMAXIMIZED||(place.flags&WPF_RESTORETOMAXIMIZED);}
 return result;
}
void requestWorkspaceSave(bool force){
 if(!workspaceReady||!workspaceWriter)return;auto revision=queue.revision();if(!force&&revision==lastRequestedQueue&&uiRevision==lastRequestedUi)return;
 auto snapshot=captureWorkspace();lastRequestedQueue=revision;lastRequestedUi=uiRevision;workspaceWriter->submit(std::move(snapshot));
}
bool saveBeforeExit(){
 requestWorkspaceSave(true);if(!workspaceWriter){if(!queue.counts().total()&&!volumeEditor.active&&!taskEditor.active)return true;return MessageBoxW(window,L"工作台自动保存不可用，当前队列和草稿只能保留在本次窗口。仍然退出吗？",L"T解压",MB_YESNO|MB_ICONWARNING)==IDYES;}if(workspaceWriter->flush())return true;
 auto text=L"工作台未能保存："+workspaceWriter->error()+L"\n原压缩包和解压结果不受影响。仍然退出吗？";
 if(MessageBoxW(window,text.c_str(),L"T解压",MB_YESNO|MB_ICONWARNING)==IDYES)return true;
 feedback(L"工作台尚未保存，当前窗口已保留。");return false;
}
void checkWorkspaceSave(){
 requestWorkspaceSave();if(!workspaceWriter)return;auto error=workspaceWriter->error();if(error==workspaceError)return;
 auto previous=lastWorkspaceErrorNotice;workspaceError=error;
 if(!error.empty()){lastWorkspaceErrorNotice=L"工作台自动保存失败："+error+L"。下次变更或退出前会重试。";feedback(lastWorkspaceErrorNotice);}
 else if(text(status)==previous)feedback(L"工作台已重新保存。");InvalidateRect(window,nullptr,FALSE);
}
void createArchive(){
 auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"创建 7z 压缩包");AppendMenuW(menu,MF_STRING,2,L"创建 ZIP 压缩包");RECT r;GetWindowRect(GetDlgItem(window,Create),&r);auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,window,nullptr);DestroyMenu(menu);if(!choice)return;
 auto paths=chooseFiles(L"选择要压缩的文件");if(paths.empty())return;std::wstring initial=(paths.front().parent_path()/(paths.front().stem().wstring()+(choice==1?L".7z":L".zip"))).wstring();std::vector<wchar_t> buffer(32768);std::copy(initial.begin(),initial.end(),buffer.begin());
 OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=window;ofn.lpstrTitle=L"新压缩包保存位置";ofn.lpstrFile=buffer.data();ofn.nMaxFile=DWORD(buffer.size());ofn.lpstrFilter=choice==1?L"7z 压缩包\0*.7z\0":L"ZIP 压缩包\0*.zip\0";ofn.lpstrDefExt=choice==1?L"7z":L"zip";ofn.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetSaveFileNameW(&ofn))return;
 Task task;task.files=paths;task.target=buffer.data();task.action=choice==1?Action::Create7z:Action::CreateZip;task.target.replace_extension(choice==1?L".7z":L".zip");auto id=queue.add(std::move(task));refresh(id);feedback(L"创建任务已加入队列。下方本批密码可用于加密。");
}
void taskMenu(std::optional<POINT> anchor={}){
 auto selected=selectedIndices(list,rows.size());bool fileTask=!selected.empty()&&std::all_of(selected.begin(),selected.end(),[](auto i){return fileActionAllowed(rows[size_t(i)].task);});auto menu=CreatePopupMenu();
 auto single=selected.size()==1&&!closing?&rows[size_t(selected.front())].task:nullptr;
 AppendMenuW(menu,MF_STRING|(single&&!single->result.output.empty()?0:MF_GRAYED),OpenOutput,L"打开结果（文件将被选中）");
 AppendMenuW(menu,MF_STRING|(single&&!single->files.empty()?0:MF_GRAYED),LocateSource,L"定位来源（首项）");
 AppendMenuW(menu,MF_STRING|(single&&!single->result.failedFile.empty()?0:MF_GRAYED),LocateProblem,L"定位问题文件/目录");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
 AppendMenuW(menu,MF_STRING|(single&&!running&&nestedTask(*single,batchSettings())?0:MF_GRAYED),RetryInner,L"只重试内层包（先加入队列）");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
 for(auto [id,label]:std::initializer_list<std::pair<Id,const wchar_t*>>{{Extract,L"解压所选文件"},{Inspect,L"仅识别格式"},{Restore,L"另存原包"},{Container,L"明确按压缩容器解压"}})AppendMenuW(menu,MF_STRING|(!running&&fileTask?0:MF_GRAYED),id,label);
 bool canReorder=taskView.unfiltered()&&!closing&&std::any_of(selected.begin(),selected.end(),[](auto i){return rows[size_t(i)].task.state==State::Waiting;});AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|(canReorder?0:MF_GRAYED),First,L"所选等待项移至最前");AppendMenuW(menu,MF_STRING|(canReorder?0:MF_GRAYED),Last,L"所选等待项移至最后");
 auto visibleFailures=std::count_if(rows.begin(),rows.end(),[](const auto& row){return retryable(row.task);});auto retryLabel=L"重试当前显示的失败/取消项 ("+std::to_wstring(visibleFailures)+L")";AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|(!running&&!closing&&visibleFailures?0:MF_GRAYED),RetryVisible,retryLabel.c_str());
 AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|(running&&!stopped&&!closing?0:MF_GRAYED),PauseBatch,pauseAfterCurrent?L"取消批次暂停请求":L"完成当前后暂停批次");AppendMenuW(menu,MF_STRING,ClearDone,L"移除全部已完成的队列项");RECT r;GetWindowRect(GetDlgItem(window,Tools),&r);taskMenuOpen=true;auto choice=TrackPopupMenu(menu,TPM_RETURNCMD,anchor?anchor->x:r.left,anchor?anchor->y:r.bottom,0,window,nullptr);taskMenuOpen=false;DestroyMenu(menu);if(choice)SendMessageW(window,WM_COMMAND,choice,0);
}
void dropFeedback(bool active,unsigned count){
 if(dragHover==active&&dragCount==count&&dropPage==currentPage)return;dragHover=active;dragCount=count;dropPage=currentPage;
 for(auto [table,page]:std::initializer_list<std::pair<HWND,Page>>{{list,Page::Queue},{passwordList,Page::Passwords},{volumeList,Page::Volumes}}){ListView_SetBkColor(table,active&&page==currentPage?RGB(235,248,242):GetSysColor(COLOR_WINDOW));ListView_SetTextBkColor(table,CLR_NONE);InvalidateRect(table,nullptr,FALSE);}
 InvalidateRect(GetDlgItem(window,DropZone),nullptr,FALSE);InvalidateRect(window,nullptr,FALSE);
}
void paint(){
 PAINTSTRUCT ps;auto dc=BeginPaint(window,&ps);RECT client;GetClientRect(window,&client);FillRect(dc,&client,background);SetViewportOrgEx(dc,-px(scrollX),-px(scrollY),nullptr);SetBkMode(dc,TRANSPARENT);auto l=currentLayout();int w=l.width,h=l.height;
 auto write=[&](int x,int y,int width,int height,const std::wstring& value,HFONT face,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){auto old=SelectObject(dc,face);SetTextColor(dc,color);RECT r{px(x),px(y),px(x+width),px(y+height)};DrawTextW(dc,value.c_str(),-1,&r,flags);SelectObject(dc,old);};
 if(iconHero)DrawIconEx(dc,px(24),px(20),iconHero,px(42),px(42),0,nullptr,DI_NORMAL);write(80,17,150,33,L"T解压",titleFont,RGB(24,40,43));write(81,49,150,18,L"看内容，不看后缀",smallFont,RGB(85,103,109));
 std::wstring hint=!workspaceError.empty()?L"工作台尚未保存":running?L"处理中 · 仍可添加文件":L"工作台自动保存 · 本机处理";if(dragCount){if(dragHover)hint=currentPage==Page::Volumes?L"松开补充分卷":currentPage==Page::Passwords?L"松开导入密码文本":L"松开加入 "+std::to_wstring(dragCount)+L" 项";else if(currentPage==Page::Passwords)hint=running?L"本轮结束后可导入密码":L"密码本接收 UTF-8 TXT";}write(w-290,27,266,34,hint,font,RGB(16,118,108),DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
 if(currentPage==Page::Queue){auto counts=queue.counts();auto total=counts.total();write(24,l.showDrop?252:184,w-48,24,L"显示 "+std::to_wstring(rows.size())+L" / "+std::to_wstring(total)+L" 项  ·  "+countSummary(counts),smallFont,RGB(70,91,96));write(388,h-100,w-412,28,L"留空时，自动使用密码本及同目录提示",smallFont,RGB(85,103,109));}
 else if(currentPage==Page::Passwords){write(24,98,w-48,28,L"密码本 · 已保存 "+std::to_wstring(book->count())+L" 条  /  当前显示 "+std::to_wstring(passwordRows.size())+L" 条",font,RGB(24,40,43));write(24,h-56,w-48,28,running?L"队列运行中，可查看与编辑草稿；本轮结束后保存或导入。":L"成功密码自动记住 · Windows 账户加密保存 · 未保存的密码输入不在工作台恢复范围",smallFont,RGB(85,103,109));}
 else if(currentPage==Page::Volumes){write(24,98,w-48,28,L"分卷顺序 · "+std::to_wstring(volumeEditor.files.size())+L" 卷",font,RGB(24,40,43));write(24,135,w-48,24,L"首卷必须在第一行。可多选整组移动；拖入、粘贴只补充文件，排序需明确操作。",smallFont,RGB(85,103,109));write(420,h-126,w-444,34,L"已选 "+std::to_wstring(selectedVolumes().size())+L" 卷",smallFont,RGB(85,103,109));}
 else {write(24,98,w-48,28,L"任务设置 · "+std::to_wstring(taskEditor.ids.size())+L" 项",font,RGB(24,40,43));write(24,138,w-48,40,L"同一队列可使用不同处理方式。“保存设置”保留当前状态；“保存并待开始”准备所选任务，仍需手动开始。",smallFont,RGB(85,103,109),DT_LEFT|DT_WORDBREAK);}
 EndPaint(window,&ps);
}
void drawButton(DRAWITEMSTRUCT* item){
 bool drop=item->CtlID==DropZone;bool active=item->CtlID==Start||item->CtlID==TaskApply||(item->CtlID==QueueTab&&currentPage==Page::Queue)||(item->CtlID==PasswordTab&&currentPage==Page::Passwords);bool disabled=(item->itemState&ODS_DISABLED)!=0;bool pressed=(item->itemState&ODS_SELECTED)!=0;
 auto fill=CreateSolidBrush(disabled?RGB(229,234,235):drop?(dragHover?RGB(222,246,238):RGB(239,247,244)):active?(pressed?RGB(10,91,84):RGB(16,118,108)):(pressed?RGB(225,235,232):RGB(247,249,250)));auto pen=CreatePen(PS_SOLID,px(drop&&dragHover?2:1),drop?RGB(100,170,157):active?RGB(16,118,108):RGB(207,219,217));auto oldB=SelectObject(item->hDC,fill);auto oldP=SelectObject(item->hDC,pen);auto oldF=SelectObject(item->hDC,font);
 RoundRect(item->hDC,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom,px(8),px(8));SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,disabled?RGB(130,143,146):active?RGB(255,255,255):RGB(24,70,66));RECT r=item->rcItem;
 if(drop){r.top+=px(5);r.bottom=r.top+px(24);auto value=dragHover?L"松开即可加入 "+std::to_wstring(dragCount)+L" 项":L"拖入文件或文件夹，也可点击添加";DrawTextW(item->hDC,value.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(item->hDC,smallFont);SetTextColor(item->hDC,RGB(85,103,109));r.top+=px(24);r.bottom+=px(20);DrawTextW(item->hDC,L"任意后缀 · 自动识别伪装与分卷 · 运行中可继续添加",-1,&r,DT_CENTER|DT_TOP|DT_SINGLELINE);}
 else {if(pressed)OffsetRect(&r,px(1),px(1));auto label=text(item->hwndItem);DrawTextW(item->hDC,label.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
 if((item->itemState&ODS_FOCUS)&&!(item->itemState&ODS_NOFOCUSRECT)){r=item->rcItem;InflateRect(&r,-px(4),-px(4));DrawFocusRect(item->hDC,&r);}SelectObject(item->hDC,oldF);SelectObject(item->hDC,oldB);SelectObject(item->hDC,oldP);DeleteObject(fill);DeleteObject(pen);
}
HWND table(Id id,const std::vector<const wchar_t*>& titles,bool single){auto h=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS|(single?LVS_SINGLESEL:0),id);ListView_SetExtendedListViewStyle(h,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);for(size_t i=0;i<titles.size();++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(titles[i]);c.cx=px(160);ListView_InsertColumn(h,int(i),&c);}SetWindowTheme(h,L"Explorer",nullptr);return h;}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
 case WM_COPYDATA:{
  auto payload=reinterpret_cast<const COPYDATASTRUCT*>(lp);if(!workspaceReady||closing||!payload||payload->dwData!=OpenFilesMessage)return FALSE;
  try{auto files=decodeOpenFiles(payload->lpData,payload->cbData);if(!files.empty())addFiles(files,IsWindowEnabled(hwnd)&&!taskMenuOpen);activateWorkspace(hwnd);return TRUE;}catch(const std::exception& e){feedback(L"文件打开请求未全部接收："+errorText(e));return FALSE;}
 }
 case WM_CREATE:{window=hwnd;dpi=GetDpiForWindow(hwnd);updateFonts();updateIcons();
  for(auto [label,id]:std::initializer_list<std::pair<const wchar_t*,Id>>{{L"解压队列",QueueTab},{L"密码本",PasswordTab},{L"添加文件…",Add},{L"添加文件夹…",AddFolder},{L"分卷顺序…",Volumes},{L"任务操作 ▾",Tools},{L"粘贴文件",PasteFiles},{L"新建压缩包 ▾",Create},{L"移除所选",Remove},{L"上移",Up},{L"下移",Down},{L"重试所选",Retry},{L"下一步",NextStep},{L"任务设置…",ConfigureTask},{L"暂缓所选",HoldTask},{L"选择目录…",Choose},{L"原包目录",Reset},{L"打开结果",OpenOutput},{L"开始队列",Start},{L"停止队列",Cancel}}){auto h=button(label,id);if(id!=QueueTab&&id!=PasswordTab)queueControls.push_back(h);}
  auto queueSearch=control(WC_EDITW,L"",ES_AUTOHSCROLL,QueueSearch);queueControls.push_back(queueSearch);SendMessageW(queueSearch,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"查找文件名或路径 · Ctrl+F"));queueControls.push_back(button(L"重置",SearchClear));
  auto filter=control(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|CBS_HASSTRINGS|WS_VSCROLL,QueueFilter);queueControls.push_back(filter);for(const auto* label:TaskFilterNames)SendMessageW(filter,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(filter,CB_SETCURSEL,0,0);
  queueControls.push_back(button(L"拖入文件或文件夹，或点击添加",DropZone));list=table(List,{L"文件 / 任务",L"操作",L"状态",L"阶段",L"说明"},false);queueControls.push_back(list);queueControls.push_back(control(WC_EDITW,L"",ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,TaskDetails));
  queueControls.push_back(control(WC_STATICW,L"本批输出",0,DirectoryLabel));directoryEdit=control(WC_EDITW,L"原包所在目录 · 创建同名文件夹，重名自动改名",ES_AUTOHSCROLL|ES_READONLY,Directory);queueControls.push_back(directoryEdit);
  queueControls.push_back(control(WC_STATICW,L"本批密码（可选）",0,BatchLabel));batchEdit=control(WC_EDITW,L"",ES_PASSWORD|ES_AUTOHSCROLL,BatchPassword);queueControls.push_back(batchEdit);SendMessageW(batchEdit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"优先尝试这个密码"));
  for(auto [label,id]:std::initializer_list<std::pair<const wchar_t*,Id>>{{L"自动解压嵌套包",Nested},{L"全部成功后，原包移入回收站",Recycle},{L"必要时深度识别",Deep}}){queueControls.push_back(control(WC_BUTTONW,label,BS_AUTOCHECKBOX,id));CheckDlgButton(hwnd,id,(id==Nested?startupWorkspace.defaults.recursive:id==Recycle?startupWorkspace.defaults.recycle:startupWorkspace.defaults.deep)?BST_CHECKED:BST_UNCHECKED);}
  phaseStatus=control(WC_STATICW,L"准备就绪",SS_LEFT|SS_ENDELLIPSIS|SS_NOTIFY,QueuePhase);queueControls.push_back(phaseStatus);
  status=control(WC_STATICW,L"添加文件，检查选项后开始。",SS_LEFT|SS_ENDELLIPSIS|SS_NOTIFY,Status);queueControls.push_back(status);for(auto h:{status,phaseStatus})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);progress=CreateWindowExW(0,PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,reinterpret_cast<HMENU>(ProgressBar),GetModuleHandleW(nullptr),nullptr);queueControls.push_back(progress);SendMessageW(progress,PBM_SETBARCOLOR,0,RGB(16,118,108));
  auto search=control(WC_EDITW,L"",ES_AUTOHSCROLL,PasswordSearch);passwordControls.push_back(search);passwordControls.push_back(button(L"所选密码用于本批",UsePassword));SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"搜索密码或关联目录（仅本机）"));passwordControls.push_back(control(WC_BUTTONW,L"显示已保存密码",BS_AUTOCHECKBOX,ShowPasswords));passwordList=table(PasswordList,{L"密码",L"使用次数",L"关联目录"},true);passwordControls.push_back(passwordList);
  passwordControls.push_back(control(WC_STATICW,L"新增密码 · 每行一条，保留空格；Enter 换行",0,PasswordLabel));passwordEdit=control(WC_EDITW,L"",ES_MULTILINE|ES_WANTRETURN|ES_AUTOHSCROLL|ES_AUTOVSCROLL|WS_VSCROLL|WS_HSCROLL,NewPassword);SendMessageW(passwordEdit,EM_SETLIMITTEXT,1024*1024,0);passwordControls.push_back(passwordEdit);passwordControls.push_back(button(L"保存新增密码",SavePasswords));passwordControls.push_back(button(L"导入密码文本…",ImportPasswords));passwordControls.push_back(control(WC_EDITW,L"可拖入 UTF-8 .txt 文本。自动跳过重复密码。",ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,PasswordFeedback));
  volumeList=table(VolumeList,{L"顺序",L"文件名",L"大小",L"所在目录"},false);volumeControls.push_back(volumeList);for(auto [label,id]:std::initializer_list<std::pair<const wchar_t*,Id>>{{L"添加分卷…",VolumeAdd},{L"粘贴分卷",VolumePaste},{L"按文件名排序",VolumeSort},{L"移除所选",VolumeRemove},{L"上移",VolumeUp},{L"下移",VolumeDown},{L"返回队列",CancelVolumes},{L"保存顺序",AcceptVolumes}})volumeControls.push_back(button(label,id));volumeControls.push_back(control(WC_STATICW,L"",SS_LEFT,VolumeFeedback));
  taskControls.push_back(control(WC_BUTTONW,L"使用专用处理设置",BS_AUTOCHECKBOX,TaskCustom));taskControls.push_back(control(WC_STATICW,L"输出到",0,TaskDirectoryLabel));taskControls.push_back(control(WC_EDITW,L"",ES_AUTOHSCROLL|ES_READONLY,TaskDirectory));
  for(auto [label,id]:std::initializer_list<std::pair<const wchar_t*,Id>>{{L"选择目录…",TaskChoose},{L"原包目录",TaskSourceDirectory},{L"返回队列",TaskBack},{L"保存设置",TaskSave},{L"保存并待开始",TaskApply}})taskControls.push_back(button(label,id));
  for(auto [label,id]:std::initializer_list<std::pair<const wchar_t*,Id>>{{L"自动展开新解出的嵌套压缩包",TaskNested},{L"解压全部成功后，将实际原包移入回收站（仅解压）",TaskRecycle},{L"必要时深度识别伪装和内嵌压缩包",TaskDeep}})taskControls.push_back(control(WC_BUTTONW,label,BS_AUTOCHECKBOX,id));
  taskControls.push_back(control(WC_STATICW,L"",SS_LEFT,TaskFeedback));
  HWND previous=HWND_TOP;for(auto id:navigationOrder()){auto target=GetDlgItem(hwnd,id);SetWindowPos(target,previous,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);previous=target;}
  SendMessageW(filter,CB_SETITEMHEIGHT,0,px(22));SendMessageW(filter,CB_SETITEMHEIGHT,WPARAM(-1),px(22));
  tips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
  SendMessageW(tips,CCM_SETUNICODEFORMAT,TRUE,0);SendMessageW(tips,TTM_SETMAXTIPWIDTH,0,px(560));SendMessageW(tips,TTM_SETDELAYTIME,TTDT_AUTOPOP,8000);SendMessageW(tips,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
  for(auto target:{status,phaseStatus}){TOOLINFOW tool{sizeof(tool)};tool.hwnd=hwnd;tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.uId=reinterpret_cast<UINT_PTR>(target);tool.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
  auto drop=createFileDropTarget({
   [](const auto& files){return !closing&&acceptsDrop(currentPage,files,running);},
   [](const auto& files){return receiveFiles(files);},
   dropFeedback,
   [](const auto& error){message(error);}});for(auto target:{hwnd,list,GetDlgItem(hwnd,DropZone),passwordList,passwordEdit,volumeList})if(SUCCEEDED(RegisterDragDrop(target,drop)))dropWindows.push_back(target);drop->Release();if(dropWindows.empty())DragAcceptFiles(hwnd,TRUE);
  columnSizes=startupWorkspace.columns;pageScroll=startupWorkspace.scroll;scrollX=pageScroll[0].x;scrollY=pageScroll[0].y;taskView=startupWorkspace.view;restoredSelection=startupWorkspace.selection;
  for(size_t i=0;i<PageCount;++i){auto target=GetDlgItem(hwnd,startupWorkspace.focus[i]);if(std::find(pageGroups[i]->begin(),pageGroups[i]->end(),target)!=pageGroups[i]->end())pageFocus[i]=target;}
  selectedOutput=startupWorkspace.defaults.output;SetWindowTextW(directoryEdit,selectedOutput.empty()?L"原包所在目录 · 创建同名文件夹，重名自动改名":selectedOutput.c_str());SendDlgItemMessageW(hwnd,QueueFilter,CB_SETCURSEL,unsigned(taskView.filter),0);SetWindowTextW(queueSearch,taskView.query.c_str());
  volumeEditor=std::move(startupWorkspace.volumes);taskEditor=std::move(startupWorkspace.editor);if(taskEditor.active)loadTaskEditorControls();refresh();
  auto page=startupWorkspace.page;if(page==Page::Volumes&&!volumeEditor.active)page=Page::Queue;if(page==Page::TaskSettings&&!taskEditor.active)page=Page::Queue;
  if(page==Page::Volumes)refreshVolumes({},true);showPage(page);workspaceReady=true;SetTimer(hwnd,WorkspaceTimer,1000,nullptr);
  if(!startupNotice.empty())feedback(startupNotice);else if(workspaceLoaded)feedback(L"已恢复工作台 "+std::to_wstring(queue.counts().total())+L" 项。未完成任务不会自动执行；上次处理中断的任务可重试。");return 0;}
 case WM_SIZE:if(wp!=SIZE_MINIMIZED&&!layingOut){workspaceChanged();layout();revealFocus();}return 0;
 case WM_VSCROLL:case WM_HSCROLL:{int bar=msg==WM_VSCROLL?SB_VERT:SB_HORZ;SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(hwnd,bar,&info);int value=info.nPos;switch(LOWORD(wp)){case SB_LINEUP:value-=32;break;case SB_LINEDOWN:value+=32;break;case SB_PAGEUP:value-=int(info.nPage)*3/4;break;case SB_PAGEDOWN:value+=int(info.nPage)*3/4;break;case SB_THUMBTRACK:value=info.nTrackPos;break;case SB_TOP:value=info.nMin;break;case SB_BOTTOM:value=info.nMax;break;}scrollView(bar,value);return 0;}
 case WM_MOUSEWHEEL:scrollView(SB_VERT,scrollY-GET_WHEEL_DELTA_WPARAM(wp)*48/WHEEL_DELTA);return 0;
 case WM_DPICHANGED:workspaceChanged();dpi=HIWORD(wp);updateFonts();updateIcons();{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}layout();revealFocus();return 0;
 case WM_GETMINMAXINFO:{MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={std::min<LONG>(px(920),monitor.rcWork.right-monitor.rcWork.left-px(24)),std::min<LONG>(px(620),monitor.rcWork.bottom-monitor.rcWork.top-px(24))};return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_CTLCOLORSTATIC:SetBkColor(reinterpret_cast<HDC>(wp),RGB(247,249,250));SetTextColor(reinterpret_cast<HDC>(wp),RGB(24,40,43));return reinterpret_cast<LRESULT>(background);
 case WM_PAINT:paint();return 0;
 case WM_DRAWITEM:if(reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlType==ODT_BUTTON){drawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}break;
 case WM_DROPFILES:{auto drop=reinterpret_cast<HDROP>(wp);auto files=pathsFromDrop(drop);DragFinish(drop);try{receiveFiles(files);}catch(const std::exception& e){message(errorText(e));}return 0;}
 case WM_QUEUE_UPDATE:if(!SetTimer(hwnd,QueueRefreshTimer,90,nullptr))flushUpdates();return 0;
 case WM_TIMER:if(wp==WorkspaceTimer){try{if(running)updatePhaseCaption();checkWorkspaceSave();}catch(const std::exception& e){feedback(L"工作台快照未能保存："+errorText(e));}return 0;}if(wp==QueueRefreshTimer){flushUpdates();return 0;}if(wp==PasswordPreviewTimer){KillTimer(hwnd,PasswordPreviewTimer);updatePasswordPreview();return 0;}break;
 case WM_FINISHED:{KillTimer(hwnd,QueueRefreshTimer);backgroundUpdates.take();running=false;currentPhase.clear();auto counts=queue.counts();if(stopped||!counts.waiting)pauseAfterCurrent=false;setProgress(!wp&&!stopped&&!counts.attention&&!counts.waiting&&!counts.paused?100:0,false);if(closing){if(wp)delete reinterpret_cast<std::wstring*>(lp);if(saveBeforeExit())DestroyWindow(hwnd);else {closing=false;refresh();}return 0;}refresh();if(!wp&&!stopped&&!pauseAfterCurrent&&counts.waiting){start();requestWorkspaceSave(true);return 0;}if(wp){std::unique_ptr<std::wstring> error(reinterpret_cast<std::wstring*>(lp));feedback(*error);}else feedback((stopped?L"已停止。":pauseAfterCurrent?L"当前项已结束，批次已暂停。":L"本轮已结束。")+countSummary(queue.counts()));if(currentPage==Page::Passwords){refreshPasswords();updatePasswordPreview();}requestWorkspaceSave(true);return 0;}
 case WM_CONTEXTMENU:if(reinterpret_cast<HWND>(wp)==list){
  POINT anchor{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
  if(anchor.x==-1&&anchor.y==-1){int index=ListView_GetNextItem(list,-1,LVNI_FOCUSED);RECT r{};if(index>=0){ListView_EnsureVisible(list,index,FALSE);ListView_GetItemRect(list,index,&r,LVIR_BOUNDS);}else GetClientRect(list,&r);anchor={r.left+px(16),index>=0?r.bottom:r.top+px(24)};ClientToScreen(list,&anchor);}
  else {auto point=anchor;ScreenToClient(list,&point);LVHITTESTINFO hit{};hit.pt=point;int index=ListView_HitTest(list,&hit);if(index<0){ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);}else if(!(ListView_GetItemState(list,index,LVIS_SELECTED)&LVIS_SELECTED)){ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(list,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}updateControls();}
  taskMenu(anchor);return 0;}break;
 case WM_NOTIFY:{auto notification=reinterpret_cast<NMHDR*>(lp);
  if(notification->hwndFrom==tips&&notification->code==TTN_GETDISPINFOW){auto info=reinterpret_cast<NMTTDISPINFOW*>(lp);tooltipText=text(reinterpret_cast<HWND>(notification->idFrom));info->lpszText=tooltipText.data();return 0;}
  if(notification->code==HDN_ENDTRACKW||notification->code==HDN_ENDTRACKA){auto header=reinterpret_cast<NMHEADERW*>(lp);auto parent=GetParent(notification->hwndFrom);Page page;
   if(parent==list)page=Page::Queue;else if(parent==passwordList)page=Page::Passwords;else if(parent==volumeList)page=Page::Volumes;else break;
   if(header->pitem&&(header->pitem->mask&HDI_WIDTH)){std::vector<int> widths;int count=Header_GetItemCount(notification->hwndFrom);for(int i=0;i<count;++i)widths.push_back(i==header->iItem?header->pitem->cxy:ListView_GetColumnWidth(parent,i));columnSizes[size_t(page)].remember(std::move(widths),dpi);workspaceChanged();}return FALSE;
  }
  if(notification->code==LVN_GETEMPTYMARKUP){auto empty=reinterpret_cast<NMLVEMPTYMARKUP*>(lp);const wchar_t* hint=L"";
   if(notification->idFrom==List)hint=taskView.unfiltered()?L"拖入文件或文件夹，或点击上方添加文件。\n任意后缀都会按内容识别；加入后由你开始队列。":L"当前查找或筛选没有任务。\n点击重置可查看全部队列。";
   else if(notification->idFrom==PasswordList)hint=book->count()?L"没有匹配的密码。清空查找可查看密码本。":L"密码本还是空的。\n可在下方每行添加一个密码，或导入 UTF-8 文本。";
   else if(notification->idFrom==VolumeList)hint=L"拖入或粘贴全部分卷，首卷放在第一行。\n调整的只是读取顺序，源文件保留原名。";
   empty->dwFlags=EMF_CENTERED;lstrcpynW(empty->szMarkup,hint,int(std::size(empty->szMarkup)));return TRUE;
  }
  if(notification->code==LVN_ODFINDITEMW){auto info=reinterpret_cast<NMLVFINDITEMW*>(lp);auto flags=info->lvfi.flags;if(!(flags&UINT(LVFI_STRING|LVFI_PARTIAL|LVFI_SUBSTRING|LVFI_WRAP))||(flags&~UINT(LVFI_STRING|LVFI_PARTIAL|LVFI_SUBSTRING|LVFI_WRAP)))return -1;
   auto query=info->lvfi.psz?std::wstring(info->lvfi.psz):std::wstring();bool prefix=(flags&UINT(LVFI_PARTIAL|LVFI_SUBSTRING))!=0,wrap=(flags&LVFI_WRAP)!=0;
   if(notification->idFrom==List)return findListItem(rows.size(),query,info->iStart,prefix,wrap,[](size_t i){return rows[i].cells[0];});
   if(notification->idFrom==VolumeList)return findListItem(volumeCells.size(),query,info->iStart,prefix,wrap,[](size_t i){return volumeCells[i][1];});
   if(notification->idFrom==PasswordList)return findListItem(passwordCells.size(),query,info->iStart,prefix,wrap,[](size_t i){return passwordCells[i][0];});return -1;
  }
  if(notification->code==LVN_GETDISPINFOW){auto info=reinterpret_cast<NMLVDISPINFOW*>(lp);int i=info->item.iItem,j=info->item.iSubItem;if(info->item.mask&LVIF_TEXT){info->item.pszText=const_cast<wchar_t*>(L"");if(i>=0&&j>=0){if(notification->idFrom==List&&size_t(i)<rows.size()&&j<5)info->item.pszText=rows[size_t(i)].cells[size_t(j)].data();else if(notification->idFrom==PasswordList&&size_t(i)<passwordCells.size()&&j<3)info->item.pszText=passwordCells[size_t(i)][size_t(j)].data();else if(notification->idFrom==VolumeList&&size_t(i)<volumeCells.size()&&j<4)info->item.pszText=volumeCells[size_t(i)][size_t(j)].data();}}return 0;}
  if((notification->code==LVN_ITEMCHANGED||notification->code==LVN_ODSTATECHANGED)&&!refreshing){if(notification->idFrom==List){updateControls();workspaceChanged();}else if(notification->idFrom==VolumeList){updateVolumeControls();workspaceChanged();}else if(notification->idFrom==PasswordList)updatePasswordControls();return 0;}
  if(notification->idFrom==PasswordList&&(notification->code==NM_DBLCLK||notification->code==NM_RETURN)){if(notification->code==NM_RETURN||reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem>=0)activateList(passwordList);return 0;}
  if(notification->idFrom==List&&(notification->code==NM_DBLCLK||notification->code==NM_RETURN)){if(notification->code==NM_RETURN||reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem>=0)activateList(list);return 0;}break;}
 case WM_COMMAND:try{auto id=LOWORD(wp);if(id==QueueSearch&&HIWORD(wp)==EN_CHANGE){taskView.query=text(GetDlgItem(hwnd,QueueSearch));workspaceChanged();if(list)refresh();return 0;}
  if(id==QueueFilter&&HIWORD(wp)==CBN_SELCHANGE){auto index=SendDlgItemMessageW(hwnd,QueueFilter,CB_GETCURSEL,0,0);if(index>=0){taskView.filter=TaskFilter(index);workspaceChanged();refresh();}return 0;}
  if(id==NewPassword&&HIWORD(wp)==EN_CHANGE){SetTimer(hwnd,PasswordPreviewTimer,160,nullptr);return 0;}
  if(id==PasswordSearch&&HIWORD(wp)==EN_CHANGE){refreshPasswords();return 0;}switch(id){
  case QueueTab:showPage(Page::Queue);break;case PasswordTab:showPage(Page::Passwords);break;
  case Add:case DropZone:addFiles(chooseFiles());break;case AddFolder:{auto dir=chooseDirectory();if(!dir.empty())addFiles({dir});break;}
  case Volumes:orderedVolumes();break;case Tools:taskMenu();break;case Create:createArchive();break;
  case Start:start();break;
  case ConfigureTask:openTaskSettings();break;
  case PauseBatch:if(running&&!stopped&&!closing){pauseAfterCurrent=!pauseAfterCurrent;feedback(pauseAfterCurrent?L"当前项继续处理，结束后暂停批次；可再次选择菜单取消暂停请求。":L"已取消暂停请求，本批次继续。 ");updateControls();}break;
  case HoldTask:{auto ids=selectedTasks();bool resume=false;for(auto row:selectedIndices(list,rows.size())){const auto& task=rows[size_t(row)].task;if(task.state==State::Waiting&&task.paused)resume=true;}auto changed=queue.setPaused(ids,!resume);refresh();feedback((resume?L"已继续 ":L"已暂缓 ")+std::to_wstring(changed)+L" 项。暂缓项保留源文件依赖，不参与开始全部。");break;}
  case TaskCustom:readTaskEditor();updateTaskControls();break;case TaskNested:case TaskRecycle:case TaskDeep:readTaskEditor();break;
  case TaskChoose:workspaceChanged();{auto dir=chooseDirectory();if(!dir.empty()){taskEditor.value.output=dir;SetWindowTextW(GetDlgItem(hwnd,TaskDirectory),dir.c_str());}break;}
  case TaskSourceDirectory:workspaceChanged();taskEditor.value.output.clear();SetWindowTextW(GetDlgItem(hwnd,TaskDirectory),L"原包所在目录");break;
  case TaskBack:readTaskEditor();showPage(Page::Queue);break;case TaskSave:saveTaskSettings(false);break;case TaskApply:saveTaskSettings(true);break;
  case SearchClear:taskView={};SendDlgItemMessageW(hwnd,QueueFilter,CB_SETCURSEL,0,0);SetWindowTextW(GetDlgItem(hwnd,QueueSearch),L"");refresh();focusAndReveal(GetDlgItem(hwnd,QueueSearch));break;
  case NextStep:{auto task=selectedTask();if(!task||task->state!=State::Attention||running)break;if(task->result.status==L"password_required"){focusAndReveal(batchEdit);SendMessageW(batchEdit,EM_SETSEL,0,-1);revealFocus();feedback(L"填写本批优先密码后，重试所选任务，再开始全部等待项。");}else if(task->result.status==L"source_unavailable")relocateSource(*task);else if(task->result.status==L"missing_volume"||task->result.status==L"ambiguous_volume")orderedVolumes();break;}case Cancel:stopped=true;feedback(L"未开始的任务留在队列，本轮结束后可重试取消项。");updateControls();break;
  case Choose:{auto dir=chooseDirectory();if(!dir.empty()){selectedOutput=dir;SetWindowTextW(directoryEdit,dir.c_str());workspaceChanged();}break;}
  case Reset:workspaceChanged();selectedOutput.clear();SetWindowTextW(directoryEdit,L"原包所在目录 · 创建同名文件夹，重名自动改名");break;
  case ShowPasswords:refreshPasswords();break;case UsePassword:useSavedPassword();break;
  case Nested:case Recycle:case Deep:workspaceChanged();break;
  case SavePasswords:{if(running)break;auto values=splitLines(text(passwordEdit));if(values.empty()){SetWindowTextW(GetDlgItem(hwnd,PasswordFeedback),L"先输入密码，每行一条。也可直接导入文本。");focusAndReveal(passwordEdit);break;}auto result=book->add(values);SetWindowTextW(passwordEdit,L"");SetWindowTextW(GetDlgItem(hwnd,PasswordFeedback),importReport(result).c_str());refreshPasswords();updatePasswordPreview();focusAndReveal(passwordEdit);break;}
  case ImportPasswords:if(running)break;importPasswords(chooseFiles(L"导入密码文本",L"UTF-8 密码文本\0*.txt\0所有文件\0*.*\0"));break;
  case VolumeAdd:addVolumes(chooseFiles(L"补充分卷"));break;
  case PasteFiles:pasteFiles(false);break;case VolumePaste:pasteFiles(true);break;
  case VolumeRemove:workspaceChanged();{auto selected=selectedVolumes();if(!selected.empty()){int next=selected.front();for(auto it=selected.rbegin();it!=selected.rend();++it)volumeEditor.files.erase(volumeEditor.files.begin()+*it);auto remaining=volumeEditor.files.empty()?std::vector<fs::path>{}:std::vector<fs::path>{volumeEditor.files[size_t(std::min(next,int(volumeEditor.files.size())-1))]};refreshVolumes(remaining);SetWindowTextW(GetDlgItem(hwnd,VolumeFeedback),L"已移除选中的列表项，源文件保留。");}break;}
  case VolumeUp:case VolumeDown:workspaceChanged();{moveVolumes(volumeEditor.files,selectedVolumes(),id==VolumeUp?-1:1);refreshVolumes();break;}
  case VolumeSort:workspaceChanged();sortVolumes(volumeEditor.files);refreshVolumes();SetWindowTextW(GetDlgItem(hwnd,VolumeFeedback),L"已按文件名排序，请核对首卷及实际分卷顺序；源文件未改动。");break;
  case AcceptVolumes:acceptVolumes();break;case CancelVolumes:showPage(Page::Queue);refresh(volumeEditor.taskId);break;
  case RetryInner:{if(running||closing)break;auto parent=selectedTask();if(!parent)break;auto child=nestedTask(*parent,batchSettings());if(!child)break;
   std::error_code error;if(!fs::is_regular_file(child->files.front(),error)){feedback(L"内层文件不存在或无法读取，请定位问题文件检查："+child->files.front().wstring());break;}
   auto id=queue.add(std::move(*child));if(!id){feedback(L"该内层包已在等待队列或正在使用。外层记录和已有结果保留。");break;}
   refresh(id);feedback(L"已单独准备内层包，输出留在该包所在目录；开始全部后处理。外层记录仍标记未完成，清理遵守新任务设置。若当前筛选未显示，请切回全部任务。");break;}
  case OpenOutput:case LocateSource:case LocateProblem:{if(closing)break;auto task=selectedTask();if(!task)break;
   fs::path path=id==OpenOutput?task->result.output:id==LocateProblem?task->result.failedFile:task->files.empty()?fs::path{}:task->files.front();
   if(path.empty()){focusAndReveal(GetDlgItem(hwnd,TaskDetails));break;}
   try{ShellLocation location(path,id!=OpenOutput);location.open(hwnd);}catch(const std::exception& error){feedback(errorText(error));}break;}
  case Extract:case Inspect:case Restore:case Container:{if(running)break;auto action=id==Inspect?Action::Inspect:id==Restore?Action::Restore:Action::Extract;auto changed=queue.setAction(selectedTasks(),action,id==Container);refresh();feedback(changed?L"已将 "+std::to_wstring(changed)+L" 项设为“"+(id==Container?L"容器解压":actionName(action))+L"”，开始全部后执行。":L"请选择可处理的文件任务。");break;}
  case Remove:case Retry:{auto ids=selectedTasks();unsigned changed=0,eligible=0;
   if(id==Remove)changed=queue.remove(ids);
   else {std::vector<uint64_t> retry;for(auto i:selectedIndices(list,rows.size()))if(retryable(rows[size_t(i)].task))retry.push_back(rows[size_t(i)].task.id);eligible=unsigned(retry.size());changed=queue.retry(retry);}
   refresh();if(id==Retry)feedback(retrySummary(unsigned(ids.size()),eligible,changed));
   else feedback(L"已移除队列项 "+std::to_wstring(changed)+L" 项。"+(changed<ids.size()?L" 正在处理的任务保持不变。":L""));break;
  }
  case Up:case Down:case First:case Last:{if(closing)break;if(!taskView.unfiltered()){feedback(L"重置查找并切换到全部任务后，可调整执行顺序。");break;}auto direction=id==Up?QueueMove::Up:id==Down?QueueMove::Down:id==First?QueueMove::First:QueueMove::Last;auto changed=queue.reorder(selectedTasks(),direction);refresh();int focused=ListView_GetNextItem(list,-1,LVNI_FOCUSED);if(focused>=0)ListView_EnsureVisible(list,focused,FALSE);if(GetFocus()==list)revealFocus();feedback(changed?L"已调整 "+std::to_wstring(changed)+L" 个等待项。处理中及历史项位置保持，暂缓设置不变。":L"所选等待项已在边界；不能跨过正在处理的任务。");break;}
  case RetryVisible:{if(running||closing){feedback(L"请先暂停批次或等待结束，再准备当前显示项的重试。");break;}auto ids=taskView.retryIds(queue.snapshot());auto changed=queue.retry(ids);refresh();feedback(L"当前显示的失败/取消项中，已准备重试 "+std::to_wstring(changed)+L" 项。尚未开始；点击开始全部会处理所有等待项，包括隐藏项。");break;}
  case ClearDone:{std::vector<uint64_t> ids;for(const auto& task:queue.snapshot())if(task.state==State::Done)ids.push_back(task.id);queue.remove(ids);refresh();break;}
 }}catch(const std::exception& e){message(errorText(e));}return 0;
 case WM_CLOSE:if(closing)return 0;if(running){if(MessageBoxW(hwnd,L"停止当前操作并退出？已完成任务保留现有结果。",L"T解压",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;if(running){stopped=true;closing=true;feedback(L"正在停止，结束后退出…");updateControls();return 0;}}closing=true;if(saveBeforeExit())DestroyWindow(hwnd);else {closing=false;refresh();}return 0;
 case WM_DESTROY:workspaceReady=false;RemovePropW(hwnd,instanceMarker.c_str());KillTimer(hwnd,WorkspaceTimer);KillTimer(hwnd,QueueRefreshTimer);KillTimer(hwnd,PasswordPreviewTimer);for(auto target:dropWindows)RevokeDragDrop(target);dropWindows.clear();for(auto icon:{iconLarge,iconSmall,iconHero})if(icon)DestroyIcon(icon);PostQuitMessage(0);return 0;
 }return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 std::unique_ptr<InstanceGate> gate;std::vector<fs::path> paths;
 try{int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);if(!args)throw std::runtime_error("Cannot read file-open arguments");struct FreeArgs{LPWSTR* value;~FreeArgs(){LocalFree(value);}} freeArgs{args};for(int i=1;i<argc;++i)paths.push_back(fs::absolute(args[i]).lexically_normal());instanceMarker=workspaceInstanceName();gate=std::make_unique<InstanceGate>(instanceMarker);
  if(!gate->primary()){gate.reset();auto result=forwardOpenFiles(instanceMarker,paths);if(result!=ForwardResult::Received){auto note=result==ForwardResult::Unavailable?L"已有工作台尚未就绪，请稍后重试。没有打开第二个工作台。":result==ForwardResult::Unconfirmed?L"未能确认文件请求的接收状态，请先检查已有工作台。窗口权限不同或正忙时可能无法转交。":L"已有工作台未确认完整接收，请检查主窗口；若正在退出，请结束后再打开。";MessageBoxW(nullptr,note,L"T解压",MB_OK|MB_ICONINFORMATION);}return result==ForwardResult::Received?0:1;}
 }catch(const std::exception& e){auto note=L"无法打开本机工作台："+errorText(e);MessageBoxW(nullptr,note.c_str(),L"T解压",MB_OK|MB_ICONERROR);return 1;}
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);auto ole=OleInitialize(nullptr);if(FAILED(ole))return 1;
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
 try{book=std::make_unique<PasswordBook>();}catch(const std::exception& e){MessageBoxW(nullptr,errorText(e).c_str(),L"T解压",MB_OK|MB_ICONERROR);OleUninitialize();return 1;}
 try{WorkspaceStore store;workspaceFile=store.path();try{if(auto saved=store.load()){startupWorkspace=std::move(*saved);queue.restore(std::move(startupWorkspace.tasks));workspaceLoaded=true;}}catch(const std::exception& e){auto backup=store.preserveUnreadable();startupWorkspace=Workspace{};queue.restore({});startupNotice=L"上次工作台无法读取，原记录已保留为 "+backup.filename().wstring()+L"。";}
  workspaceWriter=std::make_unique<WorkspaceWriter>(workspaceFile);
 }catch(const std::exception& e){startupNotice=L"工作台恢复/自动保存暂不可用："+errorText(e);workspaceError=startupNotice;}
 WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"TUnpack";cls.lpfnWndProc=procedure;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));cls.hbrBackground=background;RegisterClassW(&cls);
 dpi=GetDpiForSystem();RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);RECT initial{0,0,std::min<LONG>(px(startupWorkspace.width),work.right-work.left-px(48)),std::min<LONG>(px(startupWorkspace.height),work.bottom-work.top-px(72))};if(!workspaceLoaded)AdjustWindowRectExForDpi(&initial,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VSCROLL|WS_HSCROLL,FALSE,0,dpi);
 window=CreateWindowExW(0,cls.lpszClassName,L"T解压",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VSCROLL|WS_HSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,initial.right-initial.left,initial.bottom-initial.top,nullptr,nullptr,instance,nullptr);
 if(!window||!SetPropW(window,instanceMarker.c_str(),reinterpret_cast<HANDLE>(1))){MessageBoxW(window,L"无法建立工作台接收入口，请重试。",L"T解压",MB_OK|MB_ICONERROR);if(window)DestroyWindow(window);workspaceWriter.reset();OleUninitialize();return 1;}
 ShowWindow(window,startupWorkspace.maximized?SW_SHOWMAXIMIZED:show);UpdateWindow(window);if(!paths.empty())addFiles(paths);
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){
  rememberFocus();
  if(msg.message==WM_KEYDOWN){bool ctrl=GetKeyState(VK_CONTROL)<0;auto focus=GetFocus();
   if(!ctrl&&GetKeyState(VK_MENU)>=0&&msg.wParam==VK_RETURN&&(focus==list||focus==passwordList)){activateList(focus);continue;}
   if(ctrl&&GetKeyState(VK_SHIFT)<0&&msg.wParam==VK_SPACE){SendMessageW(window,WM_COMMAND,PauseBatch,0);continue;}
   if(ctrl&&msg.wParam=='O'){invokeCommand(currentPage==Page::TaskSettings?TaskChoose:currentPage==Page::Volumes?VolumeAdd:currentPage==Page::Passwords?ImportPasswords:Add);continue;}
   if(ctrl&&msg.wParam==VK_RETURN){invokeCommand(currentPage==Page::TaskSettings?TaskApply:currentPage==Page::Volumes?AcceptVolumes:currentPage==Page::Passwords?SavePasswords:Start);continue;}
   if(ctrl&&msg.wParam=='A'&&(focus==list||focus==volumeList)){ListView_SetItemState(focus,-1,LVIS_SELECTED,LVIS_SELECTED);if(focus==list)updateControls();else updateVolumeControls();continue;}
   if(ctrl&&msg.wParam=='V'&&(focus==list||focus==volumeList)){invokeCommand(focus==list?PasteFiles:VolumePaste);continue;}
   if(ctrl&&msg.wParam=='F'&&(currentPage==Page::Queue||currentPage==Page::Passwords)){auto search=GetDlgItem(window,currentPage==Page::Queue?QueueSearch:PasswordSearch);focusAndReveal(search);SendMessageW(search,EM_SETSEL,0,-1);revealFocus();continue;}
   if(ctrl&&(msg.wParam==VK_UP||msg.wParam==VK_DOWN)&&(focus==list||focus==volumeList)){invokeCommand(focus==list?(msg.wParam==VK_UP?Up:Down):(msg.wParam==VK_UP?VolumeUp:VolumeDown));continue;}
   if(msg.wParam==VK_DELETE&&(focus==list||focus==volumeList)){invokeCommand(focus==list?Remove:VolumeRemove);continue;}
   if(msg.wParam==VK_ESCAPE&&(focus==GetDlgItem(window,QueueSearch)||focus==GetDlgItem(window,PasswordSearch))){SetWindowTextW(focus,L"");continue;}
   if(msg.wParam==VK_ESCAPE&&currentPage==Page::TaskSettings){SendMessageW(window,WM_COMMAND,TaskBack,0);continue;}
   if(msg.wParam==VK_ESCAPE&&focus==volumeList){SendMessageW(window,WM_COMMAND,CancelVolumes,0);continue;}
  }
  if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(msg.message==WM_KEYDOWN||msg.message==WM_CHAR)revealFocus();
 }
 if(worker.joinable())worker.join();workspaceWriter.reset();for(auto face:{font,titleFont,smallFont})if(face)DeleteObject(face);DeleteObject(background);OleUninitialize();return 0;
}
