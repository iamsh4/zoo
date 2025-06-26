// vim: expandtab:ts=2:sw=2

#pragma once

#include <vector>
#include <string>

#include "fox/fox_types.h"
#include "fox/ir/instruction.h"

namespace fox {
namespace ir {

class Assembler;
class Compiler;

/*!
 * @class fox::ir::ExecutionUnit
 * @brief Container for an assembled IR program. The IR program can consist of
 *        one or more extended basic blocks.
 */
class ExecutionUnit {
public:
  /*!
   * @brief Construct a new ExecutionUnit. Register IDs can optionally be
   *        offset to allow insertion of instructions that already use lower
   *        register IDs.
   */
  ExecutionUnit(unsigned register_offset = 0);

  ExecutionUnit(ExecutionUnit &&other);

  ExecutionUnit &operator=(ExecutionUnit &&other);

  /*!
   * @brief Create a full copy of the execution unit. Used instead of a copy
   *        constructor / assignment to avoid unintentional copy overhead.
   */
  ExecutionUnit copy() const;

  /*!
   * @brief Return the total number of registers allocated by the execution
   *        unit. Some registers may be unused.
   */
  unsigned register_count() const
  {
    return m_register_count;
  }

  /*!
   * @brief Append a new IR instruction at the end of this execution unit.
   *
   * TODO Multiple basic blocks.
   */
  void append(Opcode opcode,
              Type type,
              const std::initializer_list<Operand> &results,
              const std::initializer_list<Operand> &sources);

  /*!
   * @brief Add an instruction at the end of the execution unit.
   * 
   * TODO Deprecate for append().
   */
  void add_instruction(const Instruction &instruction);

  /*!
   * @brief Read-write access to the sequence of IR instructions.
   */
  Instructions &instructions()
  {
    return m_instructions;
  }

  /*!
   * @brief Read-only access to the sequence of IR instructions.
   */
  const Instructions &instructions() const
  {
    return m_instructions;
  }

  /*!
   * @brief Return the current assembly stream as a human readable string for
   *        debugging.
   */
  std::string disassemble() const;

  /*!
   * @brief Print the current assembly stream to stdout for debugging.
   */
  void debug_print() const;

private:
  friend class Assembler;

  /*!
   * @brief Ordered set of instructions assembled so far.
   */
  Instructions m_instructions;

  /*!
   * @brief The number of IR registers allocated so far. Used to generate the
   *        next IR register's ID.
   */
  unsigned m_register_count = 0u;

  /*!
   * @brief Allocate a new IR register of the given type. Used internally to
   *        create opcode destination registers.
   */
  Operand allocate_register(Type type);

  /*!
   * @brief Return a line with a human-readable form of the indicated
   *        instruction.
   *
   * The following format is used:
   *
   *     ${out} = {mnemonic}.{type} {source1}, {source2}, {source3}
   */
  std::string disassemble_instruction(const Instruction &instruction) const;

  /*!
   * @brief Return a human-readable representation of the provided Operand,
   *        which may be a dynamic valued register or constant.
   */
  std::string string_operand(Operand operand) const;
};

}
}
