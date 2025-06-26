// vim: expandtab:ts=2:sw=2

#include <cstdio>

#include "jit/linear_register_allocator.h"

using namespace fox;

static const jit::HwRegister::Type type = jit::HwRegister::Type(1);
static const jit::HwRegister HW_ANY(type);

void
test_range_set()
{
  printf("======== test_range_set ========\n");
  jit::RangeSet rset;

  rset.add_range(0, 5, 10);
  rset.add_range(0, 20, 21);
  rset.add_range(0, 11, 12);
  rset.add_range(1, 7, 13);
  rset.add_range(0, 13, 15);
  rset.add_range(0, 12, 13);
  rset.debug_print();

  const unsigned test_points1[] = { 3, 7, 13, 17, 24 };
  for (const unsigned position : test_points1) {
    printf(
      "is_contended(0, %u) = %s\n", position, rset.is_contended(0, position) ? "y" : "n");
    printf(
      "is_contended(1, %u) = %s\n", position, rset.is_contended(1, position) ? "y" : "n");
  }

  const std::pair<unsigned, unsigned> test_points2[] = {
    std::make_pair(1u, 5u),
    std::make_pair(3u, 7u),
    std::make_pair(0u, 50u),
    std::make_pair(25u, 50u),
  };
  for (const auto &at : test_points2) {
    printf("is_contended_range(0, %u, %u) = %s\n",
           at.first,
           at.second,
           rset.is_contended_range(0, at.first, at.second) ? "y" : "n");
    printf("is_contended_range(1, %u, %u) = %s\n",
           at.first,
           at.second,
           rset.is_contended_range(1, at.first, at.second) ? "y" : "n");
  }

  printf("\n");
}

void
test_allocate_constraints()
{
  printf("======== test_allocate_constraints ========\n");

  jit::LinearAllocator allocator;
  jit::RtlProgram target;
  allocator.define_register_type(jit::RegisterSet(type, 3));

  const jit::RtlProgram::BlockHandle block_handle = target.allocate_block("test");
  assert(block_handle == 0);
  (void)block_handle;

  /* Bogus instructions to create a few RTL registers / fill in space. */
  for (unsigned i = 0; i < 4; ++i) {
    jit::RtlInstruction entry(0u, 1u);
    entry.op = 1;
    entry.result(0u) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    target.block(0).push_back(entry);
  }

  /* Instruction with an input constraint. */
  {
    jit::RtlInstruction entry(2u, 1u);
    entry.op = 1;
    entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    entry.source(0) =
      jit::RegisterAssignment { jit::RtlRegister(1), jit::HwRegister(type, 2) };
    entry.source(1) = jit::RegisterAssignment { jit::RtlRegister(3), HW_ANY };
    target.block(0).push_back(entry);
  }

  /* Instruction with an output constraint. */
  {
    jit::RtlInstruction entry(2u, 1u);
    entry.op = 1;
    entry.result(0) =
      jit::RegisterAssignment { target.ssa_allocate(0), jit::HwRegister(type, 2) };
    entry.source(0) = jit::RegisterAssignment { jit::RtlRegister(2), HW_ANY };
    entry.source(1) = jit::RegisterAssignment { jit::RtlRegister(4), HW_ANY };
    target.block(0).push_back(entry);
  }

  /* Instruction with an constraints on both sides. */
  {
    jit::RtlInstruction entry(2u, 1u);
    entry.op = 1;
    entry.result(0) =
      jit::RegisterAssignment { target.ssa_allocate(0), jit::HwRegister(type, 2) };
    entry.source(0) =
      jit::RegisterAssignment { jit::RtlRegister(2), jit::HwRegister(type, 1) };
    entry.source(1) = jit::RegisterAssignment { jit::RtlRegister(0), HW_ANY };
    target.block(0).push_back(entry);
  }

  target.debug_print();

  target = allocator.execute(std::move(target));
  printf("---\n");
  target.debug_print();
  printf("\tTotal spill memory: %u\n", target.spill_size());

  printf("\n");
}

