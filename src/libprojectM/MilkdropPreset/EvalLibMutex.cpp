#include <projectm-eval.h>
#include <mutex>

static std::mutex g_eval_memory_mutex;

void projectm_eval_memory_host_lock_mutex() {
    g_eval_memory_mutex.lock();
}

void projectm_eval_memory_host_unlock_mutex() {
    g_eval_memory_mutex.unlock();
}
