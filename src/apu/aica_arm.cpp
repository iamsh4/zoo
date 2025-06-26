#include "apu/aica_arm.h"

namespace apu {

// TODO List
// - Exceptions
// - raise_exception(int exception)
// - raise_fiq
// - reset
// - restore_cpsr
//     case 3:                                  /* Copro */
// if ((ir & 0x0F000000) == 0x0F000000) { /* SWI */
//   raise_exception(EXC_SOFTWARE);

/*
 * Memory map for AICA core's ARM:
 *   00000000 - 007FFFFF: System Memory (WAV memory?)
 *   00800000 - 008027FF: Channel Memory
 *   00802800 - 00802FFF: Common Memory (Shared with SH4?)
 *   00803000 - 00807FFF: DSP Memory
 *
 * The physical range for system memory is 8MiB, but only development consoles
 * and NAOMI arcade boards had 8MiB. Production dreamcasts only have 2MiB.
 */

// Notes
// - All modes other than user are "privileged".
// - All modes other than user and FIQ are "exception" modes.
// - SPSR is only accessible in exception modes.

AicaArm::AicaArm(fox::MemoryTable *mem_table) : guest::arm7di::Arm7DI(mem_table)
{
  reset();
}

template<typename T>
T
AicaArm::mem_read(u32 address)
{
  // Unaligned reads are always re-aligned to the nearest 4-byte boundary.
  address = address & ~(sizeof(T) - 1);

  // TODO : We can actually get rid of this if/else later.
  u32 result;
  if (address < 0x00800000u) {
    /* System Memory */
    result = m_mem->read<T>(address + 0x00800000u);

  } else if (address < 0x00802800u) {
    /* Channel Data */
    result = m_mem->read<T>(address - 0x00800000u + 0x00700000);

  } else if (address < 0x00803000u) {
    /* Common Memory */
    result = m_mem->read<T>(address - 0x00800000u + 0x00700000);

  } else if (address < 0x00808000u) {
    /* DSP Memory */
    result = m_mem->read<T>(address - 0x00800000u + 0x00700000);

  } else {
    /* Not mapped */
    // printf("ARM read32 unmapped 0x%08x @ PC 0x%08x\n", address,
    // m_registers.R[guest::arm7di::Arm7DIRegisterIndex_PC]);
    throw std::logic_error("Not Mapped");
    result = 0;
  }

  // arm read (0000006c, 4, 00000028) 0x00000068
  if (arm7di_debug_enabled() &&
      address != m_registers.R[guest::arm7di::Arm7DIRegisterIndex_PC] && address > 0x40) {
    // printf("farm read (0x%08x, %d, 0x%08x) 0x%08x\n",
    //        address,
    //        sizeof(T),
    //        m_registers.R[15],
    //        result);
  }

  return result;
}

template<typename T>
void
AicaArm::mem_write(u32 address, const T value)
{
  /* Unaligned writes are always re-aligned. */
  address = address & ~(sizeof(T) - 1);

  if (arm7di_debug_enabled() && address > 0x48) {
    // arm write (00000048, 4, 00000040) 0x00000007
    // printf("farm write (0x%08x, %d, 0x%08x) 0x%08x\n",
    //        address,
    //        sizeof(T),
    //        m_registers.R[15],
    //        value);
  }

  if (address < 0x00800000u) {
    /* System Memory */
    m_mem->write<T>(address + 0x00800000u, value);
    return;
  } else if (address < 0x00802800u) {
    /* Channel Memory */
    m_mem->write<T>(address - 0x00800000u + 0x00700000, value);
    return;
  } else if (address < 0x00803000u) {
    /* Common Memory */
    m_mem->write<T>(address - 0x00800000u + 0x00700000, value);
    return;
  } else if (address < 0x00808000u) {
    /* DSP Memory */
    m_mem->write<T>(address - 0x00800000u + 0x00700000, value);
    return;
  } else {
    throw std::logic_error("Not Mapped");
  }
}

fox::Value
AicaArm::guest_load(u32 address, size_t bytes)
{
  fox::Value result;

  if (bytes == 1)
    result = fox::Value { .u8_value = mem_read<u8>(address) };
  else if (bytes == 2)
    result = fox::Value { .u16_value = mem_read<u16>(address) };
  else if (bytes == 4)
    result = fox::Value { .u32_value = mem_read<u32>(address) };
  else {
    assert(false);
    throw std::runtime_error("Unhandled guest load");
  }

#if 0
  if (bytes == 1)
    printf("arm7di::guest_load(size=%d) 0x%08x = 0x%08x\n",
           int(bytes),
           address,
           result.u8_value);
  else if (bytes == 2)
    printf("arm7di::guest_load(size=%d) 0x%08x = 0x%08x\n",
           int(bytes),
           address,
           result.u16_value);
  else if (bytes == 4)
    printf("arm7di::guest_load(size=%d) 0x%08x = 0x%08x\n",
           int(bytes),
           address,
           result.u32_value);
#endif

  return result;
}

void
AicaArm::guest_store(u32 address, size_t bytes, fox::Value value)
{
  if (bytes == 1)
    mem_write<u8>(address, value.u8_value);
  else if (bytes == 2)
    mem_write<u16>(address, value.u16_value);
  else if (bytes == 4)
    mem_write<u32>(address, value.u32_value);
  else {
    assert(false);
    throw std::runtime_error("Unhandled guest store");
  }

#if 0
const u32 pc = m_registers.R[15];
  printf("arm7di::guest_store(size=%d) 0x%08x < 0x%08x pc=0x%08x\n",
           int(bytes),
           address,
           value.u32_value, pc);
#endif
}

void
AicaArm::serialize(serialization::Snapshot &snapshot)
{
  static_assert(sizeof(m_registers) == 480);
  snapshot.add_range("aica.arm.registers", sizeof(m_registers), &m_registers);
}

void
AicaArm::deserialize(const serialization::Snapshot &snapshot)
{
  snapshot.apply_all_ranges("aica.arm.registers", &m_registers);
  m_jit_cache.invalidate_all();
}

} // namespace apu
