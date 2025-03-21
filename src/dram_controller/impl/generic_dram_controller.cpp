#include "dram_controller/controller.h"
#include "memory_system/memory_system.h"

namespace Ramulator {

 /**
 * @brief GenericDRAMController 类实现了 IDRAMController 接口，表示一个通用的 DRAM 控制器。
 * 
 * 这个类提供了内存控制器的基本功能，包括请求的调度、管理、刷新操作等。
 * 它通过继承 IDRAMController 并实现其方法，来执行内存访问请求，并通过各种缓冲区对不同类型的请求进行优先级管理。
 */
class GenericDRAMController final : public IDRAMController, public Implementation {
  // 注册该控制器实现，供框架自动识别和实例化
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRAMController, GenericDRAMController, "Generic", "A generic DRAM controller.");
  private:
    // 存放即将完成的读请求队列，回调将在行延迟 (RL) 后执行
    std::deque<Request> pending;          

    // 活动缓冲区：正在被服务的请求，优先级最高
    ReqBuffer m_active_buffer;            
    // 高优先级缓冲区：存放高优先级请求（例如维护请求，如刷新）
    ReqBuffer m_priority_buffer;          
    // 读请求缓冲区
    ReqBuffer m_read_buffer;              
    // 写请求缓冲区
    ReqBuffer m_write_buffer;            

    // 存储行地址索引，指示当前使用的内存行
    int m_bank_addr_idx = -1;

    // 写模式的低水位和高水位，控制何时切换读/写模式
    float m_wr_low_watermark;
    float m_wr_high_watermark;
    // 标记当前是否处于写模式
    bool  m_is_write_mode = false;

    // 行命中、未命中、冲突的统计数据
    size_t s_row_hits = 0;
    size_t s_row_misses = 0;
    size_t s_row_conflicts = 0;

    // 读请求的行命中、未命中、冲突统计
    size_t s_read_row_hits = 0;
    size_t s_read_row_misses = 0;
    size_t s_read_row_conflicts = 0;

    // 写请求的行命中、未命中、冲突统计
    size_t s_write_row_hits = 0;
    size_t s_write_row_misses = 0;
    size_t s_write_row_conflicts = 0;

    // 核心数
    size_t m_num_cores = 0;
    // 每个核心的读命中、未命中、冲突的统计数据
    std::vector<size_t> s_read_row_hits_per_core;
    std::vector<size_t> s_read_row_misses_per_core;
    std::vector<size_t> s_read_row_conflicts_per_core;

    // 请求数量统计
    size_t s_num_read_reqs = 0;
    size_t s_num_write_reqs = 0;
    size_t s_num_other_reqs = 0;
    // 请求队列长度的统计
    size_t s_queue_len = 0;
    size_t s_read_queue_len = 0;
    size_t s_write_queue_len = 0;
    // 各队列的平均长度
    size_t s_priority_queue_len = 0;
    float s_queue_len_avg = 0;
    float s_read_queue_len_avg = 0;
    float s_write_queue_len_avg = 0;
    float s_priority_queue_len_avg = 0;

    // 读请求的延迟统计
    size_t s_read_latency = 0;
    float s_avg_read_latency = 0;


  public:
    /**
    * @brief 初始化 DRAM 控制器及其组件
    * 
    * 在此方法中，控制器初始化其各种参数和子组件（如调度器、刷新管理器、行策略）。
    * 还将加载并初始化所有插件配置。
    */
    void init() override {
      // 读取写模式切换的低/高水位参数
      m_wr_low_watermark =  param<float>("wr_low_watermark").desc("Threshold for switching back to read mode.").default_val(0.2f);
      m_wr_high_watermark = param<float>("wr_high_watermark").desc("Threshold for switching to write mode.").default_val(0.8f);

      // 创建子组件接口：调度器、刷新管理器、行策略
      m_scheduler = create_child_ifce<IScheduler>();
      m_refresh = create_child_ifce<IRefreshManager>();    
      m_rowpolicy = create_child_ifce<IRowPolicy>();    

      // 加载插件配置并实例化插件
      if (m_config["plugins"]) {
        YAML::Node plugin_configs = m_config["plugins"];
        for (YAML::iterator it = plugin_configs.begin(); it != plugin_configs.end(); ++it) {
          m_plugins.push_back(create_child_ifce<IControllerPlugin>(*it));
        }
      }
    };

