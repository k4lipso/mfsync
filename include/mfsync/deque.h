#pragma once

#include <deque>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <chrono>

namespace mfsync::concurrent
{

using namespace std::chrono_literals;

template<typename T>
class deque
{

public:
  deque() = default;

  std::optional<T> try_pop()
  {
    std::scoped_lock lk{mutex_};

    if(deque_.empty())
    {
      return std::nullopt;
    }

    auto result = std::make_optional(std::move(*deque_.front.get()));
    deque_.pop();

    return result;
  }

  std::optional<T> wait_for_and_pop(int dur = 1)
  {
    std::scoped_lock lk(mutex_);
    if(condition_.wait_for(lk, dur*1ms, [this] { return !deque_.empty(); }))
    {
      auto result = std::move(deque_.front());
      deque_.pop();
      return result;
    }

    return std::nullopt;
  }

  template<typename Pred>
  bool contains(Pred pred) const
  {
    std::scoped_lock lk{mutex_};

    if(std::any_of(deque_.begin(), deque_.end(), pred))
    {
      return true;
    }

    return false;
  }

  bool empty() const
  {
    std::scoped_lock lk(mutex_);
    return deque_.empty();
  }

  void push_back(T new_value)
  {
    std::scoped_lock lk(mutex_);
    deque_.push_back(std::move(new_value));
    condition_.notify_one();
  }

  void clear()
  {
    std::scoped_lock lk(mutex_);
    deque_.clear();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<T> deque_;
};

} // closing namespace mfsync::concurrent
