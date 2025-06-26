// vim: expandtab:ts=2:sw=2

#include <string>
#include <cstdio>
#include <cstring>
#include <cassert>

#include "amd64/amd64_assembler.h"
#include "amd64/amd64_routine.h"

using namespace fox;
using namespace fox::codegen::amd64;

void
test_mov()
{
  printf("======== test_mov ========\n");

  Assembler assembler;
  assembler.mov(RegMem<BYTE>(RAX), Register<BYTE>(RBX));
  assembler.mov(RegMem<WORD>(RAX), Register<WORD>(RBX));
  assembler.mov(RegMem<DWORD>(RAX), Register<DWORD>(RBX));
  assembler.mov(RegMem<QWORD>(RAX), Register<QWORD>(RBX));

  assembler.mov(RegMem<BYTE>(R8), Register<BYTE>(R10));
  assembler.mov(RegMem<WORD>(R15), Register<WORD>(R10));
  assembler.mov(RegMem<DWORD>(R8), Register<DWORD>(R10));
  assembler.mov(RegMem<QWORD>(R15), Register<QWORD>(R10));

  assembler.mov(FixedAddress<WORD>(0xaabbccddaabbccddu), FixedRegister<WORD, RAX>());
  assembler.mov(FixedRegister<QWORD, RAX>(), FixedAddress<QWORD>(0xaabbccddaabbccddu));

  assembler.mov(Register<BYTE>(RAX), 0x55u);
  assembler.mov(Register<WORD>(RCX), 0xb0a0u);
  assembler.mov(Register<DWORD>(R13), 0xc001deafu);
  assembler.mov(Register<QWORD>(R15), 0xbadaffeclu);

  assembler.mov(RegMem<DWORD>(R13), 0xc001deafu);
  assembler.mov(RegMem<QWORD>(R15), 0x7e110u);
  assembler.mov(RegMem<QWORD>(R15), -32);

  Routine r(assembler.data(), assembler.size());
  r.debug_print();

  printf("\n");
}

void
test_regmem()
{
  printf("======== test_regmem ========\n");

  Assembler assembler;

  /* Basic set. */
  assembler.mov(RegMem<QWORD>(Register<QWORD>(R10)), Register<QWORD>(R8));
  assembler.mov(Address<QWORD>(R10), Register<QWORD>(R8));
  assembler.mov(Address<QWORD>(R10, -32), Register<QWORD>(R8));
  assembler.mov(Address<QWORD>(R10, 0xacafe), Register<QWORD>(R8));
  assembler.mov(IndexedAddress<QWORD>(R10, R10, 2), Register<QWORD>(R8));

  /* Reverse set. */
  assembler.mov(Register<QWORD>(R8), RegMem<QWORD>(Register<QWORD>(R10)));
  assembler.mov(Register<QWORD>(R8), Address<QWORD>(R10));
  assembler.mov(Register<QWORD>(R8), Address<QWORD>(R10, -32));
  assembler.mov(Register<QWORD>(R8), Address<QWORD>(R10, 0xacafe));
  assembler.mov(Register<QWORD>(R8), IndexedAddress<QWORD>(R10, R10, 2));

  /* Encodings with special cases / edge cases. */
  assembler.mov(Register<QWORD>(R8), Address<QWORD>(RSP));
  assembler.mov(Register<QWORD>(R8), Address<QWORD>(RBP));
  assembler.mov(Address<QWORD>(RSP), Register<QWORD>(RAX));
  assembler.mov(Address<QWORD>(RBP), Register<QWORD>(RAX));

  Routine r(assembler.data(), assembler.size());
  r.debug_print();

  printf("\n");
}

void
test_branch()
{
  printf("======== test_branch ========\n");

  Assembler assembler;

  /* Different sized conditional branch displacements. */
  assembler.ja(i8(5));
  assembler.ja(i32(5));

  Routine r(assembler.data(), assembler.size());
  r.debug_print();

  printf("\n");
}

