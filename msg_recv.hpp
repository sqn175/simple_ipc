
#ifndef MSG_RECV_HPP
#define MSG_RECV_HPP

#include "ipc_lock.h"
#include "msg_comm.hpp"

template <typename T>
class MsgRecv
{
public:
    using Buffer = Item<T>;

    MsgRecv(const std::string &msg_name)
    {
        msg_info_.type = typeid(T).hash_code();
        msg_info_.name = std::move(msg_name);
        Init();
    }

    MsgRecv() = delete;
    MsgRecv(const MsgRecv &that) = delete;
    MsgRecv &operator=(const MsgRecv &that) = delete;

    ~MsgRecv()
    {
        Release();
    }

    bool Init()
    {
        if (msg_info_.name.empty() || msg_info_.name.at(0) == '\0')
        {
            printf("MsgRecv failed: msg_name is empty \n");
            return false;
        }
        int oflag = O_RDWR;
        int fd = shm_open(msg_info_.name.c_str(), oflag, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (fd == -1)
        {
            printf("MsgRecv fail shm_open[%d]: %s\n", errno, msg_info_.name.c_str());
            return false;
        }
        msg_info_.fd = fd;
        struct stat st;
        if (fstat(fd, &st) != 0)
        {
            printf("MsgRecv fail fstat[%d]: %s, size = %zd\n", errno, msg_info_.name.c_str(), msg_info_.size);
            return false;
        }
        msg_info_.size = static_cast<std::size_t>(st.st_size);
        if (msg_info_.size <= sizeof(MsgHeader))
        {
            printf("MsgRecv fail to_mem: %s, invalid size = %zd\n", msg_info_.name.c_str(), msg_info_.size);
            return false;
        }

        void *mem = mmap(nullptr, msg_info_.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mem == MAP_FAILED)
        {
            printf("MsgRecv fail mmap[%d]: %s, size = %zd\n", errno, msg_info_.name.c_str(), msg_info_.size);
            return false;
        }
        close(fd);

        msg_info_.fd = -1;
        msg_info_.mem = mem;

        // Initialize the contents in shared memory
        msg_header_ = reinterpret_cast<MsgHeader *>(mem);
        buffer_ = reinterpret_cast<Buffer *>((uint8_t *)mem + sizeof(MsgHeader));

        if (msg_info_.type != msg_header_->type_hash)
        {
            printf("MsgRecv type mismatch\n");
            Release();
            return false;
        }

        isValid_ = true;
        return true;
    }

    bool Get(T &data, std::size_t tm = 0)
    {
        // If not properly initialized, try re-init
        if (!isValid_)
        {
            if (!ReInit())
            {
                return false;
            }
        }

        while (!msg_header_->shut_down)
        {
            msg_header_->mutex.Lock();

            // Check for connection
            if (!msg_header_->conn.IsConnected(conn_id_))
            {
                printf("MsgRecv: %s disconnected. Try reconnect ... \n", msg_info_.name.c_str());
                if (!Connect())
                {
                    printf("MsgRecv: %s reconnect failed ... \n", msg_info_.name.c_str());
                    msg_header_->mutex.Unlock();
                    return false;
                }
                printf("MsgRecv: %s connected \n", msg_info_.name.c_str());
            }

            std::uint32_t readable = buffer_[ri_].rc & conn_id_;

            // Loop to (or wait for) the first readable data
            while (readable == 0)
            {
                // If the reader index is behind the writer index
                if (!msg_header_->IsEqualWi(ri_))
                {
                    msg_header_->IncRi(ri_);
                }
                else if (!msg_header_->cond_not_empty.Wait(msg_header_->mutex, tm))
                {
                    // We cannot exceed anymore, i.e., we need to wait for data production
                    // printf("MsgRecv fail cond_not_empty.wait: %s\n", msg_info_.name.c_str());
                    msg_header_->mutex.Unlock();

                    return false;
                }
                readable = buffer_[ri_].rc & conn_id_;
            }

            // We arrived at the first readable data, read it!
            // Clear the read flag for this reader
            buffer_[ri_].rc &= ~conn_id_;
            // Get the internal data
            new (&data) T(std::move(*static_cast<T *>(reinterpret_cast<void *>(&buffer_[ri_].data))));
            ri_ = msg_header_->IncRi(ri_);

            msg_header_->mutex.Unlock();
            return true;
        }
        Release();
        return false;
    }

private:
    void
    Release()
    {
        isValid_ = false;

        if (msg_info_.mem == nullptr || msg_info_.size == 0)
        {
            return;
        }
        // Disconnect
        msg_header_->conn.DisconnectId(conn_id_);

        // Clear the shared memory
        if (munmap(msg_info_.mem, msg_info_.size) != 0)
        {
            printf("MsgRecv fail munmap[%d]: %s\n", errno, msg_info_.name.c_str());
        }

        msg_info_.mem = nullptr;
        msg_info_.size = 0;
        conn_id_ = 0;
        ri_ = 0;
    }

    bool ReInit()
    {
        if (Init())
        {
            tried_cnt_ = 0;
            return true;
        }
        else if (tried_cnt_ < try_reconnect_cnt_)
        {
            tried_cnt_++;
        }
        else
        {
            std::this_thread::sleep_for(dura_);
        }
        std::cout << "ReInit tried cnt: " << tried_cnt_ << std::endl;
        return false;
    }

    bool Connect()
    {
        // If exceeds connection limits
        if (msg_header_ == nullptr)
        {
            return false;
        }
        std::uint32_t cc = msg_header_->conn.CurConn();
        if (cc + 1 == 0)
        {
            printf("MsgRecv exceed connection limit: %zd", msg_header_->conn.ConnCount());
            return false;
        }

        // TODO: thread-safe
        conn_id_ = msg_header_->conn.GetConnectId();
        ri_ = msg_header_->wi == 0 ? msg_header_->capacity - 1 : msg_header_->wi;

        return true;
    }

private:
    MsgHeader *msg_header_;
    Buffer *buffer_;
    MsgInfo msg_info_;

    // Indicates if this reader is connected to the writer
    bool isValid_ = false;

    // If shared memory is shutdown or the reader is disconnected,
    // try reconnect at most try_reconnect_cnt_ times.
    // Otherwise, sleep dura after every try to reduce CPU load.
    int try_reconnect_cnt_ = 10;
    int tried_cnt_ = 0;
    std::chrono::milliseconds dura_{100};
    std::uint32_t conn_id_ = 0; // Connection ID

    std::size_t ri_ = 0;
};

#endif
