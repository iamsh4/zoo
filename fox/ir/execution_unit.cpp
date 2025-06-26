// vim: expandtab:ts=2:sw=2

#include <map>
#include <cassert>
#include <fmt/core.h>

#include "fox/ir/execution_unit.h"

namespace fox {
namespace ir {

ExecutionUnit::ExecutionUnit(const unsigned register_offset)
  : m_register_count(register_offset)
{
  return;
}

ExecutionUnit::ExecutionUnit(ExecutionUnit &&other)
  : m_instructions(std::move(other.m_instructions)),
    m_register_count(other.m_register_count)
{
  other.m_register_count = 0lu;
}

ExecutionUnit &
ExecutionUnit::operator=(ExecutionUnit &&other)
{
  if (this != &other) {
    m_instructions = std::move(other.m_instructions);
    m_register_count = other.m_register_count;
    other.m_register_count = 0lu;
  }
  return *this;
}

ExecutionUnit
ExecutionUnit::copy() const
{
  ExecutionUnit result;
  result.m_instructions = m_instructions;
  result.m_register_count = m_register_count;
  return result;
}

Operand
ExecutionUnit::allocate_register(const Type type)
{
  return Operand(type, m_register_count++);
}

void
ExecutionUnit::append(const Opcode opcode,
                      const Type type,
                      const std::initializer_list<Operand> &results,
                      const std::initializer_list<Operand> &sources)
{
  m_instructions.append(opcode, type, results, sources);
}

void
ExecutionUnit::add_instruction(const Instruction &instruction)
{
  m_instructions.push_back(instruction);
}

std::string
ExecutionUnit::disassemble() const
{
  if (m_instructions.empty()) {
    return "<none>\n";
  }

  std::string result;
  result.reserve(m_instructions.size() * 24lu /* Rough estimate */);
  size_t offset = 0;
  for (const Instruction &instruction : m_instructions) {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "[%04lu] ", offset);
    result += buffer;
    result += disassemble_instruction(instruction);
    ++offset;
  }
  return result;
}

void
ExecutionUnit::debug_print() const
{
  puts(disassemble().c_str());
}

std::string
ExecutionUnit::disassemble_instruction(const Instruction &instruction) const
{
  std::string result;
  if (instruction.result_count() > 0) {
    assert(instruction.result_count() == 1);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "$%u := ", instruction.result(0).register_index());
    result += buffer;
  }

  switch (instruction.source_count()) {
    char buffer[128];
    case 0:
      snprintf(buffer, sizeof(buffer), "%s\n", opcode_to_name(instruction.opcode()));
      result += buffer;
      break;

    case 1:
      snprintf(buffer,
               sizeof(buffer),
               "%s.%s %s\n",
               opcode_to_name(instruction.opcode()),
               type_to_name(instruction.type()),
               string_operand(instruction.source(0)).c_str());
      result += buffer;
      break;

    case 2:
      snprintf(buffer,
               sizeof(buffer),
               "%s.%s %s, %s\n",
               opcode_to_name(instruction.opcode()),
               type_to_name(instruction.type()),
               string_operand(instruction.source(0)).c_str(),
               string_operand(instruction.source(1)).c_str());
      result += buffer;
      break;

    case 3:
      snprintf(buffer,
               sizeof(buffer),
               "%s.%s %s, %s, %s\n",
               opcode_to_name(instruction.opcode()),
               type_to_name(instruction.type()),
               string_operand(instruction.source(0)).c_str(),
               string_operand(instruction.source(1)).c_str(),
               string_operand(instruction.source(2)).c_str());
      result += buffer;
      break;

    case 4:
      snprintf(buffer,
               sizeof(buffer),
               "%s.%s %s, %s, %s, %s\n",
               opcode_to_name(instruction.opcode()),
               type_to_name(instruction.type()),
               string_operand(instruction.source(0)).c_str(),
               string_operand(instruction.source(1)).c_str(),
               string_operand(instruction.source(2)).c_str(),
               string_operand(instruction.source(3)).c_str());
      result += buffer;
      break;

    default:
      assert(false);
  }

  return result;
}

std::string
ExecutionUnit::string_operand(const Operand operand) const
{
  assert(operand.is_valid());

  char buffer[128];
  if (operand.is_constant()) {
    const Constant value = operand.value();
    switch (operand.type()) {
      case Type::Integer8:
        snprintf(buffer, sizeof(buffer), "#{%02x}", value.u8_value);
        break;
      case Type::Integer16:
        snprintf(buffer, sizeof(buffer), "#{%04x}", value.u16_value);
        break;
      case Type::Integer32:
        snprintf(buffer, sizeof(buffer), "#{%08x}", value.u32_value);
        break;
      case Type::Integer64:
        strcpy(buffer, fmt::format("#{:#018x}", value.u64_value).c_str());
        break;
      case Type::Float32:
        snprintf(buffer, sizeof(buffer), "#{%f}", value.f32_value);
        break;
      case Type::Float64:
        snprintf(buffer, sizeof(buffer), "#{%lf}", value.f64_value);
        break;
      case Type::Bool:
        snprintf(buffer, sizeof(buffer), "%s", value.bool_value ? "true" : "false");
        break;
      case Type::BranchLabel:
        snprintf(buffer, sizeof(buffer), "label.%u", value.label_value);
        break;
      case Type::HostAddress:
        snprintf(buffer, sizeof(buffer), "@0x%p", value.hostptr_value);
        break;
    }
  } else {
    snprintf(buffer, sizeof(buffer), "$%u", operand.register_index());
  }

  return buffer;
}

}
}
