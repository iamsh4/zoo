#include <array>
#include <cstdio>
#include <fstream>
#include <exception>
#include <filesystem>
#include <vector>
#include <fcntl.h>

#include <guest/arm7di/arm7di.h>
#include <guest/arm7di/arm7di_shared.h>
#include <shared/types.h>
#include "shared/exec.h"

#include <gtest/gtest.h>

// These tests include tests written by us, as well as tests from the
// "armwrestler" test suite from snickerbockers, which was ported from
// tests designed for DS ARM7 processors. I copy-pasted the relevant portions
// of each test into the test cases below. The original tests can be found here:
// https://github.com/snickerbockers/dc-arm7wrestler?tab=readme-ov-file

// These tests require the arm-none-eabi gcc/binutils to be installed.
// MacOS       : brew install arm-none-eabi-gcc
// Fedora Linux: dnf install arm-none-eabi-*

// 1MiB addressable RAM
#define RAM_SIZE (1024 * 1024)
#define STACK_PTR_INIT 8192
#define MAX_ADDRESS RAM_SIZE

// If we ever reach this as an instruction, we've completed the test successfully
#define ARM7DI_TEST_EXIT_SUCCESS u32(0xCAFECAFE)
// If we ever reach this as an instruction, we've failed the test
#define ARM7DI_TEST_EXIT_FAILURE u32(0xCAFEBEEF)
// If we ever reach this as an instruction, we need to draw a string pointed to by r0
#define ARM7DI_TEST_DRAW_STRING_R0 u32(0xCAFEEEEE)

static const char *WORK_DIR      = "/tmp/penguin-testing/";
static const char *ASSEMBLY_FILE = "/tmp/penguin-testing/test.s";
static const char *OBJECT_FILE   = "/tmp/penguin-testing/test.o";

class Arm7DIBasic : public guest::arm7di::Arm7DI {
public:
  Arm7DIBasic(fox::MemoryTable *mem) : guest::arm7di::Arm7DI(mem) {}

  fox::Value guest_load(u32 address, size_t bytes) final
  {
    if (bytes == 1)
      return fox::Value { .u8_value = m_mem->read<u8>(address) };
    else if (bytes == 2)
      return fox::Value { .u16_value = m_mem->read<u16>(address) };
    else if (bytes == 4)
      return fox::Value { .u32_value = m_mem->read<u32>(address) };
    else
      assert(false);

    throw std::runtime_error("Unhandled guest load");
  }

  void guest_store(u32 address, size_t bytes, fox::Value value) final
  {
    if (bytes == 1)
      m_mem->write<u8>(address, value.u8_value);
    else if (bytes == 2)
      m_mem->write<u16>(address, value.u16_value);
    else if (bytes == 4)
      m_mem->write<u32>(address, value.u32_value);
    else {
      assert(false);
      throw std::runtime_error("Unhandled guest store");
    }
  }
};

class Arm7DI : public ::testing::Test {
protected:
  // Note: SetUp is for each test, not the entire suite
  void SetUp() override
  {
    memory_table = std::make_unique<fox::MemoryTable>(MAX_ADDRESS, MAX_ADDRESS);
    memory_table->map_sdram(0, RAM_SIZE, "test_ram_1_MiB");
    memory_table->finalize();

    arm7 = std::make_unique<Arm7DIBasic>(memory_table.get());
    arm7->reset();

    // While the main console needs PC to be remapped because of how memory tables work
    // these tests don't need that, so we override the default to 0, which will give us
    // a basic flat memory address space.
    arm7->set_fixed_pc_fetch_offset(0);
  }

  std::vector<u8> assemble(std::string_view code);
  void prepare_test(std::string_view asm_code);
  u32 run_prog(u32 limit_instructions);

  void prepare_wrestler_test(std::string_view test_code);

  std::unique_ptr<fox::MemoryTable> memory_table;
  std::unique_ptr<Arm7DIBasic> arm7;
};

u32
get_z_bit(u32 raw)
{
  // guest::arm7di::CPSR_bits cpsr_bits { .raw = raw };
  // return cpsr_bits.Z;
  return (raw >> 30) & 1;
};

std::vector<u8>
Arm7DI::assemble(std::string_view code)
{
  // Write assembly to scratch file
  std::filesystem::create_directories(WORK_DIR);
  std::ofstream asm_file(ASSEMBLY_FILE);
  asm_file << ".macro exit_success" << std::endl;
  asm_file << "  .word 0xCAFECAFE" << std::endl;
  asm_file << ".endm" << std::endl;
  asm_file << ".macro exit_failure" << std::endl;
  asm_file << "  .word 0xCAFEBEEF" << std::endl;
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
           "(arm-none-eabi-as -mcpu=arm7di %s -o %s"
           " && arm-none-eabi-objcopy %s /dev/null --dump-section .text=/dev/stdout)",
           ASSEMBLY_FILE,
           OBJECT_FILE,
           OBJECT_FILE);
  // printf("RUNNING '%s'\n", buff);
  return exec(buff);
}

void
Arm7DI::prepare_test(std::string_view asm_code)
{
  std::vector<u8> program_data = assemble(asm_code);
  // printf("Assembled program is %lu bytes\n", program_data.size());
  memory_table->dma_write(
    0, program_data.data(), program_data.size() * sizeof(program_data[0]));

  // Initialize processor, stack at some in-bounds address
  arm7->reset();
  arm7->registers().R[guest::arm7di::Arm7DIRegisterIndex_PC] = 0;
  arm7->registers().R[13]                                    = STACK_PTR_INIT;
}

void
Arm7DI::prepare_wrestler_test(std::string_view test_code)
{
  std::string result = R"(
    _start:
      mov 	r9,#0 @ from "forever" in the original code
  )";

  result += std::string(test_code) + "\n";

  result += R"(
    romvar:  	.byte 0x80,0,0,0
    romvar2: 	.byte 0x00,0x8f,0,0xff
    romvar3: 	.byte 0x80,0x7f,0,0
    var64:		.word 0x11223344,0x55667788

    wrestler_test_end:
      @ All tests call DrawResult when they're done.
      @ bl DrawResult

      @ Then we need to bail out of the test itself
      mov r2, #0
      cmp r1, r2 @ Failure flags are in r1, so 0 == success
      beq wrestler_end_success
      exit_failure

    wrestler_end_success:
      exit_success

    @@@@@@@@@@@@@@@@@ Built-ins
    .equ BAD_Rd,	0x10
    .equ BAD_Rn,	0x20
    .equ VARBASE,	0x80000
    .equ TESTNUM,	(VARBASE+8)
    .equ CURSEL,	(VARBASE+16)

    rdVal:		.word 0
    rnVal:		.word 0
    memVal:		.word 0
  )";

  // TODO : Extract failure code etc. print line, disassembly etc.

  prepare_test(result);
}

