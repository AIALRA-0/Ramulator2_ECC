#include <vector>
#include <unordered_map>
#include <limits>
#include <random>
#include <filesystem>
#include <fstream>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

// For CRC EDC computation (Boost)
#include <boost/crc.hpp>  

// For RS ECC computation 
#include "reedSolomon.h"

namespace Ramulator
{

  class ECCPlugin : public IControllerPlugin, public Implementation
  {
    RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, ECCPlugin, "ECCPlugin", "This plugin adds large-size ECC/EDC emulation to Ramulator2 to evaluate memory reliability, bandwidth, and latency trade-offs in AI and HPC workloads.")
  

  private:
    std::string ecc_type;  // ECC type
    std::string edc_type;  // EDC type

    // ECC/EDC library configuration
    
  protected:
    IDRAM *m_dram = nullptr;
    IDRAMController *m_ctrl = nullptr;

    // Internal plugin storage structures
    std::unordered_map<Addr_t, std::vector<uint8_t>> m_data_storage; // Data + EDC storage
    std::unordered_map<Addr_t, std::vector<uint8_t>> m_ecc_storage;  // ECC storage

    // Configuration parameters
    size_t DATA_BLOCK_SIZE;  // Data block size
    size_t EDC_SIZE;         // EDC size
    size_t ECC_SIZE;         // ECC size
    double bit_error_rate;   // Bit Error Rate (BER)
    double max_failure_prob; // Maximum allowed failure probability

    // Performance parameters
    // TODO: Needs more accurate estimation
    double BUS_BW_GBs = 512.0;     // 512 GB/s HBM
    double MEM_RD_BW_GBs = 512.0;  // 512 GB/s
    double MEM_WR_BW_GBs = 512.0;  // 512 GB/s
    double EDC_COMPUTE_PER_BYTE_NS = 0.01;
    double ECC_COMPUTE_PER_BYTE_NS = 0.02;

    // Statistics variables
    int total_ecc_size = 0;      // Total ECC memory usage (in bytes)
    int total_edc_size = 0;      // Total EDC memory usage (in bytes)
    int edc_success_count = 0;   // Number of successful EDC checks
    int edc_failure_count = 0;   // Number of failed EDC checks
    int ecc_success_count = 0;   // Number of successful ECC corrections
    int ecc_failure_count = 0;   // Number of failed ECC corrections
    // int total_corrected_bits = 0;
    // int total_write_latency_ns = 0;
    // int total_read_latency_ns = 0;
    // int total_raw_write_latency_ns = 0;
    // int total_raw_read_latency_ns = 0;
    // int total_ecc_write_latency_ns = 0;
    // int total_ecc_read_latency_ns = 0;
    // int total_transmission_latency_ns = 0;
    // int total_memory_write_latency_ns = 0;
    // int total_memory_read_latency_ns = 0;
    // int total_edc_check_latency_ns = 0;
    // int total_ecc_check_latency_ns = 0;
    // int total_edc_compute_latency_ns = 0;
    // int total_ecc_compute_latency_ns = 0;

  public:
    // Initialize plugin parameters
    void init() override
    {
      // Read configuration parameters
      // VariableName = param<VariableType>("ParameterName").desc("Description").default_val(DefaultValue);

      // Load parameters from configuration file
      DATA_BLOCK_SIZE = param<size_t>("data_block_size").desc("Size of each data block in bytes.").default_val(128);
      EDC_SIZE = param<size_t>("edc_size").desc("Size of EDC in bytes.").default_val(4);
      ECC_SIZE = param<size_t>("ecc_size").desc("Size of ECC in bytes.").default_val(8);

      ecc_type = param<std::string>("ecc_type").desc("ECC type to use: hamming, rs, bch.").default_val("bch");
      edc_type = param<std::string>("edc_type").desc("EDC type to use: checksum, crc32, crc64.").default_val("crc32");
      bit_error_rate = param<double>("bit_error_rate").desc("Raw bit error rate (BER)").default_val(1e-6);
      max_failure_prob = param<double>("max_failure_prob").desc("Maximum allowed failure probability").default_val(1e-14);
      
      // Register runtime statistics
      // register_stat(VariableName).name("OutputStatName");
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

      // Input parameters
      register_stat(DATA_BLOCK_SIZE).name("config_data_block_size");
      register_stat(EDC_SIZE).name("config_edc_size");
      register_stat(ECC_SIZE).name("config_ecc_size");
      register_stat(bit_error_rate).name("config_bit_error_rate");
      register_stat(max_failure_prob).name("config_max_failure_prob");
          
      // Bandwidth parameters
      register_stat(BUS_BW_GBs).name("param_bus_bw_GBs");
      register_stat(MEM_RD_BW_GBs).name("param_mem_read_bw_GBs");
      register_stat(MEM_WR_BW_GBs).name("param_mem_write_bw_GBs");
      register_stat(EDC_COMPUTE_PER_BYTE_NS).name("param_edc_compute_ns_per_byte");
      register_stat(ECC_COMPUTE_PER_BYTE_NS).name("param_ecc_compute_ns_per_byte");
    };

