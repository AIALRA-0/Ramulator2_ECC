#ifndef RAMULATOR_DRAM_DRAM_H
#define RAMULATOR_DRAM_DRAM_H

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "base/base.h"
#include "dram/spec.h"
#include "dram/node.h"

namespace Ramulator {

 /**
 * @class IDRAM
 * @brief 抽象基类，用于定义所有 DRAM 模型的接口和基础结构。
 * 
 * IDRAM 类提供了对 DRAM 设备的统一接口，包括组织结构定义、命令处理、请求翻译以及未来操作的管理。
 * 所有具体的 DRAM 模型（如 HBM3、DDR4 等）都应继承自该类并实现其接口。
 */
class IDRAM : public Clocked<IDRAM> {
  RAMULATOR_REGISTER_INTERFACE(IDRAM, "DRAM", "DRAM Device Model Interface")

  /************************************************
   *                Organization
   ***********************************************/   
  public:
    int m_internal_prefetch_size = -1;  // 内部预取大小 (xn)，如 DDR4 为 8n 预取 Internal prefetch (xn) size: How many columns are fetched into the I/O? e.g., DDR4 has 8n prefetch.
    SpecDef m_levels;                   // 内存层次结构定义 (如：channel, rank, bank, row, column) Definition (i.e., names and ids) of the levels in the hierarchy
    Organization m_organization;        // 内存设备的基本组织结构 (如密度、数据位宽、各层次的数量) The organization of the device (density, dq, levels)
    int m_channel_width = -1;           // 通道宽度 (单位：位)，例如 64 或 128 位 Channel width (should be set by the implementation's config)


  /************************************************
   *             Requests & Commands
   ***********************************************/
  public:
    SpecDef m_commands;                                   // 所有支持的 DRAM 命令定义 (如：ACT, PRE, RD, WR) The definition of all DRAM commands
    SpecLUT<Level_t> m_command_scopes{m_commands};        // 命令作用域查找表，用于确定命令的作用范围 (如：行、列、bank) A LUT of the scopes (i.e., at which organization level) of the DRAM commands
    SpecLUT<DRAMCommandMeta> m_command_meta{m_commands};  // 命令元数据查找表，定义命令的属性 (如是否打开或关闭行) A LUT to check which DRAM command opens a row 

    SpecDef m_requests;                                     // 所有支持的请求类型定义 (如：read, write, refresh) The definition of all requests supported
    SpecLUT<Command_t> m_request_translations{m_requests};  // 请求与命令的映射表 (如：read -> RD, write -> WR) A LUT of the final DRAM commands needed by every request

    // TODO: make this a priority queue
    std::vector<FutureAction> m_future_actions;  // 存储未来的操作，需改进为优先队列 (如延迟执行的写操作) A vector of requests that requires future state changes

  /************************************************
   *                Node States
   ***********************************************/
  public:
    SpecDef m_states; // 内存状态定义 (如：Opened, Closed, Refreshing)
    SpecLUT<State_t> m_init_states{m_states}; // 初始状态查找表，定义每个节点的初始状态 (如：bank 初始为 Closed)


  /************************************************
   *                   Timing
   ***********************************************/
  public:
    SpecDef m_timings;                      // 定义所有时序约束的名称 (如：tRCD, tRP, tRAS, tRC 等) The names of the timing constraints
    SpecLUT<int> m_timing_vals{m_timings};  // 时序约束值查找表，每个时序约束名称对应一个整数值 (单位：时钟周期) The LUT of the values for each timing constraints

    TimingCons m_timing_cons;           // Ramulator 用于执行内存时序限制的结构体，包括所有定义的时序约束 The actual timing constraints used by Ramulator's DRAM model

    Clk_t m_read_latency = -1;          // 读取延迟 (单位：时钟周期)，从发出 RD 命令到数据被接收到的时间 Number of cycles needed between issuing RD command and receiving data.

  /***********************************************
   *                   Power
   ***********************************************/
  public:
    bool m_drampower_enable = false;             // 是否启用 DRAM 功率模型 (用于能耗模拟) Whether to enable DRAM power model

