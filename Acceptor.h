#pragma once
#include "noncopyable.h"
#include "Channel.h"
#include "Socket.h"
#include "InetAddress.h"

#include <functional>
class EventLoop;

class Acceptor : noncopyable
{

public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress &)>;
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    void setNewConnectionCallback(const NewConnectionCallback &cb)
    {
        newConnectionCallback_ = cb;
    }

    bool listenning() const { return listenning_; }
    void listen();

private:
    void handleRead();

    EventLoop *loop_;
    Socket acceptSocket_;
    Channel acceptChannel_;
    bool listenning_;
    NewConnectionCallback newConnectionCallback_;
};
