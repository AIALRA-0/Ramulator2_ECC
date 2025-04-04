#ifndef     RAMULATOR_BASE_REQUEST_H
#define     RAMULATOR_BASE_REQUEST_H

#include <vector>
#include <list>
#include <string>

#include "base/base.h"

namespace Ramulator {

// 请求：表示一个内存访问请求，包含地址、类型、命令状态、来源、时间戳、回调等信息
struct Request { 
  Addr_t    addr = -1;    // 地址：内存请求的目标物理地址
  AddrVec_t addr_vec {};  // 地址向量：用于分层地址映射（如通道/排/列等）

  // 基本请求类型定义约定
  // 0 = 读请求 (Read), 1 = 写请求 (Write)，设备规范可以定义其他类型
  struct Type {
    enum : int {
      Read = 0, 
      Write,
      PartialWrite,
    };
  };

  int type_id = -1;    // 请求类型标识符（如读、写等）
  int source_id = -1;  // 请求来源标识符（例如来自哪个 CPU 核心）

  int command = -1;         // 当前需要发出的命令，以推动请求向前执行
  int final_command = -1;    // 完成该请求所需发出的最终命令
  bool is_stat_updated = false; // 内存控制器状态是否更新

  Clk_t arrive = -1;   // 请求到达内存控制器的时钟周期
  Clk_t depart = -1;   // 请求从内存控制器发出的时钟周期（完成时）

  std::array<int, 4> scratchpad = { 0 };    // 临时数据存储区：供控制器/插件使用的辅助空间

  std::function<void(Request&)> callback;   // 请求完成后的回调函数（通常用于通知前端）

  void* m_payload = nullptr;    // 指向任意数据结构的指针，可携带额外上下文（如CPU包等）

  Request(Addr_t addr, int type);     // 构造函数：根据地址和类型初始化请求
  Request(AddrVec_t addr_vec, int type);    // 构造函数：根据地址向量和类型初始化请求
  Request(Addr_t addr, int type, int source_id, std::function<void(Request&)> callback);    // 构造函数：包括来源和回调函数的初始化
};

// 请求缓冲区：封装了一个链表结构用于保存内存请求，支持基本操作如添加、删除等
struct ReqBuffer {
  std::list<Request> buffer;    // 实际请求队列，使用 std::list 便于中间插入和删除
  size_t max_size = 32;   // 队列最大长度限制，防止无限增长导致内存过载

  // 定义迭代器类型，便于对外使用 ReqBuffer::iterator 表示 list 的迭代器
  using iterator = std::list<Request>::iterator;

  // 返回缓冲区的起始迭代器（支持范围 for 循环、遍历等）
  iterator begin() { return buffer.begin(); };

  // 返回缓冲区的结束迭代器
  iterator end() { return buffer.end(); };

  // 返回当前缓冲区中请求的数量
  size_t size() const { return buffer.size(); }

  // 向缓冲区添加一个新请求，如果尚未超出最大容量
  bool enqueue(const Request& request) {
    if (buffer.size() <= max_size) {
      buffer.push_back(request);
      return true;
    } else {
      return false;
    }
  }

  // 移除指定位置的请求（通过迭代器指向的元素）
  void remove(iterator it) {
    buffer.erase(it);
  }
};

}        // namespace Ramulator


#endif   // RAMULATOR_BASE_REQUEST_H