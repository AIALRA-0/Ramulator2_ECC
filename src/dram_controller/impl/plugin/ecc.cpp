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
    void update(bool request_found, ReqBuffer::iterator &req_it) override
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

        }

        // 处理读取请求（READ）
        if (req_it->type_id == Request::Type::Read)
        {
            // 数据块定位

            // EDC 校验

            // EDC 失败

            // 读取 ECC 码字

            // 执行 ECC 更正

            // 更正成功

            // EDC 通过
        }
      }
    };

    // 在模拟结束时调用，可用于输出数据，清理数据等
    void finalize() override
    {
    }
  };

} // namespace Ramulator
