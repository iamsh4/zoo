#pragma once

#include "fox/fox_types.h"
#include "fox/jit/types.h"

namespace fox {
namespace codegen {
namespace amd64 {

/*!
 * @enum  fox::codegen::amd64::GeneralRegister
 * @brief The x86-64 integer register set. The enum values are based on the
 *        encoding of the register when used as an operand.
 *
 * The least significant 3 bits of each enum's value is the encoding used for
 * that register in rm and reg fields of ModRegRM. The upper bit is encoded
 * with the REX prefix. SPL/BPL/SIL/DIL require the REX prefix or they will
 * instead access the second byte of AX/CX/DX/BX.
 */
enum GeneralRegister : u8
{
  RAX = 0,
  RCX = 1,
  RDX = 2,
  RBX = 3,
  RSP = 4,
  RBP = 5,
  RSI = 6,
  RDI = 7,
  R8 = 8,
  R9 = 9,
  R10 = 10,
  R11 = 11,
  R12 = 12,
  R13 = 13,
  R14 = 14,
  R15 = 15,
};

/*!
 * @enum  fox::codegen::amd64::VectorRegister
 * @brief The x86-64 vector register set. These are the registers used with
 *        SSE and AVX instructions.
 *
 * As with GeneralRegister, the values here are used directly to encode the
 * instructions that use them. Unlike GeneralRegister, the values actually
 * follow common sense.
 */
enum VectorRegister : u8
{
  XMM0 = 0,
  XMM1 = 1,
  XMM2 = 2,
  XMM3 = 3,
  XMM4 = 4,
  XMM5 = 5,
  XMM6 = 6,
  XMM7 = 7,
  XMM8 = 8,
  XMM9 = 9,
  XMM10 = 10,
  XMM11 = 11,
  XMM12 = 12,
  XMM13 = 13,
  XMM14 = 14,
  XMM15 = 15,
};

/*!
 * @enum fox::codegen::amd64::RegisterSize
 * @brief Bit widths of standard amd64 registers. Sizes through 64 bits are
 *        used for general purpose registers. Vector registers mainly use sizes
 *        32 through 256.
 *
 * Note: Encoding of YMM registers / AVX instructions isn't supported or planned
 *       at this time.
 */
enum RegisterSize
{
  /*! @brief Size not specified */
  ANY = 0,

  /*! @brief 1-byte integer */
  BYTE = 1,

  /*! @brief 2-byte integer */
  WORD = 2,

  /*! @brief 4-byte integer */
  DWORD = 3,

  /*! @brief 8-byte integer */
  QWORD = 4,

  /*! @brief Generic XMM (128-bit) vector register */
  XMM = 5,

  /*! @brief 1 single precision float */
  VECSS = 6,

  /*! @brief 4 single precision floats */
  VECPS = 7,

  /*! @brief 1 double precision float */
  VECSD = 8,

  /*! @brief 2 double precision floats */
  VECPD = 9,
};

/*
 * Allocation sets for registers. The first set includes all allocable general
 * registers, and the second set includes the vector registers.
 */
static constexpr jit::HwRegister::Type ScalarType = jit::HwRegister::Type(1);
static constexpr jit::HwRegister::Type VectorType = jit::HwRegister::Type(2);

/*
 * Configuration structures for the two register types. These are used to
 * simplify templated logic below.
 */
struct _Scalar {
  static constexpr jit::HwRegister::Type Type = ScalarType;
  typedef GeneralRegister Enum;
};

struct _Vector {
  static constexpr jit::HwRegister::Type Type = VectorType;
  typedef VectorRegister Enum;
};

/*
 * Configuration for a meta register type. This cannot be used directly, but
 * any object using this type can be converted to the scalar or vector variant.
 */
struct _Any {
  static constexpr jit::HwRegister::Type Type = jit::HwRegister::Type(9999);
  typedef unsigned Enum;
};

/*!
 * @class fox::codegen::amd64::RegisterBase
 * @brief Class representing a single register operand on AMD64 platforms.
 *        It is templated on the access size and type of the register. This way
 *        methods that use different types of registers have unique signatures.
 */
template<typename T, RegisterSize s>
class RegisterBase {
public:
  RegisterBase(const typename T::Enum value = (typename T::Enum)0) : m_encoding((u8)value)
  {
    assert(m_encoding < 16u);
  }

