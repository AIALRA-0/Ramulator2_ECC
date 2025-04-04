#ifndef RAMULATOR_DRAM_NODE_H
#define RAMULATOR_DRAM_NODE_H

#include <vector>
#include <map>
#include <deque>
#include <functional>
#include <concepts>

#include "base/type.h"
#include "dram/spec.h"

namespace Ramulator {

class IDRAM;

template<typename T>
concept IsDRAMSpec = requires(T t) { 
  typename T::Node; 
};

// CRTP class defnition is not complete, so we cannot have something nice like:
// template<typename T>
// concept IsDRAMSpec = std::is_base_of_v<IDRAM, T> && requires(T t) { 
//   typename T::Node; 
// };


/**
 * @brief     CRTP-ish (?) base class of a DRAM Device Node
 * 
 */
template<IsDRAMSpec T>
struct DRAMNodeBase {
    using NodeType = typename T::Node;
    NodeType* m_parent_node = nullptr;
    std::vector<NodeType*> m_child_nodes;

    T* m_spec = nullptr;

    int m_level = -1;      // The level of this node in the organization hierarchy
    int m_node_id = -1;    // The id of this node at this level
    int m_size = -1;       // The size of the node (e.g., how many rows in a bank)

    int m_state = -1;      // The state of the node

    std::vector<Clk_t> m_cmd_ready_clk;             // The next cycle that each command can be issued again at this level
    std::vector<std::deque<Clk_t>> m_cmd_history;   // Issue-history of each command at this level

    using RowId_t = int;
    using RowState_t = int;
    std::map<RowId_t, RowState_t> m_row_state;  // The state of the rows, if I am a bank-ish node

    DRAMNodeBase(T* spec, NodeType* parent, int level, int id):
    m_spec(spec), m_parent_node(parent), m_level(level), m_node_id(id) {
      int num_cmds = T::m_commands.size();
      m_cmd_ready_clk.resize(num_cmds, -1);
      m_cmd_history.resize(num_cmds);
      for (int cmd = 0; cmd < num_cmds; cmd++) {
        int window = 0;
        for (const auto& t : spec->m_timing_cons[level][cmd]) {
          window = std::max(window, t.window);
        }
        if (window != 0) {
          m_cmd_history[cmd].resize(window, -1);
        } else {
          m_cmd_history[cmd].clear();
        }
      }

      m_state = spec->m_init_states[m_level];

      // Recursively construct next levels
      int next_level = level + 1;
      int last_level = T::m_levels["row"];
      if (next_level == last_level) {
        return;
      } else {
        int next_level_size = m_spec->m_organization.count[next_level];
        if (next_level_size == 0) {
          return;
        } else {
          for (int i = 0; i < next_level_size; i++) {
            NodeType* child = new NodeType(spec, static_cast<NodeType*>(this), next_level, i);
            static_cast<NodeType*>(this)->m_child_nodes.push_back(child);
          }
        }
      }
    };

    /**
     * @brief 更新内存节点的状态 (State Machine)。
     * @details
     * 在接收到一个命令 (command) 时，更新当前节点 (Node) 及其子节点的状态。
     * 每个节点的状态更新逻辑由 `m_actions` 提供的函数指针来实现。
     * 状态更新的过程可以递归地应用于层次结构中的所有相关节点。
     * 
     * @param command 要执行的命令 (如：ACT, WR, RD 等)。
     * @param addr_vec 地址向量，描述命令操作的内存层次结构 (如：channel, bank, row, column)。
     * @param clk 当前时钟周期 (用于记录或判断命令的执行时间)。
     */
    void update_states(int command, const AddrVec_t& addr_vec, Clk_t clk) {
      
      // 获取当前节点的下一级节点的 ID (例如，如果当前节点是 bank，则 child_id 表示 row 的索引)
      int child_id = addr_vec[m_level+1];

      // 检查当前节点的行为表 (m_actions) 中是否存在对应于给定命令的处理函数
      if (m_spec->m_actions[m_level][command]) {
        // 更新当前层级的状态机
        m_spec->m_actions[m_level][command](
            static_cast<NodeType*>(this), // 当前节点 (将指针类型转换为具体的节点类型)
            command,                      // 要执行的命令
            child_id,                     // 子节点 ID
            clk                           // 当前时钟周期
        ); 
      }

      // 如果当前节点已经是命令作用的目标层次 (Scope) 或者没有子节点，则停止递归
      if (m_level == m_spec->m_command_scopes[command] || !m_child_nodes.size()) {
        return; // 停止递归更新状态
      }

      // 如果 child_id 是 -1，说明命令的作用范围是广播到所有的子节点
      if (child_id == -1) {
        for (auto child : m_child_nodes) {
          // 对每一个子节点递归调用 `update_states` 来更新状态
          child->update_states(command, addr_vec, clk);
        }
      } else {
        // 否则，只对指定的子节点进行状态更新
        m_child_nodes[child_id]->update_states(command, addr_vec, clk);
      }
    };

