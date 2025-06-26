#include <array>
#include <cstdio>
#include <fstream>
#include <exception>
#include <filesystem>
#include <vector>
#include <fcntl.h>

#include <guest/rv32/rv32.h>
#include <shared/types.h>
#include "shared/exec.h"

#include <gtest/gtest.h>

// 1MiB addressable RAM
#define RAM_SIZE (1024 * 1024)
#define STACK_PTR_INIT 4096
#define MAX_ADDRESS RAM_SIZE

// If we ever reach this as an instruction, we've completed the test successfully
#define RV32_TEST_EXIT_SUCCESS u32(0xCAFECAFE)
#define RV32_TEST_EXIT_FAILURE u32(0xBADBAD00)

static const char *WORK_DIR = "/tmp/penguin-testing/";
static const char *ASSEMBLY_FILE = "/tmp/penguin-testing/test.s";
static const char *OBJECT_FILE = "/tmp/penguin-testing/test.o";

class RV32 : public ::testing::Test {
protected:
  void SetUp() override
  {
    memory_table = std::make_unique<fox::MemoryTable>(MAX_ADDRESS, MAX_ADDRESS);
    memory_table->map_sdram(0, RAM_SIZE, "test_ram_1_MiB");
    memory_table->finalize();

    rv32 = std::make_unique<guest::rv32::RV32>(memory_table.get());
    rv32->add_instruction_set<guest::rv32::RV32I>();
    rv32->add_instruction_set<guest::rv32::RV32M>();
  }

  std::vector<u8> assemble(std::string_view code);
  void prepare_test(std::string_view asm_code);
  u32 run_prog(u32 limit_instructions);

  void run_test_bin(const char* file_path, u32 instruction_limit);

  std::unique_ptr<fox::MemoryTable> memory_table;
  std::unique_ptr<guest::rv32::RV32> rv32;
};

std::vector<u8>
RV32::assemble(std::string_view code)
{
  // Write assembly to scratch file
  std::filesystem::create_directories(WORK_DIR);
  std::ofstream asm_file(ASSEMBLY_FILE);
  asm_file << ".macro exit_success" << std::endl;
  asm_file << "  .word 0xCAFECAFE" << std::endl;
  asm_file << ".endm" << std::endl;
  asm_file << ".macro exit_fail" << std::endl;
  asm_file << "  .word 0xBADBAD00" << std::endl;
  asm_file << ".endm" << std::endl;
  asm_file << code.data();
  asm_file.close();

  if (!strstr(code.data(), "exit_success")) {
    printf("WARNING: Running a test which has no exit_success. "
           "We need this to determine when to stop executing code.\n");
  }

  char buff[4096];
  snprintf(buff,
           sizeof(buff),
           "riscv64-linux-gnu-as -fpic -march=rv32im %s -o %s && "
           "riscv64-linux-gnu-objcopy %s /dev/null --dump-section .text=/dev/stdout",
           ASSEMBLY_FILE,
           OBJECT_FILE,
           OBJECT_FILE);
  // printf("RUNNING '%s'\n", buff);
  return exec(buff);
}

void
RV32::prepare_test(std::string_view asm_code)
{
  std::vector<u8> program_data = assemble(asm_code);
  memory_table->dma_write(
    0, program_data.data(), program_data.size() * sizeof(program_data[0]));

  // Initialize processor, stack at some in-bounds address
  rv32->reset();

  // "The standard calling convention uses register x2 as the stack pointer."
  rv32->registers()[2] = STACK_PTR_INIT;
}

u32
RV32::run_prog(u32 limit_instructions)
{
  for (u32 i = 0; i < limit_instructions; ++i) {
    const u32 pc = rv32->registers()[guest::rv32::Registers::REG_PC];
    if (pc >= MAX_ADDRESS) {
      printf("PC too large 0x%08x\n", pc);
      assert(false && "rv32 pc too large");
    }
    assert((pc % 4) == 0);

    // Check if we've completed the test
    const u32 next_instruction_word = memory_table->read<u32>(pc);
    if (next_instruction_word == RV32_TEST_EXIT_SUCCESS) {
      return i;
    }
    if (next_instruction_word == RV32_TEST_EXIT_FAILURE) {
      const char* gtest_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
      const u32 gp = rv32->registers()[guest::rv32::Registers::REG_X_START + 3];
      fprintf(stderr, "Failed rv32 test '%s' gp=0x%x\n", gtest_name, gp);
      throw std::exception();
    }

    rv32->step();
  }

  return limit_instructions;
}

#include "./riscv_tests/gtest.h"

