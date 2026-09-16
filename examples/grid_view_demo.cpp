// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <microfmt/formatters/grid_view.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// Hardware / Driver Data Structures
// ============================================================================

// Standard Cortex-M / RISC-V stacked CPU context
struct cpu_fault_context {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  uint32_t lr;
  uint32_t pc;
  uint32_t psr;
};

// Generic DMA Hardware Channel Descriptor Block
struct dma_channel_hw {
  uint32_t ccr;   // Control register
  uint32_t cndtr; // Number of data items
  uint32_t cpar;  // Peripheral address
  uint32_t cmar;  // Memory address
};

// ============================================================================
// Static Descriptors (.rodata / Flash)
// ============================================================================

namespace descriptors {

// 4-column layout for CPU core dumps
inline constexpr auto CPU_FAULT_GRID = microfmt::reg_grid_desc<uint32_t, 8>{
    "CPU Exception Stack Frame",
    4, // 4 registers per row
    {"R0", "R1", "R2", "R3", "R12", "LR", "PC", "PSR"}};

// 2-column layout for peripheral DMA blocks
inline constexpr auto DMA_CHANNEL_GRID =
    microfmt::reg_grid_desc<uint32_t, 4>{"DMA1 Channel 3 State",
                                         2, // 2 registers per row
                                         {"CCR", "CNDTR", "CPAR", "CMAR"}};

// 8-column layout for raw hardware word arrays
inline constexpr auto WORD_BLOCK_GRID = microfmt::reg_grid_desc<uint16_t, 8>{
    "ADC Conversion FIFO Dump",
    4,
    {"CH0", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7"}};

} // namespace descriptors

// ============================================================================
// 3. Application Workflow
// ============================================================================

int main() {
  auto term = microfmt::stdout_sink();

  // Scenario A: Simulating a HardFault / Panic Dump
  const cpu_fault_context crash_ctx{
      0x00000000, // R0
      0x20000100, // R1 (RAM pointer)
      0x0000002A, // R2
      0xDEADBEEF, // R3 (Magic/poison)
      0x40001000, // R12 (Peripheral base)
      0x080014B3, // LR (Return address in Flash)
      0x08001620, // PC (Fault location in Flash)
      0x21000000  // PSR
  };

  microfmt::println(term, "[SYSTEM] Panic dump requested:");
  microfmt::println(
      term, "{}",
      microfmt::make_reg_grid(crash_ctx, descriptors::CPU_FAULT_GRID));

  // Scenario B: Hardware Peripheral Inspection (DMA Channel)
  const dma_channel_hw dma1_ch3{
      0x000030A1, // Circular mode, half-transfer & transfer-complete IE
      0x00000200, // 512 bytes remaining
      0x40013804, // USART1->RDR
      0x20001800  // SRAM receive buffer
  };

  microfmt::println(term, "[DRV_DMA] Inspecting active channel:");
  microfmt::println(
      term, "{}",
      microfmt::make_reg_grid(dma1_ch3, descriptors::DMA_CHANNEL_GRID));

  // Scenario C: Dumping fixed-size ADC array buffer
  const std::array<uint16_t, 8> adc_samples = {0x03FF, 0x0180, 0x0000, 0x07FE,
                                               0x0400, 0x01F4, 0x0A20, 0x0012};

  microfmt::println(term, "[DRV_ADC] Inspecting sample buffer:");
  microfmt::println(
      term, "{}",
      microfmt::make_reg_grid(adc_samples, descriptors::WORD_BLOCK_GRID));

  return 0;
}