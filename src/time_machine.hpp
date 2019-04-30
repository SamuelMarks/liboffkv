#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>



namespace time_machine {

class QueueClosed : public std::runtime_error {
public:
    QueueClosed()
        : std::runtime_error("Queue closed for Puts")
    {}
};


template<typename T, class Container = std::deque<T>>
class BlockingQueue {
public:
    // capacity == 0 means queue is unbounded
    explicit BlockingQueue()
    {}

    // throws QueueClosed exception after Close
    template <typename U>
    void put(U&& item)
    {
        std::unique_lock <std::mutex> lock(lock_);

        if (closed_) {
            throw QueueClosed();
        }

        items_.push_back(std::forward<U>(item));
        consumer_cv_.notify_one();
    }

    // returns false if queue is empty and closed
    bool get(T& item)
    {
        std::unique_lock<std::mutex> lock(lock_);

        while (!closed_ && isEmpty())
            consumer_cv_.wait(lock);

        if (isEmpty())
            return false;

        item = std::move(items_.front());
        items_.pop_front();

        return true;
    }

    bool get(std::vector<T>& out_items, size_t max_count, bool require_at_least_one)
    {
        if (!max_count)
            return true;

        std::unique_lock<std::mutex> lock(lock_);

        if (require_at_least_one) {
            while (!closed_ && isEmpty())
                consumer_cv_.wait(lock);

            if (isEmpty())
                return false;
        }

        size_t count = std::min(max_count, items_.size());
        for (size_t i = 0; i < count; ++i) {
            out_items.push_back(std::move(items_.front()));
            items_.pop_front();
        }

        return true;
    }

    void close()
    {
        std::unique_lock<std::mutex> lock(lock_);

        closed_ = true;
        consumer_cv_.notify_all();
    }

    bool isEmpty() const
    {
        return items_.empty();
    }

private:
    Container items_;
    bool closed_{false};
    std::mutex lock_;
    std::condition_variable consumer_cv_;
};


template<template<class> class Promise = std::promise,
         template<class> class Future  = std::future>
class TimeMachine {
public:
    explicit TimeMachine(size_t number_of_threads = 1,
                         size_t objects_per_thread = 10, unsigned long long wait_for_object_ms = 20)
        : objects_per_thread_(objects_per_thread), wait_for_object_ms_(wait_for_object_ms)
    {
        for (size_t i = 0; i < number_of_threads; ++i)
            run_thread_();
    }

    TimeMachine(const TimeMachine&) = delete;

    TimeMachine(TimeMachine&& machine)
        : queue_(std::move(machine.queue_)), state_(std::move(machine.state_)),
          objects_per_thread_(machine.objects_per_thread_),
          wait_for_object_ms_(machine.wait_for_object_ms_)
    {}

    TimeMachine& operator=(const TimeMachine&) = delete;

    TimeMachine& operator=(TimeMachine&& machine)
    {
        queue_ = std::move(machine.queue_);
        state_ = std::move(machine.state_);
        objects_per_thread_ = machine.objects_per_thread_;
        wait_for_object_ms_ = machine.wait_for_object_ms_;

        return *this;
    }


    template<typename T, class Function>
    Future<std::result_of_t<Function(Future<T>&&)>> then(Future<T>&& future, Function&& func)
    {
        auto promise = std::make_shared<Promise<std::result_of_t<Function(Future<T>&&)>>>();
        auto future_ptr = std::make_shared<Future<T>>(std::move(future));
        auto new_future = promise->get_future();

        queue_->put(std::make_pair(
            [future_ptr](std::chrono::milliseconds timeout_duration) {
                return future_ptr->wait_for(timeout_duration) == std::future_status::ready;
            },
            [future_ptr, promise = std::move(promise), func = std::forward<Function>(func)]() {
                try {
                    try {
                        if constexpr (std::is_same_v<void, std::result_of_t<Function(Future<T>&&)>>) {
                            func(std::move(*future_ptr));
                            promise->set_value();
                        } else {
                            auto to = func(std::move(*future_ptr));
                            promise-> set_value(std::move(to));
                        }
                    } catch (...) {
                        promise->set_exception(std::current_exception());
                    }
                } catch (...) {
                }
            }
        ));

        future_ptr.reset();

        return new_future;
    }


    ~TimeMachine()
    {
        queue_->close();
        while (!queue_->isEmpty())
            std::this_thread::yield();
        while (state_->load());
    }


private:
    using QueueData = std::pair<std::function<bool(std::chrono::milliseconds)>, std::function<void()>>;

    void run_thread_()
    {
        std::thread([this] {
            std::vector<QueueData> picked;
            state_->fetch_add(1);

            while (queue_->get(picked, objects_per_thread_ - picked.size(), picked.empty()))
                process_objects_(picked);

            state_->fetch_sub(1);
        }).detach();
    }

    void process_objects_(std::vector<QueueData>& picked) const
    {
        for (auto it = picked.begin(); it < picked.end(); ++it) {
            if (it->first(std::chrono::milliseconds(wait_for_object_ms_))) {
                it->second();
                picked.erase(it);
            }
        }
    }

private:
    std::unique_ptr<BlockingQueue<QueueData>> queue_ = std::make_unique<BlockingQueue<QueueData>>();
    std::unique_ptr<std::atomic<long long>> state_ = std::make_unique<std::atomic<long long>>(0);
    size_t objects_per_thread_;
    unsigned long long wait_for_object_ms_;
};

} // namespace time_machine