    // Set up the connection between the plugin, memory controller, and frontend interface
    void setup(IFrontEnd *frontend, IMemorySystem *memory_system) override
    {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;
    };

    // Called every time a memory request is scheduled
    void update(bool request_found, ReqBuffer::iterator &req_it) override  // "base/request.h"类
    {
      if (request_found)
      {
        // If no request is found, return immediately
        if (!request_found)
            return;

        // Only perform ECC when a valid request is found

        // Handle write requests (WRITE)
        if (req_it->type_id == Request::Type::Write)
        {
            
            // 1. WRITE request: memory controller receives a write request (WRITE command), preparing to store a new data block from the high-speed parallel bus
            
            // Get the target address of the command
            Addr_t addr = req_it->addr;

            // Data to be written
            std::vector<uint8_t> data_block;

            // Check if the payload is availabl
            if (req_it->m_payload != nullptr)
            {
                // TODO: Process user-provided data (m_payload already exists)

                // You may need to cast m_payload to uint8_t* or std::vector<uint8_t> depending on your use case
                // Here assuming m_payload is a uint8_t* type and size is DATA_BLOCK_SIZE
                uint8_t* raw_data = static_cast<uint8_t*>(req_it->m_payload);
                data_block.assign(raw_data, raw_data + DATA_BLOCK_SIZE);
            }
            else
            {
                // m_payload not present, generate a data block using generateRandomDataBlock()
                data_block = generateRandomDataBlock(DATA_BLOCK_SIZE);
                inject_random_errors(data_block);
            }

            // EDC computation: compute EDC value for the new data block and append it to the end
            std::vector<uint8_t> edc_value = calculateEDC(data_block);
            data_block.insert(data_block.end(), edc_value.begin(), edc_value.end());

            // Dynamically calculate required ECC size
            int dynamic_ecc_size = calculate_dynamic_ecc_size(data_block.size());

            // ECC computation: compute ECC codeword for the new data block and store it in the ECC memory region
            std::vector<uint8_t> ecc_value = calculateECC(data_block, dynamic_ecc_size);

            // Data write: store [Data + EDC] in the data region
            m_data_storage[addr] = data_block;

            // ECC write: store ECC codeword separately
            m_ecc_storage[addr] = ecc_value;

            total_edc_size += EDC_SIZE;
            total_ecc_size += m_ecc_storage[addr].size();
        }

        // Handle read requests (READ)
        if (req_it->type_id == Request::Type::Read)
        {
            // Get the target address of the command
            Addr_t addr = req_it->addr;

            // Check if data block exists, if not, create one
            if (m_data_storage.find(addr) == m_data_storage.end())
            {
                // std::cerr << "[ECCPlugin] Data block not found! Generating fake data block..." << std::endl;
            
                std::vector<uint8_t> fake_data = generateRandomDataBlock(DATA_BLOCK_SIZE);
                std::vector<uint8_t> fake_edc = calculateEDC(fake_data);
            
                std::vector<uint8_t> fake_data_block_with_edc = fake_data;
                fake_data_block_with_edc.insert(fake_data_block_with_edc.end(), fake_edc.begin(), fake_edc.end());
                inject_random_errors(fake_data_block_with_edc);   // Inject random bit errors
                m_data_storage[addr] = fake_data_block_with_edc;
                }

            /// Read existing data block (with EDC)
            std::vector<uint8_t>& data_block_with_edc = m_data_storage[addr];

            // Ensure block size is correct (in case of corruption)
            if (data_block_with_edc.size() != DATA_BLOCK_SIZE + EDC_SIZE)
            {
                // std::cerr << "[ECCPlugin] Data block size mismatch! Regenerating EDC..." << std::endl;

                std::vector<uint8_t> fixed_data_block = data_block_with_edc;
                fixed_data_block.resize(DATA_BLOCK_SIZE, 0);  // Pad to standard size
                std::vector<uint8_t> fake_edc = calculateEDC(fixed_data_block);

                fixed_data_block.insert(fixed_data_block.end(), fake_edc.begin(), fake_edc.end());
                m_data_storage[addr] = fixed_data_block;
            }

            // Check and regenerate ECC if missing
            if (m_ecc_storage.find(addr) == m_ecc_storage.end())
            {
                // std::cerr << "[ECCPlugin] ECC not found! Generating fake ECC..." << std::endl;
                int dynamic_fake_ecc_size = calculate_dynamic_ecc_size(m_data_storage[addr].size());
                std::vector<uint8_t> fake_ecc = calculateECC(m_data_storage[addr], dynamic_fake_ecc_size);
                m_ecc_storage[addr] = fake_ecc;
            }

            // Separate data block and old EDC
            std::vector<uint8_t> data_block(data_block_with_edc.begin(), data_block_with_edc.begin() + DATA_BLOCK_SIZE);
            std::vector<uint8_t> old_edc(data_block_with_edc.begin() + DATA_BLOCK_SIZE, data_block_with_edc.end());
            
            // EDC verification: controller reads the data block and its corresponding EDC, then performs EDC check
            std::vector<uint8_t> expected_edc = calculateEDC(data_block);
            bool edc_pass = (expected_edc == old_edc);

            if (edc_pass)
            {
                edc_success_count++;

                // If EDC passes, return data directly
                if (req_it->m_payload != nullptr)
                {
                    std::memcpy(req_it->m_payload, data_block.data(), DATA_BLOCK_SIZE);
                }
            }

            // EDC failure: if EDC check fails, data may be corrupted, trigger ECC correction process
            else
            {   
                edc_failure_count++;

                // Read ECC codeword: memory controller retrieves full ECC codeword
                // std::cerr << "[ECCPlugin] Warning: EDC failed. Attempting ECC correction..." << std::endl;
                if (m_ecc_storage.find(addr) == m_ecc_storage.end())
                {
                    // std::cerr << "[ECCPlugin] Read Error: ECC not found for address!" << std::endl;
                    return;
                }
                // Perform ECC correction using ECC algorithm
                std::vector<uint8_t> ecc_codeword = m_ecc_storage[addr];

                // TODO: Invoke actual ECC decoder
                bool corrected = decodeECC(data_block, ecc_codeword);

                // Correction succeeded: if number of errors ≤ t, ECC successfully repairs data and writes updated ECC/EDC
                if (corrected)
                {
                    ecc_success_count++;

                    // std::cerr << "[ECCPlugin] ECC Correction Success." << std::endl;
                
                    // Recalculate EDC and ECC
                    std::vector<uint8_t> new_edc = calculateEDC(data_block);
                    std::vector<uint8_t> new_data_block_with_edc = data_block;
                    new_data_block_with_edc.insert(new_data_block_with_edc.end(), new_edc.begin(), new_edc.end());
                
                    m_data_storage[addr] = new_data_block_with_edc;
                    
                    int dynamic_new_ecc_size = calculate_dynamic_ecc_size(m_data_storage[addr].size());
                    std::vector<uint8_t> new_ecc = calculateECC(new_data_block_with_edc, dynamic_new_ecc_size);
                    m_ecc_storage[addr] = new_ecc;
                
                    // Return corrected data
                    if (req_it->m_payload != nullptr)
                    {
                        std::memcpy(req_it->m_payload, data_block.data(), DATA_BLOCK_SIZE);
                    }
                }
                // Correction failed: if ECC fails, mark as uncorrectable error (UE)
                else
                {
                    ecc_failure_count++;
                    
                    // ECC correction failed, mark as UE
                    // std::cerr << "[ECCPlugin] UE: Uncorrectable error during read!" << std::endl;

                    // Retry logic: controller may attempt a retry to re-read data and ECC
                    bool retry_success = false;
                    
                    // Simulate retry logic
                    if (retry_success)
                    {
                        // TODO: Retry read
                        // std::cerr << "[ECCPlugin] Retry succeeded!" << std::endl;
                    }
                    else
                    {
                        // TODO: Support for RAID/mirroring
                        bool raid_recovery_success = false;
                        if (raid_recovery_success)
                        {
                            // std::cerr << "[ECCPlugin] Recovery from RAID redundancy success." << std::endl;
                        }
                        else
                        {
                            // Cannot recover, report fatal UE to CPU/system
                            // std::cerr << "[ECCPlugin] Fatal UE reported to CPU." << std::endl;
                            // TODO: Notify upper-level system / terminate simulation / trigger error handling logic
                        }
                    }
                }
            }

            // If EDC passes, data is verified and returned directly over the high-speed bus
        }

        // Handle partial write request (Partial WRITE)
        if (req_it->type_id == Request::Type::PartialWrite) // Extended request type
        {
            // TODO: Handle partial write logic

            // Get the target address of the command
            Addr_t addr = req_it->addr;
            
            // Read the old [Data + EDC]
            auto& data_block_with_edc = m_data_storage[addr];

            std::vector<uint8_t> old_data(data_block_with_edc.begin(), data_block_with_edc.begin() + DATA_BLOCK_SIZE);
            std::vector<uint8_t> old_edc(data_block_with_edc.begin() + DATA_BLOCK_SIZE, data_block_with_edc.end());

            // Verify EDC
            if (calculateEDC(old_data) != old_edc) 
            {
                // EDC verification failed -> full ECC decode is needed first
                // std::cerr << "[ECCPlugin] Partial Write: EDC check failed, need full ECC decoding!" << std::endl;
                // TODO: Call ECCDecode to repair old data
                // ECCDecode(old_data, m_ecc_storage[addr]);
            }

            // Update partial region of the data block
            // TODO: Extend Request structure to support offset and length
            // size_t offset = req_it->m_offset;  
            // size_t length = req_it->m_length;    
            size_t offset = 0;  
            size_t length = 0;  

            // Extract old data chunk and get new chunk to write
            uint8_t* new_data_ptr = static_cast<uint8_t*>(req_it->m_payload);
            std::vector<uint8_t> old_chunk(old_data.begin() + offset, old_data.begin() + offset + length);
            std::vector<uint8_t> new_chunk(new_data_ptr, new_data_ptr + length);

            // Update the old data
            std::copy(new_chunk.begin(), new_chunk.end(), old_data.begin() + offset);

            // Partial write command: update only the modified region and incrementally update ECC to reduce computation
            std::vector<uint8_t> &old_ecc = m_ecc_storage[addr];

            std::vector<uint8_t> enc_old_chunk = ReedSolomonEncode(old_chunk, old_ecc.size());
            std::vector<uint8_t> enc_new_chunk = ReedSolomonEncode(new_chunk, old_ecc.size());

            for (size_t i = 0; i < old_ecc.size(); i++) {
                old_ecc[i] ^= enc_old_chunk[i] ^ enc_new_chunk[i];
            }

            // Recalculate EDC
            std::vector<uint8_t> new_edc = calculateEDC(old_data);

            // Write back updated [Data + EDC]
            std::vector<uint8_t> new_data_block_with_edc = old_data;
            new_data_block_with_edc.insert(new_data_block_with_edc.end(), new_edc.begin(), new_edc.end());
            m_data_storage[addr] = new_data_block_with_edc;
        }  
      }
    };