  RegisterBase(const jit::HwRegister hw) : m_encoding(hw.index())
  {
    assert(hw.type() == T::Type && hw.index() < 16u);
  }

  u8 encoding() const
  {
    return m_encoding;
  }

private:
  u8 m_encoding;
};

template<RegisterSize s>
using Register = RegisterBase<_Scalar, s>;

template<RegisterSize s>
using Vector = RegisterBase<_Vector, s>;

/*!
 * @class fox::codegen::amd64::FixedRegister
 * @brief An extension of the Register class that has a different type for each
 *        register and access size. This is used to distinguish method
 *        signatures for instructions that always use specific registers.
 */
template<RegisterSize s, GeneralRegister r>
class FixedRegister : public Register<s> {
public:
  FixedRegister() : Register<s>(r)
  {
    static_assert(r < 16);
  }
};

/*!
 * @class fox::codegen::amd64::FixedAddress
 * @brief A fixed 64-bit address (called an 'moffset' in the instruction encoding
 *        manuals from AMD). Templated on the size of the memory access.
 */
template<RegisterSize s>
class FixedAddress {
public:
  FixedAddress() : m_address(0lu)
  {
    return;
  }

  FixedAddress(const FixedAddress<ANY> &other) : m_address(other.m_address)
  {
    return;
  }

  FixedAddress(const uint64_t value) : m_address(value)
  {
    return;
  }

  uint64_t value() const
  {
    return m_address;
  }

private:
  uint64_t m_address;
};

/*!
 * @class fox::codegen::amd64::Address
 * @brief Representation of an address calculated from a fixed byte offset from
 *        a register (including 0-byte offsets).
 */
template<RegisterSize s>
class Address {
public:
  Address()
  {
    return;
  }

  Address(const Address<ANY> &other) : m_base(other.m_base), m_offset(other.m_offset)
  {
    return;
  }

  explicit Address(const GeneralRegister base, const int32_t offset = 0)
    : m_base(base),
      m_offset(offset)
  {
    return;
  }

  explicit Address(const jit::HwRegister hw, const int32_t offset = 0)
    : m_base(hw),
      m_offset(offset)
  {
    assert(hw.type() == ScalarType && hw.index() < 16u);
  }

  explicit Address(const Register<QWORD> &base, const int32_t offset = 0)
    : m_base(base),
      m_offset(offset)
  {
    return;
  }

  Register<QWORD> base() const
  {
    return m_base;
  }

  int32_t offset() const
  {
    return m_offset;
  }

private:
  Register<QWORD> m_base;
  int32_t m_offset;
};

/*!
 * @class fox::codegen::amd64::IndexedAddress
 * @brief Representation of an address calculated with the SIB byte extension
 *        on amd64 platforms.
 */
template<RegisterSize s>
class IndexedAddress {
public:
  IndexedAddress()
  {
    return;
  }

  IndexedAddress(const IndexedAddress<ANY> &other)
    : m_base(other.base()),
      m_index(other.index()),
      m_scale(other.scale()),
      m_offset(other.offset())
  {
    assert(m_scale == 1 || m_scale == 2 || m_scale == 4 || m_scale == 8);
    assert(m_index.encoding() != RSP);
  }

  IndexedAddress(const GeneralRegister base,
                 const GeneralRegister index,
                 const unsigned scale,
                 const i32 offset = 0)
    : m_base(base),
      m_index(index),
      m_scale(scale),
      m_offset(offset)
  {
    assert(scale == 1 || scale == 2 || scale == 4 || scale == 8);
    assert(m_index.encoding() != RSP);
  }

  IndexedAddress(const jit::HwRegister base,
                 const jit::HwRegister index,
                 const unsigned scale,
                 const i32 offset = 0)
    : m_base((GeneralRegister)base.index()),
      m_index((GeneralRegister)index.index()),
      m_scale(scale),
      m_offset(offset)
  {
    assert(scale == 1 || scale == 2 || scale == 4 || scale == 8);
    assert(m_index.encoding() != RSP);
  }

