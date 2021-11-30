
#ifndef MSG_COMM_HPP
#define MSG_COMM_HPP

#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <type_traits>
#include <atomic>
#include <string>
#include <utility>
#include <cstring>
#include <iostream>

#include "ipc_lock.h"

// Reside in user process memory
struct MsgInfo
{
    int fd = -1;
    void *mem = nullptr;
    // Total size in shared memory
    std::size_t size = 0;
    std::string name;
    size_t type;
};

// TODO: lock-free implementation
class HeaderConn
{
public:
    std::uint32_t GetConnectId()
    {
        // If connection slot is full
        if ((curr_mask_ + 1) == 0)
        {
            return 0;
        }
        // find the first 0, and set it to 1.
        std::uint32_t next = curr_mask_ | (curr_mask_ + 1);
        std::uint32_t connected_id = next ^ curr_mask_;
        curr_mask_ = next;
        return connected_id;
    }

    // Return the mask after removing the disconnected id
    std::uint32_t DisconnectId(std::uint32_t id)
    {
        curr_mask_ = (curr_mask_ & ~id) & ~id;
        return curr_mask_;
    }

    std::size_t ConnCount()
    {
        std::uint32_t mask = curr_mask_;
        std::uint32_t cnt;
        for (cnt = 0; curr_mask_; ++cnt)
        {
            curr_mask_ &= curr_mask_ - 1;
        }

        return cnt;
    }

    std::uint32_t CurConn() const
    {
        return curr_mask_;
    }

    bool IsConnected(std::uint32_t rid)
    {
        return ((curr_mask_ | ~rid) & rid) != 0;
    }

private:
    std::uint32_t curr_mask_ = 0;
};

// Reside in shared memory header for synchronization
struct alignas(64) MsgHeader
{
    // Indicates whether the shared memory should shut down in case of dead writer
    std::atomic_bool shut_down = ATOMIC_VAR_INIT(false);
    Mutex mutex;
    ConditionVar cond_not_empty;

    // Internal circular buffer
    std::size_t capacity;
    std::size_t size;
    std::size_t wi = 0; // TODO: not thread-safe
    size_t type_hash;

    // Connected readers
    HeaderConn conn;

    bool IsEqualWi(std::size_t ri)
    {
        return ri % capacity == wi;
    }

    std::size_t IncRi(std::size_t &ri)
    {
        ri = (ri + 1) % capacity;
        return ri;
    }
};

template <typename T>
struct Item
{
    // Uninitialized memory blocks to hold the object
    typename std::aligned_storage<sizeof(T), alignof(T)>::type data{};
    std::uint32_t rc{0}; // Reader flags, bit 0 for being read, 1 for not read
};

std::size_t GetTotalSize(std::size_t len, std::size_t data_size)
{
    return sizeof(MsgHeader) + len * data_size;
}

#endif
