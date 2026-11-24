#pragma once
#include "ui_model.hpp"

namespace lu::ui {
// The desktop worker and windowless flow checks use this same executor.
void runQueue(Queue& queue,Engine& engine,const Options& options,
              const std::function<bool()>& cancelled,
              const std::function<void(uint64_t)>& notify,
              const std::function<bool()>& pauseAfterCurrent={},
              unsigned concurrency=1);
}
