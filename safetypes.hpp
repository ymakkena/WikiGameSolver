#ifndef SAFETYPES_H

#define SAFETYPES_H

#include <deque>
#include <unordered_set>
#include <mutex>
#include <functional>
#include <atomic>

#include "logging.hpp"

extern std::atomic<bool> searching;

template<typename T>
class SafeQueue {
  private:
    std::deque<T> queue;
    std::mutex queue_mutex;

    std::mutex cond_mutex;
    std::condition_variable cond;

  public:
    SafeQueue() {};
    ~SafeQueue() {};

    void push(const T& item) {
        std::lock_guard<std::mutex> lk(queue_mutex);
        queue.emplace_back(item);
        cond.notify_one();
    };

    void notify_all(void) {
        cond.notify_all();
    };

    unsigned size(void) {
        std::lock_guard<std::mutex> lk(queue_mutex);
        return queue.size();
    };

    T wait_for_element() {
        T front;
        if(!searching)
            return front;
        {
            std::lock_guard<std::mutex> queue_lock(queue_mutex);
            if(queue.size() > 0) {
                front = queue.front();
                queue.pop_front();
                return front;
            }
        }
        std::unique_lock<std::mutex> lk(cond_mutex);
        cond.wait(lk, [this, &front]{
            if(!searching)
                return true;
            std::lock_guard<std::mutex> inner_lock(queue_mutex);
            if(queue.size() > 0) {
                front = queue.front();
                queue.pop_front();
                return true;
            } else {
                return false;
            }
        });
        return front;
    };

};

template<typename T>
class SafeSet {
  private:
    std::unordered_set<T> set;
    std::mutex set_mutex;
  public:
    SafeSet() {};
    ~SafeSet() {};

    void add(const T& element) {
        std::lock_guard<std::mutex> lk(set_mutex);
        set.insert(element);
    }
    bool contains(const T& element) {
        std::lock_guard<std::mutex> lk(set_mutex);
        auto got = set.find(element);
        return got != set.end();
    }
    bool if_not_contains_add(const T& element) {
        std::lock_guard<std::mutex> lk(set_mutex);
        auto got = set.find(element);
        if(got == set.end()) {
            set.insert(element);
            return true;
        }
        return false;
    }
    unsigned size(void) {
        std::lock_guard<std::mutex> lk(set_mutex);
        return set.size();
    }
};

#endif // SAFETYPES_H
