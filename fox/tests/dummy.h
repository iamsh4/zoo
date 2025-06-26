#pragma once

#include <array>

#include "fox/fox_types.h"
#include "fox/ir_assembler.h"
#include "fox/guest.h"

namespace fox {

/*!
 * @brief Assembler implementation that just natively uses the IR bytecode
 *        without any translation from a guest CPU.
 */
class DummyAssembler : public ir::Assembler {
public:
  ir::ExecutionUnit &&assemble()
  {
    return export_unit();
  }
};

/*!
 * @brief Basic guest CPU interface for bytecode backend. Has four integer
 *        registers.
 */
class DummyGuest : public Guest {
  static constexpr unsigned REGISTER_COUNT = 4;

public:
  DummyGuest()
    : m_memory(new u8[0x1000])
  {
    memset(&m_registers[0], 0, sizeof(m_registers));
  }

  ~DummyGuest()
  {
    return;
  }

  std::array<u32, REGISTER_COUNT> &registers()
  {
    return m_registers;
  }

  void *register_base()
  {
    return (void *)&m_registers[0];
  }

  const void *memory_base() const
  {
    return m_memory.get();
  }

  Value guest_register_read(const unsigned index,
                                   const size_t bytes) override final
  {
    assert(bytes == 4);
    return Value { .u32_value = m_registers[index] };
  }

  void guest_register_write(const unsigned index,
                            const size_t bytes,
                            const Value value) override final
  {
    assert(bytes == 4);
    m_registers[index] = value.u32_value;
  }

  Value guest_load(const u32 address, const size_t bytes) override final
  {
    Value result;
    switch (bytes) {
      case 1:
        memcpy(&result.u8_value, &m_memory[address], 1);
        break;
      case 2:
        memcpy(&result.u16_value, &m_memory[address], 2);
        break;
      case 4:
        memcpy(&result.u32_value, &m_memory[address], 4);
        break;
      case 8:
        memcpy(&result.u64_value, &m_memory[address], 8);
        break;
      default:
        assert(false);
    }
    return result;
  }

  void guest_store(const u32 address,
                   const size_t bytes,
                   const Value value) override final
  {
    switch (bytes) {
      case 1:
        memcpy(&m_memory[address], &value.u8_value, 1);
        break;
      case 2:
        memcpy(&m_memory[address], &value.u16_value, 2);
        break;
      case 4:
        memcpy(&m_memory[address], &value.u32_value, 4);
        break;
      case 8:
        memcpy(&m_memory[address], &value.u64_value, 8);
        break;
      default:
        assert(false);
    }
  }

  void print_state()
  {
    printf("DummyGuest:\n");
    printf("\t[R0] => %08x (%d)\n", m_registers[0], m_registers[0]);
    printf("\t[R1] => %08x (%d)\n", m_registers[1], m_registers[1]);
    printf("\t[R2] => %08x (%d)\n", m_registers[2], m_registers[2]);
    printf("\t[R3] => %08x (%d)\n", m_registers[3], m_registers[3]);
  }

private:
  std::unique_ptr<u8[]> m_memory;
  std::array<u32, REGISTER_COUNT> m_registers;
};

}
