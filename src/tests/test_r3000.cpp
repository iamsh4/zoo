#include <array>
#include <shared/types.h>
#include <guest/r3000/r3000.h>

#include <gtest/gtest.h>

// https://pastebin.com/im9bBBTG

// 1MiB addressable RAM
#define RAM_SIZE (1024 * 1024)
#define MAX_ADDRESS RAM_SIZE

class R3000Fixture : public ::testing::Test {
protected:
  void SetUp() override
  {
    memory_table = std::make_unique<fox::MemoryTable>(MAX_ADDRESS, MAX_ADDRESS);
    memory_table->map_sdram(0, RAM_SIZE, "test_ram_1_MiB");
    memory_table->finalize();

    r3000 = std::make_unique<guest::r3000::R3000>(memory_table.get());
  }

  std::unique_ptr<fox::MemoryTable> memory_table;
  std::unique_ptr<guest::r3000::R3000> r3000;
};

#if 0
void generate_ir() {

  bool has_pending_load = false;
  Operand pending_load;
  u32 pending_load_target_register;

  while(true) {
    Instruction ins = get_instruction(PC);
    u32 target_PC = get_ins_target_register(PC);

    // Read stage
    Operand rs = read_guest(ins.rs);
    Operand rt = read_guest(ins.rt);

    // Retire-Load stage
    if(has_pending_load) {
      write_guest_reg(pending_load_target_register, pending_load);
      has_pending_load = false;
    }
    
    // 
    InsResult {Operand result; ... } = do_ins(ins);

    // Do write-back for the current instruction (some have none)
    write_guest_reg(ins.rd, result);
  }
}
#endif

#if 0
// u32
// shift(const u32 a, const u32 b)
// {
//         return a >> b;
// }
constexpr u32 program_shift[] = {
  0x27bdfff8, // addiu  sp,sp,-8
  0xafbe0004, // sw     s8,4(sp)
  0x03a0f025, // move   s8,sp
  0xafc40008, // sw     a0,8(s8)
  0xafc5000c, // sw     a1,12(s8)
  0x8fc30008, // lw     v1,8(s8)
  0x8fc2000c, // lw     v0,12(s8)
  0x00000000, // nop
  0x00431006, // srlv   v0,v1,v0
  0x03c0e825, // move   sp,s8
  0x8fbe0004, // lw     s8,4(sp)
  0x27bd0008, // addiu  sp,sp,8
  0x03e00008, // jr     ra
  0x00000000, // nop
};

// u32
// rotate(const u32 a, const u32 b)
// {
//         return (a >> b) || (a << (32 - b));
// }
constexpr u32 program_rotate[] = {
  0x27bdfff8, // addiu  sp,sp,-8
  0xafbe0004, // sw     s8,4(sp)
  0x03a0f025, // move   s8,sp
  0xafc40008, // sw     a0,8(s8)
  0xafc5000c, // sw     a1,12(s8)
  0x8fc30008, // lw     v1,8(s8)
  0x8fc2000c, // lw     v0,12(s8)
  0x00000000, // nop
  0x00431006, // srlv   v0,v1,v0
  0x1440000a, // bnez   v0,88 <rotate+0x50>
  0x00000000, // nop
  0x24030020, // li     v1,32
  0x8fc2000c, // lw     v0,12(s8)
  0x00000000, // nop
  0x00621023, // subu   v0,v1,v0
  0x8fc30008, // lw     v1,8(s8)
  0x00000000, // nop
  0x00431004, // sllv   v0,v1,v0
  0x10400004, // beqz   v0,94 <rotate+0x5c>
  0x00000000, // nop
  0x24020001, // li     v0,1
  0x10000002, // b      98 <rotate+0x60>
  0x00000000, // nop
  0x00001025, // move   v0,zero
  0x03c0e825, // move   sp,s8
  0x8fbe0004, // lw     s8,4(sp)
  0x27bd0008, // addiu  sp,sp,8
  0x03e00008, // jr     ra
  0x00000000, // nop
};

// u32
// many_bitwise(const u32 a, const u32 b)
// {
//         return ((a >> b) | ~b) ^ a;
// }
constexpr u32 program_bitwise[] = {
  0x27bdfff8, // addiu  sp,sp,-8
  0xafbe0004, // sw     s8,4(sp)
  0x03a0f025, // move   s8,sp
  0xafc40008, // sw     a0,8(s8)
  0xafc5000c, // sw     a1,12(s8)
  0x8fc30008, // lw     v1,8(s8)
  0x8fc2000c, // lw     v0,12(s8)
  0x00000000, // nop
  0x00431806, // srlv   v1,v1,v0
  0x8fc2000c, // lw     v0,12(s8)
  0x00000000, // nop
  0x00021027, // nor    v0,zero,v0
  0x00621825, // or     v1,v1,v0
  0x8fc20008, // lw     v0,8(s8)
  0x00000000, // nop
  0x00621026, // xor    v0,v1,v0
  0x03c0e825, // move   sp,s8
  0x8fbe0004, // lw     s8,4(sp)
  0x27bd0008, // addiu  sp,sp,8
  0x03e00008, // jr     ra
  0x00000000, // nop
};

