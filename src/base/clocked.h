#ifndef     RAMULATOR_BASE_Clocked_H
#define     RAMULATOR_BASE_Clocked_H

#include <vector>
#include <string>

#include "base/type.h"

namespace Ramulator {

/**
 * @brief    CRTP interface for all clocked objects (i.e., can be ticked)
 * CRTP（Curiously Recurring Template Pattern）接口，用于所有基于时钟的对象（即可以“tick”的对象）
 * 这个类模板提供了一种接口，使得所有可以按时钟周期推进的对象都能使用此接口。
 * 它通过 CRTP 允许派生类直接访问模板类中的方法或成员变量。
 * 
 * @tparam   T 
 */
template<class T>
class Clocked {
  friend T;   // 允许派生类直接访问 Clocked 类中的成员

  protected:
    Clk_t m_clk = 0;    // 当前时钟周期，类型为 Clk_t（通常是整数或某种时间单位）

  public:
    virtual void tick() = 0;    // 纯虚函数，派生类必须实现此函数以响应每个时钟周期的行为

  public:
    // 默认构造函数
    Clocked() {};
};

}        // namespace Ramulator


#endif   // RAMULATOR_BASE_Clocked_H