u32
Arm7DI::run_prog(u32 limit_instructions)
{
  u32 instructions_executed;
  for (instructions_executed = 0; instructions_executed < limit_instructions;
       ++instructions_executed) {
    const u32 pc = arm7->registers().R[guest::arm7di::Arm7DIRegisterIndex_PC];
    if (pc >= MAX_ADDRESS) {
      printf("PC too large 0x%08x\n", pc);
      assert(false && "arm7di pc too large");
    }
    assert((pc % 4) == 0);

    // Check if we've completed the test
    const u32 next_instruction_word = memory_table->read<u32>(pc);
    if (next_instruction_word == ARM7DI_TEST_EXIT_SUCCESS) {
      return instructions_executed;
    } else if (next_instruction_word == ARM7DI_TEST_EXIT_FAILURE) {
      printf("reached exit_failure, r1=0x%08x\n", arm7->registers().R[1]);
      throw std::runtime_error("exit_failure");
    } else if (next_instruction_word == ARM7DI_TEST_DRAW_STRING_R0) {
      const u32 lpszText = arm7->registers().R[0];
      char buffer[256];
      memset(buffer, 0, sizeof(buffer));
      for (u32 i = 0; i < sizeof(buffer) - 1; ++i) {
        const u8 c = memory_table->read<u8>(lpszText + i);
        if (c == 0) {
          break;
        }
        buffer[i] = c;
      }
      printf("DrawString: %s\n", buffer);
    }

    arm7->step();
  }

  arm7->get_jit_cache()->invalidate_all();
  arm7->get_jit_cache()->garbage_collect();

  if (instructions_executed == limit_instructions) {
    throw std::runtime_error("Hit instruction limit\n");
  }
  return instructions_executed;
}

TEST_F(Arm7DI, Wrestler_ADC)
{
  prepare_wrestler_test(R"(
    @ ADC
    mov 	r1,#0
    mov 	r2,#0x80000000
    mov 	r3,#0xF
    adds 	r9,r9,r9	@ clear carry
    adcs 	r2,r2,r3
    orrcs 	r1,r1,#1
    orrpl 	r1,r1,#2
    orrvs 	r1,r1,#4
    orreq 	r1,r1,#8

    adcs 	r2,r2,r2	
    orrcc 	r1,r1,#1
    orrmi 	r1,r1,#2
    
    adc 	r3,r3,r3
    cmp 	r3,#0x1F
    orrne 	r1,r1,#BAD_Rd
    
    adds 	r9,r9,r9	@ clear carry
    mov 	r0,#0
    mov 	r2,#1
    adc 	r0,r0,r2,lsr#1
    cmp 	r0,#1
    @orrne 	r1,r1,#BAD_Rd
    
    ldr 	r0,=szADC
    bl 	wrestler_test_end
    add 	r8,r8,#8

    exit_failure
  )");
  u32 instructions_executed = run_prog(50);
  printf("Instructions executed: %u\n", instructions_executed);
}

