#pragma once
#include "input.hpp"
#include <oleidl.h>
#include <shellapi.h>

namespace lu::ui {
struct FileDropCallbacks {
    std::function<bool(const std::vector<fs::path>&)> accepts;
    std::function<bool(const std::vector<fs::path>&)> receive;
    std::function<void(bool,unsigned)> hover;
    std::function<void(const std::wstring&)> error;
};
// The same native target is used by the app and windowless COM tests.
IDropTarget* createFileDropTarget(FileDropCallbacks callbacks);
std::vector<fs::path> pathsFromDrop(HDROP drop);
}
