#include "EventLoopThread.h"
#include "EventLoop.h"



EventLoopThread::EventLoopThread(const ThreadInitCallback &cb, const std::string &name )
                                 : loop_(nullptr)
                                 , exiting_(false)
                                 , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
                                 , mutex_()
                                 , cond_()
                                 , callback_(cb)
{
}
EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}

EventLoop *EventLoopThread::startLoop()
{
    thread_.start(); //启动底层的线程

    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == nullptr)    // 等待loop创建完成后通知
        {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}

// 下面这个方法，是在新线程中运行的
void EventLoopThread::threadFunc()
{
    EventLoop loop; //创建一个新线程
    
    if (callback_)
    {
        callback_(&loop);
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    loop.loop(); //EventLoop loop => Poller.poll 
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}