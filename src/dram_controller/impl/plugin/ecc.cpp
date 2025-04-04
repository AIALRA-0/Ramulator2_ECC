#include <vector>
#include <unordered_map>
#include <limits>
#include <random>
#include <filesystem>
#include <fstream>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

// 用于 CRC EDC 计算 (Boost)
#include <boost/crc.hpp>  

// 用于 RS ECC 计算 
#include "reedSolomon.h"

namespace Ramulator
{

  class ECCPlugin : public IControllerPlugin, public Implementation
  {
    RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, ECCPlugin, "ECCPlugin", "This plugin adds large-size ECC/EDC emulation to Ramulator2 to evaluate memory reliability, bandwidth, and latency trade-offs in AI and HPC workloads.")
  

  private:
    std::string ecc_type;  // ECC 类型
    std::string edc_type;  // EDC 类型

    // 配置 ecc/edc 库
    
  protected:
    IDRAM *m_dram = nullptr;
    IDRAMController *m_ctrl = nullptr;

    // 插件内部存储结构
    std::unordered_map<Addr_t, std::vector<uint8_t>> m_data_storage; // 数据 + EDC 存储
    std::unordered_map<Addr_t, std::vector<uint8_t>> m_ecc_storage;              // ECC 存储

    // 配置参数
    size_t DATA_BLOCK_SIZE;  // 数据块大小
    size_t EDC_SIZE;         // EDC 大小
    size_t ECC_SIZE;         // ECC 大小
    double bit_error_rate;  // BER
    double max_failure_prob;    // 最大失败可能

    // 性能参数
    // TODO: 需要更精确的估计
    double BUS_BW_GBs = 512.0;     // 512 GB/s HBM
    double MEM_RD_BW_GBs = 512.0;  // 512 GB/s
    double MEM_WR_BW_GBs = 512.0;  // 512 GB/s
    double EDC_COMPUTE_PER_BYTE_NS = 0.01;
    double ECC_COMPUTE_PER_BYTE_NS = 0.02;

    // 统计变量
    int total_ecc_size = 0;      // 总ECC占用空间（单位：byte）
    int total_edc_size = 0;      // 总EDC占用空间（单位：byte）
    int edc_success_count = 0;   // EDC校验成功次数
    int edc_failure_count = 0;   // EDC校验失败次数
    int ecc_success_count = 0;   // ECC纠错成功次数
    int ecc_failure_count = 0;   // ECC纠错失败次数
    // int total_corrected_bits = 0;
    // int total_write_latency_ns = 0;
    // int total_read_latency_ns = 0;
    // int total_raw_write_latency_ns = 0;
    // int total_raw_read_latency_ns = 0;
    // int total_ecc_write_latency_ns = 0;
    // int total_ecc_read_latency_ns = 0;
    // int total_transmission_latency_ns = 0;
    // int total_memory_write_latency_ns = 0;
    // int total_memory_read_latency_ns = 0
    // int total_edc_check_latency_ns = 0;
    // int total_ecc_check_latency_ns = 0;
    // int total_edc_compute_latency_ns = 0;
    // int total_ecc_compute_latency_ns = 0;


