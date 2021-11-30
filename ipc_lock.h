#ifndef IPC_LOCK_H
#define IPC_LOCK_H
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <semaphore.h>
#include <stdio.h>
#include <errno.h>

#include <limits>
#include <atomic>

enum : std::size_t
{
    invalid_value = (std::numeric_limits<std::size_t>::max)(),
    default_timeout = 100 // ms
};

class Mutex
{
public:
    pthread_mutex_t &Native();

    bool Open();
    bool Close();
    bool Lock();
    bool Unlock();

private:
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
};

class ConditionVar
{
public:
    bool Open();
    bool Close();
    bool Wait(Mutex &mtx, std::size_t tm);
    bool Notify();
    bool Broadcast();

private:
    pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;
};

inline static bool CalcWaitTime(timespec &ts, std::size_t tm /*ms*/)
{
    timeval now;
    int eno = gettimeofday(&now, NULL);
    if (eno != 0)
    {
        printf("fail gettimeofday [%d]\n", eno);
        return false;
    }
    ts.tv_nsec = (now.tv_usec + (tm % 1000) * 1000) * 1000;
    ts.tv_sec = now.tv_sec + (tm / 1000) + (ts.tv_nsec / 1000000000);
    ts.tv_nsec %= 1000000000;
    return true;
}

#endif
