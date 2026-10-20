// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "aeraui/core/runner.cpp"

using namespace std::chrono_literals;
using aeraui::RunResult;
static std::mutex test_mutex;
static std::condition_variable test_condition;
static bool initialization_started = false;
static bool finish_initialization = false;
static bool backend_mounted_vendor = false;
static bool startup_returned = false;
static bool finish_ui = false;
static bool selected_installed_mapper = false;
static RunResult selected = RunResult::kUnexpectedExit;

static void Require(bool value, const char* message) {
    if (value) return;
    std::fprintf(stderr, "%s\n", message);
    std::_Exit(1);
}

namespace aeraui {
void TestDisplayInitialized();
RunResult TestRunLoop(bool, const DisplayMetrics&, bool) {
    std::unique_lock<std::mutex> lock(test_mutex);
    // Graphics can resolve the mapper more than once during initialization.
    // A backend mount between those resolutions changes which inode is loaded.
    initialization_started = true;
    test_condition.notify_all();
    test_condition.wait(lock, [] { return finish_initialization; });
    selected_installed_mapper = backend_mounted_vendor;
    lock.unlock();
    if (selected != RunResult::kUnexpectedExit) return selected;
    TestDisplayInitialized();
    lock.lock();
    test_condition.wait(lock, [] { return finish_ui; });
    return selected;
}
}

int main(int argc, char** argv) {
    Require(argc == 2, "expected scenario");
    if (std::strcmp(argv[1], "failure") == 0) selected = RunResult::kEngineFailure;
    if (std::strcmp(argv[1], "terminal") == 0) selected = RunResult::kPowerOff;
    std::thread backend([] {
        aeraui::StartAeraUiEarly();
        // Models Setup_Fstab_Partitions immediately after gui_init returns.
        std::lock_guard<std::mutex> lock(test_mutex);
        backend_mounted_vendor = true;
        startup_returned = true;
        test_condition.notify_all();
    });
    std::unique_lock<std::mutex> lock(test_mutex);
    Require(test_condition.wait_for(lock, 1s, [] { return initialization_started; }),
            "UI initialization did not start");
    Require(!test_condition.wait_for(lock, 150ms, [] { return startup_returned; }),
            "backend mounted vendor while graphics initialization was incomplete");
    finish_initialization = true;
    test_condition.notify_all();
    Require(test_condition.wait_for(lock, 1s, [] { return startup_returned; }),
            "startup was not released after initialization finished or failed");
    Require(!selected_installed_mapper, "graphics selected an installed-vendor mapper");
    finish_ui = true;
    test_condition.notify_all();
    lock.unlock();
    backend.join();
    Require(aeraui::RunAeraUi() == selected, "lost UI result after startup barrier");
    if (selected == RunResult::kPowerOff) {
        aeraui::StartAeraUiEarly();
        Require(aeraui::RunAeraUi() == selected, "lost terminal result on later startup");
    }
    // Terminal threads intentionally remain alive until init ends recovery.
    std::_Exit(0);
}
