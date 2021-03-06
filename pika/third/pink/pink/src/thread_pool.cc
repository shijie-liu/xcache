// Copyright (c) 2018-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <sstream>

#include "pink/include/thread_pool.h"
#include "pink/src/pink_thread_name.h"

namespace pink {

void* ThreadPool::Worker::WorkerMain(void* arg) {
  ThreadPool* tp = static_cast<ThreadPool*>(arg);
  tp->runInThread();
  return nullptr;
}

int ThreadPool::Worker::start() {
  if (!start_.load()) {
    if (pthread_create(&thread_id_, NULL, &WorkerMain, thread_pool_)) {
      return -1;
    } else {
      if (!worker_name_.empty()) {
        SetThreadName(thread_id_, worker_name_);
      }
      start_.store(true);
    }
  }
  return 0;
}

int ThreadPool::Worker::stop() {
  if (start_.load()) {
    if (pthread_join(thread_id_, nullptr)) {
      return -1;
    } else {
      start_.store(false);
    }
  }
  return 0;
}

ThreadPool::ThreadPool(size_t worker_num,
                       size_t max_queue_size,
                       const std::string& thread_pool_name) :
  worker_num_(worker_num),
  max_queue_size_(max_queue_size),
  thread_pool_name_(thread_pool_name),
  running_(false),
  should_stop_(false),
  rsignal_(&mu_),
  wsignal_(&mu_) {}

ThreadPool::~ThreadPool() {
  stop_thread_pool();
}

int ThreadPool::start_thread_pool() {
  if (!running_.load()) {
    should_stop_.store(false);
    for (size_t i = 0; i < worker_num_; ++i) {
      std::stringstream thread_name;
      thread_name << thread_pool_name_ << i;
      workers_.push_back(new Worker(this, thread_name.str()));
      int res = workers_[i]->start();
      if (res != 0) {
        return kCreateThreadError;
      }
    }
    running_.store(true);
  }
  return kSuccess;
}

int ThreadPool::stop_thread_pool() {
  int res = 0;
  if (running_.load()) {
    should_stop_.store(true);
    rsignal_.SignalAll();
    wsignal_.SignalAll();
    for (const auto worker : workers_) {
      res = worker->stop();
      if (res != 0) {
        break;
      } else {
        delete worker;
      }
    }
    workers_.clear();
    running_.store(false);
  }
  return res;
}

bool ThreadPool::should_stop() {
  return should_stop_.load();
}

void ThreadPool::set_should_stop() {
  should_stop_.store(true);
}

void ThreadPool::Schedule(TaskFunc func, void* arg) {
  mu_.Lock();
  while (queue_.size() >= max_queue_size_ && !should_stop()) {
    wsignal_.Wait();
  }
  if (!should_stop()) {
    queue_.push(Task(func, arg));
    rsignal_.SignalAll();
  }
  mu_.Unlock();
}

size_t ThreadPool::queue_size() {
  return max_queue_size_;
}

void ThreadPool::runInThread() {
  while (!should_stop()) {
    mu_.Lock();
    while (queue_.empty() && !should_stop()) {
      rsignal_.Wait();
    }
    if (should_stop()) {
      mu_.Unlock();
      break;
    }
    if (!queue_.empty()) {
      TaskFunc func = queue_.front().func;
      void* arg = queue_.front().arg;
      queue_.pop();
      wsignal_.SignalAll();
      mu_.Unlock();
      (*func)(arg);
    }
  }
}
}  // namespace pink