  public:
    // 初始化插件参数
    void init() override
    {
      // 读取配置参数
      // 变量名称 = param<变量类型>("读取名称").desc("描述").default_val(默认值);

      // 从配置文件读取参数
      DATA_BLOCK_SIZE = param<size_t>("data_block_size").desc("Size of each data block in bytes.").default_val(128);
      EDC_SIZE = param<size_t>("edc_size").desc("Size of EDC in bytes.").default_val(4);
      ECC_SIZE = param<size_t>("ecc_size").desc("Size of ECC in bytes.").default_val(8);

      ecc_type = param<std::string>("ecc_type").desc("ECC type to use: hamming, rs, bch.").default_val("bch");
      edc_type = param<std::string>("edc_type").desc("EDC type to use: checksum, crc32, crc64.").default_val("crc32");
      bit_error_rate = param<double>("bit_error_rate").desc("Raw bit error rate (BER)").default_val(1e-6);
      max_failure_prob = param<double>("max_failure_prob").desc("Maximum allowed failure probability").default_val(1e-14);
      
      // 注册运行时统计变量
      // register_stat(变量名称).name("统计输出名称");
      register_stat(total_ecc_size).name("ecc_total_size_bytes");
      register_stat(total_edc_size).name("edc_total_size_bytes");
      register_stat(edc_success_count).name("edc_success_count");
      register_stat(edc_failure_count).name("edc_failure_count");
      register_stat(ecc_success_count).name("ecc_success_count");
      register_stat(ecc_failure_count).name("ecc_failure_count");
      // register_stat(total_corrected_bits).name("total_corrected_bits");
      // register_stat(total_write_latency_ns).name("total_write_latency_ns");
      // register_stat(total_read_latency_ns).name("total_read_latency_ns");
      // register_stat(total_raw_write_latency_ns).name("total_raw_write_latency_ns");
      // register_stat(total_raw_read_latency_ns).name("total_raw_read_latency_ns");
      // register_stat(total_ecc_write_latency_ns).name("total_ecc_write_latency_ns");
      // register_stat(total_ecc_read_latency_ns).name("total_ecc_read_latency_ns");
      // register_stat(total_transmission_latency_ns).name("total_transmission_latency_ns");
      // register_stat(total_memory_write_latency_ns).name("total_memory_write_latency_ns");
      // register_stat(total_memory_read_latency_ns).name("total_memory_read_latency_ns");
      // register_stat(total_edc_check_latency_ns).name("total_edc_check_latency_ns");
      // register_stat(total_ecc_check_latency_ns).name("total_ecc_check_latency_ns");
      // register_stat(total_edc_compute_latency_ns).name("total_edc_compute_latency_ns");
      // register_stat(total_ecc_compute_latency_ns).name("total_ecc_compute_latency_ns");

      // 输入变量
      register_stat(DATA_BLOCK_SIZE).name("config_data_block_size");
      register_stat(EDC_SIZE).name("config_edc_size");
      register_stat(ECC_SIZE).name("config_ecc_size");
      register_stat(bit_error_rate).name("config_bit_error_rate");
      register_stat(max_failure_prob).name("config_max_failure_prob");
          
      // 带宽
      register_stat(BUS_BW_GBs).name("param_bus_bw_GBs");
      register_stat(MEM_RD_BW_GBs).name("param_mem_read_bw_GBs");
      register_stat(MEM_WR_BW_GBs).name("param_mem_write_bw_GBs");
      register_stat(EDC_COMPUTE_PER_BYTE_NS).name("param_edc_compute_ns_per_byte");
      register_stat(ECC_COMPUTE_PER_BYTE_NS).name("param_ecc_compute_ns_per_byte");
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

            // 需要被写入的数
            std::vector<uint8_t> data_block;

            // 检查 payload 是否为空
            if (req_it->m_payload != nullptr)
            {
                // TODO: 处理用户提供的数据 (m_payload 已存在)

                // 这里你需要根据你的需求来转换 m_payload，通常是一个 uint8_t* 或 std::vector<uint8_t> 的指针
                // 假设 m_payload 是 uint8_t* 类型，且数据大小为 DATA_BLOCK_SIZE
                uint8_t* raw_data = static_cast<uint8_t*>(req_it->m_payload);
                data_block.assign(raw_data, raw_data + DATA_BLOCK_SIZE);
            }
            else
            {
                // m_payload 不存在，使用 generateRandomDataBlock() 生成数据块
                data_block = generateRandomDataBlock(DATA_BLOCK_SIZE);
                inject_random_errors(data_block);
            }

            // EDC 计算：为新数据块计算 EDC 值，计算 EDC 并附加到数据块末尾
            std::vector<uint8_t> edc_value = calculateEDC(data_block);
            data_block.insert(data_block.end(), edc_value.begin(), edc_value.end());

            // 计算动态需要的ECC大小
            int dynamic_ecc_size = calculate_dynamic_ecc_size(data_block.size());

            // ECC 计算：为新数据块计算 ECC 码字，并将其存储在 HBM-ECC 区域
            std::vector<uint8_t> ecc_value = calculateECC(data_block, dynamic_ecc_size);

            // 数据写入：将数据块 + EDC 存储在数据区域
            m_data_storage[addr] = data_block;

            // ECC 写入：将 ECC 单独存储
            m_ecc_storage[addr] = ecc_value;

            total_edc_size += EDC_SIZE;
            total_ecc_size += m_ecc_storage[addr].size();
        }

