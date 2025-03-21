#include <vector>

#include "base/base.h"
#include "dram_controller/bh_controller.h"
#include "dram_controller/bh_scheduler.h"
#include "dram_controller/impl/plugin/prac/prac.h"

namespace Ramulator {

/**
 * @brief PRACScheduler 类实现了 DRAM 调度策略。
 * 
 * 该类实现了 `IBHScheduler` 接口，并采用 PRAC（Pre-Request-Aware-Controller）调度策略来调度内存请求。
 * 它根据请求的某些属性（例如是否适配、是否准备好）来决定调度优先级，并结合 PRAC 插件来进行调度。
 */
class PRACScheduler : public IBHScheduler, public Implementation {
    // 注册此调度器实现，供框架自动识别和实例化
    RAMULATOR_REGISTER_IMPLEMENTATION(IBHScheduler, PRACScheduler, "PRACScheduler", "PRAC Scheduler.")

private:
    // 指向 DRAM 模型的指针
    IDRAM* m_dram;
    // 指向 BH 控制器的指针
    IBHDRAMController* m_ctrl;
    // 指向 PRAC 插件的指针
    IPRAC* m_prac;

    // 用于存储不同请求的 LUT（查找表）所需的周期数
    std::unordered_map<int, int> lut_cycles_needed;

    // 当前时钟周期
    Clk_t m_clk = 0;

    // 调试开关，用于打印调试信息
    bool m_is_debug = false; 

    // 常量：表示 FITS 索引的位置
    const int FITS_IDX = 0;
    // 常量：表示 READY 索引的位置
    const int READY_IDX = 1;

public:
    /**
    * @brief 初始化调度器
    * @details 该方法在调度器初始化时调用，设置调试模式（如果有的话）。
    */
    void init() override {
        m_is_debug = param<bool>("debug").default_val(false);
    }

    /**
    * @brief 设置调度器，建立与前端和内存系统的连接
    * @details 该方法将调度器与内存系统和前端连接，并确保 PRAC 插件存在。如果没有找到 PRAC 插件，则退出程序。
    * 
    * @param frontend 前端接口，用于与模拟的其他组件交互
    * @param memory_system 内存系统接口，用于访问底层 DRAM 模型
    */
    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
        // 获取 BH 控制器，并从中获取 DRAM 和 PRAC 插件
        m_ctrl = cast_parent<IBHDRAMController>();
        m_dram = m_ctrl->m_dram;
        m_prac = m_ctrl->get_plugin<IPRAC>();

        // 如果没有找到 PRAC 插件，则退出程序
        if (!m_prac) {
            std::cout << "[RAMULATOR::PRACSched] Need PRAC plugin!" << std::endl;
            std::exit(0);
        }
    }

    /**
    * @brief 比较两个请求，选择优先调度的请求
    * @details 该方法会根据请求的适配状态（是否适配）、准备状态（是否准备好）和到达时间来比较两个请求。
    * 
    * @param req1 请求 1 的迭代器
    * @param req2 请求 2 的迭代器
    * @return 返回优先调度的请求的迭代器
    */
    ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) override {
        // 获取两个请求的 FITS 状态
        bool fits1 = req1->scratchpad[FITS_IDX];
        bool fits2 = req2->scratchpad[FITS_IDX];

        // 如果一个请求适配，另一个没有适配，选择适配的请求
        if (fits1 ^ fits2) {
            if (fits1) {
                return req1;
            }
            else {
                return req2;
            }
        }

        // 如果两个请求都适配或都没有适配，检查它们的准备状态
        bool ready1 = req1->scratchpad[READY_IDX];
        bool ready2 = req2->scratchpad[READY_IDX];

        // 如果一个请求准备好，另一个没有准备好，选择准备好的请求
        if (ready1 ^ ready2) {
            if (ready1) {
                return req1;
            }
            else {
                return req2;
            }
        }

        // 如果两个请求都准备好了，使用 FCFS（先到先服务）策略
        if (req1->arrive <= req2->arrive) {
            return req1;
        }
        else {
            return req2;
        } 
    }

    /**
    * @brief 获取缓冲区中最佳请求
    * @details 该方法会遍历缓冲区中的请求，为每个请求生成预请求命令，并根据适配状态、准备状态和到达时间来选择最佳请求。
    * 
    * @param buffer 存储请求的缓冲区（如读缓冲区、写缓冲区等）
    * @return 返回最佳请求的迭代器
    */
    ReqBuffer::iterator get_best_request(ReqBuffer& buffer) override {
        // 如果缓冲区为空，返回 end 迭代器
        if (buffer.size() == 0) {
            return buffer.end();
        }

        // 获取下一个恢复周期（来自 PRAC 插件）
        Clk_t next_recovery = m_prac->next_recovery_cycle();

        // 为缓冲区中的每个请求生成预请求命令，并更新 FITS 和 READY 状态
        for (auto& req : buffer) {
            req.command = m_dram->get_preq_command(req.final_command, req.addr_vec);
            req.scratchpad[FITS_IDX] = m_clk + m_prac->min_cycles_with_preall(req) < next_recovery;
            req.scratchpad[READY_IDX] = m_dram->check_ready(req.command, req.addr_vec);
        }

        // 默认选择缓冲区中的第一个请求作为候选
        auto candidate = buffer.begin();
        // 遍历缓冲区中的其他请求，使用 compare 方法选择最佳请求
        for (auto next = std::next(buffer.begin(), 1); next != buffer.end(); next++) {
            candidate = compare(candidate, next);   // 选择优先调度的请求
        }
        // 返回最佳请求的迭代器
        return candidate;
    }

    /**
    * @brief 时钟周期更新函数
    * @details 该方法每个时钟周期调用一次，用于增加时钟周期。
    */
    virtual void tick() override {
        m_clk++;    // 增加时钟周期
    }
};

}       // namespace Ramulator
