#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <thread>

#include "msg_recv.hpp"
#include "msg_send.hpp"

const std::string name = "imu_msg";
constexpr char const mode_s__[] = "s";
constexpr char const mode_r__[] = "r";
std::chrono::milliseconds dura(500);
std::chrono::milliseconds dura2(100);

std::atomic<bool> is_quit__{false};

#pragma pack(push, 1)

struct Data1
{
    int a = 0;
    float b = 1;
    char c = 2;
};
struct Data
{
    Data1 d;
    int e;
};
#pragma pack(pop)

MsgSend<Data, 10> *msg_send = nullptr;

void DoSend()
{
    msg_send = new MsgSend<Data, 10>(name);

    std::cout << __func__ << ": start\n";
    Data a;
    a.e = 0;
    while (!is_quit__.load(std::memory_order_acquire))
    {
        if (msg_send->Pub(a))
        {
            std::cout << "Send: " << a.d.a << "," << a.e << std::endl;
            a.e++;
        }
        else
        {
            std::cout << "Send failed" << std::endl;
        }
        std::this_thread::sleep_for(dura);
    }
    delete msg_send;
    msg_send = nullptr;
}

void DoRecv()
{
    MsgRecv<Data> msg_recv(name);
    std::cout << __func__ << ": start\n";
    Data a;
    while (!is_quit__.load(std::memory_order_acquire))
    {
        if (msg_recv.Get(a, 100))
        {
            std::cout << "get data: " << a.d.a << "," << a.e << std::endl;
        }
        // else
        // {
        //     std::cout << "get data failed " << a.e << std::endl;
        // }
        std::this_thread::sleep_for(dura2);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 0;

    auto exit = [](int)
    {
        is_quit__.store(true, std::memory_order_release);
        if (msg_send != nullptr)
        {
            std::cout << "Shut down msg_send" << std::endl;
            msg_send->ShutDown();
        }
    };
    signal(SIGINT, exit);
    signal(SIGABRT, exit);
    signal(SIGSEGV, exit);
    signal(SIGTERM, exit);
    signal(SIGHUP, exit);

    std::thread send, recv;
    if (std::string{argv[1]} == mode_s__)
    {
        send = std::thread{DoSend};
    }
    else if (std::string{argv[1]} == mode_r__)
    {
        recv = std::thread{DoRecv};
    }

    while (!is_quit__.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(dura);
    }

    if (send.joinable())
    {
        std::cout << "join send" << std::endl;
        send.join();
    }
    if (recv.joinable())
    {
        std::cout << "join recv" << std::endl;
        recv.join();
    }

    return 0;
}