        // 处理读取请求（READ）
        if (req_it->type_id == Request::Type::Read)
        {
            // 获取命令目标地址
            Addr_t addr = req_it->addr;

            // 检查并补数据块
            if (m_data_storage.find(addr) == m_data_storage.end())
            {
                std::cerr << "[ECCPlugin] Data block not found! Generating fake data block..." << std::endl;
            
                std::vector<uint8_t> fake_data = generateRandomDataBlock(DATA_BLOCK_SIZE);
                std::vector<uint8_t> fake_edc = calculateEDC(fake_data);
            
                std::vector<uint8_t> fake_data_block_with_edc = fake_data;
                fake_data_block_with_edc.insert(fake_data_block_with_edc.end(), fake_edc.begin(), fake_edc.end());
                inject_random_errors(fake_data_block_with_edc);   // 随机注入bit错误
                m_data_storage[addr] = fake_data_block_with_edc;
                }
            // 读取现有数据块（含EDC）
            std::vector<uint8_t>& data_block_with_edc = m_data_storage[addr];

            // 确保数据块尺寸正确（防止乱）
            if (data_block_with_edc.size() != DATA_BLOCK_SIZE + EDC_SIZE)
            {
                std::cerr << "[ECCPlugin] Data block size mismatch! Regenerating EDC..." << std::endl;

                std::vector<uint8_t> fixed_data_block = data_block_with_edc;
                fixed_data_block.resize(DATA_BLOCK_SIZE, 0);  // 补0到标准大小
                std::vector<uint8_t> fake_edc = calculateEDC(fixed_data_block);

                fixed_data_block.insert(fixed_data_block.end(), fake_edc.begin(), fake_edc.end());
                m_data_storage[addr] = fixed_data_block;
            }

            // 检查并补 ECC
            if (m_ecc_storage.find(addr) == m_ecc_storage.end())
            {
                std::cerr << "[ECCPlugin] ECC not found! Generating fake ECC..." << std::endl;
                int dynamic_fake_ecc_size = calculate_dynamic_ecc_size(m_data_storage[addr].size());
                std::vector<uint8_t> fake_ecc = calculateECC(m_data_storage[addr], dynamic_fake_ecc_size);
                m_ecc_storage[addr] = fake_ecc;
            }

            // 分离数据块和旧EDC
            std::vector<uint8_t> data_block(data_block_with_edc.begin(), data_block_with_edc.begin() + DATA_BLOCK_SIZE);
            std::vector<uint8_t> old_edc(data_block_with_edc.begin() + DATA_BLOCK_SIZE, data_block_with_edc.end());
            
            // EDC 校验：控制器读取数据块及其对应的 EDC（错误检测码），并执行 EDC 校验
            std::vector<uint8_t> expected_edc = calculateEDC(data_block);
            bool edc_pass = (expected_edc == old_edc);

            if (edc_pass)
            {
                edc_success_count++;

                // EDC校验通过，直接返回数据
                if (req_it->m_payload != nullptr)
                {
                    std::memcpy(req_it->m_payload, data_block.data(), DATA_BLOCK_SIZE);
                }
            }

            // EDC 失败：如果 EDC 校验失败，数据可能已损坏，触发 ECC（错误更正码）错误恢复过程
            else
            {   
                edc_failure_count++;

                // 读取 ECC 码字：存储控制器获取完整的 ECC 码字
                std::cerr << "[ECCPlugin] Warning: EDC failed. Attempting ECC correction..." << std::endl;
                if (m_ecc_storage.find(addr) == m_ecc_storage.end())
                {
                    std::cerr << "[ECCPlugin] Read Error: ECC not found for address!" << std::endl;
                    return;
                }
                // 执行 ECC 更正：控制器使用 ECC 错误更正算法尝试修复数据
                std::vector<uint8_t> ecc_codeword = m_ecc_storage[addr];

                // TODO: 这里应该调用真正的ECC解码
                bool corrected = decodeECC(data_block, ecc_codeword);

                // 更正成功：如果错误数量 ≤ t（纠错阈值），ECC 成功修复数据。修复后的数据通过并行高速总线返回，并将ECC写回内存
                if (corrected)
                {
                    ecc_success_count++;

                    std::cerr << "[ECCPlugin] ECC Correction Success." << std::endl;
                
                    // 纠错成功，重新生成 EDC 和 ECC
                    std::vector<uint8_t> new_edc = calculateEDC(data_block);
                    std::vector<uint8_t> new_data_block_with_edc = data_block;
                    new_data_block_with_edc.insert(new_data_block_with_edc.end(), new_edc.begin(), new_edc.end());
                
                    m_data_storage[addr] = new_data_block_with_edc;
                    
                    int dynamic_new_ecc_size = calculate_dynamic_ecc_size(m_data_storage[addr].size());
                    std::vector<uint8_t> new_ecc = calculateECC(new_data_block_with_edc, dynamic_new_ecc_size);
                    m_ecc_storage[addr] = new_ecc;
                
                    // 返回修正后的数据
                    if (req_it->m_payload != nullptr)
                    {
                        std::memcpy(req_it->m_payload, data_block.data(), DATA_BLOCK_SIZE);
                    }
                }
                // 更正失败：如果 ECC 无法修复数据，向控制器报告为不可更正错误（UE）
                else
                {
                    ecc_failure_count++;
                    
                    // ECC纠错失败，标记不可更正错误UE
                    std::cerr << "[ECCPlugin] UE: Uncorrectable error during read!" << std::endl;

                    // 重试读取：当内存控制器遇到 UE 错误时，会尝试重新读取数据和 ECC 码字，进行第二次 ECC 更正尝试
                    bool retry_success = false;
                    
                    // 这里可以模拟重读逻辑
                    if (retry_success)
                    {
                        // TODO: 重新尝试读取
                        std::cerr << "[ECCPlugin] Retry succeeded!" << std::endl;
                    }
                    else
                    {
                        // TODO：系统支持RAID或镜像
                        bool raid_recovery_success = false;
                        if (raid_recovery_success)
                        {
                            std::cerr << "[ECCPlugin] Recovery from RAID redundancy success." << std::endl;
                        }
                        else
                        {
                            // 无法修复，向CPU/系统报告不可恢复错误
                            std::cerr << "[ECCPlugin] Fatal UE reported to CPU." << std::endl;
                            // TODO: 通知上层系统/停止仿真/触发错误处理逻辑
                        }
                    }
                }
            }

            // EDC 通过：如果 EDC 校验通过，数据被验证为无误，并直接通过并行高速总线返回
        }