void
test_allocate_external()
{
  printf("======== test_allocate_external ========\n");

  jit::LinearAllocator allocator;
  jit::RtlProgram target;
  allocator.define_register_type(jit::RegisterSet(type, 3));

  const jit::RtlProgram::BlockHandle block_handle = target.allocate_block("test");
  assert(block_handle == 0);
  (void)block_handle;

  /* Bogus instruction to create an RTL register / fill in space. */
  jit::RtlInstruction entry(0u, 1u);
  entry.op = 1;
  entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
  target.block(0).push_back(entry);

  /* Instruction that uses an RTL register and a manually assigned non-alllocable
   * register. */
  {
    jit::RtlInstruction entry(2u, 1u);
    entry.op = 1;
    entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    entry.source(0) = jit::RegisterAssignment { jit::RtlRegister(0), HW_ANY };
    entry.source(1) =
      jit::RegisterAssignment { jit::RtlRegister(), jit::HwRegister(type, 99) };
    target.block(0).push_back(entry);
  }

  target.debug_print();

  target = allocator.execute(std::move(target));
  printf("---\n");
  target.debug_print();

  printf("\n");
}

void
test_allocate_duplicate()
{
  printf("======== test_allocate_duplicate ========\n");

  jit::LinearAllocator allocator;
  jit::RtlProgram target;
  allocator.define_register_type(jit::RegisterSet(type, 3));

  const jit::RtlProgram::BlockHandle block_handle = target.allocate_block("test");
  assert(block_handle == 0);
  (void)block_handle;

  /* Bogus instruction to create an RTL register / fill in space. */
  {
    jit::RtlInstruction entry(0u, 1u);
    entry.op = 1;
    entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    target.block(0).push_back(entry);
  }

  /* Instruction that uses the same RTL register for multiple sources. */
  {
    jit::RtlInstruction entry(2u, 1u);
    entry.op = 1;
    entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    entry.source(0) = jit::RegisterAssignment { jit::RtlRegister(0), HW_ANY };
    entry.source(1) = jit::RegisterAssignment { jit::RtlRegister(0), HW_ANY };
    target.block(0).push_back(entry);
  }

  target.debug_print();

  target = allocator.execute(std::move(target));
  printf("---\n");
  target.debug_print();

  printf("\n");
}

void
test_allocate_multiple_results()
{
  printf("======== test_allocate_multiple_results ========\n");

  jit::LinearAllocator allocator;
  jit::RtlProgram target;
  allocator.define_register_type(jit::RegisterSet(type, 3));

  const jit::RtlProgram::BlockHandle block_handle = target.allocate_block("test");
  assert(block_handle == 0);
  (void)block_handle;

  /* Bogus instruction to create an RTL register / fill in space. */
  {
    jit::RtlInstruction entry(0u, 1u);
    entry.op = 1;
    entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    target.block(0).push_back(entry);
  }

  /* Bogus instruction to create an RTL register / fill in space. */
  {
    jit::RtlInstruction entry(0u, 1u);
    entry.op = 1;
    entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    target.block(0).push_back(entry);
  }

  /* Instruction that has multiple inputs and outputs. */
  {
    jit::RtlInstruction entry(2u, 2u);
    entry.op = 2;
    entry.result(0) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    entry.result(1) = jit::RegisterAssignment { target.ssa_allocate(0), HW_ANY };
    entry.source(0) = jit::RegisterAssignment { jit::RtlRegister(0), HW_ANY };
    entry.source(1) = jit::RegisterAssignment { jit::RtlRegister(1), HW_ANY };
    target.block(0).push_back(entry);
  }

  target.debug_print();

  target = allocator.execute(std::move(target));
  printf("---\n");
  target.debug_print();

  printf("\n");
}

int
main(int argc, char *argv[])
{
  test_range_set();
  test_allocate_constraints();
  test_allocate_external();
  test_allocate_duplicate();
  test_allocate_multiple_results();

  return 0;
}
