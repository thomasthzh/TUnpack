#include "ui_model.hpp"
#include <algorithm>

namespace lu::ui {
HFONT makeUiFont(unsigned dpi,int size,int weight){
    return CreateFontW(-MulDiv(size,int(dpi),96),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
}
Layout makeLayout(int width,int height){
    width=std::max(880,width);height=std::max(620,height);bool drop=height>=760;
    Layout l{width,height,drop?280:212,height-300,height-292,76,drop,height-186,height-178,height-126,height-82};
    auto put=[&](Id id,int x,int y,int w,int h){l.boxes[size_t(id-Add)]={x,y,w,h};};
    int w=width,h=height;
    put(QueueTab,244,27,114,34);put(PasswordTab,366,27,104,34);
    put(Add,24,94,116,34);put(AddFolder,148,94,116,34);put(Volumes,272,94,116,34);put(Tools,396,94,112,34);put(Create,w-184,94,160,34);put(PasteFiles,516,94,112,34);
    put(QueueSearch,24,144,w-376,30);put(SearchClear,w-344,144,72,30);put(QueueFilter,w-264,144,240,30);
    put(DropZone,24,184,w-48,56);put(List,24,l.listTop,w-48,l.listBottom-l.listTop);put(TaskDetails,24,l.detailTop,w-48,l.detailHeight);
    put(Remove,24,h-214,94,30);put(Up,126,h-214,54,30);put(Down,188,h-214,54,30);put(Retry,250,h-214,94,30);put(HoldTask,352,h-214,108,30);put(NextStep,468,h-214,140,30);put(ConfigureTask,616,h-214,110,30);put(OpenOutput,w-144,h-214,120,30);
    put(DirectoryLabel,24,h-176,60,30);put(Directory,92,h-176,w-332,30);put(Choose,w-228,h-176,96,30);put(Reset,w-124,h-176,100,30);
    put(Nested,24,h-138,174,26);put(Recycle,206,h-138,280,26);put(Deep,w-216,h-138,192,26);
    put(BatchLabel,24,h-100,124,28);put(BatchPassword,156,h-100,220,28);
    put(QueuePhase,24,h-64,w-352,20);put(Status,24,h-44,w-352,20);put(ProgressBar,24,h-16,w-352,4);put(Cancel,w-300,h-58,104,38);put(Start,w-184,h-58,160,38);
    put(PasswordSearch,24,142,w-232,30);put(ShowPasswords,w-192,142,168,30);put(PasswordList,24,188,w-48,h-460);put(PasswordLabel,24,h-254,w-232,24);put(UsePassword,w-192,h-258,168,30);put(NewPassword,24,h-226,w-48,96);
    put(SavePasswords,24,h-114,160,36);put(ImportPasswords,196,h-114,176,36);put(PasswordFeedback,388,h-118,w-412,46);
    put(VolumeList,24,174,w-48,l.volumeBottom-174);put(VolumeFeedback,24,l.volumeFeedback,w-48,44);
    put(VolumeAdd,24,l.volumeActionsTop,108,34);put(VolumePaste,140,l.volumeActionsTop,116,34);put(VolumeSort,264,l.volumeActionsTop,140,34);put(VolumeRemove,24,l.volumeActionsBottom,94,36);put(VolumeUp,126,l.volumeActionsBottom,66,36);put(VolumeDown,200,l.volumeActionsBottom,66,36);put(CancelVolumes,w-296,l.volumeActionsBottom,116,36);put(AcceptVolumes,w-168,l.volumeActionsBottom,144,36);
    put(TaskCustom,24,192,w-48,28);put(TaskDirectoryLabel,24,238,60,30);put(TaskDirectory,92,238,w-332,30);put(TaskChoose,w-228,238,96,30);put(TaskSourceDirectory,w-124,238,100,30);
    put(TaskNested,24,292,500,28);put(TaskRecycle,24,336,w-48,28);put(TaskDeep,24,380,500,28);put(TaskFeedback,24,432,w-48,64);
    put(TaskBack,w-436,h-72,128,36);put(TaskSave,w-296,h-72,112,36);put(TaskApply,w-168,h-72,144,36);
    return l;
}
Box pixelBox(const Box& box,unsigned dpi,int scrollX,int scrollY){
    int left=MulDiv(box.x-scrollX,int(dpi),96),top=MulDiv(box.y-scrollY,int(dpi),96);
    return {left,top,MulDiv(box.right()-scrollX,int(dpi),96)-left,MulDiv(box.bottom()-scrollY,int(dpi),96)-top};
}
Viewport fitViewport(int width,int height,int barWidth,int barHeight,unsigned dpi){
    int minimumWidth=MulDiv(880,int(dpi),96),minimumHeight=MulDiv(620,int(dpi),96);
    bool horizontal=false,vertical=false;
    // Start without bars, so old bars cannot keep each other alive after enlargement.
    for(int pass=0;pass<3;++pass){
        bool nextH=width-(vertical?barWidth:0)<minimumWidth;
        bool nextV=height-(horizontal?barHeight:0)<minimumHeight;
        if(nextH==horizontal&&nextV==vertical)break;
        horizontal=nextH;vertical=nextV;
    }
    return {width-(vertical?barWidth:0),height-(horizontal?barHeight:0),horizontal,vertical};
}
int revealOffset(int current,int viewport,int start,int end,int content){
    int next=current;
    if(start<current||end-start>viewport)next=start;
    else if(end>current+viewport)next=end-viewport;
    return std::clamp(next,0,std::max(0,content-viewport));
}
std::vector<int> tableColumns(Page page,int width,unsigned dpi){
    auto px=[&](int v){return MulDiv(v,int(dpi),96);};
    std::vector<int> columns;
    if(page==Page::Queue)columns={width*30/100,px(84),px(130),px(62),0};
    else if(page==Page::Passwords)columns={px(240),px(90),0};
    else columns={px(64),width*38/100,px(100),0};
    int used=0;for(size_t i=0;i+1<columns.size();++i)used+=columns[i];
    columns.back()=std::max(0,width-used);return columns;
}
const std::vector<Id>& navigationOrder(){
    static const std::vector<Id> order={QueueTab,PasswordTab,Add,AddFolder,Volumes,Tools,PasteFiles,Create,
        QueueSearch,SearchClear,QueueFilter,DropZone,List,TaskDetails,Remove,Up,Down,Retry,HoldTask,NextStep,ConfigureTask,OpenOutput,
        Directory,Choose,Reset,Nested,Recycle,Deep,BatchPassword,Cancel,Start,
        PasswordSearch,ShowPasswords,PasswordList,UsePassword,NewPassword,SavePasswords,ImportPasswords,PasswordFeedback,
        VolumeList,VolumeAdd,VolumePaste,VolumeSort,VolumeRemove,VolumeUp,VolumeDown,CancelVolumes,AcceptVolumes,
        TaskCustom,TaskDirectory,TaskChoose,TaskSourceDirectory,TaskNested,TaskRecycle,TaskDeep,TaskBack,TaskSave,TaskApply};
    return order;
}
std::vector<int> TableSizing::fit(Page page,int width,unsigned scale){
    if(clientWidth==width&&dpi==scale)return {};
    clientWidth=width;dpi=scale;
    if(custom.empty())return tableColumns(page,width,scale);
    auto result=custom;for(auto& value:result)value=MulDiv(value,int(scale),int(customDpi));return result;
}
}
