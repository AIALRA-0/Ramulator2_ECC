#ifndef RAMULATOR_CONTROLLER_CONTROLLER_H
#define RAMULATOR_CONTROLLER_CONTROLLER_H

#include <vector>
#include <deque>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"
#include "dram/dram.h"
#include "dram_controller/scheduler.h"
#include "dram_controller/plugin.h"
#include "dram_controller/refresh.h"
#include "dram_controller/rowpolicy.h"


namespace Ramulator {

// 此类作为抽象基类，为实际的 DRAM 控制器实现提供统一接口，派生自 Clocked，支持基于时钟的行为（tick）
class IDRAMController : public Clocked<IDRAMController> {
  // 注册此控制器接口供框架识别与创建
  RAMULATOR_REGISTER_INTERFACE(IDRAMController, "Controller", "Memory Controller Interface");

  public:
    // 控制器所管理的 DRAM 模型对象
    IDRAM*  m_dram = nullptr;      
    // 请求调度器：决定哪个请求何时发送    
    IScheduler*   m_scheduler = nullptr;
    // 刷新管理器：处理定期刷新操作（如行刷新）
    IRefreshManager*   m_refresh = nullptr;
    // 行缓冲区策略：决定如何预充、保留、关闭行缓冲区
    IRowPolicy*   m_rowpolicy = nullptr;
    // 控制器插件列表，可插拔功能扩展（如统计、调试、预取等）
    std::vector<IControllerPlugin*> m_plugins;

    // 控制器对应的 channel ID（通道编号）
    int m_channel_id = -1;
  public:
    /**
     * @brief       Send a request to the memory controller.
     * 发送一个普通优先级的请求到内存控制器
     * 
     * @param    req        The request to be enqueued.
     * @return   true       Successful.
     * @return   false      Failed (e.g., buffer full).
     */
    virtual bool send(Request& req) = 0;

    /**
     * @brief       Send a high-priority request to the memory controller.
     * 发送一个高优先级的请求到内存控制器
     * 
     */
    virtual bool priority_send(Request& req) = 0;

    /**
     * @brief       Ticks the memory controller.
     * 模拟时钟推进时的行为（每个周期调用一次）
     * 
     */
    virtual void tick() = 0;
   
};

}       // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_CONTROLLER_H