    /**
    * @brief 初始化 DRAM 控制器，设置与前端和内存系统的连接
    * 
    * 该方法设置了 DRAM 控制器的各种参数，包括与前端和内存系统的连接、统计信息的注册、
    * 缓冲区大小的设置以及核心数等信息的初始化。它还初始化了一些与 DRAM 操作相关的统计数据。
    *
    * @param frontend      指向前端接口的指针，前端通常负责发出内存请求。
    * @param memory_system 指向内存系统接口的指针，提供对底层 DRAM 的访问。
    */
    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      // 获取内存系统中的 DRAM 接口
      m_dram = memory_system->get_ifce<IDRAM>();
      // 获取内存系统的 bank 地址索引，用于访问特定的内存 bank
      m_bank_addr_idx = m_dram->m_levels("bank");
      // 设置优先级缓冲区的最大大小
      m_priority_buffer.max_size = 512*3 + 32;

      // 获取前端的核心数
      m_num_cores = frontend->get_num_cores();
      
      // 根据核心数初始化行命中、未命中和冲突的统计数据
      s_read_row_hits_per_core.resize(m_num_cores, 0);
      s_read_row_misses_per_core.resize(m_num_cores, 0);
      s_read_row_conflicts_per_core.resize(m_num_cores, 0);
      
      // 注册全局统计数据：行命中、未命中、冲突等
      register_stat(s_row_hits).name("row_hits_{}", m_channel_id);
      register_stat(s_row_misses).name("row_misses_{}", m_channel_id);
      register_stat(s_row_conflicts).name("row_conflicts_{}", m_channel_id);
      register_stat(s_read_row_hits).name("read_row_hits_{}", m_channel_id);
      register_stat(s_read_row_misses).name("read_row_misses_{}", m_channel_id);
      register_stat(s_read_row_conflicts).name("read_row_conflicts_{}", m_channel_id);
      register_stat(s_write_row_hits).name("write_row_hits_{}", m_channel_id);
      register_stat(s_write_row_misses).name("write_row_misses_{}", m_channel_id);
      register_stat(s_write_row_conflicts).name("write_row_conflicts_{}", m_channel_id);

      // 为每个核心注册其相关的统计数据：读命中、未命中、冲突
      for (size_t core_id = 0; core_id < m_num_cores; core_id++) {
        register_stat(s_read_row_hits_per_core[core_id]).name("read_row_hits_core_{}", core_id);
        register_stat(s_read_row_misses_per_core[core_id]).name("read_row_misses_core_{}", core_id);
        register_stat(s_read_row_conflicts_per_core[core_id]).name("read_row_conflicts_core_{}", core_id);
      }

      // 注册读请求、写请求和其他请求的统计数据
      register_stat(s_num_read_reqs).name("num_read_reqs_{}", m_channel_id);
      register_stat(s_num_write_reqs).name("num_write_reqs_{}", m_channel_id);
      register_stat(s_num_other_reqs).name("num_other_reqs_{}", m_channel_id);

      // 注册请求队列的长度相关统计数据
      register_stat(s_queue_len).name("queue_len_{}", m_channel_id);
      register_stat(s_read_queue_len).name("read_queue_len_{}", m_channel_id);
      register_stat(s_write_queue_len).name("write_queue_len_{}", m_channel_id);
      register_stat(s_priority_queue_len).name("priority_queue_len_{}", m_channel_id);

      // 注册各个队列的平均长度
      register_stat(s_queue_len_avg).name("queue_len_avg_{}", m_channel_id);
      register_stat(s_read_queue_len_avg).name("read_queue_len_avg_{}", m_channel_id);
      register_stat(s_write_queue_len_avg).name("write_queue_len_avg_{}", m_channel_id);
      register_stat(s_priority_queue_len_avg).name("priority_queue_len_avg_{}", m_channel_id);
      
