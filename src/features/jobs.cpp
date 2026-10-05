#include "jobs.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>

namespace Jobs
{
static std::vector<std::thread>          s_workers;
static std::deque<std::function<void()>> s_queue;
static std::mutex                        s_mtx;
static std::condition_variable           s_cv;
static bool                              s_stopping = false;

static void WorkerLoop()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;)
    {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(s_mtx);
            s_cv.wait(lk, [] { return s_stopping || !s_queue.empty(); });
            if (s_stopping && s_queue.empty()) return;
            job = std::move(s_queue.front());
            s_queue.pop_front();
        }
        // A job that throws must not take the worker down with it -- the pool
        // has to outlive any single piece of work.
        try { job(); } catch (...) {}
    }
}

void Start()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    if (!s_workers.empty()) return;
    s_stopping = false;

    // Leave the game two cores of headroom. One worker is still worth having on
    // a dual core: it moves the work off the render thread even without a spare
    // core to run it on.
    unsigned hw = std::thread::hardware_concurrency();
    int n = (hw > 3) ? (int)(hw - 2) : 1;
    if (n > 4) n = 4;   // past this the raster jobs are not the bottleneck

    s_workers.reserve(n);
    for (int i = 0; i < n; ++i)
        s_workers.emplace_back(WorkerLoop);
    DebugLog("jobs: %d workers (hw=%u)", n, hw);
}

void Stop()
{
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        if (s_workers.empty()) return;
        s_stopping = true;
    }
    s_cv.notify_all();
    for (auto& t : s_workers) if (t.joinable()) t.join();
    s_workers.clear();
    std::lock_guard<std::mutex> lk(s_mtx);
    s_queue.clear();
}

void Submit(std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        if (s_stopping || s_workers.empty()) return;
        s_queue.emplace_back(std::move(fn));
    }
    s_cv.notify_one();
}

int WorkerCount()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    return (int)s_workers.size();
}

int Pending()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    return (int)s_queue.size();
}
} // namespace Jobs