TEST_F(Arm7DI, Wrestler_ADD)
{
  prepare_wrestler_test(R"(
    @ ADD
    mov 	r1,#0
    ldr 	r2,=0xFFFFFFFE
    mov 	r3,#1
    
    adds 	r2,r2,r3   @ Result should be -1
    orrcs 	r1,r1,#1 @ no carry-out from bit31
    orrpl 	r1,r1,#2 @ result should be negative
    orrvs 	r1,r1,#4 @ No overflow occurs
    orreq 	r1,r1,#8 @ The result is not zero

    adds 	r2,r2,r3	 @ Result should be 0
    orrcc 	r1,r1,#1 @ Carry present out of bit31
    orrmi 	r1,r1,#2 @ Result is non-negative
    orrvs 	r1,r1,#4 @ No overflow takes place
    orrne 	r1,r1,#8
    ldr 	r0,=szADD
    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_AND)
{
  prepare_wrestler_test(R"(
    @ AND
    mov 	r1,#0
    mov 	r2,#2
    mov 	r3,#5
    ands 	r2,r2,r3,lsr#1
    orrcc 	r1,r1,#1
    orreq 	r1,r1,#8
    cmp 	r2,#2
    orrne 	r1,r1,#BAD_Rd
    mov 	r2,#0xC00
    mov 	r3,r2

    mov 	r4,#0x80000000
    ands 	r2,r2,r4,asr#32
    orrcc 	r1,r1,#1
    orrmi 	r1,r1,#2
    orreq 	r1,r1,#8
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=szAND
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(128);
}

TEST_F(Arm7DI, Wrestler_BIC)
{
  prepare_wrestler_test(R"(
    @ BIC
    mov 	r1,#0
    adds 	r9,r9,r9 @ clear carry
    ldr 	r2,=0xFFFFFFFF
    ldr 	r3,=0xC000000D
    bics 	r2,r2,r3,asr#1
    orrcc 	r1,r1,#1
    orrmi 	r1,r1,#2	
    orreq 	r1,r1,#8
    ldr 	r3,=0x1FFFFFF9
    cmp 	r2,r3
    orrne 	r1,r1,#16
    ldr 	r0,=szBIC
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_CMN)
{
  prepare_wrestler_test(R"(
    @ CMN
    mov 	r1,#0
    adds 	r9,r9,r9 @ clear carry
    ldr 	r2,=0x7FFFFFFF
    ldr 	r3,=0x70000000
    cmn 	r2,r3
    orrcs 	r1,r1,#1
    orrpl 	r1,r1,#2
    orrvc 	r1,r1,#4
    orreq 	r1,r1,#8
    ldr 	r3,=0x7FFFFFFF
    cmp 	r2,r3
    orrne 	r1,r1,#16
    ldr 	r0,=szCMN
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_EOR)
{
  prepare_wrestler_test(R"(
    @ EOR
    mov 	r1,#0
    mov 	r2,#1
    mov 	r3,#3
    eors 	r2,r2,r3,lsl#31
    eors 	r2,r2,r3,lsl#0
    orrcc 	r1,r1,#1
    orrpl 	r1,r1,#2
    orreq 	r1,r1,#8
    ldr 	r4,=0x80000002
    cmp 	r4,r2
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=szEOR
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_MVN)
{
  prepare_wrestler_test(R"(
    @ MVN
  	mov 	r1,#0
    ldr 	r2,=labelthree	
    ldr 	r3,=0xFFFFFFFF
    eor 	r2,r2,r3
    mvn 	r3,r15
    cmp 	r3,r2
    labelthree:	
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=szMVN
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_ORR)
{
  prepare_wrestler_test(R"(
    @ ORR
    mov 	r1,#0
    mov 	r2,#2
    mov 	r3,#3
    movs 	r4,r3,lsr#1	@ set carry 
    orrs 	r3,r3,r2,rrx
    orrcs 	r1,r1,#1
    orrpl 	r1,r1,#2
    orreq 	r1,r1,#8
    ldr 	r4,=0x80000003
    cmp 	r4,r3
    orrne 	r1,r1,#BAD_Rd
    @ldr 	r0,=szORR
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_RSC)
{
  prepare_wrestler_test(R"(
    @ RSC
    mov 	r1,#0
    mov 	r2,#2
    mov 	r3,#3
    adds 	r9,r9,r9	@ clear carry
    rscs 	r3,r2,r3
    orrcc 	r1,r1,#1
    orrmi 	r1,r1,#2
    orrne 	r1,r1,#8
    cmp 	r2,#2
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=szRSC
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_SBC)
{
  prepare_wrestler_test(R"(
    @ SBC
    mov 	r1,#0
    ldr 	r2,=0xFFFFFFFF
    adds 	r3,r2,r2	@ set carry
    sbcs 	r2,r2,r2
    orrcc 	r1,r1,#1
    orrmi 	r1,r1,#2
    orrne 	r1,r1,#8
    adds 	r9,r9,r9	@ clear carry
    sbcs 	r2,r2,#0
    orreq 	r1,r1,#8
    orrcs 	r1,r1,#1
    orrpl 	r1,r1,#2
    ldr 	r0,=szSBC
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_MLA)
{
  prepare_wrestler_test(R"(
    @ MLA
    mov 	r1,#0
    ldr 	r2,=0xFFFFFFF6
    mov 	r3,#0x14
    ldr 	r4,=0xD0
    mlas 	r2,r3,r2,r4
    orrmi 	r1,r1,#2
    orreq 	r1,r1,#8
    cmp 	r2,#8
    orrne 	r1,r1,#16
    ldr 	r0,=szMLA
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_MUL)
{
  prepare_wrestler_test(R"(
    @ MUL
    mov 	r1,#0
    ldr 	r2,=0xFFFFFFF6
    mov 	r3,#0x14
    ldr 	r4,=0xFFFFFF38
    muls 	r2,r3,r2
    orrpl 	r1,r1,#2
    orreq 	r1,r1,#8
    cmp 	r2,r4
    orrne 	r1,r1,#16
    ldr 	r0,=szMUL
    bl 	wrestler_test_end
    
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_SWP)
{
  prepare_wrestler_test(R"(
    @ SWP
    mov 	r1,#0
    adds 	r1,r1,#1		@ Clear C,N,V,Z
    mov 	r1,#0
    ldr 	r5,=(VARBASE+0x100)
    str 	r1,[r5]
    mov 	r0,#0xC0000000
    swp 	r0,r0,[r5]
    orrcs 	r1,r1,#1
    orrmi 	r1,r1,#2
    orrvs 	r1,r1,#4
    orreq 	r1,r1,#8
    cmp 	r0,#0
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r5]
    cmp 	r0,#0xC0000000
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=szSWP
    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_MSR)
{
  prepare_wrestler_test(R"(
    @ MSR
    mov 	r1,#0
    movs 	r2,#0
    msr 	cpsr_flg,#0x90000000
    orrcs 	r1,r1,#1
    orrpl 	r1,r1,#2
    orrvc 	r1,r1,#4
    orreq 	r1,r1,#8

    mov 	r11,#1
    mrs 	r2,cpsr
    bic 	r2,r2,#0x1f
    orr 	r2,r2,#0x11	
    msr 	cpsr,r2		@ Set FIQ mode
    mov 	r11,#2
    orr 	r2,r2,#0x13
    msr 	cpsr,r2		@ Set supervisor mode (XXX was originally system mode)
    cmp 	r11,#1
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=szMSR
    
    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_MRS)
{
  prepare_wrestler_test(R"(
    @ MRS
    mov 	r1,#0
    mov 	r0,#0xC0000000
    adds 	r0,r0,r0		@ Z=0, C=1, V=0, N=1
    mov 	r2,#0x50000000
    mrs 	r2,cpsr
    tst 	r2,#0x20000000
    orreq 	r1,r1,#1
    tst 	r2,#0x80000000
    orreq 	r1,r1,#2
    tst 	r2,#0x10000000
    orrne 	r1,r1,#4
    tst 	r2,#0x40000000
    orrne 	r1,r1,#8
    ldr 	r0,=szMRS
    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

//
// r4: 00 00 00 7f
// *r5 = r4
// r0: c0 00 00 80
//     swpb 	r0,r0,[r5]
//     c0 00 00 80
// r0: 00 00 00 7f

TEST_F(Arm7DI, Wrestler_SWPB)
{
  // The SWPB operation here is made of ~two operations. LDRB and STRB.
  // LDRB will load only the bottom byte and zero out the rest of the register.
  // The store to memory actually presents the bottom byte 4 times to the bus
  // but the memory will only store the correct byte.
  // This means that SWPB result register will always contain just a low byte
  // while the memory will contain three bytes of previous value and the bottom
  // byte is updated.

  prepare_wrestler_test(R"(
    @ SWPB
    mov 	r1,#0
    adds 	r1,r1,#0		@ Clear C,N,V
    ldr 	r5,=(VARBASE+0x100)
    mov 	r4,#0xff         @ 00 00 00 ff
    add 	r4,r4,#0x80      @ 00 00 01 7f
    str 	r4,[r5]          @              00 00 01 7f
    mov 	r0,#0xC0000000
    orr 	r0,r0,#0x80      @ c0 00 00 80
    swpb 	r0,r0,[r5]       @ 00 00 00 7f  00 00 01 80
    orrcs 	r1,r1,#1
    orrmi 	r1,r1,#2
    orrvs 	r1,r1,#4
    orrne 	r1,r1,#8
    cmp 	r0,#0x7f
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r5]          @              00 00 01 80
    cmp 	r0,#0x180 
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=szSWPB
    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_1)
{
  // Test basic LDR and unaligned word loads
  prepare_wrestler_test(R"(
    @ LDR
    @ +#]
    mov 	r1,#0
    ldr 	r0,=romvar
    sub 	r2,r0,#3
    mov 	r3,r2
    ldr 	r0,[r0,#0]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,#3]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    ldr 	r0,=romvar2
    ldr 	r0,[r0,#1]
    ldr 	r2,=0x00ff008f
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    ldr 	r0,=romvar2
    ldr 	r0,[r0,#2]
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2

    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,=romvar2
    ldr 	r0,[r0,#3]
    ldr 	r2,=0x008f00ff
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_2)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ -#]
    mov 	r1,#0
    ldr 	r0,=romvar
    mov 	r2,r0
    mov 	r3,r2
    add 	r0,r0,#206
    ldr 	r0,[r0,#-206]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,#-0]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    @ Test non word-aligned load
    ldr 	r0,=romvar2+4
    ldr 	r0,[r0,#-2]
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_3)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ +#]!
    mov 	r1,#0
    ldr 	r0,=romvar
    sub 	r2,r0,#3
    mov 	r3,r0
    ldr 	r0,[r0,#0]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,#3]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    @ Test non word-aligned load
    ldr 	r0,=romvar2
    ldr 	r0,[r0,#2]!
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_4)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ -#]!
    mov 	r1,#0
    ldr 	r0,=romvar
    add 	r2,r0,#1
    mov 	r3,r0
    add 	r0,r0,#206
    ldr 	r0,[r0,#-206]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,#-1]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    @ Test non word-aligned load
    ldr 	r0,=romvar2+4
    ldr 	r0,[r0,#-2]!
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

// romvar:  	.byte 0x80,0,0,0
// romvar2: 	.byte 0x00,0x8f,0,0xff
// romvar3: 	.byte 0x80,0x7f,0,0

TEST_F(Arm7DI, Wrestler_LDR_5)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ +R]
    mov 	r1,#0
    ldr 	r0,=romvar
    sub 	r2,r0,#8
    sub 	r0,r0,#1
    mov 	r3,r2
    mov 	r4,#2
    ldr 	r0,[r0,r4, lsr #1]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,r4, lsl #2]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    mov 	r2,r2,lsr#1
    mov 	r3,#0xC0000000
    ldr 	r0,[r2,r2]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd

    ldr 	r2,=romvar
    mov 	r3,#0x8
    ldr 	r0,[r2,r3, lsr #32] @ "LSR 32" is encoded as "LSR 0"
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    
    ldr 	r2,=romvar
    add 	r2,r2,#1
    mov 	r3,#0xC0000000
    ldr 	r0,[r2,r3, asr #32]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd

    ldr 	r2,=romvar
    add 	r2,r2,#2
    ldr 	r3,=0xfffffffc
    adds 	r4,r3,r3		@ set carry
    ldr 	r0,[r2,r3, rrx]
    orrcc 	r1,r1,#1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd

    @ Test non word-aligned load
    ldr 	r0,=romvar2
    mov 	r2,#2
    ldr 	r0,[r0,r2]
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_6)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ -R]
    mov 	r1,#0
    ldr 	r0,=romvar
    add 	r2,r0,#8
    add 	r0,r0,#1
    mov 	r3,r2
    mov 	r4,#2
    ldr 	r0,[r0,-r4, lsr #1]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,-r4, lsl #2]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    mov 	r3,#0x8
    ldr 	r0,[r2,-r3, lsr #32]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd

    ldr 	r2,=romvar
    sub 	r2,r2,#1
    mov 	r3,#0x80000000
    ldr 	r0,[r2,-r3, asr #32]
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    
    ldr 	r2,=romvar
    sub 	r2,r2,#4
    ldr 	r3,=0xfffffff8
    adds 	r4,r3,r3		@ set carry
    ldr 	r0,[r2,-r3, rrx]
    orrcc 	r1,r1,#1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd

    @ Test non word-aligned load
    ldr 	r0,=romvar2+4
    mov 	r2,#1
    ldr 	r0,[r0,-r2, lsl #1]
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_7)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ +R]!
    mov 	r1,#0
    ldr 	r0,=romvar
    mov 	r3,r0
    sub 	r2,r0,#8
    sub 	r0,r0,#1
    mov 	r4,#2
    ldr 	r0,[r0,r4, lsr #1]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,r4, lsl #2]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    mov 	r4,r2
    mov 	r2,r2,lsr#1
    mov 	r3,#0xC0000000
    ldr 	r0,[r2,r2]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r4
    orrne 	r1,r1,#BAD_Rn
    
    ldr 	r2,=romvar
    mov 	r4,r2
    add 	r2,r2,#1
    mov 	r3,#0xC0000000
    ldr 	r0,[r2,r3, asr #32]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r4
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    mov 	r5,r2
    add 	r2,r2,#2
    ldr 	r3,=0xfffffffc
    adds 	r4,r3,r3		@ set carry
    ldr 	r0,[r2,r3, rrx]!
    orrcc 	r1,r1,#1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r5
    orrne 	r1,r1,#BAD_Rn

    @ Test non word-aligned load
    ldr 	r0,=romvar2
    mov 	r2,#2
    ldr 	r0,[r0,r2]!
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_8)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ -R]!
    mov 	r1,#0
    ldr 	r0,=romvar
    mov 	r3,r0
    add 	r2,r0,#8
    add 	r0,r0,#1
    mov 	r4,#2
    ldr 	r0,[r0,-r4, lsr #1]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2,-r4, lsl #2]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    mov 	r4,r2
    sub 	r2,r2,#1
    mov 	r3,#0x80000000
    ldr 	r0,[r2,-r3, asr #32]!
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r4
    orrne 	r1,r1,#BAD_Rn
    
    ldr 	r2,=romvar
    mov 	r5,r2
    sub 	r2,r2,#4
    ldr 	r3,=0xfffffff8
    adds 	r4,r3,r3		@ set carry
    ldr 	r0,[r2,-r3, rrx]!
    orrcc 	r1,r1,#1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r5
    orrne 	r1,r1,#BAD_Rn

    @ Test non word-aligned load
    ldr 	r0,=romvar2+4
    mov 	r2,#2
    ldr 	r0,[r0,-r2]!
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_9)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ ]+#
    mov 	r1,#0
    ldr 	r0,=romvar
    add 	r3,r0,#3
    mov 	r2,r0
    ldr 	r0,[r0],#3
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2],#3
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    @ Test non word-aligned load
    ldr 	r0,=romvar2+2
    ldr 	r0,[r0],#5
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_10)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ ]-#
    mov 	r1,#0
    ldr 	r0,=romvar
    mov 	r2,r0
    sub 	r3,r0,#0xff
    ldr 	r0,[r0],#-0xff
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2],#-0xff
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    @ Test non word-aligned load
    ldr 	r0,=romvar2+2
    ldr 	r0,[r0],#-5
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_11)
{
  prepare_wrestler_test(R"(
    @ LDR
     @ ]+R
    mov 	r1,#0
    ldr 	r0,=romvar
    mov 	r2,r0
    add 	r5,r0,#8
    mov 	r3,r0
    mov 	r4,#2
    ldr 	r0,[r0],r4, lsr #1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2],r4, lsl #2
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r5
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    mov 	r0,#123
    add 	r3,r2,r0
    ldr 	r0,[r2],r0
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    
    ldr 	r2,=romvar
    sub 	r4,r2,#1
    mov 	r3,#0xC0000000
    ldr 	r0,[r2],r3, asr #32
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r4
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    sub 	r4,r2,#2
    ldr 	r3,=0xfffffffc
    adds 	r5,r3,r3		@ set carry
    ldr 	r0,[r2],r3, rrx
    orrcc 	r1,r1,#1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r4
    orrne 	r1,r1,#BAD_Rn

    @ Test non word-aligned load
    ldr 	r0,=romvar2+2
    mov 	r2,#1
    ldr 	r0,[r0],r2
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDR_12)
{
  prepare_wrestler_test(R"(
    @ LDR
    @ ]-R
    mov 	r1,#0
    ldr 	r0,=romvar
    mov 	r2,r0
    sub 	r5,r0,#16
    mov 	r3,r0
    mov 	r4,#2
    ldr 	r0,[r0],-r4, lsr #1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    ldr 	r0,[r2],-r4, lsl #3
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r5
    orrne 	r1,r1,#BAD_Rn

    ldr	r2,=romvar
    mov 	r0,#123
    sub 	r3,r2,r0
    ldr 	r0,[r2],-r0
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    
    ldr 	r2,=romvar
    add 	r4,r2,#1
    mov 	r3,#0xC0000000
    ldr 	r0,[r2],-r3, asr #32
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r4
    orrne 	r1,r1,#BAD_Rn

    ldr 	r2,=romvar
    add 	r4,r2,#2
    ldr 	r3,=0xfffffffc
    adds 	r5,r3,r3		@ set carry
    ldr 	r0,[r2],-r3, rrx
    orrcc 	r1,r1,#1
    cmp 	r0,#0x80
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r4
    orrne 	r1,r1,#BAD_Rn

    @ Test non word-aligned load
    ldr 	r0,=romvar2+2
    mov 	r2,#5
    ldr 	r0,[r0],-r2
    ldr 	r2,=0x8f00ff00
    cmp 	r0,r2
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDRB_1)
{
  prepare_wrestler_test(R"(
    @ LDRB
    @ +#]
    mov 	r1,#0
    ldr 	r0,=romvar2
    sub 	r2,r0,#1
    mov 	r3,r2
    ldrb 	r0,[r0,#3]
    cmp 	r0,#0xff
    orrne 	r1,r1,#BAD_Rd
    ldrb 	r0,[r2,#3]
    cmp 	r0,#0
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDRB_2)
{
  prepare_wrestler_test(R"(
    @ LDRB
    @ -#]
    mov 	r1,#0
    ldr 	r0,=romvar2
    add 	r0,r0,#4
    add 	r2,r0,#1
    mov 	r3,r2
    ldrb 	r0,[r0,#-1]
    cmp 	r0,#0xff
    orrne 	r1,r1,#BAD_Rd
    ldrb 	r0,[r2,#-3]
    cmp 	r0,#0
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDRB_3)
{
  prepare_wrestler_test(R"(
    @ LDRB
    @ +#]!
    mov 	r1,#0
    ldr 	r0,=romvar2
    add 	r3,r0,#2
    sub 	r2,r0,#3
    ldrb 	r0,[r0,#3]!
    cmp 	r0,#0xff
    orrne 	r1,r1,#BAD_Rd
    ldrb 	r0,[r2,#5]!
    cmp 	r0,#0
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDRB_4)
{
  prepare_wrestler_test(R"(
    @ LDRB
    @ -#]!
    mov 	r1,#0
    ldr 	r0,=romvar2
    add 	r3,r0,#2
    add 	r0,r0,#4
    add 	r2,r0,#1
    ldrb 	r0,[r0,#-1]!
    cmp 	r0,#0xff
    orrne 	r1,r1,#BAD_Rd
    ldrb 	r0,[r2,#-3]!
    cmp 	r0,#0
    orrne 	r1,r1,#BAD_Rd
    cmp 	r2,r3
    orrne 	r1,r1,#BAD_Rn
    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMIB_Writeback)
{
  prepare_wrestler_test(R"(
    @ LDMIB!
    mov 	r1,#0
    ldr 	r3,=var64
    sub 	r3,r3,#4
    ldmib 	r3!,{r4,r5}
    ldr 	r0,=var64+4
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    mov 	r4,#5

   @ @ Test writeback for when the base register is included in the
   @ @ register list.
@
   ldr 	r3,=var64
   sub 	r3,r3,#4
   ldmib 	r3!,{r2,r3}
   ldr 	r0,=var64+4
   mov 	r5,r2
   ldr 	r2,[r0]
   cmp 	r3,r2
   orrne 	r1,r1,#BAD_Rn
   ldrne 	r2,=rnVal
   strne 	r3,[r2]
@
   ldr 	r3,=var64
   sub 	r3,r3,#4
   ldmib 	r3!,{r3,r5}
   ldr 	r2,=var64+4
   ldr r2, [r2, #-4]
   cmp 	r3,r2
   orrne 	r1,r1,#BAD_Rn
   ldrne 	r2,=rnVal
   strne 	r3,[r2]
    
   ldr 	r2,[r0]
   cmp 	r5,r2
   orrne 	r1,r1,#BAD_Rd
   cmp 	r4,#5
   orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMIA_Writeback)
{
  prepare_wrestler_test(R"(
    @ LDMIA!
    mov 	r1,#0
    ldr 	r3,=var64
    ldmia 	r3!,{r4,r5}
    ldr 	r0,=var64+8
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    mov 	r4,#5

    @ Test writeback for when the base register is included in the
    @ register list.
    ldr 	r3,=var64
    ldmia 	r3!,{r2,r3}
    ldr 	r0,=var64+4
    mov 	r5,r2
    ldr 	r2,[r0]
    cmp 	r3,r2
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r2,=rnVal
    strne 	r3,[r2]

    ldr 	r3,=var64
    ldmia 	r3!,{r3,r5}
    ldr 	r2,=var64+8
    ldr r2, [r2, #-8]
    cmp 	r3,r2
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r2,=rnVal
    strne 	r3,[r2]
    
    ldr 	r2,[r0]
    cmp 	r5,r2
    orrne 	r1,r1,#BAD_Rd
    cmp 	r4,#5
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMDB_Writeback)
{
  prepare_wrestler_test(R"(
  @ LDMDB!
    mov 	r1,#0
    ldr 	r3,=var64+8
    ldmdb 	r3!,{r4,r5}
    ldr 	r0,=var64
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    mov 	r4,#5

    @ Test writeback for when the base register is included in the
    @ register list.
    ldr 	r3,=var64+8
    ldmdb 	r3!,{r2,r3}
    ldr 	r0,=var64+4
    mov 	r5,r2
    ldr 	r2,[r0]
    cmp 	r3,r2
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r2,=rnVal
    strne 	r3,[r2]

    ldr 	r3,=var64+8
    ldmdb 	r3!,{r3,r5}
    ldr 	r2,=var64
    ldr r2, [r2]
    cmp 	r3,r2
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r2,=rnVal
    strne 	r3,[r2]
    
    ldr 	r2,[r0]
    cmp 	r5,r2
    orrne 	r1,r1,#BAD_Rd
    cmp 	r4,#5
    orrne 	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMDA_Writeback)
{
  prepare_wrestler_test(R"(
    @ LDMDA!
    mov 	r1,#0
    ldr 	r3,=var64+4
    ldmda 	r3!,{r4,r5}
    ldr 	r0,=var64-4
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    mov 	r4,#5

    @ Test writeback for when the base register is included in the
    @ register list.
    ldr 	r3,=var64+4
    ldmda 	r3!,{r2,r3}
    ldr 	r0,=var64+4
    mov 	r5,r2
    ldr 	r2,[r0]
    cmp 	r3,r2
    orrne 	r1,r1,#BAD_Rn	@ r3 should contain the value loaded from memory
    ldrne 	r2,=rnVal
    strne 	r3,[r2]

    ldr 	r3,=var64+4
    ldmda 	r3!,{r3,r5}
    ldr 	r2,=var64-4
    ldr r2, [r2, #4]
    cmp 	r3,r2
    orrne 	r1,r1,#BAD_Rn	@ r3 should contain the updated base
    ldrne 	r2,=rnVal
    strne 	r3,[r2]
    
    ldr 	r2,[r0]
    cmp 	r5,r2
    orrne 	r1,r1,#BAD_Rd
    cmp 	r4,#5
    orrne 	r1,r1,#BAD_Rd	@ Make sure that the LDM didn't touch other registers


    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMIBS_Writeback)
{
  // Switches to IRQ mode, writes a value to r14, then
  // loads some info into user mode registers, then checks
  // that the irq mode registers were not touched.
  prepare_wrestler_test(R"(
   @ LDMIBS!
    mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
    msr	cpsr, r0
    mov	r1,#0
    mov	r14,#123
    ldr	r0,=var64-4     
    ldmib	r0!,{r3,r14}^ @ r3 will be written to, r14 of user mode will be overwritten
    ldr	r2,=var64+4     
    cmp	r0,r2
    orrne	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r0,[r5]
    sub	r2,r2,#4          
    ldr	r2,[r2]           
    cmp	r2,r3             
    orrne	r1,r1,#BAD_Rd @ 
    cmp	r14,#123
    orrne	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMIAS_Writeback)
{
  prepare_wrestler_test(R"(
    @ LDMIAS!
    mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
    msr	cpsr, r0
    mov	r1,#0
    mov	r14,#123
    ldr	r0,=var64
    ldmia	r0!,{r3,r14}^
    ldr	r2,=var64+8
    cmp	r0,r2
    orrne	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r0,[r5]
    sub	r2,r2,#8
    ldr	r2,[r2]
    cmp	r2,r3
    orrne	r1,r1,#BAD_Rd
    cmp	r14,#123
    orrne	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMDBS_Writeback)
{
  prepare_wrestler_test(R"(
    @ LDMDBS!
    mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
    msr	cpsr, r0
    mov	r1,#0
    mov	r14,#123
    ldr	r0,=var64+8
    ldmdb	r0!,{r3,r14}^
    ldr	r2,=var64
    cmp	r0,r2
    orrne	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r0,[r5]
    ldr	r2,[r2]
    cmp	r2,r3
    orrne	r1,r1,#BAD_Rd
    cmp	r14,#123
    orrne	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_LDMDAS_Writeback)
{
  prepare_wrestler_test(R"(
    @ LDMDAS!
    mov	r0, #0xd2	@ Switch to IRQ mode (XXX: keep irqs disabled)
    msr	cpsr, r0
    mov	r1,#0
    mov	r14,#123
    ldr	r0,=var64+4
    ldmda	r0!,{r3,r14}^
    ldr	r2,=var64-4
    cmp	r0,r2
    orrne	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r0,[r5]
    add	r2,r2,#4
    ldr	r2,[r2]
    cmp	r2,r3
    orrne	r1,r1,#BAD_Rd
    cmp	r14,#123
    orrne	r1,r1,#BAD_Rd

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_STMIB_Writeback)
{
  prepare_wrestler_test(R"(
    @ STMIB!
    mov 	r1,#0
    ldr 	r3,=(VARBASE+0x1FC)
    mov 	r4,#5
    stmib 	r3!,{r3,r4,r5}
    ldr 	r0,=(VARBASE+0x208)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r3,[r5]
    sub 	r0,r0,#8
    ldr 	r2,[r0]
    sub 	r0,r0,#4
    cmp 	r2,r0
    @orrne 	r1,r1,#0x80
    @ldrne	r0,=memVal
    @strne	r2,[r0]

    ldr 	r3,=(VARBASE+0x1FC)
    mov 	r4,#5
    stmib 	r3!,{r2,r3,r4}
    ldr 	r0,=(VARBASE+0x208)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_STMIA_Writeback)
{
  prepare_wrestler_test(R"(
    @ STMIA!
    mov 	r1,#0
    ldr 	r3,=(VARBASE+0x200)
    mov 	r4,#5
    stmia 	r3!,{r3,r4,r5}
    ldr 	r0,=(VARBASE+0x20C)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r3,[r5]
    sub 	r0,r0,#0xC
    ldr 	r2,[r0]
    cmp 	r2,r0
    orrne 	r1,r1,#0x80
    ldrne	r4,=memVal
    strne	r0,[r4] @r2,[r4]

    ldr 	r3,=(VARBASE+0x200)
    mov 	r4,#5
    stmia 	r3!,{r2,r3,r4}
    ldr 	r0,=(VARBASE+0x20C)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_STMDB_Writeback)
{
  prepare_wrestler_test(R"(
    @ STMDB!
    mov 	r1,#0
    ldr 	r3,=(VARBASE+0x20C)
    mov 	r4,#5
    stmdb 	r3!,{r3,r4,r5}
    ldr 	r0,=(VARBASE+0x200)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r3,[r5]
    ldr 	r2,[r0]
    add	r0,r0,#0xC
    cmp 	r2,r0
    orrne 	r1,r1,#0x80
    ldrne	r0,=memVal
    strne	r2,[r0]

    ldr 	r3,=(VARBASE+0x20C)
    mov 	r4,#5
    stmdb 	r3!,{r2,r3,r4}
    ldr 	r0,=(VARBASE+0x200)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Wrestler_STMDA_Writeback)
{
  prepare_wrestler_test(R"(
    @ STMDA!
    mov 	r1,#0
    ldr 	r3,=(VARBASE+0x208)
    mov 	r4,#5
    stmda 	r3!,{r3,r4,r5}
    ldr 	r0,=(VARBASE+0x1FC)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r3,[r5]
    add	r0,r0,#4
    ldr 	r2,[r0]
    add	r0,r0,#8
    cmp 	r2,r0
    orrne 	r1,r1,#0x80
    ldrne	r0,=memVal
    strne	r2,[r0]

    ldr 	r3,=(VARBASE+0x208)
    mov 	r4,#5
    stmda 	r3!,{r2,r3,r4}
    ldr 	r0,=(VARBASE+0x1FC)
    cmp 	r3,r0
    orrne 	r1,r1,#BAD_Rn
    ldrne 	r5,=rnVal
    strne 	r3,[r5]
    add	r0,r0,#0xC
    ldr	r2,[r0]
    cmp	r4,r2
    orrne	r1,r1,#0x80

    bl 	wrestler_test_end
    exit_failure
  )");
  run_prog(1000);
}

TEST_F(Arm7DI, Memory_Write)
{
  prepare_test(R"(
    mov     r3, #0
    ldr     r2, =0xCAFEBEEF
    str     r2, [r3, #1000]
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0xcafebeef, memory_table->read<u32>(1000));
}

TEST_F(Arm7DI, Memory_Write_OffsetWithWriteback)
{
  prepare_test(R"(
    mov     r3, #0x100
    mov     r2, #7
    str     r2, [r3, r2, LSL #2]!
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(7, memory_table->read<u32>(0x100 + 7 * 4));
  ASSERT_EQ(0x100u + 7 * 4u, arm7->registers().R[3]);
}

TEST_F(Arm7DI, Memory_Write_PostIncrement)
{
  prepare_test(R"(
    mov     r3, #0x100
    mov     r2, #7
    str     r2, [r3], #8
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(7, memory_table->read<u32>(0x100));
  ASSERT_EQ(0x100u + 8u, arm7->registers().R[3]);
}

TEST_F(Arm7DI, Memory_Write_Byte)
{
  prepare_test(R"(
    mov     r1, #0x100
    ldr     r2, =0x1234
    strb    r2, [r1]
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0x34, memory_table->read<u32>(0x100));
}

TEST_F(Arm7DI, Memory_Write_NegativeOffset)
{
  prepare_test(R"(
    mov     r3, #0x100
    mov     r2, #7
    str     r2, [r3, #-4]
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(7, memory_table->read<u32>(0x100 - 4));
}

TEST_F(Arm7DI, Memory_Write_ShiftOffset)
{
  prepare_test(R"(
    mov     r3, #0x100
    mov     r4, #1
    ldr     r2, =0xCAFEBEEF
    str     r2, [r3, r4, LSL#2]
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0xcafebeef, memory_table->read<u32>(0x100 + (1 << 2)));
}

TEST_F(Arm7DI, Memory_ReadModifyWrite)
{
  memory_table->write<u32>(1000, 0x1234);
  prepare_test(R"(
    mov  r3, #0
    ldr  r2, [r3, #1000]
    add  r2, r2, #1
    str  r2, [r3, #1000]
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0x1235u, memory_table->read<u32>(1000));
}

TEST_F(Arm7DI, DataProcessing_ADD)
{
  prepare_test(R"(
    ldr  r3, =0x1122
    ldr  r4, =0x3344
    add  r3, r3, r4
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0x4466u, arm7->registers().R[3]);
}

TEST_F(Arm7DI, DataProcessing_ADD_CarrySet)
{
  prepare_test(R"(
    ldr  r1, =0x80000000
    ldr  r2, =0x80000000
    adds  r3, r1, r2
    nop
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(0, arm7->registers().R[3]);
  ASSERT_EQ(1, arm7->registers().CPSR.V);
}

TEST_F(Arm7DI, DataProcessing_ADD_LSL)
{
  prepare_test(R"(
    ldr  r3, =0x1221
    ldr  r4, =0x3445
    ldr  r8, =32
    add  r5, r3, r4, lsl #0
    add  r6, r3, r4, lsl #2
    add  r7, r3, r4, lsl r8
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(0x1221 + (0x3445 << 0), arm7->registers().R[5]);
  ASSERT_EQ(0x1221 + (0x3445 << 2), arm7->registers().R[6]);
  ASSERT_EQ(arm7->registers().R[3], arm7->registers().R[7]);
}

TEST_F(Arm7DI, DataProcessing_ADD_LSR)
{
  prepare_test(R"(
    ldr  r1, =0x80000000
    ldr  r2, =1
    ldr  r3, =32
    add  r4, r1, r2, lsr #0
    add  r5, r1, r2, lsr #1
    add  r6, r1, r2, lsl r3
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(0x8000'0001, arm7->registers().R[4]);
  ASSERT_EQ(0x8000'0000 + (1u >> 1), arm7->registers().R[5]);
  ASSERT_EQ(0x8000'0000, arm7->registers().R[6]);
}

TEST_F(Arm7DI, DataProcessing_ADD_ASR)
{
  prepare_test(R"(
    ldr  r1, =0
    ldr  r2, =0x80000000
    ldr  r3, =7
    add  r4, r1, r2, asr #3
    add  r5, r1, r2, asr r3
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(5), instructions_executed);
  ASSERT_EQ(int32_t(0x80000000) >> 3, arm7->registers().R[4]);
  ASSERT_EQ(int32_t(0x80000000) >> 7, arm7->registers().R[5]);
}

TEST_F(Arm7DI, DataProcessing_ADDGT)
{
  prepare_test(R"(
    mov  r2, #4
    mov  r3, #1
    cmp  r3, #2
    addgt r3, r3, r2
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(1u, arm7->registers().R[3]);
}

TEST_F(Arm7DI, DataProcessing_SUB)
{
  prepare_test(R"(
    ldr  r3, =0x1122
    ldr  r4, =0x3344
    sub  r3, r4, r3
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0x2222u, arm7->registers().R[3]);
}

TEST_F(Arm7DI, DataProcessing_RSB)
{
  prepare_test(R"(
    ldr  r3, =0x1122
    ldr  r4, =0x3344
    rsb  r3, r4, r3
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  i32 expected = i32(0x1122) - i32(0x3344);
  ASSERT_EQ(expected, arm7->registers().R[3]);
}

TEST_F(Arm7DI, DataProcessing_AND)
{
  prepare_test(R"(
    mov  r2, #3
    mov  r3, #5
    and  r4, r2, r3
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(1, arm7->registers().R[4]);
}

TEST_F(Arm7DI, DataProcessing_ORR)
{
  prepare_test(R"(
    ldr  r3, =0xaabbccdd
    ldr  r4, =0x11223344
    orr  r2, r4, r3
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0xaabbccdd | 0x11223344, arm7->registers().R[2]);
}

TEST_F(Arm7DI, DataProcessing_EOR)
{
  prepare_test(R"(
    ldr  r3, =0xaabbccdd
    ldr  r4, =0x11223344
    eor  r2, r4, r3
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0xaabbccdd ^ 0x11223344, arm7->registers().R[2]);
}

TEST_F(Arm7DI, Branch_Basic)
{
  prepare_test(R"(
    b expected
    unexpected:
      mov r0, #1
      exit_success
    expected:
      mov r0, #2
      exit_success
  )");
  u32 instructions_executed = run_prog(3);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(2u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, Branch_LessThan)
{
  prepare_test(R"(
    mov r1, #1
    cmp r1, #2
    blt expected
    unexpected:
      mov r0, #1
      exit_success
    expected:
      mov r0, #2
      exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(2u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, Branch_GreaterThan)
{
  prepare_test(R"(
    mov r1, #3
    cmp r1, #2
    bgt expected
    unexpected:
      mov r0, #1
      exit_success
    expected:
      mov r0, #2
      exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(2u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, Branch_Equal)
{
  prepare_test(R"(
    mov r1, #2
    cmp r1, #2
    beq expected
    unexpected:
      mov r0, #1
      exit_success
    expected:
      mov r0, #2
      exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(2u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, Branch_NotEqual)
{
  prepare_test(R"(
    mov r1, #3
    cmp r1, #2
    bne expected
    unexpected:
      mov r0, #1
      exit_success
    expected:
      mov r0, #2
      exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(2u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, Branch_Link)
{
  prepare_test(R"(
    mov r0, #1
    cmp r0, #2 @ Z=1 to test that we don't return beyond the cmp instruction
    bl test_func
    cmp r0, #2
    beq label_success
    exit_failure

    test_func:
      mov r0, #2
      mov pc, lr

    label_success:
      exit_success
  )");
  u32 instructions_executed = run_prog(8);
  ASSERT_EQ(u32(7), instructions_executed);
  ASSERT_EQ(2u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, Stack_PushPop)
{
  prepare_test(R"(
    mov r0, #3
    push {r0}
    mov r0, #4
    pop {r0}   @ r0 <- #3
    exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(3u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, DataProcessing_BIC)
{
  prepare_test(R"(
    ldr r0, =0x11223344
    ldr r1, =0xf0f0f0f0
    bic r2, r0, r1
    exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(0x11223344u & ~0xf0f0f0f0u, arm7->registers().R[2]);
}

TEST_F(Arm7DI, DataProcessing_MVN)
{
  prepare_test(R"(
    MVN r0, #3
    exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(1), instructions_executed);
  ASSERT_EQ(~u32(3), arm7->registers().R[0]);
}

TEST_F(Arm7DI, DataProcessing_CMP)
{
  prepare_test(R"(
    mov r0, #1
    cmp r0, #1
    moveq r0, #7
    exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(7u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, DataProcessing_TST)
{
  prepare_test(R"(
    mov r0, #1
    mov r1, #1
    tst r0, r1    @ and(r0,r1), Z set if no bits common
    movne r0, #3  @ so, mov if any bits common
    exit_success
  )");
  u32 instructions_executed = run_prog(5);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(3u, arm7->registers().R[0]);
}

TEST_F(Arm7DI, DataProcessing_TEQ)
{
  prepare_test(R"(
    mov r2, #0
    ldr r0, =0x11223344
    mov r1, r0
    teq r0, r1
    moveq r2, #9
    exit_success
  )");
  u32 instructions_executed = run_prog(6);
  ASSERT_EQ(u32(5), instructions_executed);
  ASSERT_EQ(9u, arm7->registers().R[2]);
}

TEST_F(Arm7DI, DataProcessing_MRS)
{
  // pg 36: Transfer PSR contents to a register
  prepare_test(R"(
    mov  r0, #0
    mov  r1, #1
    adds r2, r0, r0   @ set Z=1
    mrs  r3, cpsr
    adds r2, r0, #1   @ set Z=0
    mrs  r4, cpsr
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(1u, get_z_bit(arm7->registers().R[3]));
  ASSERT_EQ(0u, get_z_bit(arm7->registers().R[4]));
}

TEST_F(Arm7DI, DataProcessing_MOVS_LSL0_FlagsPreserved)
{
  for (unsigned i = 0; i < 4; ++i) {
    const u32 C = i & 1;
    const u32 V = (i >> 1) & 1;

    prepare_test(R"(
      mov  r1, #1
      movs r1, r1
      exit_success
    )");

    // V should be unaffected in all logical operations (of which MOV is one)
    arm7->registers().CPSR.V = V;

    // C flag should be preserved when LSL #0 is used
    arm7->registers().CPSR.C = C;

    run_prog(10);
    ASSERT_EQ(1u, arm7->registers().R[1]);
    ASSERT_EQ(0u, arm7->registers().CPSR.Z);
    ASSERT_EQ(0u, arm7->registers().CPSR.N);
    ASSERT_EQ(C, arm7->registers().CPSR.C);
    ASSERT_EQ(V, arm7->registers().CPSR.V);
  }
}

TEST_F(Arm7DI, DataProcessing_MSR)
{
  // NOTE: MRS tests MUST pass first before the results of this test are meaningful

  // pg 36: Transfer general register to PSR
  // This test juggles status register with ALU instructions.
  prepare_test(R"(
    @ First just set Z=1
    mov r0, #0
    adds r0, r0, #1          @    Z = 0
    mrs  r1, cpsr            @ r1.Z = 0 < Check
    and  r2, r1, #0x0fffffff @          < Check
    orr  r2, r2, #0x40000000 @          
    msr  cpsr, r2            @ r2.Z = 1 < Check
    exit_success
  )");
  run_prog(10);
  ASSERT_EQ(0u, get_z_bit(arm7->registers().R[1]));
  ASSERT_EQ(1u, get_z_bit(arm7->registers().R[2]));
  ASSERT_EQ(arm7->registers().R[2], arm7->registers().CPSR.raw);
}

TEST_F(Arm7DI, DataProcessing_MSR_Immediate)
{
  // NOTE: MRS tests MUST pass first before the results of this test are meaningful
  prepare_test(R"(
    @ First just set Z=1
    mov r0, #0
    adds r0, r0, #1             @      Z = 0
    mrs  r1, cpsr               @   r1.Z = 0 < Check
    msr  cpsr_flg, #0xf0000000  @ cpsr.Z = 1 < Check
    mov r0, r0
    exit_success
  )");
  run_prog(10);
  ASSERT_EQ(0u, get_z_bit(arm7->registers().R[1]));
  ASSERT_EQ(1u, arm7->registers().CPSR.Z);
}

TEST_F(Arm7DI, MUL)
{
  // pg. 40
  prepare_test(R"(
    mov  r0, #4
    mov  r1, #3
    mul  r2, r0, r1
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);
  ASSERT_EQ(12u, arm7->registers().R[2]);
}

TEST_F(Arm7DI, MLA)
{
  // pg. 40
  prepare_test(R"(
    mov  r0, #2
    mov  r1, #3
    mov  r2, #5
    mla  r3, r0, r1, r2 @ r3 <- r0 * r1 + r2
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(u32(2 * 3 + 5), arm7->registers().R[3]);
}

TEST_F(Arm7DI, MLA_Conditional_SetFlagZ)
{
  // pg. 40
  prepare_test(R"(
    mov  r0, #2
    mov  r1, #3
    ldr  r2, =-6
    cmp r0, r1
    mlalts r3, r0, r1, r2  @ 2*3 - 6
    mrs r4, cpsr
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(6), instructions_executed);
  ASSERT_EQ(u32(0), arm7->registers().R[3]);
  ASSERT_EQ(u32(1), get_z_bit(arm7->registers().R[4]));
}

TEST_F(Arm7DI, SingleDataSwap_Word)
{
  // pg. 40
  prepare_test(R"(
    ldr  r0, =0x1234
    str  r0, [sp]
    ldr  r0, =0x5678
    swp  r1, r0, [sp]
    exit_success
  )");
  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(4), instructions_executed);
  ASSERT_EQ(u32(0x1234), arm7->registers().R[1]);
  ASSERT_EQ(u32(0x5678), arm7->registers().R[0]);
}

TEST_F(Arm7DI, MSR_SPSR_NoModeChange)
{
  // Simultaneously testing that CPSR is not changed and no mode change occurred
  const u32 original_cpsr = 0x01234567;
  const u32 original_spsr = 0x89abcdef;

  prepare_test(R"(
    ldr r1, =0x11112222
    msr SPSR_fc, r1
    exit_success
  )");

  // prepare_test performed reset. Set 'starting' values
  arm7->registers().CPSR.raw = original_cpsr;
  arm7->registers().SPSR.raw = original_spsr;

  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(2), instructions_executed);
  ASSERT_EQ(original_cpsr, arm7->registers().CPSR.raw);
  ASSERT_EQ(0x11112222, arm7->registers().SPSR.raw);
}

TEST_F(Arm7DI, LDMIA_WritebackHappensBeforeModeChange)
{
  prepare_test(R"(
    adr r0, jump_success
    stmdb sp!, {r0}      @ Push address to jump_success
    ldmia sp!, {pc}^     @ Pop address to jump_success to PC
    exit_failure         @ ... so we should jump over this ... 
    jump_success:        @ ... to here
      exit_success       @ (and also have performed mode switch)
  )");

  // First force a particular value in the user-mode stack pointer register (R13)
  arm7->mode_switch(guest::arm7di::Mode_SVC, guest::arm7di::Mode_USR);
  arm7->registers().R[13] = 0xCAFE'CACE;
  arm7->mode_switch(guest::arm7di::Mode_USR, guest::arm7di::Mode_SVC);

  // Run the test in Supervisor mode, but CPSR restore will send us to user mode
  arm7->registers().SPSR.raw = guest::arm7di::Mode_USR;
  arm7->registers().CPSR.raw = guest::arm7di::Mode_SVC;

  u32 instructions_executed = run_prog(10);
  ASSERT_EQ(u32(3), instructions_executed);

  // We should have restored CPSR to user mode
  ASSERT_EQ(guest::arm7di::Mode_USR, arm7->registers().CPSR.M);
  // We should see the special SP value we set up before execution
  ASSERT_EQ(u32(0xCAFE'CACE), arm7->registers().R[13]);
  // And the test should have ended by taking the PC branch we setup
  ASSERT_EQ(u32(16), arm7->registers().R[15]);

  // Go to supervisor register set and verify that SP was properly written back as well
  arm7->mode_switch(guest::arm7di::Mode_SVC, guest::arm7di::Mode_USR);
  // original stack pointer + (push 4 - pop 4) = original stack pointer
  ASSERT_EQ(STACK_PTR_INIT, arm7->registers().R[13]);
}

TEST_F(Arm7DI, LoopAdd)
{
  // int func() {
  //   int x=5, y = 0;
  //   while(x>0) {
  //     y += x;
  //     x -= 1;
  //   }
  //   return y;
  // }

  prepare_test(R"(
      func:
        func_00: push    {fp}            @ (str fp, [sp, #-4]!) @ e52db004
        func_04: add     fp, sp, #0                             @ e28db000
        func_08: sub     sp, sp, #12                            @ e24dd00c
        func_0c: mov     r3, #5                                 @ e3a03005
        func_10: str     r3, [fp, #-8]                          @ e50b3008
        func_14: mov     r3, #0                                 @ e3a03000
        func_18: str     r3, [fp, #-12]                         @ e50b300c
        func_1c: b       func_3c @ <func+0x3c>                  @ ea000006
        func_20: ldr     r2, [fp, #-12]  @ sum                  @ e51b200c
        func_24: ldr     r3, [fp, #-8]   @ counter              @ e51b3008
        func_28: add     r3, r2, r3                             @ e0823003
        func_2c: str     r3, [fp, #-12]                         @ e50b300c
        func_30: ldr     r3, [fp, #-8]                          @ e51b3008
        func_34: sub     r3, r3, #1                             @ e2433001
        func_38: str     r3, [fp, #-8]                          @ e50b3008
        func_3c: ldr     r3, [fp, #-8]                          @ e51b3008
        func_40: cmp     r3, #0                                 @ e3530000
        func_44: bgt     func_20 @ <func+0x20>                  @ cafffff5
        func_48: ldr     r3, [fp, #-12]                         @ e51b300c
        func_4c: mov     r0, r3                                 @ e1a00003
        func_50: add     sp, fp, #0                             @ e28bd000
        func_54: pop     {fp}            @ (ldr fp, [sp], #4)   @ e49db004
      exit_success
    )");

  u32 instructions_executed = run_prog(100);
  ASSERT_EQ(u32(65), instructions_executed);
  ASSERT_EQ(5 + 4 + 3 + 2 + 1, arm7->registers().R[0]);
}

int
main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  printf("Note: You can set ARM7DI_DEBUG environment variable for more info\n");
  return RUN_ALL_TESTS();
}
