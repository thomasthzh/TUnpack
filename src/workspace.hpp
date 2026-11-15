#pragma once
#include "ui_model.hpp"
#include <condition_variable>
#include <thread>

namespace lu::ui {
// Intentionally contains no password inputs, password search or display preference.
struct Workspace {
    TaskSettings defaults;
    std::vector<Task> tasks;
    VolumeEditor volumes;
    TaskSettingsEditor editor;
    std::array<TableSizing,PageCount> columns;
    std::array<POINT,PageCount> scroll{};
    std::array<int,PageCount> focus{};
    TaskView view;
    Selection selection;
    Page page=Page::Queue;
    int width=1120,height=820;
    bool maximized=false;
};
class WorkspaceStore {
    fs::path file_;
public:
    explicit WorkspaceStore(fs::path file={});
    const fs::path& path() const {return file_;}
    std::optional<Workspace> load() const;
    void save(const Workspace& workspace) const;
    fs::path preserveUnreadable() const;
};
class WorkspaceWriter {
    WorkspaceStore store_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Workspace> pending_;
    bool busy_=false,stopping_=false;
    std::wstring error_;
    std::jthread thread_;
    void run();
public:
    explicit WorkspaceWriter(fs::path file={});
    ~WorkspaceWriter();
    void submit(Workspace workspace);
    bool flush();
    std::wstring error() const;
};
}
