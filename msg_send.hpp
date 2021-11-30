#ifndef MSG_SEND_HPP
#define MSG_SEND_HPP

#include "ipc_lock.h"
#include "msg_comm.hpp"

template <typename T, std::size_t N = 1>
class MsgSend
{
public:
    using Buffer = Item<T>;
    MsgSend(const std::string &msg_name)
    {
        msg_info_.type = typeid(T).hash_code();
        msg_info_.name = std::move(msg_name);
        Connect();
    }

    MsgSend() = delete;
    MsgSend(const MsgSend &that) = delete;
    MsgSend &operator=(const MsgSend &that) = delete;

    ~MsgSend()
    {
        if (!isValid_)
        {
            return;
        }
        isValid_ = false;
        // Notify the readers
        msg_header_->shut_down = true;
        msg_header_->cond_not_empty.Broadcast();

        // Close the synchronization stuffs
        msg_header_->mutex.Close();
        msg_header_->cond_not_empty.Close();

        // Clear the shared memory
        if (munmap(msg_info_.mem, msg_info_.size) != 0)
        {
            printf("MsgSend fail munmap[%d]: %s\n", errno, msg_info_.name.c_str());
            return;
        }

        if (shm_unlink(msg_info_.name.c_str()) != 0)
        {
            printf("MsgSend fail shm_unlink[%d]: %s\n", errno, msg_info_.name.c_str());
            return;
        }
    }

    bool Connect()
    {
        if (msg_info_.name.empty() || msg_info_.name.at(0) == '\0')
        {
            printf("MsgSend failed: msg_name is empty \n");
            return false;
        }
        int oflag = O_RDWR | O_CREAT | O_TRUNC;
        int fd = shm_open(msg_info_.name.c_str(), oflag, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (fd == -1)
        {
            printf("MsgSend fail shm_open[%d]: %s\n", errno, msg_info_.name.c_str());
            return false;
        }
        msg_info_.fd = fd;
        msg_info_.size = GetTotalSize(N, sizeof(T));
        if (ftruncate(fd, static_cast<off_t>(msg_info_.size)) != 0)
        {
            printf("MsgSend fail ftruncate[%d]: %s, size = %zd\n", errno, msg_info_.name.c_str(), msg_info_.size);
            return false;
        }
        void *mem = mmap(nullptr, msg_info_.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED)
        {
            printf("MsgSend fail mmap[%d]: %s, size = %zd\n", errno, msg_info_.name.c_str(), msg_info_.size);
            return false;
        }
        close(fd);
        msg_info_.fd = -1;
        msg_info_.mem = mem;

        // Initialize the contents in shared memory
        msg_header_ = reinterpret_cast<MsgHeader *>(mem);
        buffer_ = reinterpret_cast<Buffer *>((uint8_t *)mem + sizeof(MsgHeader));

        msg_header_->type_hash = msg_info_.type;
        msg_header_->capacity = N;
        msg_header_->size = 0;
        msg_header_->mutex.Open();
        msg_header_->cond_not_empty.Open();

        isValid_ = true;
        return true;
    }

    bool IsValid()
    {
        return isValid_;
    }

    void ShutDown()
    {
        if (!isValid_)
        {
            return;
        }
        // Notify the readers
        msg_header_->shut_down = true;
        msg_header_->cond_not_empty.Broadcast();
    }

    bool Pub(const T &data)
    {
        // If this SendMsg is not valid, e.g., not properly initialized
        if (!isValid_)
        {
            return false;
        }

        // Connected readers
        msg_header_->mutex.Lock();
        std::uint32_t cc = msg_header_->conn.CurConn();
        std::cout << "cc:" << cc << std::endl;
        // Check the msg is not shut_down and there exists at least a reader
        while (!msg_header_->shut_down)
        {
            // The reader flags
            auto write_index = (msg_header_->wi + 1) % msg_header_->capacity;
            auto item_rc = buffer_[write_index].rc;
            // Check which readers have not read the data
            std::uint32_t rem_rc = item_rc & 0xffffffff;
            // if (cc & item_rc)
            // {
            //     printf("force push data: cc = %u, rem_rc = %u \n", cc, rem_rc);
            //     // cc = msg_header_->conn.DisconnectId(rem_rc);
            //     // if (cc == 0)
            //     // {
            //     //     msg_header_->mutex.Unlock();
            //     //     return false;
            //     // }
            // }

            // Placement new to construct an object in memory that's already allocated.
            new (&buffer_[write_index].data) T(data);
            buffer_[write_index].rc = 0xffffffff;

            msg_header_->wi = write_index;
            msg_header_->cond_not_empty.Broadcast();
            msg_header_->mutex.Unlock();
            return true;
        }
        msg_header_->mutex.Unlock();
        return false;
    }

private:
    MsgHeader *msg_header_;
    Buffer *buffer_;
    // Static buffer
    MsgInfo msg_info_;

    bool isValid_ = false;
};

#endif