    // Function to generate a random data block
    std::vector<uint8_t> generateRandomDataBlock(size_t size)
    {
        // Create a vector of size 'size' to store the data block
        std::vector<uint8_t> data_block(size);
    
        // Use a hardware entropy source to generate a seed
        std::random_device rd;
    
        // Create a Mersenne Twister random number generator using the seed
        std::mt19937 gen(rd());
    
        // Define a uniform distribution in the range 0 to 255 (standard 8-bit unsigned int)
        std::uniform_int_distribution<uint8_t> dis(0, 255);
    
        // Iterate through each byte in the data block
        for (auto &byte : data_block)
        {
            // Generate a random value and assign it to the current byte
            byte = dis(gen);
        }
      
        // Return the fully populated random data block
        return data_block;
    }

    // Supported EDC calculation methods
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
          
            // Break the checksum into multiple bytes
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

    // Supported ECC methods
    std::vector<uint8_t> calculateECC(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> ecc;

        // TODO: Replace with more robust ECC libraries if needed

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
            // std::cerr << "[ECCPlugin] Error: Unsupported ECC type!" << std::endl;
            exit(1);
        }

        return ecc;
    }

    // Simplified Hamming encoder (parity-based)
    std::vector<uint8_t> HammingEncode(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> ecc(ecc_size, 0);  // Initialize with specified size

        for (size_t i = 0; i < ecc_size; i++)
        {
            uint8_t parity = 0;
            for (size_t j = 0; j < data_block.size(); j++)
            {
                parity ^= data_block[j];
            }
            ecc[i] = parity;  // Repeat parity for all ECC bytes
        }

        return ecc;
    }

    // RS Encoder
    std::vector<uint8_t> ReedSolomonEncode(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> codeword(data_block.size() + ecc_size, 0);
    
        int m = 7;  // Galois field bit width
        int t = ecc_size / 2;  // Error correction capability
    
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
    
    // Simplified BCH encoder (basic parity-based mock)
    std::vector<uint8_t> BCHEncode(const std::vector<uint8_t>& data_block, size_t ecc_size)
    {
        std::vector<uint8_t> ecc(ecc_size, 0);  // Initialize ECC vector to desired size

        uint8_t parity = 0;
        for (auto bit : data_block)
        {
            parity ^= bit;  // Compute simple parity across all data
        }

        for (size_t i = 0; i < ecc_size; i++)
        {
            ecc[i] = parity;  // Fill ECC vector with repeated parity value
        }

        return ecc;
    }

    

    // Simplified ECC decoder interface
    bool decodeECC(std::vector<uint8_t>& data_block, const std::vector<uint8_t>& ecc_codeword)
    {
        // TODO: Implement real decoding logic for each ECC type

        if (ecc_type == "hamming")
        {
            // Hamming decoding not implemented
        }
        else if (ecc_type == "rs")
        {
            return ReedSolomonDecode(data_block, ecc_codeword);  // Use RS decoder
        }
        else if (ecc_type == "bch")
        {
            // BCH decoding not implemented
        }

        // For demonstration purposes, always assume correction succeeded
        return true;
    }

    // Compute binomial cumulative distribution function: P(number of errors ≤ k)
    double binomial_cdf_up_to(int k, int n, double q)
    {
        if (k < 0) return 0.0;
        if (k >= n) return 1.0;

        double cdf = 0.0;
        double p_i = pow(1.0 - q, n);  // Initial term: probability of zero errors

        cdf += p_i;

        for (int i = 1; i <= k; ++i)
        {
            double multiplier = (n - i + 1) / static_cast<double>(i) * (q / (1.0 - q));
            p_i *= multiplier;
            cdf += p_i;
        }

        return cdf;
    }

        // Given total number of symbols, find the minimum t (number of correctable symbols)
    int find_minimum_t(int n_total, double bit_error_rate, int symbol_size_bits, double max_failure_prob)
    {
        double p = bit_error_rate;           // Raw bit error rate (BER)
        double s = symbol_size_bits;         // Number of bits per symbol
        double q = 1.0 - pow(1.0 - p, s);    // Probability that a symbol is corrupted

        int max_t = n_total / 2;             // Maximum number of symbols that can be corrected (theoretically)

        for (int t = 0; t <= max_t; ++t)
        {
            double cdf_t = binomial_cdf_up_to(t, n_total, q);  // Probability of ≤ t symbol errors
            double p_fail = 1.0 - cdf_t;                       // Failure probability (more than t errors)

            if (p_fail <= max_failure_prob)
            {
                return t;  // Found the smallest t that satisfies the failure probability constraint
            }
        }

        return -1;  // Not possible to meet the failure probability with available t
    }

    // Calculate the dynamically required ECC size based on current data block size and error target
    int calculate_dynamic_ecc_size(size_t data_block_size)
    {
        int n_total = data_block_size;       // Total number of symbols (assuming 1 byte = 1 symbol)
        int symbol_size_bits = 8;            // Each symbol is 8 bits

        // Determine the minimum t to meet the required failure probability
        int t = find_minimum_t(n_total, bit_error_rate, symbol_size_bits, max_failure_prob);

        int dynamic_ecc_size = ECC_SIZE;     // Default to maximum ECC size configured

        if (t >= 0)
        {
            int required_ecc_size = 2 * t;   // RS/BCH codes need 2t parity symbols to correct t errors
            if (required_ecc_size <= ECC_SIZE)
            {
                dynamic_ecc_size = required_ecc_size;
            }
            else
            {
                // Warning: Needed ECC size exceeds the allowed maximum — fallback to max ECC
            }
        }
        else
        {
            // Warning: Cannot meet error tolerance even with maximum ECC
        }

        return dynamic_ecc_size;
    }


    // Inject random bit errors into the data block according to the configured bit error rate
    void inject_random_errors(std::vector<uint8_t>& data_block)
    {
        // Each bit is flipped with a probability equal to bit_error_rate
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
                    // Flip the bit
                    data_block[byte_idx] ^= (1 << bit_idx);
                }
            }
        }
    }

    // TODO: Add more variable calculations if needed

    // Called at the end of simulation — used to output final logs and clean up data
    void finalize() override
    {
        std::cout << "[ECCPlugin] Finalizing, clearing memory storage..." << std::endl;

        // Clear all stored data blocks and ECC codewords
        m_data_storage.clear();
        m_ecc_storage.clear();

        std::cout << "[ECCPlugin] Storage cleared." << std::endl;
    }
  };

} // namespace Ramulator