    void update_powers(int command, const AddrVec_t& addr_vec, Clk_t clk) {
      if (!m_spec->m_drampower_enable)
        return;

      int child_id = addr_vec[m_level+1];
      if (m_spec->m_powers[m_level][command]) {
        // update the power model at this level
        m_spec->m_powers[m_level][command](static_cast<NodeType*>(this), command, addr_vec, clk);
      }
      if (m_level == m_spec->m_command_scopes[command] || !m_child_nodes.size()) {
        // stop recursion: updated all levels
        return; 
      }
      // recursively update child nodes
      if (child_id == -1){
        for (auto child : m_child_nodes) {
          child->update_powers(command, addr_vec, clk);
        }
      } else {
        m_child_nodes[child_id]->update_powers(command, addr_vec, clk);
      }
    };

    /**
     * @brief 更新节点的时序信息 (Timing)。
     * @details
     * 当一个命令 (command) 被发出时，更新当前节点及其相邻节点的时序约束，以确保后续命令执行时符合时序要求。
     * 特别是在同一个层次结构中的兄弟节点 (Sibling Node) 之间可能存在时序依赖。
     * 
     * @param command 发出的命令 (如：ACT, WR, RD)。
     * @param addr_vec 地址向量，描述命令作用的内存层次结构 (如：channel, bank, row, column)。
     * @param clk 当前时钟周期 (用于计算时序约束)。
     */
    void update_timing(int command, const AddrVec_t& addr_vec, Clk_t clk) {

      /************************************************
       *         Update Sibling Node Timing
       ***********************************************/
      // 如果当前节点的 ID 与地址向量中的 ID 不匹配，并且地址向量中的 ID 有效 (不是 -1)
      if (m_node_id != addr_vec[m_level] && addr_vec[m_level] != -1) {

        // 遍历与当前命令相关的所有时序约束 (Timing Constraints)
        for (const auto& t : m_spec->m_timing_cons[m_level][command]) {

          // 检查是否为兄弟节点相关的时序约束 (Sibling Timing)
          if (!t.sibling) {
            continue;  // 如果不是针对兄弟节点的约束，跳过当前循环
          }

          // 计算该命令未来可执行的最早时间 (future)
          Clk_t future = clk + t.val;

          // 更新此节点上所有命令的可执行时钟 (m_cmd_ready_clk)
          // 如果已有记录的执行时间较早，则保持原值，否则更新为当前计算值
          m_cmd_ready_clk[t.cmd] = std::max(m_cmd_ready_clk[t.cmd], future); 
        }

        // 如果当前节点是兄弟节点，停止进一步递归，因为时序信息已经更新完成
        return;
      }

      /************************************************
       *          Update Target Node Timing
       ***********************************************/
      // Update history
      if (m_cmd_history[command].size()) {
        m_cmd_history[command].pop_back();
        m_cmd_history[command].push_front(clk); 
      }

      for (const auto& t : m_spec->m_timing_cons[m_level][command]) {
        if (t.sibling) {
          continue; 
        }

        // Get the oldest history
        Clk_t past = m_cmd_history[command][t.window-1];
        if (past < 0) {
          // not enough history
          continue; 
        }

        // update earliest schedulable time of every command
        Clk_t future = past + t.val;
        m_cmd_ready_clk[t.cmd] = std::max(m_cmd_ready_clk[t.cmd], future);
      }

      if (!m_child_nodes.size()) {
        // stop recursion: updated all levels
        return; 
      }

      // recursively update all of my children
      for (auto child : m_child_nodes) {
        child->update_timing(command, addr_vec, clk);
      }
    };