  IndexedAddress(const Register<QWORD> &base,
                 const Register<QWORD> &index,
                 const unsigned scale,
                 const i32 offset = 0)
    : m_base(base),
      m_index(index),
      m_scale(scale),
      m_offset(offset)
  {
    assert(scale == 1 || scale == 2 || scale == 4 || scale == 8);
    assert(m_index.encoding() != RSP);
  }

  Register<QWORD> base() const
  {
    return m_base;
  }

  Register<QWORD> index() const
  {
    return m_index;
  }

  unsigned scale() const
  {
    return m_scale;
  }

  int32_t offset() const
  {
    return m_offset;
  }

private:
  Register<QWORD> m_base;
  Register<QWORD> m_index;
  unsigned m_scale;
  int32_t m_offset;
};

/*!
 * @class fox::codegen::amd64::RegMemBase
 * @brief A mostly internal type to the assembler interface which can represnt
 *        any operand type from a "reg/mem" mnemonic field.
 *
 * Instead of being created directly during assembly, one of the more specific
 * types should be used. It will automatically be converted to a RegMemBase when
 * passed to the assembler. These are:
 *
 *     Register<>, FixedRegister<> : Use register value directly
 *     Address<> : The register value directly specifies a memory address.
 *
 * TODO
 */
template<typename T, RegisterSize s>
class RegMemBase {
public:
  RegMemBase()
  {
    return;
  }

  RegMemBase(const RegisterBase<T, s> &direct) : m_direct(direct)
  {
    return;
  }

  explicit RegMemBase(const RegMemBase<_Any, ANY> &other)
    : m_memory(other.is_memory()),
      m_has_base(other.has_base()),
      m_has_offset(other.has_offset()),
      m_has_scaled_index(other.has_scaled_index()),
      m_direct(),
      m_base(other.base()),
      m_index(other.index()),
      m_scale(other.scale()),
      m_offset(other.offset())
  {
    /* Arbitiary conversion can't happen between vector / non-vector
     * registers. */
    assert(other.is_memory());
  }

  RegMemBase(const Address<s> &from)
    : m_memory(true),
      m_has_base(true),
      m_has_offset(from.offset() != 0),
      m_base(from.base()),
      m_offset(from.offset())
  {
    /* It's not possible to use RBP / RSP in most ModRM configurations as a
     * base register without SIB or offset. RBP can still be encoded as a base
     * if we unconditionally write an zero-byte offset. The same rules apply
     * for the newer R12/R13 aliases. */
    if ((m_base.encoding() & 0x7) == 5 && !m_has_offset) {
      m_has_offset = true;
    } else if ((m_base.encoding() & 0x7) == 4) {
      /* This encodes for SIB, so it must be converted to SIB form. In SIB
       * form an index of RSP means no index, so we just get m_base plus any
       * offset. */
      m_has_scaled_index = true;
      m_index = Register<QWORD>(RSP);
      m_scale = 1;
    }
  }

  RegMemBase(const IndexedAddress<s> &from)
    : m_memory(true),
      m_has_base(true),
      m_has_offset(from.offset() != 0),
      m_has_scaled_index(true),
      m_base(from.base()),
      m_index(from.index()),
      m_scale(from.scale()),
      m_offset(from.offset())
  {
    return;
  }

  bool is_memory() const
  {
    return m_memory;
  }

  bool has_base() const
  {
    return m_has_base;
  }

  bool has_offset() const
  {
    return m_has_offset;
  }

  bool has_scaled_index() const
  {
    return m_has_scaled_index;
  }

  RegisterBase<T, s> direct() const
  {
    return m_direct;
  }

  Register<QWORD> base() const
  {
    return m_base;
  }

  Register<QWORD> index() const
  {
    return m_index;
  }

  unsigned scale() const
  {
    return m_scale;
  }

  int32_t offset() const
  {
    return m_offset;
  }

private:
  bool m_memory = false;
  bool m_has_base = false;
  bool m_has_offset = false;
  bool m_has_scaled_index = false;
  RegisterBase<T, s> m_direct;
  Register<QWORD> m_base;
  Register<QWORD> m_index;
  u16 m_scale = 0;
  int32_t m_offset = 0;
};

template<RegisterSize s>
using RegMem = RegMemBase<_Scalar, s>;

template<RegisterSize s>
using RegMemVector = RegMemBase<_Vector, s>;

using RegMemAny = RegMemBase<_Any, ANY>;

}
}
}