// u32
// load(const u32 *a)
// {
//         return a[100];
// }
constexpr u32 program_load[] = {
  0x27bdfff8, // addiu  sp,sp,-8
  0xafbe0004, // sw     s8,4(sp)
  0x03a0f025, // move   s8,sp
  0xafc40008, // sw     a0,8(s8)
  0x8fc20008, // lw     v0,8(s8)
  0x00000000, // nop
  0x24420190, // addiu  v0,v0,400
  0x8c420000, // lw     v0,0(v0)
  0x03c0e825, // move   sp,s8
  0x8fbe0004, // lw     s8,4(sp)
  0x27bd0008, // addiu  sp,sp,8
  0x03e00008, // jr     ra
  0x00000000, // nop
};

// void
// store(u32 *const a)
// {
//         a[100] = 5;
// }
constexpr u32 program_store[] = {
  0x27bdfff8, // addiu  sp,sp,-8
  0xafbe0004, // sw     s8,4(sp)
  0x03a0f025, // move   s8,sp
  0xafc40008, // sw     a0,8(s8)
  0x8fc20008, // lw     v0,8(s8)
  0x00000000, // nop
  0x24420190, // addiu  v0,v0,400
  0x24030005, // li     v1,5
  0xac430000, // sw     v1,0(v0)
  0x00000000, // nop
  0x03c0e825, // move   sp,s8
  0x8fbe0004, // lw     s8,4(sp)
  0x27bd0008, // addiu  sp,sp,8
  0x03e00008, // jr     ra
  0x00000000, // nop
};

// void
// loadstore()
// {
//         const u32 a = 5;
//         const u32 b = 3737;
//         u32 *const c = (u32*)100;
//         c[b] = c[a] + b;
//         c[a] = c[b];
// }
constexpr u32 program_loadstore[] = {
  0x27bdffe0, // addiu  sp,sp,-32
  0xafbe001c, // sw     s8,28(sp)
  0x03a0f025, // move   s8,sp
  0x24020005, // li     v0,5
  0xafc20008, // sw     v0,8(s8)
  0x24020e99, // li     v0,3737
  0xafc2000c, // sw     v0,12(s8)
  0x24020064, // li     v0,100
  0xafc20010, // sw     v0,16(s8)
  0x8fc20008, // lw     v0,8(s8)
  0x00000000, // nop
  0x00021080, // sll    v0,v0,0x2
  0x8fc30010, // lw     v1,16(s8)
  0x00000000, // nop
  0x00621021, // addu   v0,v1,v0
  0x8c440000, // lw     a0,0(v0)
  0x8fc2000c, // lw     v0,12(s8)
  0x00000000, // nop
  0x00021080, // sll    v0,v0,0x2
  0x8fc30010, // lw     v1,16(s8)
  0x00000000, // nop
  0x00621021, // addu   v0,v1,v0
  0x8fc3000c, // lw     v1,12(s8)
  0x00000000, // nop
  0x00831821, // addu   v1,a0,v1
  0xac430000, // sw     v1,0(v0)
  0x8fc2000c, // lw     v0,12(s8)
  0x00000000, // nop
  0x00021080, // sll    v0,v0,0x2
  0x8fc30010, // lw     v1,16(s8)
  0x00000000, // nop
  0x00621821, // addu   v1,v1,v0
  0x8fc20008, // lw     v0,8(s8)
  0x00000000, // nop
  0x00021080, // sll    v0,v0,0x2
  0x8fc40010, // lw     a0,16(s8)
  0x00000000, // nop
  0x00821021, // addu   v0,a0,v0
  0x8c630000, // lw     v1,0(v1)
  0x00000000, // nop
  0xac430000, // sw     v1,0(v0)
  0x00000000, // nop
  0x03c0e825, // move   sp,s8
  0x8fbe001c, // lw     s8,28(sp)
  0x27bd0020, // addiu  sp,sp,32
  0x03e00008, // jr     ra
  0x00000000, // nop
};
#endif


TEST_F(R3000Fixture, TestCompiles)
{

  ASSERT_TRUE(true);
}

int
main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
