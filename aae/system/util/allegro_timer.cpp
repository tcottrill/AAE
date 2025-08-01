#include "allegro_timer.h"

// STL headers
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <windows.h>

using namespace std;

struct TimerInfo {
    TimerInfo(void (*proc)(), int speed)
        : proc(proc), speed(speed), stopFlag(false)
    {}

    ~TimerInfo() = default;

    void (*proc)();
    int speed;
    atomic<bool> stopFlag;
    thread workerThread;
};

static map<void (*)(), shared_ptr<TimerInfo>> activeTimers;
static mutex activeTimersMutex;

thread_local TimerInfo* currentThreadTimerInfo = nullptr;

/*
static void ThreadProc(shared_ptr<TimerInfo> info)
{
    currentThreadTimerInfo = info.get();

    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    LONGLONG timerSpeed = freq.QuadPart * info->speed / TIMERS_PER_SECOND;
    LONGLONG accumulatedTime = 0;

    while (!info->stopFlag.load())
    {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        accumulatedTime += currentTime.QuadPart - lastTime.QuadPart;
        lastTime = currentTime;
       
        if (accumulatedTime < timerSpeed)
        {
            DWORD ms = static_cast<DWORD>((timerSpeed - accumulatedTime) * 1000 / freq.QuadPart);
            this_thread::sleep_for(chrono::milliseconds(ms));
        }
        else
        {
            while (accumulatedTime >= timerSpeed)
            {
                accumulatedTime -= timerSpeed;
                info->proc();
            }
        }
       
    }
}
*/

/*
static void ThreadProc(shared_ptr<TimerInfo> info)
{
    currentThreadTimerInfo = info.get();

    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    const LONGLONG timerSpeed = freq.QuadPart * info->speed / TIMERS_PER_SECOND;
    LONGLONG accumulatedTime = 0;

    while (!info->stopFlag.load())
    {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        accumulatedTime += currentTime.QuadPart - lastTime.QuadPart;
        lastTime = currentTime;

        if (accumulatedTime < timerSpeed)
        {
            // Compute time to sleep in microseconds
            LONGLONG timeLeft = timerSpeed - accumulatedTime;
            double msLeft = (1000.0 * timeLeft) / freq.QuadPart;

            if (msLeft > 2.0)
            {
                // For larger waits, sleep
                this_thread::sleep_for(chrono::milliseconds(static_cast<int>(msLeft) - 1));
            }
            else
            {
                // For short waits, yield CPU to avoid oversleeping
                this_thread::yield();
            }
        }
        else
        {
            // Call proc the appropriate number of times
            while (accumulatedTime >= timerSpeed)
            {
                accumulatedTime -= timerSpeed;
                info->proc();
            }
        }
    }
}
*/


static void ThreadProc(shared_ptr<TimerInfo> info)
{
    currentThreadTimerInfo = info.get();

    LARGE_INTEGER freq, lastTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);

    const LONGLONG timerSpeed = freq.QuadPart * info->speed / TIMERS_PER_SECOND;
    LONGLONG accumulatedTime = 0;

    while (!info->stopFlag.load())
    {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        accumulatedTime += currentTime.QuadPart - lastTime.QuadPart;
        lastTime = currentTime;

        if (accumulatedTime < timerSpeed)
        {
            LONGLONG timeLeft = timerSpeed - accumulatedTime;
            double microsecondsLeft = (1'000'000.0 * timeLeft) / freq.QuadPart;

            if (microsecondsLeft > 2000.0)
            {
                // Sleep 1 ms less than needed to avoid oversleep
                this_thread::sleep_for(chrono::microseconds(static_cast<int>(microsecondsLeft - 1000)));
            }
            else
            {
                // Spinlock using yield or nop
                while (true)
                {
                    QueryPerformanceCounter(&currentTime);
                    if ((currentTime.QuadPart - lastTime.QuadPart) >= timeLeft)
                        break;

                    // On very short waits, spinning yields better precision than sleep
                    _mm_pause(); // short CPU pause, like yield
                    //this_thread::yield();
                }
            }
        }

        while (accumulatedTime >= timerSpeed)
        {
            accumulatedTime -= timerSpeed;
            info->proc();
        }
    }
}


static void RemoveIntLockless(void (*proc)())
{
    auto it = activeTimers.find(proc);
    if (it != activeTimers.end())
    {
        auto& info = it->second;

        // Signal stop
        info->stopFlag = true;

        // Join thread unless it's this thread
        if (info.get() != currentThreadTimerInfo && info->workerThread.joinable())
            info->workerThread.join();

        activeTimers.erase(it);
    }
}

void remove_int(void (*proc)())
{
    lock_guard<mutex> lock(activeTimersMutex);
    RemoveIntLockless(proc);
}

int install_int_ex(void (*proc)(), int speed)
{
    auto info = make_shared<TimerInfo>(proc, speed);

    {
        lock_guard<mutex> lock(activeTimersMutex);
        RemoveIntLockless(proc); // remove old one if exists
        activeTimers[proc] = info;
    }

    // Launch the thread
    info->workerThread = thread(ThreadProc, info);

    return 0;
}

int install_int(void (*proc)(), int speed)
{
    return install_int_ex(proc, MSEC_TO_TIMER(speed));
}
