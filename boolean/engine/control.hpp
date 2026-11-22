#pragma once
#include <atomic>
#include <mutex>
#include <functional>
#include <exception>
namespace b2f { namespace engine {
struct Cancelled : std::exception { const char *what() const noexcept override { return "Generation cancelled"; } };
struct Control {
    bool use_cache=true;
    std::atomic<bool> cancelled{false};
    std::atomic<int> phase{0};
    std::mutex gate;
    std::function<void(int)> observer;
    bool cancel();
};
struct ControlScope {
    Control *previous;
    explicit ControlScope(Control *);
    ~ControlScope();
};
void checkpoint();
bool cache_enabled();
void progress(int phase); // 1 snapshot, 2 search, 3 selection, 4 export, 5 proof, 6 commit, 7 done
} }
