# Ramulator2_ECC
This plugin adds large-size ECC/EDC emulation to **Ramulator2** to evaluate memory reliability, bandwidth, and latency trade-offs in AI and HPC workloads.

## ECCPlugin Complete Processing Flow


### 1. Plugin Initialization Stage (`init()`)

- Read configuration parameters:
  - Data block size `DATA_BLOCK_SIZE`
  - EDC size `EDC_SIZE`
  - ECC size `ECC_SIZE`
  - Raw bit error rate `bit_error_rate`
  - Maximum allowed failure probability `max_failure_prob`
  - Selected EDC type (e.g., `crc32`)
  - Selected ECC type (e.g., `bch`)

- Register these input parameters and runtime statistics via `register_stat()`, so that they can be automatically reported at the end of the simulation.

---

### 2. Runtime Phase (`update()`)

#### **When a request is received (`update(bool request_found, ReqBuffer::iterator& req_it)`)**

Determine the request type:

####  Regular Write (`Write`)

1. Extract new data from the request (`req_it`), or generate random data if payload is missing.
2. Calculate the **EDC** checksum for the data block and append it to the end.
3. Calculate the **ECC** codeword for the data block (including EDC).
4. Store:
   - `[Data + EDC]` in `m_data_storage`
   - `[ECC]` in `m_ecc_storage`
5. Update statistics:
   - `total_ecc_size`
   - `total_edc_size`

####  Regular Read (`Read`)

1. Find the `[Data + EDC]` block in `m_data_storage`.
   - If not found, generate a fake data block.
2. Separate the `Data` and `EDC`.
3. Verify `Data` using EDC:
   - **If EDC passes**: return the data directly to the payload.
   - **If EDC fails**:
     - Read the ECC codeword.
     - Perform ECC decoding (`ECCDecode()`) to attempt correction.
     - If correction succeeds:
       - Update `[Data + EDC]` and `[ECC]` in storage.
       - Return the corrected data.
     - If correction fails:
       - Attempt retry, RAID recovery, or report a Fatal UE (Uncorrectable Error).
4. Update statistics:
   - On success or failure, update `edc_success_count`, `edc_failure_count`, `ecc_success_count`, `ecc_failure_count`.


####  Partial Write (`PartialWrite`)

(**Extended command; the payload must specify `offset` and `length`**)

1. Read the existing `[Data + EDC]` from `m_data_storage`.
2. Verify the old `Data` using EDC:
   - If EDC verification fails, perform full ECC decoding to repair.
3. Extract the partial region:
   - **old_chunk** = the original `[offset, offset+length)` region
   - **new_chunk** = new data to write (from the payload)
4. Update the data:
   - Overwrite the `[offset, offset+length)` region in `old_data` with `new_chunk`.
5. **Incrementally update the ECC** (based on the linearity property):
   - Encode both `old_chunk` and `new_chunk` using RS Encode.
   - Update the original ECC:  
     `new_ecc = old_ecc ⊕ Enc(old_chunk) ⊕ Enc(new_chunk)`
6. Recalculate the new EDC for the updated data block.
7. Update `[Data + EDC]` and `[ECC]` in `m_data_storage` and `m_ecc_storage`.

---

### 3. Simulation Finalization (`finalize()`)

- Clear `m_data_storage` and `m_ecc_storage`.
- Output a summary log, e.g., `[ECCPlugin] Storage cleared.`

---

## Algorithms Used

### **Error Detection Code (EDC)**

- **Checksum**
  - Sums all bytes together.
  - Fast to compute, suitable for simple error detection but has weaker detection capabilities.

- **CRC32**
  - 32-bit Cyclic Redundancy Check (CRC) using the Ethernet standard polynomial.
  - Very effective at detecting random bit errors.

- **CRC64**
  - 64-bit Cyclic Redundancy Check.
  - Provides even stronger error detection capability, suitable for large data blocks.

---

### **Error Correction Code (ECC)**

- **Hamming Code (Simplified)**
  - Protects against single-bit errors (simplified model here).

- **Reed-Solomon Code (Simplified)**
  - Symbol-based (byte-level) linear error-correcting code.
  - Real RS codes can correct burst errors and*support incremental updates thanks to their linearity.

- **BCH Code (Simplified)**
  - Capable of correcting multiple random bit errors.
  - Based on polynomial operations, but simplified in this implementation.

---

### **Dynamic ECC Strength Estimation**

- Automatically determines the minimum required ECC strength (i.e., number of correctable errors `t`)  
  based on data block size, raw bit error rate (BER), and the target maximum failure probability.

---

### **Random Error Injection**

- Random bit-flips are injected into the data at runtime based on the configured `bit_error_rate`,  
  simulating real-world hardware error environments.


---

## Build and Execution Scripts

- **build.bat**  
  A script to compile the Ramulator2 project.  
  It handles the standard build process using the provided CMake configuration.

- **exec_HBM3.bat**  
  A script to run simulations using a predefined HBM3 (High Bandwidth Memory 3) configuration.  
  It sets up the simulation environment and launches the corresponding workloads.

- **make_build.bat**  
  A script for initial project setup and compilation.  
  This is typically used for the first-time build or when setting up a new environment.

- **trace_generator.py**  
  A Python script designed to generate synthetic memory access traces for testing and validation purposes.  
  It can create customized read/write patterns to feed into the simulation framework.
