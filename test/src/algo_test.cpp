//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/model.hpp"
#include "sl/exec/thread/event.hpp"

#include <condition_variable>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

namespace sl::exec {

struct background_executor final : executor {
    background_executor()
        : t_{ [this] {
              while (auto maybe_task = worker_retrieve_task()) {
                  (*maybe_task)->execute();
              }
          } } {}

    ~background_executor() noexcept override {
        stop();
        t_.join();
    }

    void schedule(task_node* task_node) noexcept override {
        {
            std::unique_lock ul{ m_ };
            tq_.push_back(task_node);
        }
        cv_.notify_one();
    }

    void stop() noexcept override {
        std::unique_lock ul{ m_ };
        is_running_ = false;
        cv_.notify_one();
    }

private:
    tl::optional<task_node*> worker_retrieve_task() {
        std::unique_lock ul{ m_ };
        while (is_running_ && tq_.empty()) {
            cv_.wait(ul);
        }
        if (!is_running_) {
            return tl::nullopt;
        }
        return tq_.pop_front();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::thread t_;
    bool is_running_ = true;
    task_list tq_;
};

TEST(conn, valueSignal) {
    const tl::optional<meta::result<int, meta::unit>> maybe_result = //
        as_signal(meta::result<int, meta::unit>(42)) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, 42);
}

TEST(algo, andThen) {
    const tl::optional<meta::result<std::string, meta::unit>> maybe_result =
        as_signal(meta::result<int, meta::unit>(42)) //
        | and_then([](int i) { return meta::result<int, meta::unit>{ i + 1 }; }) //
        | and_then([](int i) { return meta::result<std::string, meta::unit>{ std::to_string(i) }; }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, "43");
}

TEST(algo, map) {
    const tl::optional<meta::result<std::string, meta::unit>> maybe_result = //
        as_signal(meta::result<int, meta::unit>(42)) //
        | map([](int i) { return i + 1; }) //
        | map([](int i) { return std::to_string(i); }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, "43");
}

TEST(algo, mapError) {
    const tl::optional<meta::result<meta::unit, std::string>> maybe_result = //
        as_signal(meta::result<meta::unit, int>(meta::err(42))) //
        | map_error([](int i) { return i + 1; }) //
        | map_error([](int i) { return std::to_string(i); }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, meta::err(std::string{ "43" }));
}

TEST(algo, orElse) {
    const tl::optional<meta::result<meta::unit, std::string>> maybe_result = //
        as_signal(meta::result<meta::unit, int>(meta::err(42))) //
        | or_else([](int i) { return meta::result<meta::unit, int>(meta::err(i + 1)); }) //
        | or_else([](int i) { return meta::result<meta::unit, std::string>(meta::err(std::to_string(i))); }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, meta::err(std::string{ "43" }));
}


TEST(algo, threadPool) {
    background_executor background_executor;
    auto scheduler = as_scheduler(background_executor);
    const tl::optional<meta::result<std::thread::id, meta::undefined>> maybe_result =
        scheduler.schedule() //
        | map([](meta::unit) { return std::this_thread::get_id(); }) //
        | get<atomic_event>();
    ASSERT_NE(*maybe_result, std::this_thread::get_id());
}

} // namespace sl::exec