    std::vector<PowerStats> m_power_stats;      // 功率统计数据，每个通道 (channel) 和每个 rank 各自独立统计 The power stats and counters PER channel PER rank (ch0rank0, ch0rank1... ch1rank0,...)
    SpecDef m_voltages;                         // 电压定义 (如：VDD, VDDQ 等) The names of the voltage constraints
    SpecLUT<double> m_voltage_vals{m_voltages}; // 电压查找表，定义每种电压的具体值 (单位：伏特) The LUT of the values for each voltage constraints
    SpecDef m_currents;                         // 电流定义 (如：IDD0, IDD4R 等) The names of the current constraints
    SpecLUT<double> m_current_vals{m_currents}; // 电流查找表，定义每种电流的具体值 (单位：安培) The LUT of the values for each current constraints
    SpecDef m_cmds_counted; // 被计数的命令集 (用于功率统计和优化)

    bool m_power_debug = false; // 是否启用功率调试模式 (用于打印和调试功率统计信息)

    double s_total_background_energy = 0; // 设备总的背景能耗 (在空闲或刷新期间的能量消耗) Total background energy consumed by the device
    double s_total_cmd_energy = 0;        // 设备总的命令能耗 (每次操作所消耗的能量) Total command energy consumed by the device
    double s_total_energy = 0;            // 设备总的能耗 (包含背景能耗和命令能耗) Total energy consumed by the device

