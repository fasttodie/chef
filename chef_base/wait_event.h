#ifndef _CHEF_BASE_WAIT_EVENT_H_
#define _CHEF_BASE_WAIT_EVENT_H_

#include <boost/noncopyable.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/chrono.hpp>

namespace chef
{

class wait_event : public boost::noncopyable
{
public:
    wait_event() :
        succ_(false)
    {}

    void notify()
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        succ_ = true;
        lock.unlock();
        cond_.notify_one();
    }

    void wait()
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        while (!succ_) {
            cond_.wait(lock);
        }
    }
    
    /**
     * @ return:
     *     true  : notify succ
     *     false : timeout
     */
    bool wait_for(uint32_t timeout_ms)
    {
        boost::unique_lock<boost::mutex> lock(mutex_);
        while (!succ_) {
            if (cond_.wait_for(lock, boost::chrono::milliseconds(timeout_ms)) ==
                    boost::cv_status::timeout) {
                return false;
            }
        }
        return true;
    }
private:
    boost::mutex mutex_;
    boost::condition_variable cond_;
    bool succ_;
};

} /// namespace chef

#endif

