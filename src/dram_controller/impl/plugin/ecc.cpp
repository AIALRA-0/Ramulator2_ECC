#include <vector>
#include <unordered_map>
#include <limits>
#include <random>
#include <filesystem>
#include <fstream>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator
{

  class ECCPlugin : public IControllerPlugin, public Implementation
  {
    RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, ECCPlugin, "ECCPlugin", "This plugin adds large-size ECC/EDC emulation to Ramulator2 to evaluate memory reliability, bandwidth, and latency trade-offs in AI and HPC workloads.")

  protected:
    IDRAM *m_dram = nullptr;
    IDRAMController *m_ctrl = nullptr;

  public:
    // 初始化插件参数
    void init() override
    {
      // 读取配置参数
      // 变量名称 = param<变量类型>("读取名称").desc("描述").default_val(默认值);

      // 注册运行时统计变量
      // register_stat(变量名称).name("统计输出名称");
    };

    // 设置插件与内存控制器、前端接口的连接
    void setup(IFrontEnd *frontend, IMemorySystem *memory_system) override
    {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;
    };

    // 每当有请求被调度时调用
    void update(bool request_found, ReqBuffer::iterator &req_it) override  // "base/request.h"类
    {
      if (request_found)
      {
        // 未找到命令，直接返回
        if (!request_found)
            return;

        // 只有在找到了要执行的请求时才进行 ECC

        // 处理写入请求（WRITE）
        if (req_it->type_id == Request::Type::Write)
        {
            // 1. 写入请求：存储控制器接收到写入请求（WRITE 命令），准备写入新的数据块，从并行高速总线获取数据
            
            // 获取命令目标地址
            Addr_t addr = req_it->addr;

            // 检查 payload 是否为空
            if (req_it->m_payload == nullptr)
            {
                std::cerr << "[ECCPlugin] Warning: Write request with null payload at addr 0x"
                  << std::hex << addr << std::dec << std::endl;
                return;
            } 

            

            // EDC 计算：为新数据块计算 EDC 值，并将其与数据一起存储

            // ECC 计算：为新数据块计算 ECC 码字，并将其存储在 HBM-ECC 区域

            // 写入数据块：新数据块被写入 HBM-Data 区域
        }

        // 处理局部写入请求（Partial WRITE）
        if (req_it->type_id == Request::Type::Write)
        {
            // 局部写入命令：仅更新修改过的数据块，使用增量 ECC 更新以减少计算负载

            // 读取数据块：读取目标数据块及其旧 EDC 和旧 EC

            // EDC 通过：如果 EDC 校验通过，更新后数据块直接写回，并计算新的 ECC。

            // EDC 失败：如果 EDC 校验失败，读取完整 ECC 码字，对旧数据进行错误更正，然后再写入新数据并存储回去。
            
            // 增量 ECC 更新：新 ECC 编码计算
        }

        // 处理读取请求（READ）
        if (req_it->type_id == Request::Type::Read)
        {
            // 数据块定位：存储控制器接收到读取请求后，定位目标数据块。HBM（高带宽内存）通常使用固定大小的数据块

            // EDC 校验：控制器读取数据块及其对应的 EDC（错误检测码），并执行 EDC 校验

            // EDC 失败：如果 EDC 校验失败，数据可能已损坏，触发 ECC（错误更正码）错误恢复过程

                // 读取 ECC 码字：存储控制器获取完整的 ECC 码字

                // 执行 ECC 更正：控制器使用 ECC 错误更正算法尝试修复数据

                // 更正成功：如果错误数量 ≤ t（纠错阈值），ECC 成功修复数据。修复后的数据通过并行高速总线返回，并将ECC写回内存

                // 更正失败：如果 ECC 无法修复数据，向控制器报告为不可更正错误（UE）
            
                    // 重试读取：当内存控制器遇到 UE 错误时，会尝试重新读取数据和 ECC 码字，进行第二次 ECC 更正尝试

                    // 冗余恢复：如果启用了 RAID 或备份机制，控制器会尝试从冗余来源恢复数据

                    // 错误报告：如果错误仍然无法更正，将其报告为不可更正错误（UE）给 CPU/处理器

            // EDC 通过：如果 EDC 校验通过，数据被验证为无误，并直接通过并行高速总线返回
        }
      }
    };

    // 在模拟结束时调用，可用于输出数据，清理数据等
    void finalize() override
    {
    }
  };

} // namespace Ramulator
