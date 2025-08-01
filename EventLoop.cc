#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>

// 防止一个线程创建多个EventLoop
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口超时时间
const int kPollTimsMs = 10000;

// 创建wakeufd, 用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
{ 
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if(t_loopInThisThread)
    {
        LOG_FATAL("Another Event %p exits in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    } 

    //设置wakeupfd_ 的时间类型以及事件发生后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupchannel的POLLIN事件了
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();   //对所有的事件都不感兴趣
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::handleRead()
{
    uint64_t one =1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if(n!=sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %zd bytes instead of 8", n);
    }
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start loop \n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        //监听两类fd   一种是clientfd  一种是wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimsMs, &activeChannels_);//发生事件的channel activeChannels_
        for (Channel *channel : activeChannels_)
        {   
            // Poller 监听哪些channel发生事件了，然后上报给eventloop,通知channl处理事件
            channel->handleEvent(pollReturnTime_);
        }
        //执行当前Eventloop事件循环需要处理的回调操作
        // wakeup subloop后执行mainloop注册的回调cb
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}

// 退出事件循环     1. loop在自己的线程中调用quit 2.在非loop线程中调用quit
void EventLoop::quit()
{
    quit_ = true;

    if(!isInLoopThread())   // 2. 在其他线程中
    {
        wakeup();           //把其他线程唤醒，他会自动quit
    }
}   

// 在当前loop中执行
void EventLoop::runInLoop(Functor cb)
{
    if(isInLoopThread())
    {
        cb();
    }
    else    //在非当前loop中执行cb
    {
        queueInLoop(cb);
    }
}


// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);  //直接构造
    }

    // 唤醒相应的执行线程 callingPendingFunctors_表示当前loop正在执行回调，但是又来了新的回调
    if(!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();   //唤醒loop所在线程
    }
}

// 唤醒loop所在的线程   向wakeupfd写一个数据
void EventLoop::wakeup()
{
    uint64_t one = 1;   //写啥无所谓
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
    }
} 

// EventLoop的方法=》 Plooer的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}
bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;  // 使用一个vector放置了所有需要执行的回调
    callingPendingFunctors_ = true;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 把pendingFunctors_中的回调放入局部vector, 并发操作，快速释放锁，方便继续往pendingFunctors_写
        functors.swap(pendingFunctors_);    
    }

    for (const Functor &functor :functors)
    {
        functor();  //执行当前loop需要执行的回调
    }

    callingPendingFunctors_ =false;
}