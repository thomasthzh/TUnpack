#include "queue_runner.hpp"
#include "workspace.hpp"
#include "shell_open.hpp"
#include "password_probe.hpp"
#include <bit7z/bitfilecompressor.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace lu;
using namespace lu::ui;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void write(const fs::path& file,const std::string& data){fs::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);out.write(data.data(),data.size());require(bool(out),"fixture write failed");}
static std::string read(const fs::path& file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static bool hasPayload(const fs::path& output,const std::string& payload){
    if(output.empty()||!fs::exists(output))return false;
    for(const auto& file:fs::recursive_directory_iterator(output))if(file.is_regular_file()&&file.path().filename()==L"payload.bin"&&read(file.path())==payload)return true;
    return false;
}
static Task task(const fs::path& file,Action action=Action::Extract){Task t;t.files={file};t.action=action;return t;}
struct Fixture {
    fs::path folder,payloadFile;std::string payload;
    PasswordBook book;Engine engine;Options options;
    explicit Fixture(const fs::path& root,const wchar_t* name):folder(uniquePath(root/name)),payloadFile(folder/L"payload.bin"),book(folder/L"isolated.dpapi"),engine(book){
        for(unsigned i=0,x=1;i<16384;++i){x=x*1664525+1013904223;payload.push_back(char(x>>24));}
        write(payloadFile,payload);options.output=folder/L"outputs";options.recycle=false;
    }
    fs::path archive(const std::vector<fs::path>& files,const wchar_t* name,const std::wstring& password={}){
        auto result=engine.compress(files,folder/name,password);require(result.ok,"fixture compression failed");return result.output;
    }
    std::vector<fs::path> pieces(const fs::path& source,const wchar_t* base=L"set.7z"){
        auto bytes=read(source);size_t part=(bytes.size()+2)/3;std::vector<fs::path> paths;
        for(size_t i=0;i<3;++i){wchar_t suffix[8];swprintf(suffix,8,L".%03u",unsigned(i+1));auto path=folder/(std::wstring(base)+suffix);write(path,bytes.substr(i*part,std::min(part,bytes.size()-i*part)));paths.push_back(path);}
        return paths;
    }
    bool hasPayload(const fs::path& output) const {
        if(output.empty())return false;
        auto relative=fs::absolute(output).lexically_normal().lexically_relative(fs::absolute(options.output).lexically_normal());
        if(relative.empty()||relative==L"."||relative.is_absolute()||*relative.begin()==L"..")return false;
        return ::hasPayload(output,payload);
    }
    void run(Queue& q,const std::function<void(uint64_t)>& notify=[](uint64_t){}){require(!options.recycle,"test recycling must stay disabled");for(const auto& item:q.snapshot())require(!item.settings||!item.settings->recycle,"test override enabled recycling");runQueue(q,engine,options,[]{return false;},notify);}
};
int wmain(int argc,wchar_t** argv){
    if(argc<3)return 2;auto root=uniquePath(fs::path(argv[1])/L"full-flows");fs::create_directories(root);const fs::path fixtures=argv[2];
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;struct Uninitialize{~Uninitialize(){CoUninitialize();}} uninitialize;
    unsigned passed=0,total=0;auto check=[&](const char* name,const auto& test){++total;try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    if(argc>3){
        check("Native ZIP folder and mixed individual volumes form one complete queue task",[&]{
            Fixture f(root,L"native-zip-queue");auto inbox=fs::absolute(argv[3]);
            std::vector<fs::path> parts;for(auto name:{L"sample.z01",L"sample.z02",L"sample.z03",L"sample.zip"})parts.push_back(inbox/name);
            Queue q;auto scan=q.add(task(inbox,Action::Scan));std::vector<uint64_t> added;
            for(auto i:{1,3,0,2})added.push_back(q.add(task(parts[size_t(i)])));
            f.run(q);auto tasks=q.snapshot();
            require(tasks.size()==2&&q.get(scan)->state==State::Done,"folder and member drops were not combined");
            auto extracted=std::find_if(tasks.begin(),tasks.end(),[](const auto& t){return t.action==Action::Extract;});
            require(extracted!=tasks.end()&&extracted->state==State::Done&&extracted->files==parts&&extracted->result.sources==parts,"complete physical ZIP source set lost");
            require(std::all_of(added.begin(),added.end(),[&](auto id){return id&&(id==extracted->id||std::find(extracted->mergedFrom.begin(),extracted->mergedFrom.end(),id)!=extracted->mergedFrom.end());}),"pending member task identities were not merged");
            require(read(extracted->result.output/L"data.bin")==read(inbox.parent_path()/L"expected-data.bin"),"queue output bytes differ");
            require(extracted->result.output.parent_path()==f.options.output,"queue output parent changed");
            for(const auto& p:parts)require(fs::exists(p),"queue removed a generated source");
        });
        std::cout<<passed<<'/'<<total<<" generated native ZIP queue flow passed; no windows or recycling.\n";return passed==total?0:1;
    }
    check("One wrong body password -> import -> retry -> verified output",[&]{
        Fixture f(root,L"body-password");auto archive=f.folder/L"body-only.7z";
        bit7z::Bit7zLibrary library((executableDir()/L"7z.dll").wstring());bit7z::BitFileCompressor compressor(library,bit7z::BitFormat::SevenZip);
        compressor.setPassword(L"public-body-right",false);compressor.compress(std::vector<std::wstring>{f.payloadFile.wstring()},archive.wstring());
        f.book.add({L"public-body-wrong"});Queue q;auto id=q.add(task(archive));f.run(q);auto failed=q.get(id).value();
        require(failed.state==State::Attention&&failed.result.status==L"password_required"&&!nextStep(failed).empty(),"missing password repair action");
        auto text=f.folder/L"public-passwords.txt";write(text,"public-body-right\n");require(f.book.importTexts({text}).totals.added==1,"import failed");
        require(q.retry({id})==1,"retry failed");f.run(q);require(q.get(id)->state==State::Done&&f.hasPayload(q.get(id)->result.output)&&fs::exists(archive),"import retry did not decode payload");
    });
    check("Nested password failure retains parent context and retries with saved password",[&]{
        Fixture f(root,L"nested-password");auto inner=f.archive({f.payloadFile},L"inner.7z",L"public-inner-password");auto outer=f.archive({inner},L"outer.zip");Queue q;auto id=q.add(task(outer));f.run(q);auto failed=q.get(id).value();
        require(failed.result.status==L"password_required"&&failed.result.type==L"zip"&&!failed.result.output.empty()&&failed.result.failedFile.filename()==L"inner.7z","nested password context lost");
        require(taskDetails(failed).find(failed.result.failedFile.wstring())!=std::wstring::npos,"inner full path missing");
        ShellLocation source(failed.files.front(),true),problem(failed.result.failedFile,true),output(failed.result.output);require(source.path()==outer&&problem.path()==failed.result.failedFile&&source.path()!=problem.path()&&problem.action()==LocationAction::SelectItem&&output.action()==LocationAction::OpenDirectory,"nested navigation lost distinct source, inner problem or root output");
        f.book.add({L"public-inner-password"});q.retry({id});f.run(q);require(q.get(id)->state==State::Done&&q.get(id)->result.archives==2&&f.hasPayload(q.get(id)->result.output)&&fs::exists(outer),"nested retry failed");
    });
    check("Renamed manual volumes -> correct order -> retry same task",[&]{
        Fixture f(root,L"manual-volumes");auto full=f.archive({f.payloadFile},L"whole.7z");auto parts=f.pieces(full);std::vector<fs::path> renamed;
        for(size_t i=0;i<parts.size();++i){auto path=f.folder/(L"video"+std::to_wstring(i)+L".docx");write(path,read(parts[i]));renamed.push_back(path);}
        Queue q;auto t=task(renamed[2]);t.manualVolumes=true;t.files={renamed[2],renamed[0]};auto id=q.add(t);f.run(q);require(q.get(id)->state==State::Attention,"bad order accepted");
        require(q.setVolumes(id,renamed),"cannot save corrected group");f.run(q);require(q.get(id)->state==State::Done&&q.get(id)->manualVolumes&&q.get(id)->files==renamed&&f.hasPayload(q.get(id)->result.output),"manual correction failed");
    });
    check("Automatic incomplete group -> newly added renamed tail -> retry within same batch",[&]{
        Fixture f(root,L"auto-refresh");auto full=f.archive({f.payloadFile},L"whole.7z");auto bytes=read(full);size_t part=(bytes.size()+2)/3;
        auto first=f.folder/L"set.7z.001",second=f.folder/L"set.7z.002",tail=f.folder/L"set.7z.003.docx";write(first,bytes.substr(0,part));write(second,bytes.substr(part,part));
        Queue q;auto id=q.add(task(first));bool repaired=false;
        f.run(q,[&](uint64_t changed){auto item=q.get(changed);if(changed==id&&item->state==State::Attention&&!repaired){
            require(item->result.status==L"missing_volume"&&item->files.size()==2,"incomplete group was not retained");repaired=true;write(tail,bytes.substr(2*part));require(q.retry({id})==1,"same batch retry failed");
        }});
        require(repaired&&q.get(id)->state==State::Done&&q.get(id)->files.size()==3&&f.hasPayload(q.get(id)->result.output),"retry used stale group/directory snapshot");
    });
    check("Nested missing volume -> separate inner repair task, parent preserved",[&]{
        Fixture f(root,L"nested-volumes");auto full=f.archive({f.payloadFile},L"whole.7z");auto parts=f.pieces(full);auto outer=f.archive({parts[0]},L"outer.zip");Queue q;auto id=q.add(task(outer));f.run(q);auto failed=q.get(id).value();
        require(failed.state==State::Attention&&failed.result.status==L"missing_volume"&&failed.result.failedFile.filename()==parts[0].filename(),"inner missing-volume status lost");
        VolumeEditor editor;editor.open(failed,settingsFrom(f.options));require(!editor.taskId&&editor.originId==id&&editor.files==std::vector<fs::path>{failed.result.failedFile},"repair points to outer archive");
        auto second=failed.result.failedFile.parent_path()/parts[1].filename(),third=failed.result.failedFile.parent_path()/parts[2].filename();write(second,read(parts[1]));write(third,read(parts[2]));appendVolumes(editor.files,{second,third});editor.open(failed,settingsFrom(f.options));require(editor.files.size()==3,"inner draft lost");
        Task repair;repair.files=editor.files;repair.manualVolumes=true;repair.settings=editor.settings;auto repaired=q.add(repair);f.run(q);
        require(q.get(repaired)->state==State::Done&&f.hasPayload(q.get(repaired)->result.output)&&q.get(repaired)->result.output.parent_path()==failed.result.failedFile.parent_path(),"inner repair failed or escaped original layer");
        require(q.get(id)->state==State::Attention&&q.get(id)->files==failed.files&&q.get(id)->result.output==failed.result.output&&fs::exists(outer),"parent record was replaced");
    });
    check("Cancel active task -> remaining work waits -> retry and resume",[&]{
        Fixture f(root,L"cancel-resume");auto archive=f.archive({f.payloadFile},L"one.zip");Queue q;auto a=q.add(task(archive));auto b=q.add(task(archive,Action::Inspect));bool cancelled=false;
        runQueue(q,f.engine,f.options,[&]{return cancelled;},[&](uint64_t id){auto t=q.get(id);if(t->state==State::Running)cancelled=true;});
        require(q.get(a)->state==State::Cancelled&&q.get(b)->state==State::Waiting&&fs::exists(archive),"cancel lost remaining/source");
        q.retry({a});f.run(q);require(q.get(a)->state==State::Done&&q.get(b)->state==State::Done&&f.hasPayload(q.get(a)->result.output),"resume failed");
    });
    check("Queue accepts new work during processing and consumes it in order",[&]{
        Fixture f(root,L"dynamic-add");auto archive=f.archive({f.payloadFile},L"one.zip");Queue q;auto a=q.add(task(archive,Action::Inspect));uint64_t b=0;
        f.run(q,[&](uint64_t id){if(id==a&&q.get(a)->state==State::Running&&!b){auto create=task(f.payloadFile,Action::Create7z);create.target=f.folder/L"added.7z";b=q.add(create);}});
        require(b&&q.get(a)->state==State::Done&&q.get(b)->state==State::Done&&fs::exists(q.get(b)->result.output),"newly admitted task was not processed");
    });
    check("Scan discovers disguised archives and executes only unique groups",[&]{
        Fixture f(root,L"scan");auto archive=f.archive({f.payloadFile},L"one.zip");auto scan=f.folder/L"inbox";write(scan/L"video.mp4",read(archive));write(scan/L"plain.txt","ordinary document");Queue q;auto id=q.add(task(scan,Action::Scan));f.run(q);
        auto tasks=q.snapshot();require(tasks.size()==2&&q.get(id)->result.archives==1&&q.counts().done==2&&f.hasPayload(tasks[1].result.output),"scan did not feed extraction queue");
    });
    check("Create -> inspect -> restore -> extract share the production dispatcher",[&]{
        Fixture f(root,L"mixed-actions");Queue q;Task create=task(f.payloadFile,Action::CreateZip);create.target=f.folder/L"created.zip";auto a=q.add(create);uint64_t b=0,c=0,d=0;
        f.run(q,[&](uint64_t id){if(id==a&&q.get(a)->state==State::Done&&!b){auto archive=q.get(a)->result.output;b=q.add(task(archive,Action::Inspect));c=q.add(task(archive,Action::Restore));d=q.add(task(archive));}});
        require(b&&c&&d&&q.counts().done==4&&q.get(b)->result.type==L"zip"&&f.hasPayload(q.get(d)->result.output),"mixed actions failed");
        require(taskDetails(*q.get(b)).find(L"zip")!=std::wstring::npos,"identified format not visible");
        require(read(q.get(a)->result.output)==read(q.get(c)->result.output),"restored bytes differ");
    });
    check("Actual SDK source list protects a later reader without recycling",[&]{
        Fixture f(root,L"source-retention");auto archive=f.archive({f.payloadFile},L"one.7z");Queue q;auto a=q.add(task(archive));auto b=q.add(task(archive,Action::Inspect));bool checked=false;
        f.options.retainSources=[&](const auto& actual,const auto& root){checked=actual==std::vector<fs::path>{archive};require(q.add(task(archive,Action::Restore))==0,"actual source unlocked before cleanup decision");return false;};
        f.run(q);require(checked&&q.get(a)->state==State::Done&&q.get(b)->state==State::Done&&fs::exists(archive),"source retention contract failed");
    });
    check("RAR middle-volume and ambiguous-volume failures expose repair actions",[&]{
        Fixture f(root,L"rar-failures");auto first=fixtures/L"split/clouds.jpg.part1.rar",third=fixtures/L"split/clouds.jpg.part3.rar";
        write(f.folder/L"gap.part1.jpg",read(first));write(f.folder/L"gap.part3.docx",read(third));Queue q;auto a=q.add(task(f.folder/L"gap.part1.jpg"));f.run(q);
        require(q.get(a)->result.status==L"missing_volume"&&!nextStep(*q.get(a)).empty(),"RAR gap not actionable");
        write(f.folder/L"dupe.part1.jpg",read(first));write(f.folder/L"dupe.part1.txt",read(first));auto b=q.add(task(f.folder/L"dupe.part1.jpg"));f.run(q);
        require(q.get(b)->result.status==L"ambiguous_volume"&&!nextStep(*q.get(b)).empty(),"RAR duplicate not actionable");
    });
    check("Dragging every legacy RAR volume extracts once and keeps merged selection identity",[&]{
        Fixture f(root,L"legacy-rar-queue");std::vector<fs::path> pieces;
        for(const auto* name:{L"clouds.jpg.r01",L"clouds.jpg.rar",L"clouds.jpg.r00"}){auto path=f.folder/name;write(path,read(fixtures/L"split"/name));pieces.push_back(path);}
        Queue q;std::vector<uint64_t> ids;for(const auto& path:pieces)ids.push_back(q.add(task(path)));f.run(q);auto tasks=q.snapshot();
        require(tasks.size()==1&&tasks[0].state==State::Done&&tasks[0].result.archives==1&&tasks[0].files.size()==3,"legacy RAR group extracted more than once");
        auto decoded=read(tasks[0].result.output/L"clouds.jpg");require(decoded.size()==478883&&crc32(decoded.data(),decoded.size())==0x515D4B66,"RAR output differs from public fixture");
        auto selection=restoreSelection(tasks,{{ids[2]},ids[2],ids[2]});require(selection.focus==tasks[0].id&&selection.ids==std::vector<uint64_t>{tasks[0].id},"SDK volume merge lost selection");
        for(const auto& path:pieces)require(fs::exists(path),"RAR test recycled a source");
    });
    check("Root volume callback excludes nested archives from queue grouping",[&]{
        Fixture f(root,L"root-source-scope");auto inner=f.archive({f.payloadFile},L"inner.7z"),outer=f.archive({inner},L"outer.zip");Queue q;auto id=q.add(task(outer));bool checked=false;
        f.options.retainSources=[&](const auto& all,const auto& roots){checked=all.size()==2&&roots==std::vector<fs::path>{outer};return false;};f.run(q);
        require(checked&&q.get(id)->state==State::Done&&q.get(id)->result.archives==2&&f.hasPayload(q.get(id)->result.output),"root/nested source contracts mixed");
    });
    check("Saved password selected for the batch is tried first without displaying it",[&]{
        Fixture f(root,L"saved-password-priority");auto archive=f.archive({f.payloadFile},L"encrypted.7z",L"public-priority-right");f.book.add({L"public-priority-wrong1",L"public-priority-wrong2",L"public-priority-right"});
        auto selected=f.book.snapshot().at(2);f.options.password=selected.value;Queue q;auto id=q.add(task(archive));f.run(q);
        require(q.get(id)->state==State::Done&&q.get(id)->result.attempts==1&&f.hasPayload(q.get(id)->result.output),"selected password was not prioritized");
    });
    check("Same archive with separate task destinations produces both verified outputs",[&]{
        Fixture f(root,L"task-destinations");auto archive=f.archive({f.payloadFile},L"one.zip");Queue q;auto first=task(archive);auto settings=settingsFrom(f.options);settings.output=f.folder/L"destination-a";first.settings=settings;auto a=q.add(first);settings.output=f.folder/L"destination-b";first.settings=settings;auto b=q.add(first);f.run(q);
        require(q.counts().done==2&&q.get(a)->result.output.parent_path()==f.folder/L"destination-a"&&q.get(b)->result.output.parent_path()==f.folder/L"destination-b","task destinations merged or ignored");require(hasPayload(q.get(a)->result.output,f.payload)&&hasPayload(q.get(b)->result.output,f.payload)&&fs::exists(archive),"per-task output invalid");
    });
    check("Folder discovery inherits task settings through real extraction",[&]{
        Fixture f(root,L"scan-policy");auto archive=f.archive({f.payloadFile},L"one.zip");auto inbox=f.folder/L"inbox";write(inbox/L"renamed.mp4",read(archive));Task scan=task(inbox,Action::Scan);auto settings=settingsFrom(f.options);settings.output=f.folder/L"scan-destination";settings.recursive=false;scan.settings=settings;Queue q;q.add(scan);f.run(q);auto tasks=q.snapshot();require(tasks.size()==2&&tasks[1].settings==scan.settings&&tasks[1].result.output.parent_path()==settings.output&&hasPayload(tasks[1].result.output,f.payload),"scan lost task settings");
    });
    check("Per-task nested policy distinguishes intact outer files from expanded inner files",[&]{
        Fixture f(root,L"nested-policy");auto inner=f.archive({f.payloadFile},L"inner.7z"),outer=f.archive({inner},L"outer.zip");auto settings=settingsFrom(f.options);settings.output=f.folder/L"flat";settings.recursive=false;auto t=task(outer);t.settings=settings;Queue q;auto flat=q.add(t);settings.output=f.folder/L"nested";settings.recursive=true;t.settings=settings;auto nested=q.add(t);f.run(q);
        require(q.get(flat)->result.archives==1&&!hasPayload(q.get(flat)->result.output,f.payload)&&fs::exists(q.get(flat)->result.output/L"inner.7z"),"flat task expanded inner archive");require(q.get(nested)->result.archives==2&&hasPayload(q.get(nested)->result.output,f.payload),"nested task not expanded");
    });
    check("Recovered task destinations and paused work continue through the real engine",[&]{
        Fixture f(root,L"recovered-work");auto archive=f.archive({f.payloadFile},L"one.zip");Queue q;auto t=task(archive);auto settings=settingsFrom(f.options);settings.output=f.folder/L"recovered-a";t.settings=settings;auto a=q.add(t);settings.output=f.folder/L"recovered-b";t.settings=settings;t.paused=true;auto b=q.add(t);Workspace saved;saved.defaults=settingsFrom(f.options);saved.tasks=q.snapshot();WorkspaceStore store(f.folder/L"workspace.dat");store.save(saved);auto restored=store.load().value();Queue again;again.restore(std::move(restored.tasks));f.run(again);require(again.get(a)->state==State::Done&&again.get(b)->paused&&again.counts().paused==1,"restored paused work ran automatically");require(again.get(a)->result.output.parent_path()==f.folder/L"recovered-a"&&hasPayload(again.get(a)->result.output,f.payload),"restored output policy ignored");again.setPaused({b},false);f.run(again);require(again.get(b)->state==State::Done&&again.get(b)->result.output.parent_path()==f.folder/L"recovered-b"&&hasPayload(again.get(b)->result.output,f.payload),"continued restored work failed");
    });
    check("Pause after current task finishes preserves pending work and resumes without redo",[&]{
        Fixture f(root,L"pause-after-current");auto archive=f.archive({f.payloadFile},L"one.zip");Queue q;auto a=q.add(task(archive)),b=q.add(task(archive,Action::Inspect));bool pause=false;uint64_t added=0;
        runQueue(q,f.engine,f.options,[]{return false;},[&](uint64_t id){auto now=q.get(id);if(id==a&&now->state==State::Running){pause=true;if(!added)added=q.add(task(f.payloadFile,Action::CreateZip));}},[&]{return pause;});
        require(q.get(a)->state==State::Done&&f.hasPayload(q.get(a)->result.output)&&q.get(b)->state==State::Waiting&&q.get(added)->state==State::Waiting,"pause cancelled current or consumed pending work");auto firstOutput=q.get(a)->result.output;q.remove({added});pause=false;runQueue(q,f.engine,f.options,[]{return false;},[](uint64_t){},[&]{return pause;});require(q.get(b)->state==State::Done&&q.get(a)->result.output==firstOutput,"resume redid completed work");
    });
    check("Retracting a batch pause consumes remaining tasks while immediate stop still cancels",[&]{
        Fixture f(root,L"pause-retract");auto archive=f.archive({f.payloadFile},L"one.zip");Queue q;auto a=q.add(task(archive)),b=q.add(task(archive,Action::Inspect));bool pause=false;
        runQueue(q,f.engine,f.options,[]{return false;},[&](uint64_t id){if(id==a)pause=q.get(id)->state==State::Running;},[&]{return pause;});require(q.counts().done==2,"retracted pause still yielded");
        auto c=q.add(task(archive));bool cancel=false;runQueue(q,f.engine,f.options,[&]{return cancel;},[&](uint64_t id){if(id==c&&q.get(id)->state==State::Running){pause=true;cancel=true;}},[&]{return pause;});require(q.get(c)->state==State::Cancelled&&fs::exists(archive),"immediate stop lost priority");
    });
    check("Real running output -> saved checkpoint -> cancellation -> recovered output and fresh retry",[&]{
        Fixture f(root,L"live-output-recovery");auto archive=f.archive({f.payloadFile},L"one.zip");Queue q;auto id=q.add(task(archive));bool cancel=false;fs::path reported;WorkspaceStore store(f.folder/L"checkpoint.dat");
        f.options.outputReady=[&](const fs::path& output){auto running=q.get(id).value();require(running.state==State::Running&&running.result.output==output&&fs::is_directory(output),"real root output was not published while running");reported=output;Workspace value;value.defaults=settingsFrom(f.options);value.tasks=q.snapshot();store.save(value);cancel=true;};
        runQueue(q,f.engine,f.options,[&]{return cancel;},[](uint64_t){});require(cancel&&q.get(id)->state==State::Cancelled&&q.get(id)->result.output==reported&&fs::is_directory(reported)&&fs::exists(archive),"cancel lost live output");Queue restored;restored.restore(store.load()->tasks);require(restored.get(id)->state==State::Cancelled&&restored.get(id)->result.output==reported,"real checkpoint lost output on recovery");f.options.outputReady={};restored.retry({id});f.run(restored);require(restored.get(id)->state==State::Done&&restored.get(id)->result.output!=reported&&f.hasPayload(restored.get(id)->result.output)&&fs::is_directory(reported),"retry overwrote old partial output");
    });
    check("Nested extraction reports its root once and never replaces it with inner output",[&]{
        Fixture f(root,L"nested-live-output");auto inner=f.archive({f.payloadFile},L"inner.7z"),outer=f.archive({inner},L"outer.zip");Queue q;auto id=q.add(task(outer));std::vector<fs::path> events;f.options.outputReady=[&](const auto& output){events.push_back(output);require(q.get(id)->result.output==output&&q.get(id)->state==State::Running,"root not live");};f.run(q);require(events==std::vector<fs::path>{q.get(id)->result.output}&&q.get(id)->result.archives==2&&f.hasPayload(q.get(id)->result.output),"nested output replaced root or content differs");
    });
    check("Create and restore publish actual unique targets before task completion",[&]{
        Fixture f(root,L"live-create-restore");auto target=f.folder/L"chosen.zip";write(target,"existing-file-kept");Queue q;Task create=task(f.payloadFile,Action::CreateZip);create.target=target;auto id=q.add(create);uint64_t restored=0;std::vector<fs::path> events;f.options.outputReady=[&](const auto& output){auto tasks=q.snapshot();auto active=std::find_if(tasks.begin(),tasks.end(),[](const auto& item){return item.state==State::Running;});require(active!=tasks.end()&&active->result.output==output&&!active->result.ok,"created target not published before completion");events.push_back(output);};f.run(q,[&](uint64_t changed){if(changed==id&&q.get(id)->state==State::Done&&!restored)restored=q.add(task(q.get(id)->result.output,Action::Restore));});require(events.size()==2&&events[0]==q.get(id)->result.output&&events[0]!=target&&events[1]==q.get(restored)->result.output&&read(events[0])==read(events[1])&&read(target)=="existing-file-kept","actual target identity/bytes lost");
        for(const auto& event:events){ShellLocation result(event);require(result.path()==event&&result.path()!=target&&result.action()==LocationAction::SelectItem,"navigation did not use actual unique target");}
    });
    check("UTF-8 BOM local hint unlocks a real encrypted archive without prior book import",[&]{
        Fixture f(root,L"bom-hint-flow");auto archive=f.archive({f.payloadFile},L"one.7z",L"public-bom-right");write(f.folder/L"password.txt",std::string("\xef\xbb\xbf")+"public-bom-right\r\n");Queue q;auto id=q.add(task(archive));f.run(q);require(q.get(id)->state==State::Done&&f.hasPayload(q.get(id)->result.output)&&q.get(id)->result.attempts==1,"BOM hint did not unlock real archive");
    });
    check("Invalid optional hint does not prevent a valid saved password from extracting",[&]{
        Fixture f(root,L"bad-hint-flow");auto archive=f.archive({f.payloadFile},L"one.7z",L"public-saved-right");f.book.add({L"public-saved-right"});write(f.folder/L"password.txt",std::string("\xff\x00PK",4));Queue q;auto id=q.add(task(archive));f.run(q);require(q.get(id)->state==State::Done&&f.hasPayload(q.get(id)->result.output),"bad hint blocked saved-password extraction");
    });
    check("Changed generated scan entry is retained elsewhere while the next archive still extracts",[&]{
        Fixture f(root,L"changing-scan");auto archive=f.archive({f.payloadFile},L"whole.zip"),inbox=f.folder/L"inbox";write(inbox/L"a.mp4",read(archive));write(inbox/L"b.mp4",read(archive));auto first=fs::directory_iterator(inbox)->path(),retained=f.folder/L"retained-source.mp4";require(first.parent_path()==inbox&&!fs::exists(retained),"generated relocation escaped fixture");Queue q;auto id=q.add(task(inbox,Action::Scan));bool moved=false;
        f.run(q,[&](uint64_t changed){auto scan=q.get(id);if(changed==id&&!moved&&scan->state==State::Running&&scan->phase==L"扫描目录"){fs::rename(first,retained);moved=true;}});auto tasks=q.snapshot();require(moved&&fs::exists(retained)&&q.get(id)->state==State::Done&&q.get(id)->result.status==L"scanned_with_skips"&&q.get(id)->result.failedFile==first&&q.get(id)->result.note.find(L"跳过")!=std::wstring::npos,"scan change aborted or warning lost");require(tasks.size()==2&&tasks[1].state==State::Done&&f.hasPayload(tasks[1].result.output),"later valid archive missed");auto missing=f.engine.scan(f.folder/L"absent",f.options,{},{});require(!missing.ok&&missing.status==L"scan_incomplete","bad root falsely scanned");
    });
    check("Password import prepares only visible password failures and leaves other problems untouched",[&]{
        Fixture f(root,L"reason-retry");auto archive=f.archive({f.payloadFile},L"encrypted.7z",L"public-visible-right");auto a=f.folder/L"batch-a.mp4",b=f.folder/L"batch-b.docx",hidden=f.folder/L"hidden.7z";write(a,read(archive));write(b,read(archive));write(hidden,read(archive));auto parts=f.pieces(archive);auto gap=f.folder/L"gap/set.7z.001";write(gap,read(parts[0]));Queue q;auto first=q.add(task(a)),second=q.add(task(b)),other=q.add(task(hidden)),volume=q.add(task(gap)),missing=q.add(task(f.folder/L"missing.mp4"));f.run(q);
        require(q.get(first)->result.status==L"password_required"&&q.get(second)->result.status==L"password_required"&&q.get(other)->result.status==L"password_required"&&q.get(volume)->result.status==L"missing_volume"&&q.get(missing)->result.status==L"source_unavailable","mixed real failures did not reproduce");
        f.book.add({L"public-visible-right"});TaskView view{L"batch-",TaskFilter::NeedsPassword};auto ids=view.retryIds(q.snapshot());require(ids==std::vector<uint64_t>({first,second})&&q.retry(ids)==2,"visible retry included hidden or different problems");require(q.counts().waiting==2&&q.get(other)->state==State::Attention&&q.get(volume)->state==State::Attention&&q.get(missing)->state==State::Attention&&view.retryIds(q.snapshot()).empty(),"preparation auto-ran or changed hidden work");f.run(q);require(f.hasPayload(q.get(first)->result.output)&&f.hasPayload(q.get(second)->result.output)&&q.get(other)->state==State::Attention&&q.get(volume)->result.status==L"missing_volume","batch repair failed or touched hidden tasks");
    });
    check("Live priority reorder keeps the current task and extracts remaining work in stable order",[&]{
        Fixture f(root,L"live-reorder");auto archive=f.archive({f.payloadFile},L"whole.zip");Queue q;std::vector<uint64_t> ids,started;std::set<uint64_t> seen;for(unsigned i=0;i<5;++i){auto file=f.folder/(L"item-"+std::to_wstring(i)+L".mp4");write(file,read(archive));ids.push_back(q.add(task(file)));}
        f.run(q,[&](uint64_t id){auto current=q.get(id);if(current&&current->state==State::Running&&seen.insert(id).second){started.push_back(id);if(id==ids[0]){require(q.reorder({ids[4],ids[2]},QueueMove::First)==2&&q.reorder({ids[1]},QueueMove::Last)==1,"live batch reorder failed");require(q.snapshot()[0].id==ids[0]&&q.get(ids[0])->state==State::Running,"priority changed current task");}}});
        require(started==std::vector<uint64_t>({ids[0],ids[2],ids[4],ids[3],ids[1]})&&q.counts().done==5,"real dispatch ignored stable priorities");for(auto id:ids)require(f.hasPayload(q.get(id)->result.output),"reordered extraction bytes differ");
    });
    check("Paused priority and task output survive a real batch, workspace restore and later continuation",[&]{
        Fixture f(root,L"paused-priority-restore");auto archive=f.archive({f.payloadFile},L"whole.zip");Queue q;std::vector<uint64_t> ids;for(unsigned i=0;i<3;++i){auto file=f.folder/(L"item-"+std::to_wstring(i)+L".mp4");write(file,read(archive));auto t=task(file);if(i==1){t.paused=true;auto settings=settingsFrom(f.options);settings.output=f.folder/L"paused-target";settings.recursive=false;t.settings=settings;}ids.push_back(q.add(t));}q.reorder({ids[1],ids[2]},QueueMove::First);f.run(q);require(q.counts().done==2&&q.get(ids[1])->paused&&q.get(ids[1])->result.output.empty(),"priority consumed paused task");auto firstOutput=q.get(ids[0])->result.output,thirdOutput=q.get(ids[2])->result.output;Workspace saved;saved.defaults=settingsFrom(f.options);saved.tasks=q.snapshot();saved.selection={{ids[1]},ids[1],ids[1]};WorkspaceStore store(f.folder/L"workspace.dat");store.save(saved);Queue again;again.restore(store.load()->tasks);again.setPaused({ids[1]},false);f.run(again);require(again.counts().done==3&&again.get(ids[1])->result.output.parent_path()==f.folder/L"paused-target"&&hasPayload(again.get(ids[1])->result.output,f.payload)&&again.get(ids[0])->result.output==firstOutput&&again.get(ids[2])->result.output==thirdOutput,"restored priority lost policy or redid completed outputs");
    });
    check("Real nested extraction immediately publishes root, password and inner stages with units",[&]{
        Fixture f(root,L"live-stages");auto inner=f.archive({f.payloadFile},L"inner.7z",L"public-inner-stage"),outer=f.archive({inner},L"outer.zip");f.book.add({L"public-inner-stage"});Queue q;auto id=q.add(task(outer));bool rootStage=false,innerStage=false,passwordStage=false;
        f.run(q,[&](uint64_t changed){auto now=q.get(id);if(changed!=id||!now||now->state!=State::Running)return;if(now->phase==L"解压 outer.zip"){rootStage=true;require(now->progressUnit==ProgressUnit::Bytes,"root unit wrong");}if(rootStage&&now->phase==L"验证密码"){passwordStage=true;require(now->progressUnit==ProgressUnit::Candidates,"password unit wrong");}if(now->phase==L"解压 inner.7z"){innerStage=true;require(now->progressUnit==ProgressUnit::Bytes,"inner unit wrong");}});require(rootStage&&passwordStage&&innerStage&&q.get(id)->state==State::Done&&f.hasPayload(q.get(id)->result.output),"short stage transitions or real nested output lost");
    });
    check("Cancelled directory construction does not cache a partial volume snapshot",[&]{
        Fixture f(root,L"volume-cancel-cache");auto archive=f.archive({f.payloadFile},L"whole.zip"),carrier=f.folder/L"cover.mp4";write(carrier,"public-video-prefix"+read(archive));VolumeResolver resolver;bool cancelled=false;try{resolver.resolve(carrier,[](std::wstring phase,uint64_t done,uint64_t,ProgressUnit){return !(phase==L"读取同目录分卷"&&done>=1);});}catch(const Cancelled&){cancelled=true;}unsigned rebuilt=0;auto paths=resolver.resolve(carrier,[&](std::wstring phase,uint64_t done,uint64_t,ProgressUnit){if(phase==L"读取同目录分卷"&&!done)++rebuilt;return true;});require(cancelled&&rebuilt==1&&paths==std::vector<fs::path>{carrier}&&fs::exists(carrier),"cancelled snapshot was cached as complete");
    });
    check("Renamed split candidate matching cancels and resumes with the same complete metadata cache",[&]{
        Fixture f(root,L"candidate-cancel");auto archive=f.archive({f.payloadFile},L"whole.7z");auto pieces=f.pieces(archive);std::vector<fs::path> selected={f.folder/L"inbox/set.7z.001",f.folder/L"inbox/set.7z.002",f.folder/L"inbox/renamed.docx"};for(size_t i=0;i<3;++i)write(selected[i],read(pieces[i]));VolumeResolver resolver;bool cancelled=false;try{resolver.resolve(selected[0],[](std::wstring phase,uint64_t,uint64_t,ProgressUnit){return phase!=L"匹配分卷候选";});}catch(const Cancelled&){cancelled=true;}auto paths=resolver.resolve(selected[0]);require(cancelled&&paths==selected,"candidate cancellation became fallback or broke complete cache");auto options=f.options;options.volumesResolved=true;auto result=f.engine.extract(paths,options);require(result.ok&&f.hasPayload(result.output),"resumed candidate group failed real extraction");for(const auto& path:selected)require(fs::exists(path),"candidate test lost a source");
    });
    check("Runner cancels during volume analysis without fallback or output and later retries normally",[&]{
        Fixture f(root,L"runner-volume-cancel");auto archive=f.archive({f.payloadFile},L"whole.zip"),a=f.folder/L"a.mp4",b=f.folder/L"b.docx";write(a,"public-prefix"+read(archive));write(b,"public-prefix"+read(archive));f.options.recursive=false;bool stop=false,output=false;f.options.outputReady=[&](const auto&){output=true;};Queue q;auto first=q.add(task(a)),second=q.add(task(b));runQueue(q,f.engine,f.options,[&]{return stop;},[&](uint64_t id){auto current=q.get(id);if(current&&current->state==State::Running&&current->phase==L"读取同目录分卷")stop=true;});require(stop&&!output&&q.get(first)->state==State::Cancelled&&q.get(first)->result.status==L"cancelled"&&q.get(second)->state==State::Waiting,"volume cancel was swallowed or processed later work");q.retry({first});f.run(q);require(q.counts().done==2&&f.hasPayload(q.get(first)->result.output)&&f.hasPayload(q.get(second)->result.output)&&fs::exists(a)&&fs::exists(b),"volume-cancel retry failed");
    });
    check("Inspect restore and extract reuse one root directory snapshot across disguised single files",[&]{
        Fixture f(root,L"root-resolution-reuse");auto archive=f.archive({f.payloadFile},L"whole.zip");std::vector<fs::path> sources={f.folder/L"inbox/a.mp4",f.folder/L"inbox/b.docx",f.folder/L"inbox/c.mp4"};for(const auto& path:sources)write(path,"public-prefix"+read(archive));f.options.recursive=false;Queue q;auto a=q.add(task(sources[0],Action::Inspect)),b=q.add(task(sources[1],Action::Restore)),c=q.add(task(sources[2]));unsigned directories=0;std::wstring previous;uint64_t previousId=0;f.run(q,[&](uint64_t id){auto now=q.get(id);if(!now||now->state!=State::Running)return;if(now->phase==L"读取同目录分卷"&&!now->progressDone&&(previous!=now->phase||previousId!=id))++directories;previous=now->phase;previousId=id;});require(directories==1&&q.counts().done==3&&q.get(a)->result.type==L"zip"&&read(q.get(b)->result.output)==read(archive)&&f.hasPayload(q.get(c)->result.output),"root operation repeated metadata work or lost decoded bytes");std::cout<<"Root directory snapshots for inspect/restore/extract: "<<directories<<'\n';
    });
    check("One-shot cancellation inside scan identification cannot become a skipped or admitted file",[&]{
        Fixture f(root,L"scan-identify-cancel");auto archive=f.archive({f.payloadFile},L"whole.zip"),inbox=f.folder/L"inbox";write(inbox/L"one.mp4",read(archive));bool requested=false;unsigned found=0;auto result=f.engine.scan(inbox,f.options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){if(!requested&&phase==L"验证密码"){requested=true;return false;}return true;},[&](const auto&,const auto&){++found;});require(requested&&!result.ok&&result.status==L"cancelled"&&!found&&fs::exists(inbox/L"one.mp4"),"identify cancelled signal was turned into ordinary scan result");
    });
    check("One-shot cancellation during nested probing retains outer output and never reports success",[&]{
        Fixture f(root,L"nested-probe-cancel");auto file=f.folder/L"ordinary.bin";write(file,"public ordinary content");auto archive=f.archive({file},L"outer.zip");bool nested=false,requested=false;auto result=f.engine.extract({archive},f.options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){if(phase==L"检查嵌套压缩包")nested=true;if(nested&&!requested&&(phase==L"验证密码"||phase==L"深度识别")){requested=true;return false;}return true;});require(requested&&!result.ok&&result.status==L"cancelled"&&fs::exists(archive)&&read(result.output/L"ordinary.bin")=="public ordinary content","nested probe cancellation was skipped or lost outer output");
    });
    check("Explicit full verification cancellation keeps cancellation ahead of verification failure",[&]{
        Fixture f(root,L"verify-cancel-status");auto archive=f.archive({f.payloadFile},L"whole.zip");auto options=f.options;options.verifyData=true;bool requested=false;auto result=f.engine.inspect({archive},options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){if(phase==L"完整校验"){requested=true;return false;}return true;});require(requested&&!result.ok&&result.status==L"cancelled"&&fs::exists(archive),"cancel was relabeled verification failure");
    });
    check("Missing-first diagnostic inspection preserves a one-shot cancellation request",[&]{
        Fixture f(root,L"missing-first-cancel");auto archive=f.archive({f.payloadFile},L"whole.7z");auto parts=f.pieces(archive);auto continuation=f.folder/L"gap/without.7z.002";write(continuation,read(parts[1]));unsigned analysis=0;bool requested=false;auto result=f.engine.extract({continuation},f.options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){if(phase==L"分析分卷"&&++analysis==3){requested=true;return false;}return true;});require(requested&&!result.ok&&result.status==L"cancelled"&&result.note.find(L"取消")!=std::wstring::npos&&fs::exists(continuation),"diagnostic fallback overwrote cancellation with missing volume");
    });
    check("SDK body verification cancellation remains cancelled rather than CRC failure",[&]{
        Fixture f(root,L"sdk-verify-cancel");auto second=f.folder/L"second.bin";write(second,f.payload+f.payload);auto archive=f.archive({f.payloadFile,second},L"whole.zip");auto options=f.options;options.verifyData=true;unsigned calls=0;auto result=f.engine.inspect({archive},options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){return phase!=L"完整校验"||++calls<2;});require(calls>=2&&!result.ok&&result.status==L"cancelled"&&fs::exists(archive),"native E_ABORT was relabeled verification failure");
    });
    check("SDK password-probe cancellation is not swallowed as an ordinary wrong candidate",[&]{
        Fixture f(root,L"sdk-password-cancel");auto archive=f.folder/L"body.7z";bit7z::Bit7zLibrary library((executableDir()/L"7z.dll").wstring());bit7z::BitFileCompressor compressor(library,bit7z::BitFormat::SevenZip);compressor.setPassword(L"public-probe-right",false);compressor.compress(std::vector<std::wstring>{f.payloadFile.wstring()},archive.wstring());f.book.add({L"public-probe-right",L"public-probe-wrong"});unsigned calls=0;auto result=f.engine.extract({archive},f.options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){return phase!=L"快速验证密码"||++calls<2;});require(calls>=2&&!result.ok&&result.status==L"cancelled"&&fs::exists(archive),"probe E_ABORT became another candidate or normal open failure");
    });
    check("Known ZIP priority candidate avoids whole-book prechecks and verifies real output",[&]{
        Fixture f(root,L"zip-priority");auto archive=f.archive({f.payloadFile},L"one.zip",L"public-priority-key");std::vector<std::wstring> wrong;for(unsigned i=0;i<512;++i)wrong.push_back(L"public-wrong-"+std::to_wstring(i));f.book.add(wrong);auto options=f.options;options.password=L"public-priority-key";uint64_t maxHeader=0;unsigned verified=0;auto result=f.engine.extract({archive},options,[&](std::wstring phase,uint64_t,uint64_t total,ProgressUnit){if(phase==L"密码头部预检")maxHeader=std::max(maxHeader,total);if(phase==L"快速验证密码")++verified;return true;});require(result.ok&&f.hasPayload(result.output)&&maxHeader>0&&maxHeader<=8&&verified>=2&&f.book.snapshot().back().value==options.password,"priority path filtered whole book or skipped full probe");std::cout<<"ZIP preferred precheck max variants with 512 saved candidates: "<<maxHeader<<'\n';
    });
    check("Plain ZIP and encrypted ZIP metadata and restore do no password KDF filtering",[&]{
        Fixture f(root,L"zip-metadata-lazy");auto plain=f.archive({f.payloadFile},L"plain.zip"),encrypted=f.archive({f.payloadFile},L"encrypted.zip",L"public-metadata-key");std::vector<std::wstring> wrong;for(unsigned i=0;i<256;++i)wrong.push_back(L"public-wrong-"+std::to_wstring(i));f.book.add(wrong);unsigned headers=0;Progress progress=[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){if(phase==L"密码头部预检")++headers;return true;};auto inspected=f.engine.inspect({encrypted},f.options,progress),restored=f.engine.restore({encrypted},f.options,progress),extracted=f.engine.extract({plain},f.options,progress);require(inspected.ok&&inspected.encrypted&&!inspected.passwordVerified&&restored.ok&&read(restored.output)==read(encrypted)&&extracted.ok&&f.hasPayload(extracted.output)&&!headers,"metadata/plain ZIP performed unnecessary key filtering");
    });
    check("A weak ZipCrypto priority-header collision fails SDK body check then tail candidate succeeds",[&]{
        Fixture f(root,L"zip-header-collision");auto archive=f.folder/L"crypto.zip";bit7z::Bit7zLibrary library((executableDir()/L"7z.dll").wstring());bit7z::BitFileCompressor compressor(library,bit7z::BitFormat::Zip);compressor.setPassword(L"public-tail-right",false);compressor.setFormatProperty(L"em",L"ZipCrypto");compressor.compress(std::vector<std::wstring>{f.payloadFile.wstring()},archive.wstring());Input input({archive});std::vector<std::wstring> guesses;for(unsigned i=0;i<10000;++i)guesses.push_back(L"public-header-"+std::to_wstring(i));auto matched=zipPasswordCandidates(input,guesses,{});require(!matched.empty()&&matched.size()<guesses.size()/2,"fixture did not produce a weak-header-only match");std::vector<std::wstring> candidates{matched[0].value,L"public-early-one",L"public-early-two",L"public-early-three"};for(unsigned i=0;i<64;++i)candidates.push_back(L"public-tail-wrong-"+std::to_wstring(i));candidates.push_back(L"public-tail-right");f.book.add(candidates);uint64_t maxHeader=0;auto result=f.engine.extract({archive},f.options,[&](std::wstring phase,uint64_t,uint64_t total,ProgressUnit){if(phase==L"密码头部预检")maxHeader=std::max(maxHeader,total);return true;});require(result.ok&&f.hasPayload(result.output)&&result.attempts>=2&&maxHeader>8,"header collision bypassed body validation or lost fallback");
    });
    check("Large single-entry ZIP keeps the bulk verifier instead of a forced extra body pass",[&]{
        Fixture f(root,L"zip-large-probe");auto large=f.folder/L"large.bin";std::string bytes(2*1024*1024,'L');write(large,bytes);auto archive=f.archive({large},L"large.zip",L"public-large-right");Input input({archive});std::vector<std::wstring> wrong;for(unsigned i=0;i<32;++i){auto value=L"public-large-wrong-"+std::to_wstring(i);if(zipPasswordCandidates(input,{value},{}).empty())wrong.push_back(value);}require(wrong.size()>4,"fixture has too few rejected headers");f.book.add(wrong);auto options=f.options;options.password=L"public-large-right";uint64_t maxHeader=0;unsigned probes=0;auto result=f.engine.extract({archive},options,[&](std::wstring phase,uint64_t,uint64_t total,ProgressUnit){if(phase==L"密码头部预检")maxHeader=std::max(maxHeader,total);if(phase==L"快速验证密码")++probes;return true;});require(result.ok&&read(result.output/L"large.bin")==bytes&&maxHeader==wrong.size()+1&&probes==1,"large entry forced redundant SDK probe or lost content");
    });
    check("Unsupported ZIP method during priority body verification remains an engine failure",[&]{
        Fixture f(root,L"zip-unsupported-priority");auto archive=f.folder/L"crypto.zip";bit7z::Bit7zLibrary library((executableDir()/L"7z.dll").wstring());bit7z::BitFileCompressor compressor(library,bit7z::BitFormat::Zip);compressor.setPassword(L"public-method-key",false);compressor.setFormatProperty(L"em",L"ZipCrypto");compressor.compress(std::vector<std::wstring>{f.payloadFile.wstring()},archive.wstring());auto bytes=read(archive);auto central=bytes.find(std::string("PK\1\2",4));require(bytes.starts_with(std::string("PK\3\4",4))&&central!=std::string::npos,"ZIP fixture layout changed");bytes[8]=char(254);bytes[9]=0;bytes[central+10]=char(254);bytes[central+11]=0;write(archive,bytes);auto options=f.options;options.password=L"public-method-key";auto result=f.engine.extract({archive},options,{});require(!result.ok&&result.status==L"verification_failed"&&!result.engineError.empty()&&result.attempts==1&&fs::exists(archive),"unsupported method was misreported as needing another password");
    });
    check("ZIP priority header and native probe and bulk fallback each preserve cancellation",[&]{
        Fixture f(root,L"zip-priority-cancel");auto archive=f.archive({f.payloadFile},L"one.zip",L"public-cancel-right");std::vector<std::wstring> wrong;for(unsigned i=0;i<32;++i)wrong.push_back(L"public-cancel-wrong-"+std::to_wstring(i));f.book.add(wrong);auto options=f.options;options.password=L"public-cancel-right";
        auto header=f.engine.extract({archive},options,[](std::wstring phase,uint64_t,uint64_t,ProgressUnit){return phase!=L"密码头部预检";});require(!header.ok&&header.status==L"cancelled","priority header cancel lost");unsigned probes=0;auto probe=f.engine.extract({archive},options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){return phase!=L"快速验证密码"||++probes<2;});require(!probe.ok&&probe.status==L"cancelled"&&probes>=2,"priority native probe cancel lost");auto fallback=f.engine.extract({archive},f.options,[](std::wstring phase,uint64_t,uint64_t total,ProgressUnit){return phase!=L"密码头部预检"||total<=8;});require(!fallback.ok&&fallback.status==L"cancelled"&&fs::exists(archive),"bulk fallback cancel lost");
    });
    check("Cancelled stale password status suggests retry rather than repair",[]{
        auto t=task(L"C:\\generated\\video.mp4");t.state=State::Cancelled;t.result.status=L"password_required";
        require(nextStep(t).find(L"重试")!=std::wstring::npos&&nextStep(t).find(L"补充本批密码")==std::wstring::npos,"cancelled task suggests password repair");
    });
    check("Missing source -> relocated first volume -> refreshed group and verified output",[&]{
        Fixture f(root,L"source-relocation");auto archive=f.archive({f.payloadFile},L"whole.7z");auto pieces=f.pieces(archive);auto t=task(f.folder/L"old-location/set.7z.001");t.files.push_back(f.folder/L"old-location/set.7z.002");t.refreshVolumes=true;t.settings=settingsFrom(f.options);Queue q;auto id=q.add(t);f.run(q);auto failed=q.get(id).value();require(failed.state==State::Attention&&failed.result.status==L"source_unavailable"&&failed.result.failedFile==t.files[0],"missing first source not actionable");require(q.replaceSource(id,failed.result.failedFile,pieces[0])&&q.get(id)->files.size()==1,"relocation retained old inferred group");f.run(q);require(q.get(id)->state==State::Done&&q.get(id)->files.size()==3&&f.hasPayload(q.get(id)->result.output)&&q.get(id)->settings==t.settings,"relocated volume group failed");
    });
    check("Manual missing middle source repairs its original slot through real engine",[&]{
        Fixture f(root,L"manual-source-relocation");auto archive=f.archive({f.payloadFile},L"whole.7z");auto pieces=f.pieces(archive);Task t;t.files={pieces[0],f.folder/L"old-middle.mp4",pieces[2]};t.manualVolumes=true;Queue q;auto id=q.add(t);f.run(q);require(q.get(id)->result.status==L"source_unavailable"&&q.get(id)->result.failedFile==t.files[1],"manual missing slot not identified");require(q.replaceSource(id,t.files[1],pieces[1])&&q.get(id)->files==pieces,"manual order changed");f.run(q);require(q.get(id)->state==State::Done&&f.hasPayload(q.get(id)->result.output),"repaired manual group failed");
    });
    check("File-open payload feeds the same real queue without duplicate extraction",[&]{
        Fixture f(root,L"file-open-flow");auto archive=f.archive({f.payloadFile},L"actual.zip");auto disguised=f.folder/L"视频 外壳.mp4";write(disguised,read(archive));auto packet=encodeOpenFiles({disguised,disguised});auto paths=decodeOpenFiles(packet.data(),packet.size());Queue q;unsigned admitted=0;for(const auto& path:paths)if(q.add(task(path)))++admitted;require(admitted==1,"duplicate file-open admitted twice");f.run(q);require(q.counts().done==1&&f.hasPayload(q.snapshot()[0].result.output),"file-open handoff did not extract real bytes");
    });
    check("Nested password repair runs only the inner archive after workspace recovery",[&]{
        Fixture f(root,L"inner-password-repair");auto inner=f.archive({f.payloadFile},L"inner.7z",L"public-inner-repair-key");auto marker=f.folder/L"outer-note.txt";write(marker,"outer content already decoded");auto outer=f.archive({inner,marker},L"outer.zip");Task parent=task(outer);parent.settings=settingsFrom(f.options);parent.settings->deep=false;Queue q;auto id=q.add(parent);f.run(q);auto failed=q.get(id).value();require(failed.state==State::Attention&&failed.result.status==L"password_required"&&failed.result.archives==1,"fixture did not reach inner password failure");auto originalOutput=failed.result.output;auto collision=failed.result.failedFile.parent_path()/L"inner";write(collision/L"existing.txt","existing content kept");
        auto repair=nestedTask(failed,{f.folder/L"unrelated-batch",false,false,true});require(repair&&repair->settings->output==failed.result.failedFile.parent_path()&&!repair->settings->deep,"inner task lost local output or parent policy");auto repairId=q.add(*repair);require(repairId&&!q.add(*repair)&&q.get(id)->state==State::Attention&&q.counts().waiting==1,"preparation repeated work or auto-started");Workspace saved;saved.defaults={f.folder/L"changed-default",false,false,true};saved.tasks=q.snapshot();WorkspaceStore store(f.folder/L"repair-session.dat");store.save(saved);Queue again;again.restore(store.load()->tasks);f.book.add({L"public-inner-repair-key"});std::set<uint64_t> ran;f.run(again,[&](uint64_t changed){if(again.get(changed)->state==State::Running)ran.insert(changed);});auto done=again.get(repairId).value();require(done.state==State::Done&&done.result.archives==1&&done.result.output.parent_path()==failed.result.failedFile.parent_path()&&done.result.output!=collision&&f.hasPayload(done.result.output),"inner-only recovery failed or used changed batch target");require(ran==std::set<uint64_t>{repairId}&&again.get(id)->state==State::Attention&&again.get(id)->result.output==originalOutput&&again.get(id)->result.failedFile==failed.result.failedFile&&read(originalOutput/L"outer-note.txt")=="outer content already decoded"&&read(collision/L"existing.txt")=="existing content kept"&&fs::exists(outer)&&fs::exists(failed.result.failedFile),"outer or existing data changed during inner repair");
    });
    check("Cancelled inner extraction restarts locally with a fresh output instead of repeating outer",[&]{
        Fixture f(root,L"inner-cancel-repair");auto inner=f.archive({f.payloadFile},L"inner.7z"),outer=f.archive({inner},L"outer.zip");Queue q;auto id=q.add(task(outer));bool cancel=false;runQueue(q,f.engine,f.options,[&]{return cancel;},[&](uint64_t changed){auto now=q.get(changed);if(now->state==State::Running&&now->phase==L"解压 inner.7z")cancel=true;});auto failed=q.get(id).value();require(cancel&&failed.state==State::Cancelled&&!failed.result.failedFile.empty()&&failed.result.archives==1,"fixture did not cancel inner work");auto oldPartial=failed.result.failedFile.parent_path()/L"inner";require(fs::is_directory(oldPartial),"partial inner folder was not published by engine");auto repair=nestedTask(failed,settingsFrom(f.options));require(repair.has_value(),"cancelled inner not actionable");auto child=q.add(*repair);f.run(q);require(q.get(child)->state==State::Done&&q.get(child)->result.output!=oldPartial&&q.get(child)->result.output.parent_path()==oldPartial.parent_path()&&f.hasPayload(q.get(child)->result.output)&&fs::is_directory(oldPartial)&&q.get(id)->state==State::Cancelled&&q.get(id)->result.output==failed.result.output,"inner restart overwrote partial output or reran parent");
    });
    check("A missing prepared inner source is reported without changing its failed parent",[&]{
        Fixture f(root,L"inner-missing-repair");auto inner=f.archive({f.payloadFile},L"inner.7z",L"public-missing-key"),outer=f.archive({inner},L"outer.zip");Queue q;auto id=q.add(task(outer));f.run(q);auto failed=q.get(id).value();auto repair=nestedTask(failed,settingsFrom(f.options));require(repair.has_value(),"no repair target");auto child=q.add(*repair);auto moved=f.folder/L"retained-inner.7z";fs::rename(failed.result.failedFile,moved);f.run(q);require(q.get(child)->state==State::Attention&&q.get(child)->result.status==L"source_unavailable"&&q.get(child)->result.failedFile==failed.result.failedFile&&q.get(id)->result.output==failed.result.output&&q.get(id)->state==State::Attention&&fs::exists(outer)&&fs::exists(moved),"missing source changed parent or produced false success");
    });
    check("A later nested read error retains the correct containing inner package for local repair",[&]{
        Fixture f(root,L"inner-context-after-deep");auto deep=f.archive({f.payloadFile},L"a-deep.zip");auto later=f.folder/L"z-later.bin";write(later,"ordinary later entry");auto inner=f.archive({deep,later},L"inner.zip"),outer=f.archive({inner},L"outer.zip");bool deepStarted=false,moved=false;fs::path retained=f.folder/L"retained-later.bin";
        auto result=f.engine.extract({outer},f.options,[&](std::wstring phase,uint64_t,uint64_t,ProgressUnit){if(phase==L"解压 a-deep.zip")deepStarted=true;if(deepStarted&&!moved&&phase==L"检查嵌套压缩包"){auto actual=f.options.output/L"outer/inner/z-later.bin";if(fs::exists(actual)){fs::rename(actual,retained);moved=true;}}return true;});require(moved&&!result.ok&&result.archives>=3&&result.failedFile.filename()==L"inner.zip"&&fs::exists(result.failedFile)&&fs::exists(retained),"later failure lost containing archive context");Task parent=task(outer);parent.state=State::Attention;parent.result=result;auto repair=nestedTask(parent,settingsFrom(f.options));require(repair&&repair->files.front()==result.failedFile,"later error cannot prepare local inner repair");Queue q;auto child=q.add(*repair);f.run(q);require(q.get(child)->state==State::Done&&q.get(child)->result.archives==2&&q.get(child)->result.output.parent_path()==result.failedFile.parent_path()&&f.hasPayload(q.get(child)->result.output)&&fs::exists(outer)&&fs::exists(retained),"local containing-archive recovery failed");
    });
    check("Nested repair draft follows changed inner file after parent retry",[]{
        auto t=task(L"C:\\generated\\outer.zip");t.id=8;t.state=State::Attention;t.result.status=L"missing_volume";t.result.output=L"C:\\generated\\first";t.result.failedFile=L"C:\\generated\\first\\inner.001";
        VolumeEditor editor;editor.open(t);appendVolumes(editor.files,{L"C:\\generated\\second.002"});editor.open(t);require(editor.files.size()==2,"same source draft reset");
        t.result.output=L"C:\\generated\\retry-output";t.result.failedFile=L"C:\\generated\\retry-output\\other.001";editor.open(t);require(editor.files==std::vector<fs::path>{t.result.failedFile},"old inner source reused after parent retry");
    });
    std::cout<<passed<<'/'<<total<<" full production queue flows passed; no windows, no recycling, generated sources retained.\n";return passed==total?0:1;
}
