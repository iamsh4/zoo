#include <array>
#include <gtest/gtest.h>

#include "fox/ir/ir_calculator.h"

#if 0
class EAGuest : public Guest {
public:
  std::array<u32, 16> registers;
  std::array<u8, 4096> memory;

  void reset()
  {
    memset(registers.data(), 0, sizeof(u32) * registers.size());
    memset(memory.data(), 0, sizeof(u8) * memory.size());
  }

  Constant guest_register_read(unsigned index, size_t bytes)
  {
    assert(index < registers.size());
    assert(bytes == 4);
    return Constant { .u32_value = registers[index] };
  }
  void guest_register_write(unsigned index, size_t bytes, Constant value)
  {
    assert(index < registers.size());
    assert(bytes == 4);
    registers[index] = value.u32_value;
  }
  Constant guest_load(u32 address, size_t bytes)
  {
    assert(address < memory.size());
    assert(address % bytes == 0);

    if (bytes == 1)
      return Constant { .u8_value = memory[address] };
    else if (bytes == 2)
      return Constant { .u16_value = *(u16 *)&memory[address] };
    else if (bytes == 4)
      return Constant { .u32_value = *(u32 *)&memory[address] };
    else
      assert(false);
  }
  void guest_store(u32 address, size_t bytes, Constant value)
  {
    assert(address < memory.size());
    assert(address % bytes == 0);

    if (bytes == 1)
      memcpy(&memory[address], &value.u8_value, bytes);
    if (bytes == 2)
      memcpy(&memory[address], &value.u16_value, bytes);
    if (bytes == 4)
      memcpy(&memory[address], &value.u32_value, bytes);
    else
      assert(false);
  }
};

class EAFixture : public ::testing::Test {
protected:
  void SetUp() override
  {
    assembler = std::make_unique<ExecutingAssembler>(&guest);
    guest.reset();
  }

  EAGuest guest;
  std::unique_ptr<ExecutingAssembler> assembler;
};

TEST_F(EAFixture, Basic)
{
  const u32 address = 40;

  assembler->store(Operand::constant(address), Operand::constant<u32>(5));
  auto x = assembler->load(Type::Integer32, Operand::constant(address));
  auto y = Operand::constant<u32>(7);
  auto z = assembler->add(x, y);
  assembler->store(Operand::constant(address), z);

  ASSERT_EQ(u32(12), guest.guest_load(address, 4).u32_value);
}
#endif

int
main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