  /************************************************
   *          Device Behavior Interface
   ***********************************************/   
  public:
    /**
     * @brief   Issues a command with its address to the device.
     * @details
     * 发出给定的命令 (command) 及其地址 (addr_vec) 到内存设备中。此函数应更新设备层次结构中涉及节点的状态
     * 及其时序信息。例如，发送一个 'RD' 命令后，行缓冲区状态应该被标记为已打开 (Opened)，并更新读取延迟 (m_read_latency)。
     * 
     * @param command 需要发出的命令 (如：RD, WR, ACT 等)
     * @param addr_vec 地址向量，描述命令操作的内存层次结构 (如：channel, rank, bank, row, column)
     */
    virtual void issue_command(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief    Returns the prerequisite command of the given command and address.
     * @details  
     * 返回给定命令 (command) 和地址 (addr_vec) 所需的前置命令。例如，当发出 'RD' 命令时，必须确认目标
     * 行已经被激活 (ACT)。如果没有，则返回一个 'ACT' 命令作为前置命令。
     * 
     * @param command 当前请求的命令 (如：RD, WR, PRE 等)
     * @param addr_vec 地址向量，用于定位内存的层次结构 (如：channel, rank, bank, row, column)
     * @return int 前置命令的 ID (例如：ACT 的 ID)
     */
    virtual int get_preq_command(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     Checks whether the device is ready to accept the given command.
     * @details
     * 检查设备当前是否可以接受给定的命令 (command)。例如，当要发送 'WR' 命令时，必须确保对应的行已经被激活，
     * 且没有违反时序约束 (如：tRCD、tWR 等)。返回 true 表示命令可以执行，false 表示设备尚未准备好。
     * 
     * @param command 要检查的命令 (如：RD, WR, ACT 等)
     * @param addr_vec 地址向量，描述内存层次结构的目标位置 (如：channel, rank, bank, row, column)
     * @return bool 若设备可以接受该命令，返回 true；否则返回 false。
     */
    virtual bool check_ready(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     Checks whether the command will result in a rowbuffer hit.
     * @details
     * 检查给定的命令 (command) 是否会命中已经打开的行缓冲区 (Row Buffer)。如果命中，则可以避免发出额外的 
     * 'ACT' 命令，从而降低访问延迟。通常在 'RD' 和 'WR' 操作中进行检查。
     * 
     * @param command 要检查的命令 (如：RD, WR)
     * @param addr_vec 地址向量，描述命令操作的内存层次结构 (如：channel, rank, bank, row, column)
     * @return bool 若命中已打开的行缓冲区，返回 true；否则返回 false。
     */
    virtual bool check_rowbuffer_hit(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     Checks whether the node corresponding to the address is open.
     * @details
     * 检查与给定地址 (addr_vec) 对应的节点 (如：bank, row) 是否处于打开状态 (Opened)。
     * 这通常用于决定是否需要发出 'ACT' 命令，或者可以直接进行读取或写入操作。
     * 
     * @param command 当前请求的命令 (如：RD, WR, ACT)
     * @param addr_vec 地址向量，定位内存层次结构的目标位置 (如：channel, rank, bank, row, column)
     * @return bool 如果节点已打开，返回 true；否则返回 false。
     */
    virtual bool check_node_open(int command, const AddrVec_t& addr_vec) = 0;

    /**
     * @brief     An universal interface for the host to change DRAM configurations on the fly.
     * @details
     * 提供一个通用的接口，用于在运行时动态修改 DRAM 配置 (例如：更改刷新模式或电源管理策略)。
     * 此接口接受一个键值对，表示配置项的名称和新值。由于此操作开销较大，不应频繁调用。
     * TODO: Alternatively, we can keep adding new functionalities to this DRAM interface...
     * 
     * @param key 配置项的名称 (例如：refresh_mode, power_mode)
     * @param value 配置项的新值 (通常是整数或枚举类型)
     */
    virtual void notify(std::string_view key, uint64_t value) {};

    /**
     * @brief     Finalizes the DRAM device state and reports statistics.
     * @details
     * 在模拟结束时调用的接口，用于计算和报告各项统计数据 (如：总延迟、命中率、功耗)。
     * 可以在此函数中完成资源清理或最终的状态输出。
     */
    virtual void finalize() {};

  /************************************************
   *        Interface to Query Device Spec
   ***********************************************/   
  /**
   * @brief 获取指定层次结构的大小 (如：通道数、bank 数、row 数等)。
   * @details
   * 给定一个层次结构的名称 (如："channel", "bank", "row")，返回该层次的大小 (即：有多少个这样的单元)。
   * 例如，如果查询 "bank"，则返回每个 channel 下有多少个 bank。
   * 如果提供的名称不存在于定义的层次结构中，则返回 -1。
   * 
   * @param name 要查询的层次结构名称 (如："channel", "bank", "row", "column")
   * @return int 如果查询成功，返回相应层次结构的大小；如果失败，返回 -1。
   */
  public:
    int get_level_size(std::string name) {
      try {
        int level_idx = m_levels(name);   // 获取层次结构的索引位置
        return m_organization.count[level_idx]; // 返回对应的大小值
      } catch (const std::out_of_range& e) {  // 如果提供的名称无效，返回 -1
        return -1;
      }
    }
};

/**
 * @brief 宏定义：用于声明并初始化所有与内存设备规格 (Spec) 相关的变量。
 * @details
 * 这个宏用于在具体的 DRAM 类中初始化其继承的所有规范定义，包括：
 *  - 内部预取大小 (m_internal_prefetch_size)
 *  - 层次结构定义 (m_levels)
 *  - 命令定义与元数据 (m_commands, m_command_scopes, m_command_meta)
 *  - 请求与其对应的命令映射 (m_requests, m_request_translations)
 *  - 状态定义与初始状态 (m_states, m_init_states)
 *  - 时序定义 (m_timings)
 *  - 电压与电流定义 (m_voltages, m_currents)
 * 
 * 使用这个宏可以确保在不同的 DRAM 模型中一致性地设置所有必要的规范定义。
 */
#define RAMULATOR_DECLARE_SPECS() \
  IDRAM::m_internal_prefetch_size = m_internal_prefetch_size; \
  IDRAM::m_levels = m_levels; \
  IDRAM::m_commands = m_commands; \
  IDRAM::m_command_scopes = m_command_scopes; \
  IDRAM::m_command_meta = m_command_meta; \
  IDRAM::m_requests = m_requests; \
  IDRAM::m_request_translations = m_request_translations; \
  IDRAM::m_states = m_states; \
  IDRAM::m_init_states = m_init_states; \
  IDRAM::m_timings = m_timings; \
  IDRAM::m_voltages = m_voltages; \
  IDRAM::m_currents = m_currents; \

}        // namespace Ramulator

#endif   // RAMULATOR_DRAM_DRAM_H

