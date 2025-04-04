#include <filesystem>
#include <iostream>
#include <fstream>

#include "frontend/frontend.h"
#include "base/exception.h"

namespace Ramulator {

// 使用文件系统库（C++17 的标准库）
namespace fs = std::filesystem;

/**
 * @brief LoadStoreTrace 类实现了 IFrontEnd 接口，提供了加载和存储负载内存地址trace的功能。
 * 
 * 该类通过加载指定的trace文件，解析其中的每个内存访问请求（读或写），并在每个时钟周期中发送对应的内存请求。
 * 它适用于模拟负载/存储内存地址的应用场景。
 */
class LoadStoreTrace : public IFrontEnd, public Implementation {
  // 注册此实现，供框架自动识别和实例化
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, LoadStoreTrace, "LoadStoreTrace", "Load/Store memory address trace.")

  private:
    // 存储每个内存访问请求的结构体（读/写请求和地址）
    // is_write 为 true 时表示写命令（Write），为 false 时表示读命令（Read）
    struct Trace {
      bool is_write;  // 是否是写请求
      Addr_t addr;  // 请求的内存地址
    };
    std::vector<Trace> m_trace; // 存储内存访问请求的向量

    // trace的总长度和当前的trace索引
    size_t m_trace_length = 0;
    size_t m_curr_trace_idx = 0;

    // 总的请求数量
    size_t m_trace_count = 0;

    // 日志记录器，用于记录加载过程和调试信息
    Logger_t m_logger;

  public:
    /**
    * @brief 初始化 LoadStoreTrace 调度器
    * @details 该方法会加载trace文件，并初始化其他参数（如时钟比率）。
    *          它还会将文件中的数据加载到 `m_trace` 向量中，并为日志记录器配置输出。
    */
    void init() override {
      // 获取trace文件路径和时钟比率配置
      std::string trace_path_str = param<std::string>("path").desc("Path to the load store trace file.").required();
      m_clock_ratio = param<uint>("clock_ratio").required();

      // 创建日志记录器并记录加载过程
      m_logger = Logging::create_logger("LoadStoreTrace");
      m_logger->info("Loading trace file {} ...", trace_path_str);

      // 加载trace数据
      init_trace(trace_path_str);

      // 日志记录文件加载完成
      m_logger->info("Loaded {} lines.", m_trace.size());
    };

    /**
    * @brief 在每个时钟周期中发送一个内存请求
    * @details 该方法会根据当前的trace索引，选择当前的内存请求（读或写），并将其发送给内存系统。
    *          如果请求成功，当前trace索引将更新为下一个请求。
    */
    void tick() override {
      // 获取当前的内存访问请求
      const Trace& t = m_trace[m_curr_trace_idx];
      // 发送内存请求（如果是写请求或读请求）
      bool request_sent = m_memory_system->send({t.addr, t.is_write ? Request::Type::Write : Request::Type::Read});

      // 如果请求成功，更新当前请求索引，并增加请求计数
      if (request_sent) {
        m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length; // 循环回到开头
        m_trace_count++;  // 增加请求计数
      }
    };


  private:
    /**
     * @brief 加载内存地址trace文件并解析其中的请求
     * @details 该方法会打开指定路径的trace文件，解析每行数据并存储在 `m_trace` 向量中。
     *          文件的每一行包含一个内存访问请求，格式应为 "LD/ST 地址"，其中 "LD" 是加载（读），"ST" 是存储（写）。
     * 
     * @param file_path_str trace文件的路径
     */
    void init_trace(const std::string& file_path_str) {
      // 将文件路径转换为 `std::filesystem::path` 类型
      fs::path trace_path(file_path_str);

      // 检查文件是否存在
      if (!fs::exists(trace_path)) {
        throw ConfigurationError("Trace {} does not exist!", file_path_str);
      }

      // 按行读取文件内容
      std::ifstream trace_file(trace_path);
      if (!trace_file.is_open()) {
        throw ConfigurationError("Trace {} cannot be opened!", file_path_str);
      }
      
      // 参考格式 <操作类型> <地址1> [地址2, 地址3, ...]
      std::string line;
  
      while (std::getline(trace_file, line)) {
        std::vector<std::string> tokens;
        tokenize(tokens, line, " ");  // 分割每一行的空格

        // TODO: Add line number here for better error messages

        // 如果一行没有两个部分，则格式无效
        if (tokens.size() != 2) {
          throw ConfigurationError("Trace {} format invalid!", file_path_str);
        }

        // 解析请求类型（LD 或 ST）
        bool is_write = false; 
        if (tokens[0] == "LD") {
          is_write = false;   // 读取请求
        } else if (tokens[0] == "ST") {
          is_write = true;   // 写入请求
        } else {
          throw ConfigurationError("Trace {} format invalid!", file_path_str);
        }

        // 解析地址（16进制或十进制）
        Addr_t addr = -1;
        if (tokens[1].compare(0, 2, "0x") == 0 | tokens[1].compare(0, 2, "0X") == 0) {
          addr = std::stoll(tokens[1].substr(2), nullptr, 16);  // 转换为16进制地址
        } else {
          addr = std::stoll(tokens[1]); // 转换为十进制地址
        }

        // 将解析的请求添加到 `m_trace` 中
        m_trace.push_back({is_write, addr});
      }

      // 关闭文件
      trace_file.close();

      // 记录trace数据的长度
      m_trace_length = m_trace.size();
    };

    // TODO: FIXME
    /**
    * @brief 检查模拟是否已经完成
    * @details 该方法检查当前的trace请求计数是否已经达到trace文件中的请求总数。如果完成，返回 `true`。
    * 
    * @return 是否完成处理
    */
    bool is_finished() override {
      return m_trace_count >= m_trace_length;   // 如果请求计数大于等于总请求数，则完成
    };
};

}        // namespace Ramulator