#include "queue_runner.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace lu::ui {
namespace {
void executeTask(Task task,Queue& queue,Engine& engine,const Options& options,
                 VolumeResolver& volumes,
                 const std::function<bool()>& cancelled,
                 const std::function<void(uint64_t)>& notify,
                 const std::function<void()>& onWorkAdded,
                 unsigned taskThreads){
    notify(task.id);Result result;ProgressGate gate;auto last=std::chrono::steady_clock::time_point{};
    Progress progress=[&](std::wstring phase,uint64_t done,uint64_t total,ProgressUnit unit){
        if(cancelled())return false;
        if(gate.accept(phase,done,total,unit)){queue.progress(task.id,phase,done,total,unit);notify(task.id);}
        return !cancelled();
    };
    try{
        bool resolved=task.manualVolumes||task.files.size()>1;
        if(!task.manualVolumes&&(task.files.size()==1||task.refreshVolumes)&&task.action!=Action::Scan&&task.action!=Action::Create7z&&task.action!=Action::CreateZip){
            // Retry may follow newly copied/renamed volumes, even within this batch.
            VolumeResolver fresh;
            auto& resolver=task.refreshVolumes?fresh:volumes;
            try{task.files=resolver.resolve(task.files.front(),progress);resolved=true;queue.resolve(task.id,task.files);notify(task.id);}
            catch(const Cancelled&){throw;}catch(const std::exception&){resolved=false;task.files={task.files.front()}; /* Let the engine report the current volume failure. */}
        }
        auto sourceEnd=task.manualVolumes||task.action==Action::Create7z||task.action==Action::CreateZip?task.files.end():task.files.begin()+1;
        auto unavailable=std::find_if(task.files.begin(),sourceEnd,[&](const auto& file){auto attributes=GetFileAttributesW(file.c_str());if(attributes==INVALID_FILE_ATTRIBUTES)return true;bool directory=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;if(task.action==Action::Scan)return !directory;if(task.action==Action::Create7z||task.action==Action::CreateZip)return false;return directory;});
        if(unavailable!=sourceEnd){
            result.status=L"source_unavailable";result.failedFile=*unavailable;result.note=L"来源不存在、类型已改变或无法访问。请重新定位，或恢复原路径后重试。";
            queue.finish(task.id,std::move(result),cancelled());notify(task.id);return;
        }
        auto taskOptions=applySettings(options,task.settings);
        taskOptions.allowContainer=task.allowContainer;taskOptions.volumesResolved=resolved;
        taskOptions.threads=taskThreads;
        taskOptions.outputReady=[&](const auto& output){queue.outputReady(task.id,output);notify(task.id);if(options.outputReady)options.outputReady(output);};
        taskOptions.retainSources=[&](const auto& actual,const auto& rootVolumes){
            if(!task.manualVolumes&&rootVolumes.size()>1){queue.resolve(task.id,rootVolumes);notify(task.id);}
            bool retain=queue.retainSources(task.id,actual);
            if(options.retainSources)retain=options.retainSources(actual,rootVolumes)||retain;
            return retain;
        };
        switch(task.action){
        case Action::Inspect:result=engine.inspect(task.files,taskOptions,progress);break;
        case Action::Restore:result=engine.restore(task.files,taskOptions,progress);break;
        case Action::Create7z:case Action::CreateZip:
            result=engine.compress(task.files,task.target,options.password,progress,taskOptions.outputReady);break;
        case Action::Scan:{
            unsigned added=0;
            result=engine.scan(task.files.front(),taskOptions,progress,[&](const auto& paths,const Result&){
                Task found;found.files=paths;found.settings=task.settings;
                if(queue.add(std::move(found))){++added;if(onWorkAdded)onWorkAdded();}
                auto now=std::chrono::steady_clock::now();
                if(now-last>=std::chrono::milliseconds(90)){notify(task.id);last=now;}
            });
            result.archives=added;
            if(result.ok)result.note+=L"；已加入 "+std::to_wstring(added)+L" 组，继续按队列处理。";
            break;
        }
        case Action::Extract:result=engine.extract(task.files,taskOptions,progress);break;
        }
    }catch(const std::exception& e){result.note=errorText(e);if(isCancelled(e))result.status=L"cancelled";}
    queue.finish(task.id,std::move(result),cancelled());notify(task.id);
}
}
void runQueue(Queue& queue,Engine& engine,const Options& options,
              const std::function<bool()>& cancelled,
              const std::function<void(uint64_t)>& notify,
              const std::function<bool()>& pauseAfterCurrent,
              unsigned concurrency){
    if(concurrency<=1){
        VolumeResolver volumes;
        while(!cancelled()&&(!pauseAfterCurrent||!pauseAfterCurrent())){
            auto next=queue.takeNext(false);if(!next)break;
            executeTask(std::move(*next),queue,engine,options,volumes,cancelled,notify,{},options.threads);
        }
        return;
    }
    unsigned hw=std::max(1u,std::thread::hardware_concurrency());
    unsigned workers=std::min(concurrency,std::min(8u,hw));
    unsigned perTaskThreads=options.threads?options.threads:std::max(1u,hw/workers);

    std::mutex poolMutex;
    std::condition_variable poolCv;
    unsigned activeWorkers=0;
    auto wakeAll=[&]{std::scoped_lock lock(poolMutex);poolCv.notify_all();};

    std::vector<std::jthread> threads;
    threads.reserve(workers);
    for(unsigned w=0;w<workers;++w){
        threads.emplace_back([&]{
            CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            VolumeResolver volumes;
            for(;;){
                std::optional<Task> next;
                {
                    std::unique_lock lock(poolMutex);
                    for(;;){
                        if(cancelled()||(pauseAfterCurrent&&pauseAfterCurrent()))break;
                        next=queue.takeNext(true);
                        if(next){++activeWorkers;break;}
                        if(activeWorkers==0)break;
                        poolCv.wait_for(lock,std::chrono::milliseconds(50));
                    }
                }
                if(!next)break;
                executeTask(std::move(*next),queue,engine,options,volumes,cancelled,notify,wakeAll,perTaskThreads);
                {
                    std::scoped_lock lock(poolMutex);
                    --activeWorkers;
                }
                poolCv.notify_all();
            }
            CoUninitialize();
        });
    }
    for(auto& t:threads)t.join();
}
}