    int get_preq_command(int command, const AddrVec_t& addr_vec, Clk_t m_clk) {
      int child_id = addr_vec[m_level + 1];
      if (m_spec->m_preqs[m_level][command]) {
        int preq_cmd = m_spec->m_preqs[m_level][command](static_cast<NodeType*>(this), command, addr_vec, m_clk);
        if (preq_cmd != -1) {
          // stop recursion: there is a prerequisite at this level
          return preq_cmd; 
        }
      }

      if (!m_child_nodes.size()) {
        // stop recursion: there were no prequisites at any level
        return command; 
      }

      // recursively get_preq_command at my child
      return m_child_nodes[child_id]->get_preq_command(command, addr_vec, m_clk);
    };

    bool check_ready(int command, const AddrVec_t& addr_vec, Clk_t clk) {
      if (m_cmd_ready_clk[command] != -1 && clk < m_cmd_ready_clk[command]) {
        // stop recursion: the check failed at this level
        return false; 
      }

      int child_id = addr_vec[m_level+1];
      if (m_level == m_spec->m_command_scopes[command] || !m_child_nodes.size()) {
        // stop recursion: the check passed at all levels
        return true; 
      }

      if (child_id == -1) {
        // if it is a same bank command, recurse all children in rank level
        bool ready = true;
        for (auto child : m_child_nodes) {
          ready = ready && child->check_ready(command, addr_vec, clk);
        }
        return ready;
      } else {
        // recursively check my child
        return m_child_nodes[child_id]->check_ready(command, addr_vec, clk);
      }
    };

    bool check_rowbuffer_hit(int command, const AddrVec_t& addr_vec, Clk_t m_clk) {
      // TODO: Optimize this by just checking the bank-levels? Have a dedicated bank structure?
      int child_id = addr_vec[m_level+1];
      if (m_spec->m_rowhits[m_level][command]) {
        // stop recursion: there is a row hit at this level
        return m_spec->m_rowhits[m_level][command](static_cast<NodeType*>(this), command, child_id, m_clk);  
      }

      if (!m_child_nodes.size()) {
        // stop recursion: there were no row hits at any level
        return false; 
      }

      // recursively check for row hits at my child
      return m_child_nodes[child_id]->check_rowbuffer_hit(command, addr_vec, m_clk);
    };    
    
    bool check_node_open(int command, const AddrVec_t& addr_vec, Clk_t m_clk) {

      int child_id = addr_vec[m_level+1];
      if (m_spec->m_rowopens[m_level][command])
        // stop recursion: there is a row open at this level
        return m_spec->m_rowopens[m_level][command](static_cast<NodeType*>(this), command, child_id, m_clk);  

      if (!m_child_nodes.size())
        // stop recursion: there were no row hits at any level
        return false; 

      // recursively check for row hits at my child
      return m_child_nodes[child_id]->check_node_open(command, addr_vec, m_clk);
    }
};

template<class T>
using ActionFunc_t = std::function<void(typename T::Node* node, int cmd, int target_id, Clk_t clk)>;
template<class T>
using PreqFunc_t   = std::function<int (typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk)>;
template<class T>
using RowhitFunc_t = std::function<bool(typename T::Node* node, int cmd, int target_id, Clk_t clk)>;
template<class T>
using RowopenFunc_t = std::function<bool(typename T::Node* node, int cmd, int target_id, Clk_t clk)>;
template<class T>
using PowerFunc_t = std::function<void(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk)>;

template<typename T>
using FuncMatrix = std::vector<std::vector<T>>;

// TODO: Enable easy syntax for FuncMatrix lookup
// template<typename T, int N, int M>
// class FuncMatrix : public std::array<std::array<T, M>, N> {

// };

}        // namespace Ramulator

#endif   // RAMULATOR_DRAM_NODE_H
