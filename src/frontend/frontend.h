#ifndef     RAMULATOR_FRONTEND_FRONTEND_H
#define     RAMULATOR_FRONTEND_FRONTEND_H

#include <vector>
#include <string>
#include <functional>

#include "base/base.h"
#include "memory_system/memory_system.h"


namespace Ramulator {

class IFrontEnd : public Clocked<IFrontEnd>, public TopLevel<IFrontEnd> {
  // 宏定义用于注册该接口类到 Ramulator 框架中，用于自动发现和使用
  RAMULATOR_REGISTER_INTERFACE(IFrontEnd, "Frontend", "The frontend that drives the simulation.");

  // 工厂类可以访问 IFrontEnd 的私有成员
  friend class Factory;

  protected:
    // 指向后端内存系统的指针，前端通过它与内存系统交互
    IMemorySystem* m_memory_system;

    // 指向后端内存系统的指针，前端通过它与内存系统交互
    uint m_clock_ratio = 1;

  public:
    // 连接前端到内存系统，并对所有子组件进行初始化
    virtual void connect_memory_system(IMemorySystem* memory_system) { 
      m_memory_system = memory_system; 
      m_impl->setup(this, memory_system);   // 设置自身组件
      for (auto component : m_components) {
        component->setup(this, memory_system);    // 设置所有子组件
      }
    };

    // 检查模拟是否结束，由子类实现
    virtual bool is_finished() = 0;

    // 模拟结束后的清理操作，收集统计信息
    virtual void finalize() { 
      for (auto component : m_components) {
        component->finalize();
      }
      
      // 输出统计信息
      YAML::Emitter emitter;
      emitter << YAML::BeginMap;
      m_impl->print_stats(emitter);
      emitter << YAML::EndMap;
      std::cout << emitter.c_str() << std::endl;
    };

    // 获取内核数，默认返回1，可由子类覆盖
    virtual int get_num_cores() { return 1; };

    // 获取前端的时钟与内存系统时钟的比率
    int get_clock_ratio() { return m_clock_ratio; };

    /**
     * @brief    Receives memory requests from external sources (e.g., coming from a full system simulator like GEM5)
     * 接收来自外部（如 GEM5）发来的内存请求
     * 
     * @details
     * This functions should take memory requests from external sources (e.g., coming from GEM5), generate Ramulator 2 Requests,
     * (tries to) send to the memory system, and return if this is successful
     * 子类可重写此方法，将外部请求封装成 Ramulator 的 Request 并发送到内存系统
     * 
     */
    virtual bool receive_external_requests(int req_type_id, Addr_t addr, int source_id, std::function<void(Request&)> callback) { return false; }
};

}        // namespace Ramulator


#endif   // RAMULATOR_FRONTEND_FRONTEND_H