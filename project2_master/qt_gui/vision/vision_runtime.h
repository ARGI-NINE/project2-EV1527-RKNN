#pragma once

#include "app_options.h"

#include <atomic>
#include <thread>

namespace dashboard {

class DashboardBackend;

class VisionRuntime {
public:
    VisionRuntime(DashboardBackend *backend, const AppOptions &options);
    ~VisionRuntime();

    void start();
    void stop();

private:
    void workerLoop();

    DashboardBackend *backend_ = nullptr;
    AppOptions options_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace dashboard
