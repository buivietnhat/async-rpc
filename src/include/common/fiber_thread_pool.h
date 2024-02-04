#pragma once

#include <utility>

#include "common/fiber_thread_manager.h"

class FiberThreadPool {
 public:
  template <typename T>
  using Future = boost::fibers::future<T>;

  FiberThreadPool(int num_worker = DEFAULT_NUM_THREAD) : ftm_(num_worker) {}

  template <typename Func>
  auto Submit(Func &&func) const -> Future<decltype(func())> {
    auto task = boost::fibers::packaged_task<decltype(func())()>(std::forward<Func>(func));
    auto fut = task.get_future();
    boost::fibers::fiber([t = std::move(task)]() mutable { t(); }).detach();
    return fut;
  }

 private:
  FiberThreadManager ftm_;
  static constexpr int DEFAULT_NUM_THREAD = 4;
};