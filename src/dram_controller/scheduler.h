#ifndef RAMULATOR_CONTROLLER_SCHEDULER_H
#define RAMULATOR_CONTROLLER_SCHEDULER_H

#include <vector>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"

namespace Ramulator {

// IScheduler 类是一个调度器接口，负责处理内存控制器中的请求调度
class IScheduler {
  RAMULATOR_REGISTER_INTERFACE(IScheduler, "Scheduler", "Memory Controller Request Scheduler");
  public:
    // 比较两个请求，返回一个迭代器
    virtual ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) = 0;

    // 获取缓冲区中最佳请求的迭代器
    virtual ReqBuffer::iterator get_best_request(ReqBuffer& buffer) = 0;
};

}       // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_RAMULATOR_CONTROLLER_SCHEDULER_H_H