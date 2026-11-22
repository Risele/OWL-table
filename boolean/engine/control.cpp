#include "control.hpp"
namespace b2f { namespace engine {
static thread_local Control *current=nullptr;
bool cache_enabled() { return !current || current->use_cache; }
bool Control::cancel() {
    std::lock_guard<std::mutex> lock(gate);
    if(phase.load()>=6)return false;
    cancelled.store(true);return true;
}
ControlScope::ControlScope(Control *c):previous(current) { current=c; }
ControlScope::~ControlScope() { current=previous; }
void checkpoint() { if(current && current->phase.load()<6 && current->cancelled.load())throw Cancelled(); }
void progress(int phase) {
    if(!current)return;
    if(phase==6) { std::lock_guard<std::mutex> lock(current->gate);checkpoint();current->phase.store(6); }
    else { checkpoint();current->phase.store(phase); }
    if(current->observer)current->observer(phase);
    checkpoint();
}
} }