void RV32::run_test_bin(const char* file_path, u32 instruction_limit)
{
  std::unique_ptr<char[]> program_data = std::make_unique<char[]>(64*1024);
  FILE *f = fopen(file_path, "rb");
  int bytes_read = fread(program_data.get(), 1, 64*1024, f);
  fclose(f);

  // prepare_test(file_content.get());
  memory_table->dma_write(    0, program_data.get(), bytes_read);

  // Initialize processor, stack at some in-bounds address
  rv32->reset();

  // "The standard calling convention uses register x2 as the stack pointer."
  rv32->registers()[2] = STACK_PTR_INIT;

  u32 instructions_executed = run_prog(instruction_limit);
  ASSERT_LT(instructions_executed, instruction_limit);

  const u32 pc = rv32->registers()[guest::rv32::Registers::REG_PC];
  ASSERT_EQ(0xCAFECAFE, memory_table->read<u32>(pc));
}

TEST_F(RV32, ADD_ADDI)
{
  prepare_test(R"(
    addi x5, x0, 0x123
    addi x6, x0, 0x444
    add  x7, x5, x6
    addi x8, x0, -1
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0x123 + 0x444, rv32->registers()[7]);
  ASSERT_EQ(0xffff'ffff, rv32->registers()[8]);
}

TEST_F(RV32, SUB_SUBI)
{
  prepare_test(R"(
    addi x5, x0, 0x345
    addi x6, x5, -0x111
    addi x5, x0, 0x001
    sub  x6, x6, x5
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0x345 - 0x111 - 0x1, rv32->registers()[6]);
}

TEST_F(RV32, AUIPC)
{
  prepare_test(R"(
    add x1, x2, x3
    auipc x5, 10000
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(0x4 + (10000<<12), rv32->registers()[5]);
}

TEST_F(RV32, LUI)
{
  prepare_test(R"(
    lui x1, 0x03578
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(1), instructions_executed);
  ASSERT_EQ(0x03578000, rv32->registers()[1]);
}

TEST_F(RV32, LB_LH_LW)
{
  prepare_test(R"(
    lw x1, 16(x0)
    lh x2, 20(x0)
    lb x3, 22(x0)
    exit_success
    const_lw: .word 0x889abcde
    const_lh: .short 0x8456
    const_lb: .byte 0x82
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0x889abcde, rv32->registers()[1]);
  ASSERT_EQ(0xFFFF8456, rv32->registers()[2]);
  ASSERT_EQ(0xFFFFFF82, rv32->registers()[3]);
}

TEST_F(RV32, LBU_LHU)
{
  prepare_test(R"(
    lhu x2, 12(x0)
    lbu x3, 14(x0)
    exit_success
    const_lh: .short 0x8456
    const_lb: .byte 0x82
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(0x8456, rv32->registers()[2]);
  ASSERT_EQ(0x82, rv32->registers()[3]);
}

TEST_F(RV32, SW)
{
  prepare_test(R"(
    lw x1, 16(x0)
    sw x1, 12(x0)
    exit_success
    store_loc: .word 0
    const_lw:  .word 0x12345678
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(0x12345678, memory_table->read<u32>(12));
}

TEST_F(RV32, SH)
{
  prepare_test(R"(
    lw x1, 16(x0)
    sh x1, 12(x0)
    exit_success
    store_loc: .word 0
    const_lw:  .word 0x12345678
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(0x5678, memory_table->read<u32>(12));
}

TEST_F(RV32, SB)
{
  prepare_test(R"(
    lw x1, 16(x0)
    sb x1, 12(x0)
    exit_success
    store_loc: .word 0
    const_lw:  .word 0x12345678
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(0x78, memory_table->read<u32>(12));
}

TEST_F(RV32, AND)
{
  prepare_test(R"(
    addi x1, x0, 0x123
    addi x2, x0, 0x456
    addi x3, x0, 0x789

    and x4, x1, x2
    and x5, x1, x3
    and x6, x2, x3
    
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(0x123 & 0x456, rv32->registers()[4]);
  ASSERT_EQ(0x123 & 0x789, rv32->registers()[5]);
  ASSERT_EQ(0x456 & 0x789, rv32->registers()[6]);
}

TEST_F(RV32, OR)
{
  prepare_test(R"(
    addi x1, x0, 0x123
    addi x2, x0, 0x456
    addi x3, x0, 0x789

    or x4, x1, x2
    or x5, x1, x3
    or x6, x2, x3
    
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(0x123 | 0x456, rv32->registers()[4]);
  ASSERT_EQ(0x123 | 0x789, rv32->registers()[5]);
  ASSERT_EQ(0x456 | 0x789, rv32->registers()[6]);
}

TEST_F(RV32, XOR)
{
  prepare_test(R"(
    addi x1, x0, 0x123
    addi x2, x0, 0x456
    addi x3, x0, 0x789

    xor x4, x1, x2
    xor x5, x1, x3
    xor x6, x2, x3
    
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(0x123 ^ 0x456, rv32->registers()[4]);
  ASSERT_EQ(0x123 ^ 0x789, rv32->registers()[5]);
  ASSERT_EQ(0x456 ^ 0x789, rv32->registers()[6]);
}

TEST_F(RV32, ANDI_XORI_ORI)
{
  prepare_test(R"(
    addi x1, x0, 0x123
    addi x2, x0, 0x456
    addi x3, x0, 0x789

    andi x4, x1, 0x111
    xori x5, x2, 0x222
    ori  x6, x3, 0x333
    
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(0x123 & 0x111, rv32->registers()[4]);
  ASSERT_EQ(0x456 ^ 0x222, rv32->registers()[5]);
  ASSERT_EQ(0x789 | 0x333, rv32->registers()[6]);
}

TEST_F(RV32, SLLI_SRLI)
{
  prepare_test(R"(
    addi x1, x0, 0x123
    slli x2, x1, 1
    srli x3, x1, 1
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0x123 << 1, rv32->registers()[2]);
  ASSERT_EQ(0x123 >> 1, rv32->registers()[3]);
}

TEST_F(RV32, SLL_SRL)
{
  prepare_test(R"(
    addi x1, x0, 0x123
    addi x2, x0, 0x1
    sll x3, x1, x2
    srl x4, x1, x2
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0x123 << 1, rv32->registers()[3]);
  ASSERT_EQ(0x123 >> 1, rv32->registers()[4]);
}

TEST_F(RV32, SRAI_SRLI_SLLI)
{
  prepare_test(R"(
    lw x1, 20(x0)
    srai x2, x1, 4
    srli x3, x2, 1
    slli x4, x3, 2
    exit_success
    const_lw:  .word 0x80000000
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0xf8000000, rv32->registers()[2]);
  ASSERT_EQ(0x7c000000, rv32->registers()[3]);
  ASSERT_EQ(0xf0000000, rv32->registers()[4]);
}

TEST_F(RV32, SLT_SLTU)
{
  prepare_test(R"(
    lw    x1, 28(x0)
    addi  x2, x0, 1
    slt   x3, x1, x2
    sltu  x4, x1, x2
    slt   x5, x2, x1
    sltu  x6, x2, x1
    exit_success
    const_lw:  .word 0x80000000
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(1, rv32->registers()[3]);
  ASSERT_EQ(0, rv32->registers()[4]);
  ASSERT_EQ(0, rv32->registers()[5]);
  ASSERT_EQ(1, rv32->registers()[6]);
}

TEST_F(RV32, SLTU_SNEZ)
{
  prepare_test(R"(
    addi  x1, x0, 0
    addi  x2, x0, 1
    sltu  x3, x0, x1
    sltu  x4, x0, x2
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0, rv32->registers()[3]);
  ASSERT_EQ(1, rv32->registers()[4]);
}

TEST_F(RV32, JAL)
{
  prepare_test(R"(
    jal success
    b1: 
      addi x1, x0, 0
      exit_success
    success:
      addi x1, x0, 1
      exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(1, rv32->registers()[1]);
}

TEST_F(RV32, JALR)
{
  prepare_test(R"(
    jalr x2, 12(x0)
    b1: 
      addi x1, x0, 0
      exit_success
    success:
      addi x1, x0, 1
      exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(4, rv32->registers()[2]);
}

TEST_F(RV32, BEQ)
{
  prepare_test(R"(
    addi x1, x0, 2
    addi x2, x0, 2
    beq  x1, x2, . + 12
    fail: 
      addi x1, x0, 0
      exit_success
    success:
      addi x1, x0, 1
      exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(1, rv32->registers()[1]);
}

TEST_F(RV32, BNE)
{
  prepare_test(R"(
    addi x1, x0, 2
    addi x2, x0, 1
    bne  x1, x2, . + 12
    fail: 
      addi x1, x0, 0
      exit_success
    success:
      addi x1, x0, 1
      exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(1, rv32->registers()[1]);
}

TEST_F(RV32, BLTU)
{
  prepare_test(R"(
    addi x1, x0, 1
    addi x2, x0, 2
    bltu  x1, x2, . + 12
    fail: 
      addi x1, x0, 0
      exit_success
    success:
      addi x1, x0, 1
      exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(1, rv32->registers()[1]);
}

TEST_F(RV32, BGE)
{
  prepare_test(R"(
    addi x1, x0, 3
    addi x2, x0, 2
    bge  x1, x2, . + 12
    fail: 
      addi x1, x0, 0
      exit_success
    success:
      addi x1, x0, 1
      exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(1, rv32->registers()[1]);
}

TEST_F(RV32, MUL)
{
  prepare_test(R"(
    addi  x1, x0, -20
    addi  x2, x0, -43
    mul   x3, x1, x2
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(-20, *(i32*)&rv32->registers()[1]);
  ASSERT_EQ(-43, *(i32*)&rv32->registers()[2]);
  ASSERT_EQ(-43 * -20, *(i32*)&rv32->registers()[3]);
}

int
main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  printf("Note: You can set ARM7DI_DEBUG environment variable for more info\n");
  return RUN_ALL_TESTS();
}
