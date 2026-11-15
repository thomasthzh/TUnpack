#include "ui_model.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

using namespace lu::ui;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static constexpr unsigned scales[]={96,120,144,168,192,216,240,288};
static bool overlaps(const Box& a,const Box& b){return a.x<b.right()&&a.right()>b.x&&a.y<b.bottom()&&a.bottom()>b.y;}
static const std::vector<Id> queueIds={QueueTab,PasswordTab,Add,AddFolder,Volumes,Tools,Create,PasteFiles,QueueSearch,SearchClear,QueueFilter,List,TaskDetails,Remove,Up,Down,Retry,HoldTask,NextStep,ConfigureTask,OpenOutput,DirectoryLabel,Directory,Choose,Reset,Nested,Recycle,Deep,BatchLabel,BatchPassword,QueuePhase,Status,ProgressBar,Cancel,Start};
static const std::vector<Id> passwordIds={QueueTab,PasswordTab,PasswordSearch,ShowPasswords,PasswordList,PasswordLabel,UsePassword,NewPassword,SavePasswords,ImportPasswords,PasswordFeedback};
static const std::vector<Id> volumeIds={QueueTab,PasswordTab,VolumeList,VolumeFeedback,VolumeAdd,VolumePaste,VolumeSort,VolumeRemove,VolumeUp,VolumeDown,CancelVolumes,AcceptVolumes};
static const std::vector<Id> taskIds={QueueTab,PasswordTab,TaskCustom,TaskDirectoryLabel,TaskDirectory,TaskChoose,TaskSourceDirectory,TaskNested,TaskRecycle,TaskDeep,TaskFeedback,TaskBack,TaskSave,TaskApply};
int main(){
    unsigned passed=0,total=0;auto check=[&](const char* name,const auto& test){++total;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    check("Production control rectangles stay inside every supported canvas at 100-300 percent",[]{
        for(int width:{540,720,880,920,1120,1600})for(int height:{200,440,457,620,759,760,820,1080})for(auto dpi:scales){
            auto layout=makeLayout(width,height);auto canvas=pixelBox({0,0,layout.width,layout.height},dpi);
            for(auto box:layout.boxes)if(box.width){auto scaled=pixelBox(box,dpi);require(scaled.x>=0&&scaled.y>=0&&scaled.right()<=canvas.right()&&scaled.bottom()<=canvas.bottom()&&scaled.width>0&&scaled.height>0,"control outside canvas");}
        }
    });
    auto disjoint=[](const std::vector<Id>& ids,bool drop){for(int width:{540,720,880,920,1120,1600})for(int height:{200,620,759,760,820,1080})for(auto dpi:scales){auto layout=makeLayout(width,height);auto visible=ids;if(drop&&layout.showDrop)visible.push_back(DropZone);for(size_t i=0;i<visible.size();++i)for(size_t j=i+1;j<visible.size();++j)require(!overlaps(pixelBox(layout.at(visible[i]),dpi),pixelBox(layout.at(visible[j]),dpi)),"controls overlap");}};
    check("Queue controls and optional drop area do not overlap",[&]{disjoint(queueIds,true);});
    check("Password search, list, draft and feedback do not overlap",[&]{disjoint(passwordIds,false);});
    check("Volume feedback and both action rows do not overlap",[&]{disjoint(volumeIds,false);});
    check("Task settings editor keeps controls and actions separate",[&]{disjoint(taskIds,false);});
    check("Fractional DPI uses shared edges instead of rounding each width separately",[]{
        for(auto dpi:scales)for(int offset:{0,13,137}){auto a=pixelBox({25,17,113,31},dpi,offset,offset);auto b=pixelBox({138,48,77,17},dpi,offset,offset);require(a.right()==b.x&&a.bottom()==b.y,"fractional shared edge split");}
    });
    check("Scrollbar interdependence leaves the final row and right edge reachable",[]{
        auto view=fitViewport(890,600,17,17,96);require(view.horizontal&&view.vertical&&view.width==873&&view.height==583,"one bar did not account for the other");
        require(620-view.height==37&&880-view.width==7,"final scrollbar range shortened");
    });
    check("Enlarging a window releases both unnecessary scrollbars",[]{
        // Previous client 873x613 plus the two existing 17px bars = 890x630.
        auto view=fitViewport(873+17,613+17,17,17,96);require(!view.horizontal&&!view.vertical&&view.width==890&&view.height==630,"old bars kept each other alive");
    });
    check("Scrollbar decisions include one-pixel limits at fractional DPI",[]{
        for(auto dpi:scales){int w=MulDiv(880,int(dpi),96),h=MulDiv(620,int(dpi),96),bar=MulDiv(17,int(dpi),96);
            auto fit=fitViewport(w,h,bar,bar,dpi);require(!fit.horizontal&&!fit.vertical,"exact fit gained scrollbars");
            auto shortWidth=fitViewport(w-1,h+bar,bar,bar,dpi);require(shortWidth.horizontal&&!shortWidth.vertical,"one-pixel horizontal overflow lost");
            auto shortHeight=fitViewport(w+bar,h-1,bar,bar,dpi);require(!shortHeight.horizontal&&shortHeight.vertical,"one-pixel vertical overflow lost");
        }
    });
    check("Large focused controls align their leading edge without oscillation",[]{
        auto vertical=revealOffset(0,200,174,446,620);require(vertical==174&&revealOffset(vertical,200,174,446,620)==vertical,"large focus jumps between edges");
        auto horizontal=revealOffset(0,720,24,856,880);require(horizontal==24&&revealOffset(horizontal,720,24,856,880)==horizontal,"filename left edge hidden");
    });
    check("Focused row is revealed without pulling the entire table into view",[]{
        require(revealOffset(0,220,398,424,620)==204,"focused row lower edge hidden");require(revealOffset(200,220,180,206,620)==180,"focused row upper edge hidden");require(revealOffset(200,220,240,266,620)==200,"visible row moved");
        require(revealOffset(136,720,24,273,880)==24,"main filename column hidden");
    });
    check("Wide password editing follows the caret instead of the full edit frame",[]{
        for(auto dpi:scales){int viewport=MulDiv(540,int(dpi),96),content=MulDiv(880,int(dpi),96),start=MulDiv(692,int(dpi),96),end=MulDiv(710,int(dpi),96);int offset=revealOffset(0,viewport,start,end,content);require(start>=offset&&end<=offset+viewport,"right-side password caret remains outside viewport");}
        require(revealOffset(170,540,32,48,880)==32,"moving caret home did not reveal the left side");
    });
    check("Reveal offsets stay within range after resizing or DPI changes",[]{
        for(int content:{620,880,1600})for(int viewport:{200,540,720,1920})for(int current:{0,137,5000}){auto offset=revealOffset(current,viewport,content-40,content-16,content);require(offset>=0&&offset<=std::max(0,content-viewport),"scroll range exceeded");require(offset==revealOffset(offset,viewport,content-40,content-16,content),"reveal is not stable");}
    });
    check("Columns fit the actual list client width including vertical scrollbar changes",[]{
        for(auto page:{Page::Queue,Page::Passwords,Page::Volumes})for(auto dpi:scales)for(int width:{832,1072,1552})for(int scrollbar:{0,17}){
            int client=MulDiv(width-scrollbar,int(dpi),96);auto columns=tableColumns(page,client,dpi);require(std::accumulate(columns.begin(),columns.end(),0)==client,"unnecessary horizontal scrollbar");require(columns.back()>MulDiv(120,int(dpi),96),"last column collapsed");
        }
    });
    check("Unchanged background refresh does not reset manually adjusted widths",[]{
        TableSizing state;auto initial=state.fit(Page::Queue,815,96);state.remember({350,84,130,62,303},96);
        require(!initial.empty()&&state.fit(Page::Queue,815,96).empty(),"unchanged refresh reapplies columns");
        require(state.fit(Page::Queue,798,96)==std::vector<int>({350,84,130,62,303}),"scrollbar change overwrote custom widths");
    });
    check("Custom column sizes scale from their original DPI without round-trip drift",[]{
        TableSizing state;state.fit(Page::Volumes,1000,144);const std::vector<int> widths={97,401,151,351};state.remember(widths,144);
        for(auto dpi:scales){auto scaled=state.fit(Page::Volumes,MulDiv(1000,int(dpi),144),dpi);if(!scaled.empty())for(size_t i=0;i<scaled.size();++i)require(scaled[i]==MulDiv(widths[i],int(dpi),144),"custom DPI scaling drifted");}
        require(state.fit(Page::Volumes,1000,144)==widths,"returning to original DPI changed custom widths");
    });
    check("Queue keyboard order follows search, list, actions and extraction settings",[]{
        const auto& order=navigationOrder();std::set<Id> unique(order.begin(),order.end());require(order.size()==unique.size(),"duplicate navigation entry");
        auto before=[&](Id a,Id b){return std::find(order.begin(),order.end(),a)<std::find(order.begin(),order.end(),b);};
        require(before(Create,QueueSearch)&&before(QueueSearch,List)&&before(List,TaskDetails)&&before(TaskDetails,Remove)&&before(OpenOutput,Directory)&&before(Directory,Nested)&&before(Deep,BatchPassword)&&before(BatchPassword,Start),"queue tab order crosses groups");
    });
    check("Password and volume keyboard order follows each page workflow",[]{
        const auto& order=navigationOrder();auto before=[&](Id a,Id b){return std::find(order.begin(),order.end(),a)<std::find(order.begin(),order.end(),b);};
        require(before(PasswordSearch,PasswordList)&&before(PasswordList,NewPassword)&&before(NewPassword,SavePasswords)&&before(SavePasswords,ImportPasswords),"password tab order crosses groups");
        require(before(VolumeList,VolumeAdd)&&before(VolumeAdd,VolumeSort)&&before(VolumeSort,VolumeRemove)&&before(VolumeDown,CancelVolumes)&&before(CancelVolumes,AcceptVolumes),"volume tab order crosses groups");
    });
    check("Persistent notice and live phase fit separate footer rows at all DPI scales",[]{
        auto dc=CreateCompatibleDC(nullptr);require(dc!=nullptr,"cannot create memory DC");bool fits=true;
        for(auto dpi:scales){auto font=makeUiFont(dpi,12);auto previous=SelectObject(dc,font);SIZE text{};GetTextExtentPoint32W(dc,L"正在停止当前操作",8,&text);auto layout=makeLayout(880,620);auto phase=pixelBox(layout.at(QueuePhase),dpi),notice=pixelBox(layout.at(Status),dpi);fits&=phase.bottom()<=notice.y&&text.cy+MulDiv(2,int(dpi),96)<=phase.height&&text.cy+MulDiv(2,int(dpi),96)<=notice.height;SelectObject(dc,previous);DeleteObject(font);}
        DeleteDC(dc);require(fits,"footer text rows overlap or clip");
    });
    check("Real GDI font metrics fit long Chinese labels at all eight DPI scales",[]{
        struct Label{Id id;const wchar_t* text;int padding;};
        const Label labels[]={{AddFolder,L"添加文件夹…",20},{Create,L"新建压缩包 ▾",24},{NextStep,L"填写本批密码",24},{NextStep,L"补充分卷…",24},{NextStep,L"重新定位源…",24},{OpenOutput,L"查看当前输出",24},{OpenOutput,L"查看保留结果",24},{Start,L"开始全部 99999 项",24},{Nested,L"自动解压嵌套包",32},{Recycle,L"全部成功后，原包移入回收站",32},{Deep,L"必要时深度识别",32},{ShowPasswords,L"显示已保存密码",32},{ConfigureTask,L"任务设置…",24},{HoldTask,L"继续暂缓项",24},{TaskApply,L"保存并待开始",24},{VolumeSort,L"按文件名排序",24},{UsePassword,L"所选密码用于本批",24},{SavePasswords,L"保存新增密码",24},{ImportPasswords,L"导入密码文本…",24}};
        auto dc=CreateCompatibleDC(nullptr);require(dc!=nullptr,"cannot create memory DC");
        bool fits=true;
        for(auto dpi:scales){auto font=makeUiFont(dpi);auto previous=SelectObject(dc,font);auto layout=makeLayout(880,620);
            for(const auto& label:labels){SIZE size{};GetTextExtentPoint32W(dc,label.text,int(wcslen(label.text)),&size);auto box=pixelBox(layout.at(label.id),dpi);if(size.cx+MulDiv(label.padding,int(dpi),96)>box.width||size.cy+MulDiv(6,int(dpi),96)>box.height){std::cout<<"label id "<<int(label.id)<<" dpi "<<dpi<<" measured "<<size.cx<<"x"<<size.cy<<" box "<<box.width<<"x"<<box.height<<'\n';fits=false;}}
            SelectObject(dc,previous);DeleteObject(font);
        }
        DeleteDC(dc);require(fits,"native font clips a primary control label");
    });
    check("Every shared state and problem filter label fits the native combo at eight DPI scales",[]{auto dc=CreateCompatibleDC(nullptr);require(dc!=nullptr,"cannot create memory DC");bool fits=true;for(auto dpi:scales){auto font=makeUiFont(dpi);auto old=SelectObject(dc,font);auto box=pixelBox(makeLayout(880,620).at(QueueFilter),dpi);for(auto label:TaskFilterNames){SIZE size{};GetTextExtentPoint32W(dc,label,int(wcslen(label)),&size);fits&=size.cx+MulDiv(40,int(dpi),96)<=box.width&&size.cy+MulDiv(6,int(dpi),96)<=box.height;}SelectObject(dc,old);DeleteObject(font);}DeleteDC(dc);require(fits,"native problem filter label clipped");});
    check("Typed stage counters and elapsed labels fit the native footer at eight DPI scales",[]{auto dc=CreateCompatibleDC(nullptr);require(dc!=nullptr,"cannot create memory DC");Task task;task.state=State::Running;task.started=std::chrono::steady_clock::time_point(std::chrono::seconds(10));auto now=task.started+std::chrono::seconds(125);bool fits=true;for(auto dpi:scales){auto font=makeUiFont(dpi,12);auto old=SelectObject(dc,font);auto box=pixelBox(makeLayout(880,620).at(QueuePhase),dpi);for(auto unit:{lu::ProgressUnit::Bytes,lu::ProgressUnit::Files,lu::ProgressUnit::Candidates}){task.phase=L"当前阶段";task.progressUnit=unit;task.progressDone=unit==lu::ProgressUnit::Bytes?1048576:120;task.progressTotal=unit==lu::ProgressUnit::Bytes?2097152:0;auto text=taskProgressCaption(task,now);SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&size);fits&=size.cx<=box.width&&size.cy+MulDiv(2,int(dpi),96)<=box.height;}SelectObject(dc,old);DeleteObject(font);}DeleteDC(dc);require(fits,"stage labels clip native footer");});
    std::cout<<passed<<'/'<<total<<" layout and DPI checks passed; shared production geometry and GDI metrics, no windows created.\n";return passed==total?0:1;
}
