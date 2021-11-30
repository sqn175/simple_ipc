
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <atomic>
#include <string>
#include <utility>
#include <cstring>
#include <iostream>

namespace
{
    struct info_t
    {
        std::atomic_size_t acc_;
    };

    struct id_info_t
    {
        int fd_ = -1;
        void *mem_ = nullptr;
        std::size_t size_ = 0;
        std::string name_;
    };

    constexpr std::size_t calc_size(std::size_t size)
    {
        return ((((size - 1) / alignof(info_t)) + 1) * alignof(info_t)) + sizeof(info_t);
    }

    inline auto &acc_of(void *mem, std::size_t size)
    {
        return reinterpret_cast<info_t *>(static_cast<std::uint8_t *>(mem) + size - sizeof(info_t))->acc_;
    }

} // internal-linkage

namespace ipc
{
    namespace shm
    {
        enum : unsigned
        {
            create = 0x01,
            open = 0x02
        };

        void *acquire(char const *name, std::size_t size, unsigned mode)
        {
            if (name == nullptr || name[0] == '\0')
            {
                std::cerr << "fail acquire: name is empty\n";
                return nullptr;
            }
            std::string op_name = std::string{"__IPC_SHM__"} + name;
            // Open the object for read-write access.
            int flag = O_RDWR;
            switch (mode)
            {
            case open:
                size = 0;
                break;
            // The check for the existence of the object,
            // and its creation if it does not exist, are performed atomically.
            case create:
                flag |= O_CREAT | O_EXCL;
                break;
            // Create the shared memory object if it does not exist.
            default:
                flag |= O_CREAT;
                break;
            }
            int fd = shm_open(op_name.c_str(), flag, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            if (fd == -1)
            {
                printf("fail shm_open[%d]: %s\n", errno, name);
                return nullptr;
            }
            auto ii = new id_info_t();
            ii->fd_ = fd;
            ii->size_ = size;
            ii->name_ = std::move(op_name);
            return ii;
        }

        void *get_mem(void *id, std::size_t *size)
        {
            if (id == nullptr)
            {
                printf("fail get_mem: invalid id (null)\n");
                return nullptr;
            }
            auto ii = static_cast<id_info_t *>(id);
            if (ii->mem_ != nullptr)
            {
                if (size != nullptr)
                    *size = ii->size_;
                return ii->mem_;
            }
            int fd = ii->fd_;
            if (fd == -1)
            {
                printf("fail to_mem: invalid id (fd = -1)\n");
                return nullptr;
            }
            if (ii->size_ == 0)
            {
                struct stat st;
                if (::fstat(fd, &st) != 0)
                {
                    printf("fail fstat[%d]: %s, size = %zd\n", errno, ii->name_.c_str(), ii->size_);
                    return nullptr;
                }
                ii->size_ = static_cast<std::size_t>(st.st_size);
                if ((ii->size_ <= sizeof(info_t)) || (ii->size_ % sizeof(info_t)))
                {
                    printf("fail to_mem: %s, invalid size = %zd\n", ii->name_.c_str(), ii->size_);
                    return nullptr;
                }
            }
            else
            {
                ii->size_ = calc_size(ii->size_);
                if (::ftruncate(fd, static_cast<off_t>(ii->size_)) != 0)
                {
                    printf("fail ftruncate[%d]: %s, size = %zd\n", errno, ii->name_.c_str(), ii->size_);
                    return nullptr;
                }
            }
            void *mem = mmap(nullptr, ii->size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (mem == MAP_FAILED)
            {
                printf("fail mmap[%d]: %s, size = %zd\n", errno, ii->name_.c_str(), ii->size_);
                return nullptr;
            }
            close(fd);
            ii->fd_ = -1;
            ii->mem_ = mem;
            if (size != nullptr)
                *size = ii->size_;
            acc_of(mem, ii->size_).fetch_add(1, std::memory_order_release);
            return mem;
        }

        void release(void *id)
        {
            if (id == nullptr)
            {
                printf("fail release: invalid id (null)\n");
                return;
            }
            auto ii = static_cast<id_info_t *>(id);
            if (ii->mem_ == nullptr || ii->size_ == 0)
            {
                printf("fail release: invalid id (mem = %p, size = %zd)\n", ii->mem_, ii->size_);
            }
            else if (acc_of(ii->mem_, ii->size_).fetch_sub(1, std::memory_order_acquire) == 1)
            {
                munmap(ii->mem_, ii->size_);
                if (!ii->name_.empty())
                {
                    shm_unlink(ii->name_.c_str());
                }
            }
            else
                munmap(ii->mem_, ii->size_);
            free(ii);
        }

        void remove(void *id)
        {
            if (id == nullptr)
            {
                printf("fail remove: invalid id (null)\n");
                return;
            }
            auto ii = static_cast<id_info_t *>(id);
            auto name = std::move(ii->name_);
            release(id);
            if (!name.empty())
            {
                shm_unlink(name.c_str());
            }
        }

        void remove(char const *name)
        {
            if (name == nullptr || name[0] == '\0')
            {
                printf("fail remove: name is empty\n");
                return;
            }
            shm_unlink((std::string{"__IPC_SHM__"} + name).c_str());
        }

    } // namespace shm
} // namespace ipc
