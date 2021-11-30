#include "ipc_lock.h"

#pragma push_macro("IPC_PTHREAD_FUNC_")
#undef IPC_PTHREAD_FUNC_
#define IPC_PTHREAD_FUNC_(CALL, ...)          \
    int eno;                                  \
    if ((eno = CALL(__VA_ARGS__)) != 0)       \
    {                                         \
        printf("fail " #CALL " [%d]\n", eno); \
        return false;                         \
    }                                         \
    return true

pthread_mutex_t &Mutex::Native()
{
    return mutex_;
}

bool Mutex::Open()
{
    int eno;
    // init mutex
    pthread_mutexattr_t mutex_attr;
    if ((eno = pthread_mutexattr_init(&mutex_attr)) != 0)
    {
        printf("fail pthread_mutexattr_init[%d]\n", eno);
        return false;
    }
    if ((eno = pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED)) != 0)
    {
        printf("fail pthread_mutexattr_setpshared[%d]\n", eno);
        return false;
    }
    if ((eno = pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST)) != 0)
    {
        printf("fail pthread_mutexattr_setrobust[%d]\n", eno);
        return false;
    }
    if ((eno = pthread_mutex_init(&mutex_, &mutex_attr)) != 0)
    {
        printf("fail pthread_mutex_init[%d]\n", eno);
        return false;
    }
    if ((eno = pthread_mutexattr_destroy(&mutex_attr)) != 0)
    {
        printf("fail pthread_mutexattr_destroy[%d]\n", eno);
        return false;
    }
    return true;
}

bool Mutex::Close()
{
    IPC_PTHREAD_FUNC_(pthread_mutex_destroy, &mutex_);
}

bool Mutex::Lock()
{
    for (;;)
    {
        int eno = pthread_mutex_lock(&mutex_);
        switch (eno)
        {
        case 0:
            return true;
        case EOWNERDEAD:
            if (::pthread_mutex_consistent(&mutex_) == 0)
            {
                pthread_mutex_unlock(&mutex_);
                break;
            }
        case ENOTRECOVERABLE:
            if (Close() && Open())
            {
                break;
            }
        default:
            printf("fail pthread_mutex_lock[%d]\n", eno);
            return false;
        }
    }
}

bool Mutex::Unlock()
{
    IPC_PTHREAD_FUNC_(pthread_mutex_unlock, &mutex_);
}

bool ConditionVar::Open()
{
    int eno;
    // init condition
    pthread_condattr_t cond_attr;
    if ((eno = pthread_condattr_init(&cond_attr)) != 0)
    {
        printf("fail pthread_condattr_init[%d]\n", eno);
        return false;
    }
    if ((eno = pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED)) != 0)
    {
        printf("fail pthread_condattr_setpshared[%d]\n", eno);
        return false;
    }
    if ((eno = pthread_cond_init(&cond_, &cond_attr)) != 0)
    {
        printf("fail pthread_cond_init[%d]\n", eno);
        return false;
    }
    if ((eno = pthread_condattr_destroy(&cond_attr)) != 0)
    {
        printf("fail pthread_condattr_destroy[%d]\n", eno);
        return false;
    }
    return true;
}

bool ConditionVar::Close()
{
    IPC_PTHREAD_FUNC_(pthread_cond_destroy, &cond_);
}

bool ConditionVar::Wait(Mutex &mtx, std::size_t tm = invalid_value)
{
    switch (tm)
    {
    case 0:
        return false;
    case invalid_value:
        IPC_PTHREAD_FUNC_(pthread_cond_wait, &cond_, &mtx.Native());
    default:
    {
        timespec ts;
        if (!CalcWaitTime(ts, tm))
        {
            printf("fail CalcWaitTime: tm = %zd, tv_sec = %ld, tv_nsec = %ld\n",
                   tm, ts.tv_sec, ts.tv_nsec);
            return false;
        }
        int eno;
        if ((eno = pthread_cond_timedwait(&cond_, &mtx.Native(), &ts)) != 0)
        {
            if (eno != ETIMEDOUT)
            {
                printf("fail pthread_cond_timedwait[%d]: tm = %zd, tv_sec = %ld, tv_nsec = %ld\n",
                       eno, tm, ts.tv_sec, ts.tv_nsec);
            }
            return false;
        }
    }
        return true;
    }
}

bool ConditionVar::Notify()
{
    IPC_PTHREAD_FUNC_(pthread_cond_signal, &cond_);
}

bool ConditionVar::Broadcast()
{
    IPC_PTHREAD_FUNC_(pthread_cond_broadcast, &cond_);
}

#pragma pop_macro("IPC_PTHREAD_FUNC_")