void
test_variety()
{
  printf("======== test_variety ========\n");

  Assembler assembler;

  /* Basic ALU ops. */
  assembler.add(Address<QWORD>(R10, 0xacafe), Register<QWORD>(RBX));
  assembler.add(IndexedAddress<QWORD>(RSP, RAX, 2), Register<QWORD>(RBP));
  assembler.add(IndexedAddress<QWORD>(RSP, RAX, 2), 37);
  assembler.add(IndexedAddress<QWORD>(RSP, RAX, 2), -207);
  assembler.add(IndexedAddress<QWORD>(RSP, RAX, 2), 207);
  assembler._and(Address<QWORD>(R10, 0xacafe), Register<QWORD>(RBX));
  assembler._and(IndexedAddress<QWORD>(RSP, RAX, 2), Register<QWORD>(RBP));
  assembler._and(IndexedAddress<QWORD>(RSP, RAX, 2, 5), (u8)207);

  /* Sign extension with corner-case encodings. */
  assembler.movsx(Register<DWORD>(RAX), Register<BYTE>(RSI));
  assembler.movsx(Register<DWORD>(RAX), Register<BYTE>(RBX));
  assembler.movsx(Register<DWORD>(RAX), Register<BYTE>(R15));

  /* Some more complex ops. */
  // assembler.lea(Register<QWORD>(R10), IndexedAddress<BYTE>(RAX, RAX, 2, 0xf234));
  assembler.lea(Register<QWORD>(R10), IndexedAddress<BYTE>(R10, R11, 2, 0xf234));

  /* Control flow ops. */
  assembler.call(-302);
  assembler.call(Address<QWORD>(R10, 0x1234));
  assembler.ret();
  assembler.ret(32);

  // printf("%s", disassemble(assembler.data(), assembler.size()).c_str());
  Routine r(assembler.data(), assembler.size());
  r.debug_print();

  printf("\n");
}

void
test_vector()
{
  printf("======== test_vector ========\n");

  Assembler assembler;

  /* Move operations */
  assembler.movapd(Vector<XMM>(XMM0), RegMemVector<XMM>(XMM1));
  assembler.movapd(RegMemVector<XMM>(XMM9), Vector<XMM>(XMM3));
  assembler.movaps(Vector<XMM>(XMM0), RegMemVector<XMM>(XMM1));
  assembler.movaps(RegMemVector<XMM>(XMM9), Vector<XMM>(XMM3));
  assembler.movd(Vector<DWORD>(XMM0), RegMem<DWORD>(RCX));
  assembler.movd(Vector<QWORD>(XMM0), RegMem<QWORD>(RCX));
  assembler.movd(RegMem<DWORD>(R9), Vector<DWORD>(XMM3));
  assembler.movd(RegMem<QWORD>(R9), Vector<QWORD>(XMM3));

  /* Basic add / subtract. */
  assembler.addpd(Vector<XMM>(XMM0), RegMemVector<XMM>(XMM1));
  assembler.addpd(Vector<XMM>(XMM9), RegMemVector<XMM>(XMM7));
  assembler.addsd(Vector<QWORD>(XMM0), RegMemVector<QWORD>(XMM1));
  assembler.addsd(Vector<QWORD>(XMM9), RegMemVector<QWORD>(XMM7));
  assembler.subps(Vector<XMM>(XMM0), RegMemVector<XMM>(XMM1));
  assembler.subps(Vector<XMM>(XMM9), RegMemVector<XMM>(XMM7));
  assembler.subss(Vector<DWORD>(XMM0), RegMemVector<DWORD>(XMM1));
  assembler.subss(Vector<DWORD>(XMM9), RegMemVector<DWORD>(XMM7));

  Routine r(assembler.data(), assembler.size());
  r.debug_print();
  printf("\n");
}

void
test_cvt()
{
  printf("======== test_cvt ========\n");

  Assembler assembler;

  /* Float -> Int */
  assembler.cvtss2si(Register<DWORD>(RAX), Vector<DWORD>(XMM0));
  assembler.cvtss2si(Register<QWORD>(RAX), Vector<QWORD>(XMM0));
  assembler.cvtsd2si(Register<DWORD>(RAX), Vector<DWORD>(XMM0));
  assembler.cvtsd2si(Register<QWORD>(RAX), Vector<QWORD>(XMM0));

  /* Int -> Float */
  assembler.cvtsi2ss(Vector<DWORD>(XMM0), Register<DWORD>(RAX));
  assembler.cvtsi2ss(Vector<QWORD>(XMM0), Register<QWORD>(RAX));
  assembler.cvtsi2sd(Vector<DWORD>(XMM0), Register<DWORD>(RAX));
  assembler.cvtsi2sd(Vector<QWORD>(XMM0), Register<QWORD>(RAX));

  Routine r(assembler.data(), assembler.size());
  r.debug_print();

  printf("\n");
}

int
main(int argc, char *argv[])
{
  test_mov();
  test_regmem();
  test_branch();
  test_variety();
  test_vector();
  test_cvt();
  return 0;
}
