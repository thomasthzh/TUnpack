#include "workspace.hpp"
#include "passwords.hpp"
#include <wincrypt.h>
#include <fstream>
#include <iostream>
#include <chrono>

using namespace lu;using namespace lu::ui;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::string bytes(const fs::path& file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;auto folder=uniquePath(fs::path(argv[1])/L"workspace");fs::create_directories(folder);
    unsigned passed=0,total=0;auto check=[&](const char* name,const auto& test){++total;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    check("Absent workspace uses defaults without writing",[&]{WorkspaceStore store(folder/L"absent.dat");require(!store.load()&&!fs::exists(store.path()),"missing workspace changed disk");});
    check("Runtime progress units, counters and steady clock never enter workspace records",[&]{Queue q;Task task;task.files={folder/L"runtime.mp4"};auto id=q.add(task);q.takeNext();q.progress(id,L"scan",1,0,ProgressUnit::Files);Workspace value;value.tasks=q.snapshot();WorkspaceStore store(folder/L"runtime.dat");store.save(value);auto first=bytes(store.path());q.progress(id,L"extract",1048576,2097152,ProgressUnit::Bytes);value.tasks=q.snapshot();store.save(value);auto loaded=store.load()->tasks[0];require(bytes(store.path())==first&&loaded.progressUnit==ProgressUnit::None&&!loaded.progressDone&&loaded.started==std::chrono::steady_clock::time_point{},"transient telemetry changed saved format");});
    check("Old filter numbers remain stable and every appended reason filter roundtrips",[&]{
        static_assert(unsigned(TaskFilter::All)==0&&unsigned(TaskFilter::Waiting)==1&&unsigned(TaskFilter::Running)==2&&unsigned(TaskFilter::Attention)==3&&unsigned(TaskFilter::Done)==4&&unsigned(TaskFilter::Cancelled)==5&&unsigned(TaskFilter::Paused)==6&&unsigned(TaskFilter::NeedsPassword)==7);
        for(unsigned i=0;i<TaskFilterNames.size();++i){Workspace value;value.view={L"result-path",TaskFilter(i)};WorkspaceStore store(folder/(L"filter-"+std::to_wstring(i)+L".dat"));store.save(value);auto again=store.load().value();require(again.view.filter==value.view.filter&&again.view.query==value.view.query,"filter/query did not restore");}
    });
    check("Workspace roundtrip keeps task policies, results, draft order and stable identities",[&]{
        Workspace original;original.defaults={folder/L"输出",false,false,true};Task t;t.id=73;t.files={folder/L"卷.003.docx",folder/L"卷.001"};t.manualVolumes=true;t.paused=true;t.settings=original.defaults;t.result.output=folder/L"result";t.result.failedFile=folder/L"result/inner.7z";t.result.status=L"missing_volume";t.result.note=L"public repair note";original.tasks={t};original.volumes.open(t);original.volumes.files.push_back(folder/L"added.002");original.editor.open(original.tasks,original.defaults);original.editor.custom=true;original.editor.value.deep=false;original.selection={{73},73,73};original.columns[0].remember({350,84,130,62,303},144);original.page=Page::Volumes;
        WorkspaceStore store(folder/L"roundtrip.dat");store.save(original);auto restored=store.load().value();require(restored.tasks[0].id==73&&restored.tasks[0].files==t.files&&restored.tasks[0].settings==t.settings&&restored.tasks[0].paused,"task policy or identity lost");require(restored.tasks[0].result.failedFile==t.result.failedFile&&restored.tasks[0].result.output==t.result.output,"repair context lost");require(restored.volumes.files==original.volumes.files&&restored.editor.custom&&!restored.editor.value.deep&&restored.selection.focus==73&&restored.columns[0].customDpi==144,"draft or view lost");
    });
    check("Running task becomes interrupted without losing partial output or enabling cleanup",[&]{
        Task task;task.id=11;task.files={folder/L"archive.zip"};task.state=State::Running;task.result.output=folder/L"partial";task.result.ok=true;Queue q;q.restore({task});auto restored=q.get(11).value();require(restored.state==State::Cancelled&&!restored.result.ok&&restored.result.status==L"interrupted"&&restored.result.output==task.result.output,"interrupted recovery contract lost");require(!q.takeNext()&&q.counts().cancelled==1,"recovered running task auto-executed");require(q.retry({11})==1&&q.takeNext()->id==11,"recovered task cannot retry");
    });
    check("Recovery rebuilds paused reader protection and future task IDs",[&]{
        Task a;a.id=99;a.files={folder/L"shared.zip"};Task b=a;b.id=151;b.action=Action::Inspect;b.paused=true;Queue q;q.restore({a,b});auto next=q.takeNext();require(next&&next->id==99&&q.retainSources(99,a.files),"restored paused reader missing");Task added;added.files={folder/L"new.zip"};require(q.add(added)>151,"restored IDs reused");
    });
    check("Recovered paused work resumes with a runnable caption",[&]{
        Task task;task.id=1;task.files={folder/L"paused.zip"};task.paused=true;Queue q;q.restore({task});require(taskStatus(q.get(1).value())==L"已暂缓"&&!q.takeNext(),"paused task did not remain paused");q.setPaused({1},false);require(taskStatus(q.get(1).value())==L"等待开始"&&q.takeNext()->id==1,"resume caption or scheduling disagrees");
    });
    check("Recovered merged identities are never reused by later tasks",[&]{
        Task task;task.id=1;task.mergedFrom={10};task.files={folder/L"merged.zip"};Queue q;q.restore({task});Task added;added.files={folder/L"later.zip"};auto id=q.add(added);require(id>10&&q.reorder({id},QueueMove::Up),"merged identity reused");auto selected=restoreSelection(q.snapshot(),{{id},id,id});require(selected.ids==std::vector<uint64_t>{id}&&selected.focus==id&&selected.anchor==id,"new task selection mapped to old merge");
    });
    check("Reordered queue restores policies, pause and independent selection in its full view",[&]{
        Queue q;Task a;a.files={folder/L"a.mp4"};auto first=q.add(a);a.files={folder/L"b.docx"};a.paused=true;a.settings=TaskSettings{folder/L"chosen",false,false,true};auto second=q.add(a);a.files={folder/L"c.7z"};a.paused=false;auto third=q.add(a);q.reorder({second,third},QueueMove::First);Workspace value;value.tasks=q.snapshot();value.selection={{second,third},third,second};value.view={L"",TaskFilter::All};WorkspaceStore store(folder/L"reordered.dat");store.save(value);auto again=store.load().value();Queue restored;restored.restore(std::move(again.tasks));auto tasks=restored.snapshot();auto selection=restoreSelection(tasks,again.selection);require(tasks[0].id==second&&tasks[1].id==third&&tasks[2].id==first&&tasks[0].paused&&tasks[0].settings==a.settings,"reordered task definition lost");require(selection.ids==value.selection.ids&&selection.focus==third&&selection.anchor==second&&again.view.filter==TaskFilter::All&&restored.takeNext()->id==third,"restored view/selection/schedule changed");
    });
    check("Detached volume draft preserves files and settings as a new group",[&]{
        Workspace value;value.volumes.active=true;value.volumes.taskId=999;value.volumes.originId=999;value.volumes.files={folder/L"a",folder/L"b"};value.volumes.settings=TaskSettings{folder/L"target",true,false,true};WorkspaceStore store(folder/L"orphan.dat");store.save(value);auto restored=store.load().value();require(!restored.volumes.taskId&&restored.volumes.files==value.volumes.files&&restored.volumes.settings==value.volumes.settings,"orphan draft lost");
    });
    check("Session schema drops batch password and callbacks by construction",[&]{
        Options batch;batch.password=L"public-secret-not-in-workspace";batch.recycle=false;batch.output=folder/L"out";Workspace value;value.defaults=settingsFrom(batch);WorkspaceStore store(folder/L"no-password.dat");store.save(value);require(bytes(store.path()).find("public-secret-not-in-workspace")==std::string::npos,"batch password persisted");
    });
    check("Corrupt workspace is rejected and preserved before replacement",[&]{
        WorkspaceStore store(folder/L"corrupt.dat");{std::ofstream out(store.path(),std::ios::binary);out<<"bad-data";}bool rejected=false;try{store.load();}catch(const std::exception&){rejected=true;}require(rejected&&bytes(store.path())=="bad-data","bad data silently replaced");auto backup=store.preserveUnreadable();store.save(Workspace{});require(bytes(backup)=="bad-data"&&store.load().has_value(),"unreadable record was not preserved");
    });
    check("Truncated workspace never partially restores a queue",[&]{
        WorkspaceStore store(folder/L"truncated.dat");store.save(Workspace{});auto data=bytes(store.path());data.resize(data.size()-3);{std::ofstream out(store.path(),std::ios::binary|std::ios::trunc);out.write(data.data(),data.size());}bool rejected=false;try{store.load();}catch(const std::exception&){rejected=true;}require(rejected&&bytes(store.path())==data,"truncated data accepted or rewritten");
    });
    check("Invalid UTF-8 in a framed record is rejected without replacing the record",[&]{
        Workspace value;value.view.query=L"unique-public-query";WorkspaceStore store(folder/L"invalid-utf8.dat");store.save(value);auto data=bytes(store.path());auto at=data.find("unique-public-query");require(at!=std::string::npos,"fixture query absent");data[at]=char(0xff);{std::ofstream out(store.path(),std::ios::binary|std::ios::trunc);out.write(data.data(),data.size());}bool rejected=false;try{store.load();}catch(const std::exception&){rejected=true;}require(rejected&&bytes(store.path())==data,"invalid text silently repaired or overwritten");
    });
    check("Background writer keeps the newest submitted workspace",[&]{
        auto file=folder/L"coalesced.dat";{WorkspaceWriter writer(file);for(unsigned i=0;i<30;++i){Workspace value;value.width=1120+int(i);writer.submit(std::move(value));}require(writer.flush()&&writer.error().empty(),"background commit failed");}require(WorkspaceStore(file).load()->width==1149,"latest workspace lost");
    });
    check("Failed atomic save keeps old record and a later retry succeeds",[&]{
        auto file=folder/L"locked.dat";WorkspaceStore store(file);Workspace old;old.width=1000;store.save(old);auto lock=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);require(lock!=INVALID_HANDLE_VALUE,"fixture lock failed");WorkspaceWriter writer(file);Workspace changed;changed.width=1300;writer.submit(changed);bool failed=!writer.flush();CloseHandle(lock);require(failed&&store.load()->width==1000,"failed commit damaged old workspace");writer.submit(changed);require(writer.flush()&&store.load()->width==1300,"retry did not commit");
    });
    check("Original DPAPI password record format still loads and appends byte-compatibly",[&]{
        auto put=[](std::string& out,uint64_t n){for(unsigned i=0;i<8;++i)out.push_back(char(n>>(8*i)));};std::string legacy;put(legacy,1);put(legacy,10);legacy+="public-old";put(legacy,7);put(legacy,123);put(legacy,0);DATA_BLOB source{DWORD(legacy.size()),reinterpret_cast<BYTE*>(legacy.data())},encrypted{};require(CryptProtectData(&source,L"legacy",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted),"fixture encryption failed");auto file=folder/L"legacy.dpapi";{std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<char*>(encrypted.pbData),encrypted.cbData);}LocalFree(encrypted.pbData);PasswordBook book(file);require(book.count()==1&&book.snapshot()[0].value==L"public-old"&&book.snapshot()[0].uses==7,"legacy password not read");book.add({L"public-new"});auto saved=bytes(file);DATA_BLOB protectedData{DWORD(saved.size()),reinterpret_cast<BYTE*>(saved.data())},clear{};require(CryptUnprotectData(&protectedData,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&clear),"cannot verify saved fixture");std::string raw(reinterpret_cast<char*>(clear.pbData),clear.cbData);LocalFree(clear.pbData);require(static_cast<unsigned char>(raw[0])==2&&raw.substr(8,legacy.size()-8)==legacy.substr(8),"password record format changed");
    });
    check("5000-task session saves, reloads and rebuilds indexes without source reads",[&]{
        Workspace value;value.defaults.recycle=false;for(unsigned i=1;i<=5000;++i){Task task;task.id=i;task.files={folder/(std::to_wstring(i)+L".mp4")};value.tasks.push_back(std::move(task));}WorkspaceStore store(folder/L"bulk.dat");auto start=std::chrono::steady_clock::now();store.save(value);auto restored=store.load().value();Queue q;q.restore(std::move(restored.tasks));require(q.counts().waiting==5000&&q.takeNext()->id==1,"bulk workspace lost tasks");std::cout<<"Workspace 5000 save/load/reindex ms: "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<'\n';
    });
    std::cout<<passed<<'/'<<total<<" workspace checks passed; generated records only, no windows or source cleanup.\n";return passed==total?0:1;
}
