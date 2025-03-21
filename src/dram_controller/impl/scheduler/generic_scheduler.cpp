#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/scheduler.h"

namespace Ramulator {

/**
 * @brief FRFCFS 类实现了 DRAM 控制器中的调度算法。
 * 
 * 该类实现了 `IScheduler` 接口，采用了 `First Ready, First Come First Serve (FRFCFS)` 策略来调度内存请求。
 * 首先，它会根据请求的准备状态（是否可以被服务）选择请求；如果请求都已准备好，它会按照到达时间进行排序（FCFS）。
 */
class FRFCFS : public IScheduler, public Implementation {
  // 注册此调度器实现，供框架自动识别和实例化
  RAMULATOR_REGISTER_IMPLEMENTATION(IScheduler, FRFCFS, "FRFCFS", "FRFCFS DRAM Scheduler.")
  private:
    // 指向 DRAM 模型的指针，用于检查请求的准备情况
    IDRAM* m_dram;

  public:
    /**
    * @brief 初始化调度器
    * @details 该方法在调度器初始化时调用。此处不进行任何操作。
    */
    void init() override { };

    /**
    * @brief 设置调度器，与前端和内存系统建立连接
    * @details 该方法将调度器与内存系统和前端连接，获取 DRAM 接口。
    * 
    * @param frontend 前端接口，用于与模拟的其他组件交互
    * @param memory_system 内存系统接口，用于访问底层 DRAM 模型
    */
    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = cast_parent<IDRAMController>()->m_dram;
    };

    /**
    * @brief 比较两个请求，选择优先调度的请求
    * 
    * 该方法首先检查两个请求的准备状态（是否可以服务）。如果只有一个请求准备好，则选择它。如果两个请求都准备好，
    * 则使用到达时间进行比较，选择最早到达的请求。
    * 
    * @param req1 请求 1 的迭代器
    * @param req2 请求 2 的迭代器
    * @return 返回优先调度的请求的迭代器
    */
    ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) override {
      // 检查两个请求是否准备好
      bool ready1 = m_dram->check_ready(req1->command, req1->addr_vec);
      bool ready2 = m_dram->check_ready(req2->command, req2->addr_vec);
      
      // 如果一个请求准备好，另一个没有准备好，选择准备好的请求
      if (ready1 ^ ready2) {
        if (ready1) {
          return req1;
        } else {
          return req2;
        }
      }

      // 如果两个请求都准备好了，使用 FCFS 策略，根据到达时间进行选择
      if (req1->arrive <= req2->arrive) {
        return req1;
      } else {
        return req2;
      } 
    }

    /**
    * @brief 获取缓冲区中最佳请求
    * 
    * 该方法遍历缓冲区中的请求，并选择最佳请求。首先，它为每个请求生成预请求命令，并使用 `compare` 方法选择最佳请求。
    * 
    * @param buffer 存储请求的缓冲区（如读缓冲区、写缓冲区等）
    * @return 返回最佳请求的迭代器
    */
    ReqBuffer::iterator get_best_request(ReqBuffer& buffer) override {
      // 如果缓冲区为空，返回 end 迭代器
      if (buffer.size() == 0) {
        return buffer.end();
      }

      // 为缓冲区中的每个请求生成预请求命令，将请求的最终命令转换成一个实际的命令
      for (auto& req : buffer) {
        // 内存访问操作（如读或写）需要先经过一定的处理或转换，才能转化为最终的硬件命令
        req.command = m_dram->get_preq_command(req.final_command, req.addr_vec);
      }

      // 默认选择缓冲区中的第一个请求作为候选
      auto candidate = buffer.begin();
      // 遍历缓冲区中的其他请求，使用 compare 方法选择最佳请求
      for (auto next = std::next(buffer.begin(), 1); next != buffer.end(); next++) {
        candidate = compare(candidate, next);
      }
      // 返回最佳请求的迭代器
      return candidate;
    }
};

}       // namespace Ramulator
