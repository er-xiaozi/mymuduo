#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

class EventLoop;
/*
    Channel 理解为通道，封装了sockfd和其感兴趣的event，如EPOLLIN、EPOLLOUT 事件
    还绑定了poller返回的具体事件
*/

class Channel : noncopyable
{
public:
    // typedef std::function<void()> EventCallback;
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd); // 用了EventLoop的指针可以只在前面声明类，指针4字节
    ~Channel();

    // fd得到poller通知以后，处理事件的
    void handleEvent(Timestamp receiveTime); // 用了Timestamp类型的变量，要确定变量的大小

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); } // 出了这个对象以后，cb的局部对象不需要了，使用move把左值转换为右值
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void> &);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    //设置fd相应的事件状态
    void enableReading() {events_ |=kReadEvent; update();}
    void disableReading() {events_ &=~kReadEvent; update();}
    void enableWriting() {events_ |=kWriteEvent; update();}
    void disableWriting() {events_ &=~kWriteEvent; update();}
    void disableAll()  {events_ =kNoneEvent; update();}

    //返回fd当前事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent;}
    bool isWriting() const { return events_ & kWriteEvent;}
    bool isReading() const { return events_ & kReadEvent;}

    int index(){ return index_ ;}
    void set_index(int idx) { index_ = idx; }   //和视频返回类型不一样

    EventLoop * ownerLoop() { return loop_; }
    void remove();

private:

    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_; // 事件循环
    const int fd_;    // fd,Poller监听的对象
    int events_;      // 注册监听器感兴趣的事件
    int revents_;     // poller返回的具体发生的事件
    int index_;

    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为channel通道里面能够获知fd最终发送具体的事件revents_,所以它负责回调
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};