      // 注册读请求的延迟统计信息
      register_stat(s_read_latency).name("read_latency_{}", m_channel_id);
      register_stat(s_avg_read_latency).name("avg_read_latency_{}", m_channel_id);
    };

    /**
    * @brief 处理并发送内存请求
    * 
    * 根据请求的类型（读、写或其他），将请求添加到相应的缓冲区。请求将按时钟周期（`m_clk`）进行处理。
    * 此方法还包括对读取请求的特殊处理（即转发已存在的写入请求）。
    * 
    * @param req 内存请求，包含请求类型、地址等信息
    * @return true 请求成功入队
    * @return false 请求无法入队（例如缓冲区满）
    */
    bool send(Request& req) override {
      // 根据请求类型，获取最终的命令（例如读命令、写命令等）
      req.final_command = m_dram->m_request_translations(req.type_id);

      // 根据请求类型更新统计数据
      switch (req.type_id) {
        case Request::Type::Read: {
          s_num_read_reqs++; 
          break;
        }
        case Request::Type::Write: {
          s_num_write_reqs++;
          break;
        }
        default: {
          s_num_other_reqs++;
          break;
        }
      }

      // 针对读请求，检查是否有匹配的写请求，进行转发
      if (req.type_id == Request::Type::Read) {
        // 比较读取请求的地址与现有写请求的地址
        auto compare_addr = [req](const Request& wreq) {
          return wreq.addr == req.addr; // 比较地址是否相同
        };
        // 如果找到匹配的写请求，则将读请求转发处理
        if (std::find_if(m_write_buffer.begin(), m_write_buffer.end(), compare_addr) != m_write_buffer.end()) {
          // 将请求的离开时间设置为下一个时钟周期，表示请求将在下一个周期处理
          req.depart = m_clk + 1;
          pending.push_back(req);   // 将请求添加到等待队列
          return true;
        }
      }

      // 对于其他请求类型（读写），将请求添加到相应的缓冲区
      bool is_success = false;
      req.arrive = m_clk;   // 记录请求到达时间
      if        (req.type_id == Request::Type::Read) {
        is_success = m_read_buffer.enqueue(req);    // 读取请求入队
      } else if (req.type_id == Request::Type::Write) {
        is_success = m_write_buffer.enqueue(req);   // 写入请求入队
      } else {
        // 如果请求类型无效，抛出异常
        throw std::runtime_error("Invalid request type!");
      }
      // 如果请求未成功入队，表示队列已满，返回 false
      if (!is_success) {
        // 设置请求的到达时间为 -1，表示入队失败
        req.arrive = -1;
        return false;
      }

      return true;    // 请求成功入队
    };

    /**
    * @brief 处理并发送高优先级请求
    * 
    * 该方法用于处理高优先级的内存请求（例如刷新等重要操作）。它将请求直接入队到优先级缓冲区，
    * 确保这些请求优先被处理。
    * 
    * @param req 高优先级的内存请求，包含请求类型、地址等信息
    * @return true 请求成功入队
    * @return false 请求无法入队（例如缓冲区满）
    */
    bool priority_send(Request& req) override {
      // 根据请求类型，获取最终的命令（例如读命令、写命令等）
      req.final_command = m_dram->m_request_translations(req.type_id);

      // 尝试将请求加入优先级缓冲区
      bool is_success = false;
      is_success = m_priority_buffer.enqueue(req);

      // 返回请求是否成功入队
      return is_success;
    }

    /**
    * @brief 执行一个时钟周期的操作
    * 
    * 该方法在每个时钟周期内执行以下操作：
    * 1. 更新请求队列的长度统计。
    * 2. 处理完成的读取请求。
    * 3. 调用刷新管理器的 `tick` 方法来处理内存刷新操作。
    * 4. 尝试从请求队列中选择一个请求来服务。
    * 5. 根据行策略对请求进行处理。
    * 6. 更新所有插件（如调试、统计等）。
    * 7. 向 DRAM 发出命令来处理请求。
    * 
    * @details 该函数是时钟驱动的内存控制器的核心逻辑，确保内存请求按时序被调度并执行。
    */
    void tick() override {
      // 增加时钟周期
      m_clk++;

      // 更新统计数据：请求队列的总长度和各个缓冲区的长度
      s_queue_len += m_read_buffer.size() + m_write_buffer.size() + m_priority_buffer.size() + pending.size();
      s_read_queue_len += m_read_buffer.size() + pending.size();
      s_write_queue_len += m_write_buffer.size();
      s_priority_queue_len += m_priority_buffer.size();

      // 1. 处理已经完成的读取请求
      serve_completed_reads();

      // 处理内存刷新的操作
      m_refresh->tick();

      // 2. 尝试查找一个请求来进行服务
      ReqBuffer::iterator req_it;
      ReqBuffer* buffer = nullptr;
      bool request_found = schedule_request(req_it, buffer);

      // 2.1 执行行策略操作
      m_rowpolicy->update(request_found, req_it);

      // 3. 更新所有插件
      for (auto plugin : m_plugins) {
        plugin->update(request_found, req_it);
      }

      // 4. 最后，发出命令来服务请求
      if (request_found) {
        // 如果找到了一个需要服务的请求
        if (req_it->is_stat_updated == false) {
          update_request_stats(req_it); // 更新请求的统计信息
        }

        // 向 DRAM 发出命令
        m_dram->issue_command(req_it->command, req_it->addr_vec);

        // 如果当前命令是请求的最后一个命令，设置请求的离开时钟周期，并将请求移至 pending 队列
        if (req_it->command == req_it->final_command) {
          if (req_it->type_id == Request::Type::Read) {
            req_it->depart = m_clk + m_dram->m_read_latency;  // 设置读请求的离开时间
            pending.push_back(*req_it);   // 将读请求添加到 pending 队列
          } else if (req_it->type_id == Request::Type::Write) {
            // TODO: 添加写请求的统计更新代码
          }
          buffer->remove(req_it);   // 从缓冲区中移除已服务的请求
        } else {
          // 如果当前命令是打开命令（指与内存访问相关的操作，行激活，行关闭等），则将请求添加到活动缓冲区
          if (m_dram->m_command_meta(req_it->command).is_opening) {
            m_active_buffer.enqueue(*req_it);   // 将请求加入活动缓冲区
            buffer->remove(req_it);   // 从缓冲区中移除已调度的请求
          } 
        }

      }

    };


  private:
    /**
     * @brief    Helper function to check if a request is hitting an open row
     * @details  检查请求是否命中了一个已打开的内存行。
     *           如果请求的地址与当前已打开的行地址匹配，返回 `true`，表示命中该行。
     * 
     * @param req 请求迭代器，包含请求的详细信息，如命令和地址
     * @return true 如果命中了已打开的内存行
     * @return false 如果没有命中已打开的内存行
     */
    bool is_row_hit(ReqBuffer::iterator& req)
    {
        return m_dram->check_rowbuffer_hit(req->final_command, req->addr_vec);
    }
    
    /**
    * @brief    Helper function to check if a request is opening a row
    * @details  检查请求是否为行打开命令（如行激活命令）。
    *           如果请求的命令是打开某行的命令，返回 `true`，表示当前请求正在打开行。
    * 
    * @param req 请求迭代器，包含请求的详细信息，如命令和地址
    * @return true 如果请求正在打开一行
    * @return false 如果请求没有打开行
    */
    bool is_row_open(ReqBuffer::iterator& req)
    {
        return m_dram->check_node_open(req->final_command, req->addr_vec);
    }

    /**
     * @brief    Update the statistics for a given request
     * @details  更新与请求相关的统计数据，包括行命中、行未命中、行冲突等信息。
     *           统计数据会根据请求的类型（读取或写入）以及其是否命中、打开等情况进行更新。
     *           统计数据将用于性能分析和模拟调试。
     * 
     * @param req 请求迭代器，包含请求的详细信息
     */
    void update_request_stats(ReqBuffer::iterator& req)
    {
      req->is_stat_updated = true;    // 标记请求的统计信息已更新

      // 处理读取请求
      if (req->type_id == Request::Type::Read) 
      {
        if (is_row_hit(req)) {    // 如果命中已打开的内存行，命中计数增加
          s_read_row_hits++;  
          s_row_hits++;
          if (req->source_id != -1)   // 如果请求来自某个核心，更新核心级统计
            s_read_row_hits_per_core[req->source_id]++;
        } else if (is_row_open(req)) {    // 如果请求正在打开内存行，冲突计数增加，通常在同一时刻只能激活一行，必须等待当前行的操作完成
          s_read_row_conflicts++;
          s_row_conflicts++;
          if (req->source_id != -1)   // 如果请求来自某个核心，更新核心级统计
            s_read_row_conflicts_per_core[req->source_id]++;
        } else {    // 如果既没有命中也没有打开内存行，未命中计数增加
          s_read_row_misses++;
          s_row_misses++;
          if (req->source_id != -1)
            s_read_row_misses_per_core[req->source_id]++;
        } 
      } 

      // 处理写入请求
      else if (req->type_id == Request::Type::Write) 
      {
        if (is_row_hit(req)) {  // 如果命中已打开的内存行，命中计数增加
          s_write_row_hits++;
          s_row_hits++;
        } else if (is_row_open(req)) {    // 如果请求正在打开内存行，冲突计数增加
          s_write_row_conflicts++;
          s_row_conflicts++;
        } else {    // 如果既没有命中也没有打开内存行，未命中计数增加
          s_write_row_misses++;
          s_row_misses++;
        }
      }
    }

    /**
    * @brief    Helper function to serve the completed read requests
    * @details  这个函数在每个时钟周期的开始时被调用。
    *           它会检查挂起队列（`pending`）中的第一个请求，判断该请求是否已经从 DRAM 获得了数据。
    *           如果请求已经完成，则调用其回调函数，并将其从挂起队列中移除。
    */
    void serve_completed_reads() {
      // 如果挂起队列不为空，检查第一个请求
      if (pending.size()) {
        // 获取挂起队列中的第一个请求
        auto& req = pending[0];
        // 检查该请求是否已经完成（即已接收到来自 DRAM 的数据）
        if (req.depart <= m_clk) {
          // 请求已完成数据接收（即已经度过了预定的时钟周期）
          if (req.depart - req.arrive > 1) {
            // 检查命令是否被DRAM处理或者被转发
            // TODO add the stats back
            s_read_latency += req.depart - req.arrive;
          }

          if (req.callback) {
            // 如果请求有回调函数（来自外部，例如处理器），则调用回调函数
            req.callback(req);    // 调用回调函数，传入请求作为参数
          }
          // 最后，将该请求从挂起队列中移除
          pending.pop_front();    // 移除队列中的第一个请求
        }
      };
    };


    /**
    * @brief    Checks if we need to switch to write mode
    * @details  这个函数检查当前的请求队列状态，以决定是否需要切换到写模式。
    *           如果写缓冲区的队列长度超过了一个阈值，或者读缓冲区为空，则切换到写模式。
    *           如果写缓冲区的队列长度低于一个阈值，并且读缓冲区不为空，则切换回读模式。
    */
    void set_write_mode() {
      // 如果当前不是写模式
      if (!m_is_write_mode) {
        // 如果写缓冲区的大小超过了写模式的阈值，或者读缓冲区为空，切换到写模式
        if ((m_write_buffer.size() > m_wr_high_watermark * m_write_buffer.max_size) || m_read_buffer.size() == 0) {
          // 切换到写模式
          m_is_write_mode = true;
        }
      // 如果当前是写模式
      } else {
        // 如果写缓冲区的大小小于写模式的低水位，且读缓冲区不为空，切换回读模式
        if ((m_write_buffer.size() < m_wr_low_watermark * m_write_buffer.max_size) && m_read_buffer.size() != 0) {
          // 切换回读模式
          m_is_write_mode = false;
        }
      }
    };


    /**
    * @brief    Helper function to find a request to schedule from the buffers.
    * @details  该函数用于从多个缓冲区中找到一个请求进行调度。它首先检查正在激活的请求，然后优先调度高优先级的请求（如维护请求），
    *           如果没有合适的请求，则从读写缓冲区中选择请求。最后，如果找到请求，还需要检查是否会关闭已打开的内存行。
    * 
    * @param req_it      请求的迭代器，用于返回找到的请求
    * @param req_buffer  用于返回找到请求的缓冲区
    * @return true 如果找到了合适的请求
    * @return false 如果没有合适的请求可以调度
    */
    bool schedule_request(ReqBuffer::iterator& req_it, ReqBuffer*& req_buffer) {
      bool request_found = false;
      // 2.1 首先检查活动缓冲区，处理已激活的请求，避免发出无效的 ACT 命令
      if (req_it= m_scheduler->get_best_request(m_active_buffer); req_it != m_active_buffer.end()) {
        if (m_dram->check_ready(req_it->command, req_it->addr_vec)) {
          request_found = true;
          req_buffer = &m_active_buffer;  // 将缓冲区指针指向活动缓冲区
        }
      }

      // 2.2 如果活动缓冲区没有可调度的请求，检查其他缓冲区
      if (!request_found) {
        // 2.2.1 首先检查优先级缓冲区，优先调度如维护请求（例如刷新）
        if (m_priority_buffer.size() != 0) {
          req_buffer = &m_priority_buffer;
          req_it = m_priority_buffer.begin();
          req_it->command = m_dram->get_preq_command(req_it->final_command, req_it->addr_vec);
          
          // 检查该请求是否准备好发出
          request_found = m_dram->check_ready(req_it->command, req_it->addr_vec);
          if (!request_found & m_priority_buffer.size() != 0) {
            return false;   // 如果优先级缓冲区没有可调度的请求，返回 false
          }
        }

        // 2.2.2 如果优先级缓冲区没有请求，检查读写缓冲区
        if (!request_found) {
          // 根据写模式选择缓冲区
          set_write_mode();   // 设置当前是否处于写模式
          auto& buffer = m_is_write_mode ? m_write_buffer : m_read_buffer;    // 根据当前模式选择读或写缓冲区
          if (req_it = m_scheduler->get_best_request(buffer); req_it != buffer.end()) {   
            request_found = m_dram->check_ready(req_it->command, req_it->addr_vec);   // 检查请求是否准备好发出
            req_buffer = &buffer;   // 设置缓冲区指针
          }
        }
      }

      // 2.3 如果找到请求，我们需要检查它是否会关闭已打开的内存行
      if (request_found) {
        if (m_dram->m_command_meta(req_it->command).is_closing) {   // 如果当前命令是关闭命令
          auto& rowgroup = req_it->addr_vec;
          for (auto _it = m_active_buffer.begin(); _it != m_active_buffer.end(); _it++) {
            auto& _it_rowgroup = _it->addr_vec;
            bool is_matching = true;

            // 比较请求的地址与活动缓冲区中的每个行地址
            for (int i = 0; i < m_bank_addr_idx + 1 ; i++) {
              if (_it_rowgroup[i] != rowgroup[i] && _it_rowgroup[i] != -1 && rowgroup[i] != -1) {
                is_matching = false;    
                break;    // 如果地址不匹配，标记为不匹配并跳出循环
              }
            }

            // 如果找到匹配的行，则将当前请求标记为不可调度
            if (is_matching) {
              request_found = false;
              break;
            }
          }
        }
      }

      return request_found;   // 返回是否找到可调度的请求
    }

    /**
    * @brief    Finalizes the statistics and computes averages at the end of the simulation.
    * @details  该函数在模拟结束时调用，计算并保存一些关键的性能统计数据。
    *           它主要计算如下统计信息：
    *           1. 平均读取延迟
    *           2. 各种请求队列的平均长度（包括总队列长度、读队列长度、写队列长度和优先级队列长度）
    */
    void finalize() override {
      s_avg_read_latency = (float) s_read_latency / (float) s_num_read_reqs;

      s_queue_len_avg = (float) s_queue_len / (float) m_clk;
      s_read_queue_len_avg = (float) s_read_queue_len / (float) m_clk;
      s_write_queue_len_avg = (float) s_write_queue_len / (float) m_clk;
      s_priority_queue_len_avg = (float) s_priority_queue_len / (float) m_clk;

      return;
    }

};
  
}   // namespace Ramulator