        // 处理局部写入请求（Partial WRITE）
        if (req_it->type_id == Request::Type::PartialWrite) // 扩展了request
        {
            // TODO: 处理局部写入情况

            // 获取命令目标地址
            Addr_t addr = req_it->addr;
            
            // 读出老的 Data + EDC
            auto& data_block_with_edc = m_data_storage[addr];

            std::vector<uint8_t> old_data(data_block_with_edc.begin(), data_block_with_edc.begin() + DATA_BLOCK_SIZE);
            std::vector<uint8_t> old_edc(data_block_with_edc.begin() + DATA_BLOCK_SIZE, data_block_with_edc.end());

            // 校验 EDC
            if (calculateEDC(old_data) != old_edc) 
            {
                // EDC校验失败 -> 需要先 full decode ECC
                std::cerr << "[ECCPlugin] Partial Write: EDC check failed, need full ECC decoding!" << std::endl;
                // TODO：调用 ECCDecode 先修复老数据
                // ECCDecode(old_data, m_ecc_storage[addr]);
            }

            // 更新数据块的部分区域
            // TODO: 需要扩展 Request 命令结构
            // size_t offset = req_it->m_offset;  
            // size_t length = req_it->m_length;  
            size_t offset = 0;  
            size_t length = 0;  

            // 从数据块里提取旧的数据片段、以及获取新的要写入的数据片段
            uint8_t* new_data_ptr = static_cast<uint8_t*>(req_it->m_payload);
            std::vector<uint8_t> old_chunk(old_data.begin() + offset, old_data.begin() + offset + length);
            std::vector<uint8_t> new_chunk(new_data_ptr, new_data_ptr + length);

            // 更新老数据
            std::copy(new_chunk.begin(), new_chunk.end(), old_data.begin() + offset);

            // 局部写入命令：仅更新修改过的数据块，使用增量 ECC 更新以减少计算负载
            std::vector<uint8_t> &old_ecc = m_ecc_storage[addr];

            std::vector<uint8_t> enc_old_chunk = ReedSolomonEncode(old_chunk, old_ecc.size());
            std::vector<uint8_t> enc_new_chunk = ReedSolomonEncode(new_chunk, old_ecc.size());

            for (size_t i = 0; i < old_ecc.size(); i++) {
                old_ecc[i] ^= enc_old_chunk[i] ^ enc_new_chunk[i];
            }

            // 更新新的 EDC
            std::vector<uint8_t> new_edc = calculateEDC(old_data);

            // 写回 Data + EDC
            std::vector<uint8_t> new_data_block_with_edc = old_data;
            new_data_block_with_edc.insert(new_data_block_with_edc.end(), new_edc.begin(), new_edc.end());
            m_data_storage[addr] = new_data_block_with_edc;
        }  
      }
    };

    // 生成随机数据块的函数
    std::vector<uint8_t> generateRandomDataBlock(size_t size)
    {
        // 创建一个大小为 'size' 的向量用于存储数据块
        std::vector<uint8_t> data_block(size);
    
        // 使用随机设备（hardware entropy source）来生成种子
        std::random_device rd;
    
        // 创建一个 Mersenne Twister 随机数生成器，使用 rd 提供的种子进行初始化
        std::mt19937 gen(rd());
    
        // 定义一个分布范围，从 0 到 255（标准的 8 位无符号整数范围）
        std::uniform_int_distribution<uint8_t> dis(0, 255);
    
        // 遍历数据块中的每一个字节
        for (auto &byte : data_block)
        {
            // 为每一个字节生成一个随机值，并赋值给数据块中的当前位置
            byte = dis(gen);
        }
      
        // 返回填充完成的随机数据块
        return data_block;
    }

    // 支持的EDC方法
    std::vector<uint8_t> calculateEDC(const std::vector<uint8_t>& data_block)
    {
        std::vector<uint8_t> edc(EDC_SIZE, 0);
    
        if (edc_type == "checksum")
        {
            uint32_t checksum = 0;
            for (auto byte : data_block)
            {
                checksum += byte;
            }
          
            // 将 checksum 分解为多个字节
            for (size_t i = 0; i < EDC_SIZE; ++i)
            {
                edc[i] = (checksum >> (i * 8)) & 0xFF;
            }
        }
        else if (edc_type == "crc32")
        {
            boost::crc_32_type crc;
            crc.process_bytes(data_block.data(), data_block.size());
            uint32_t crc_value = crc.checksum();
        
            for (size_t i = 0; i < EDC_SIZE && i < 4; ++i)
            {
                edc[i] = (crc_value >> (i * 8)) & 0xFF;
            }
        }
        else if (edc_type == "crc64")
        {
            boost::crc_optimal<64, 0x42F0E1EBA9EA3693> crc64;
            crc64.process_bytes(data_block.data(), data_block.size());
            uint64_t crc_value = crc64.checksum();
        
            for (size_t i = 0; i < EDC_SIZE && i < 8; ++i)
            {
                edc[i] = (crc_value >> (i * 8)) & 0xFF;
            }
        }
      
        return edc;
    }
    // 支持的ECC方法
    std::vector<uint8_t> calculateECC(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> ecc;

        // TODO: 使用更成熟的ECC库直接调用

        if (ecc_type == "hamming")
        {
            ecc = HammingEncode(data_block, ecc_size);
        }
        else if (ecc_type == "rs")
        {
            ecc = ReedSolomonEncode(data_block, ecc_size);
        }
        else if (ecc_type == "bch")
        {
            ecc = BCHEncode(data_block, ecc_size);
        }
        else
        {
            std::cerr << "[ECCPlugin] Error: Unsupported ECC type!" << std::endl;
            exit(1);
        }

        return ecc;
    }

    // 简化版Hamming
    std::vector<uint8_t> HammingEncode(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> ecc(ecc_size, 0);  // 初始化为指定长度

        for (size_t i = 0; i < ecc_size; i++)
        {
            uint8_t parity = 0;
            for (size_t j = 0; j < data_block.size(); j++)
            {
                parity ^= data_block[j];
            }
            ecc[i] = parity;  // 简单方案：将 parity 重复填充到 ecc
        }

        return ecc;
    }

    // RS Encoder
    std::vector<uint8_t> ReedSolomonEncode(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> codeword(data_block.size() + ecc_size, 0);
    
        int m = 7;  // Galois 字长
        int t = ecc_size / 2;
    
        reedSolomon rs(m, t);
        rs.gen_rand_msg();
        rs.encode();
    
        int n = rs.get_n();
        int* c_x = rs.get_c_x();
    
        for (int i = 0; i < n; ++i)
        {
            codeword[i] = static_cast<uint8_t>(c_x[i]);
        }
    
        return codeword;
    }
    

    // RS Decoder
    bool ReedSolomonDecode(std::vector<uint8_t>& data_block, const std::vector<uint8_t>& ecc_codeword)
    {
        int m = 7;
        int t = ecc_codeword.size() / 2;
    
        reedSolomon rs(m, t);
    
        int n = rs.get_n();
        int* rc_x = new int[n];
        rs.set_rc_x(rc_x);
    
        for (int i = 0; i < data_block.size(); ++i)
        {
            rc_x[i] = static_cast<int>(data_block[i]);
        }
    
        for (int i = 0; i < ecc_codeword.size(); ++i)
        {
            rc_x[data_block.size() + i] = static_cast<int>(ecc_codeword[i]);
        }
    
        rs.decode();
    
        if (rs.compare())
        {
            int k = rs.get_k();
            int* dc_x = rs.get_dc_x();
    
            data_block.clear();
            for (int i = 0; i < k; ++i)
            {
                data_block.push_back(static_cast<uint8_t>(dc_x[i]));
            }
            return true;
        }
    
        return false;
    }
    
    // 简化版BCH
    std::vector<uint8_t> BCHEncode(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> ecc(ecc_size, 0);  // 初始化为指定长度

        uint8_t parity = 0;
        for (auto bit : data_block)
        {
            parity ^= bit;
        }

        for (size_t i = 0; i < ecc_size; i++)
        {
            ecc[i] = parity;  // 简单方案：将 parity 重复填充到 ecc
        }

        return ecc;
    }

    

    // 简单的ECC解码器接口
    bool decodeECC(std::vector<uint8_t>& data_block, const std::vector<uint8_t>& ecc_codeword)
    {
        // TODO: 真正实现解码和错误纠正

        if (ecc_type == "hamming")
        {
            
        }
        else if (ecc_type == "rs")
        {
            return ReedSolomonDecode(data_block, ecc_codeword);
        }
        else if (ecc_type == "bch")
        {
           
        }

        // 这里为了演示，总是假设纠错成功
        return true;
    }

    // 计算 binomial 分布的 CDF：P(错误数 <= k)
    double binomial_cdf_up_to(int k, int n, double q)
    {
        if (k < 0) return 0.0;
        if (k >= n) return 1.0;

        double cdf = 0.0;
        double p_i = pow(1.0 - q, n);  // p(0)

        cdf += p_i;

        for (int i = 1; i <= k; ++i)
        {
            double multiplier = (n - i + 1) / (double)i * (q / (1.0 - q));
            p_i *= multiplier;
            cdf += p_i;
        }

        return cdf;
    }

    // 给定总符号数，求最小t
    int find_minimum_t(int n_total, double bit_error_rate, int symbol_size_bits, double max_failure_prob)
    {
        double p = bit_error_rate;
        double s = symbol_size_bits;
        double q = 1.0 - pow(1.0 - p, s);  // 符号出错率

        int max_t = n_total / 2;

        for (int t = 0; t <= max_t; ++t)
        {
            double cdf_t = binomial_cdf_up_to(t, n_total, q);
            double p_fail = 1.0 - cdf_t;

            if (p_fail <= max_failure_prob)
            {
                return t;  // 找到满足的t
            }
        }

        return -1;  // 找不到
    }

    // 计算给定数据块大小下，动态需要的 ECC 大小
    int calculate_dynamic_ecc_size(size_t data_block_size)
    {
        int n_total = data_block_size;    // 当前数据块符号数
        int symbol_size_bits = 8;         // 1 symbol = 8 bits

        int t = find_minimum_t(n_total, bit_error_rate, symbol_size_bits, max_failure_prob);

        int dynamic_ecc_size = ECC_SIZE;  // 默认最大保护

        if (t >= 0)
        {
            int required_ecc_size = 2 * t;  // RS/BCH码，2t个奇偶符号
            if (required_ecc_size <= ECC_SIZE)
            {
                dynamic_ecc_size = required_ecc_size;
            }
            else
            {
                std::cerr << "[ECCPlugin] Warning: Required ECC (" << required_ecc_size 
                          << ") exceeds maximum ECC size (" << ECC_SIZE 
                          << "), using max ECC!" << std::endl;
            }
        }
        else
        {
            std::cerr << "[ECCPlugin] Warning: Cannot meet target failure probability even with maximum ECC size!" << std::endl;
        }

        return dynamic_ecc_size;
    }

    void inject_random_errors(std::vector<uint8_t>& data_block)
    {
        // 每一bit翻转的概率 = bit_error_rate
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> prob_dist(0.0, 1.0);

        for (size_t byte_idx = 0; byte_idx < data_block.size(); ++byte_idx)
        {
            for (int bit_idx = 0; bit_idx < 8; ++bit_idx)
            {
                double prob = prob_dist(gen);
                if (prob < bit_error_rate)
                {
                    // 翻转这个bit
                    data_block[byte_idx] ^= (1 << bit_idx);
                }
            }
        }
    }

    // TODO: 更多的变量计算

    // 在模拟结束时调用，可用于输出数据，清理数据等
    void finalize() override
    {
        std::cout << "[ECCPlugin] Finalizing, clearing memory storage..." << std::endl;
    
        // 清理所有保存的数据
        m_data_storage.clear();
        m_ecc_storage.clear();

        std::cout << "[ECCPlugin] Storage cleared." << std::endl;
    }
  };

} // namespace Ramulator
