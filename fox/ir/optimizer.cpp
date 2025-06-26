#include <algorithm>
#include <climits>

#include "ir/ir_calculator.h"
#include "ir/optimizer.h"

namespace fox {
namespace ir {
namespace optimize {

/*******************************************************************************
 * ir::Optimize::Pass
 ******************************************************************************/

Pass::Pass()
{
  return;
}

Pass::~Pass()
{
  return;
}

/*******************************************************************************
 * ir::Optimize::ConstantPropagation
 ******************************************************************************/

ConstantPropagation::ConstantPropagation()
{
  return;
}

ConstantPropagation::~ConstantPropagation()
{
  return;
}

ExecutionUnit
ConstantPropagation::execute(const ExecutionUnit &source)
{
  ExecutionUnit result(source.register_count());
  std::vector<Operand> outputs;
  outputs.resize(source.register_count());

  /* Pass 1 - Identify and fold constant expressions. */
  const Instructions &instructions = source.instructions();
  Calculator calculator;
  for (auto it = instructions.begin(); it != instructions.end(); ++it) {
    const Instruction &current = *it;
    if (current.result_count() != 1) {
      /* TODO If IR supports multiple outputs, revisit this. */
      continue;
    }

    bool all_constant = true;
    Operand sources[current.source_count()];
    for (unsigned i = 0; i < current.source_count(); ++i) {
      if (current.source(i).is_constant()) {
        sources[i] = current.source(i);
      } else {
        const unsigned index = current.source(i).register_index();
        if (outputs[index].is_valid()) {
          sources[i] = outputs[index];
        } else {
          sources[i] = current.source(i);
        }

        all_constant = all_constant && sources[i].is_constant();
      }
    }

    const unsigned result_index = current.result(0).register_index();
    switch (current.opcode()) {
      case Opcode::RotateRight:
        if (all_constant) {
          outputs[result_index] = calculator.rotr(sources[0], sources[1]);
          break;
        }
        break;

      case Opcode::RotateLeft:
        if (all_constant) {
          outputs[result_index] = calculator.rotl(sources[0], sources[1]);
          break;
        }
        break;

      case Opcode::LogicalShiftRight:
        if (all_constant) {
          outputs[result_index] = calculator.shiftr(sources[0], sources[1]);
          break;
        }
        break;

      case Opcode::LogicalShiftLeft:
        if (all_constant) {
          outputs[result_index] = calculator.shiftl(sources[0], sources[1]);
          break;
        }
        break;

      case Opcode::ArithmeticShiftRight:
        if (all_constant) {
          outputs[result_index] = calculator.ashiftr(sources[0], sources[1]);
          break;
        }
        break;

      case Opcode::And:
        if (all_constant) {
          outputs[result_index] = calculator._and(sources[0], sources[1]);
          break;
        }

        if (sources[0].is_constant() && sources[0].zero_extended() == 0) {
          outputs[result_index] = Operand(sources[0].type(), { .u64_value = 0 });
          break;
        } else if (sources[1].is_constant() && sources[1].zero_extended() == 0) {
          outputs[result_index] = Operand(sources[0].type(), { .u64_value = 0 });
          break;
        }
        break;

      case Opcode::Or:
        if (all_constant) {
          outputs[result_index] = calculator._or(sources[0], sources[1]);
          break;
        }

        if (sources[0] == sources[1]) {
          outputs[result_index] = sources[0];
        } else if (sources[0].is_constant() && sources[0].zero_extended() == 0) {
          outputs[result_index] = sources[1];
          break;
        } else if (sources[1].is_constant() && sources[1].zero_extended() == 0) {
          outputs[result_index] = sources[0];
          break;
        }
        break;

      case Opcode::ExclusiveOr:
        if (all_constant) {
          outputs[result_index] = calculator._xor(sources[0], sources[1]);
          break;
        }

        if (sources[0] == sources[1]) {
          outputs[result_index] = Operand(sources[0].type(), { .u64_value = 0 });
        } else if (sources[0].is_constant() && sources[0].zero_extended() == 0) {
          outputs[result_index] = sources[1];
          break;
        } else if (sources[1].is_constant() && sources[1].zero_extended() == 0) {
          outputs[result_index] = sources[0];
          break;
        }
        break;

      case Opcode::Not:
        if (all_constant) {
          outputs[result_index] = calculator._not(sources[0]);
          break;
        }
        break;

      case Opcode::Add:
        if (all_constant) {
          outputs[result_index] = calculator.add(sources[0], sources[1]);
          break;
        }

        if (is_float_type(sources[0].type())) {
          break;
        }

        if (sources[0].is_constant() && sources[0].zero_extended() == 0) {
          outputs[result_index] = sources[1];
          break;
        } else if (sources[1].is_constant() && sources[1].zero_extended() == 0) {
          outputs[result_index] = sources[0];
          break;
        }
        break;

      case Opcode::Subtract:
        if (all_constant) {
          outputs[result_index] = calculator.sub(sources[0], sources[1]);
          break;
        }

        if (is_float_type(sources[0].type())) {
          break;
        }

        if (sources[1].is_constant() && sources[1].zero_extended() == 0) {
          outputs[result_index] = sources[0];
          break;
        }
        break;

      case Opcode::Multiply:
        if (all_constant) {
          outputs[result_index] = calculator.mul(sources[0], sources[1]);
          break;
        }

        if (is_float_type(sources[0].type())) {
          break;
        }

        if (sources[0].is_constant() && sources[0].zero_extended() == 0) {
          outputs[result_index] = Operand(sources[0].type(), Constant { .u64_value = 0 });
          break;
        } else if (sources[1].is_constant() && sources[1].zero_extended() == 0) {
          outputs[result_index] = Operand(sources[0].type(), Constant { .u64_value = 0 });
          break;
        }
        break;

      case Opcode::Multiply_u:
        if (all_constant) {
          outputs[result_index] = calculator.umul(sources[0], sources[1]);
          break;
        }

        if (sources[0].is_constant() && sources[0].zero_extended() == 0) {
          outputs[result_index] = Operand(sources[0].type(), Constant { .u64_value = 0 });
          break;
        } else if (sources[1].is_constant() && sources[1].zero_extended() == 0) {
          outputs[result_index] = Operand(sources[0].type(), Constant { .u64_value = 0 });
          break;
        }
        break;

      case Opcode::Divide:
        if (all_constant) {
          outputs[result_index] = calculator.div(sources[0], sources[1]);
          break;
        }

        if (is_float_type(sources[0].type())) {
          break;
        }

        if (sources[1].is_constant() && sources[1].zero_extended() == 1) {
          outputs[result_index] = sources[0];
          break;
        }
        break;

      case Opcode::Divide_u:
        if (all_constant) {
          outputs[result_index] = calculator.udiv(sources[0], sources[1]);
          break;
        }

        if (sources[1].is_constant() && sources[1].zero_extended() == 1) {
          outputs[result_index] = sources[0];
          break;
        }
        break;

      case Opcode::Modulus:
        if (all_constant) {
          outputs[result_index] = calculator.mod(sources[0], sources[1]);
          break;
        }
        break;

      case Opcode::SquareRoot:
        if (all_constant) {
          outputs[result_index] = calculator.sqrt(sources[0]);
          break;
        }
        break;

      case Opcode::Extend16:
        if (!all_constant) {
          outputs[result_index] = calculator.extend16(sources[0]);
          break;
        }
        break;

      case Opcode::Extend32:
        if (all_constant) {
          outputs[result_index] = calculator.extend32(sources[0]);
          break;
        }
        break;

      case Opcode::Extend64:
        if (all_constant) {
          outputs[result_index] = calculator.extend64(sources[0]);
          break;
        }
        break;

      default:
        break;
    }
  }

  /*
  for (size_t i = 0; i < outputs.size(); ++i) {
    printf("O[%lu] = %s\n", i, outputs[i].is_valid() ? "Removed" : "");
  }
  */

  /* Pass 2 - Produce simplfied execution unit. */
  for (auto it = instructions.begin(); it != instructions.end(); ++it) {
    const Instruction &current = *it;
    if (current.result_count() == 1) {
      const unsigned result_index = current.result(0).register_index();
      if (outputs[result_index].is_valid()) {
        /* Result was turned into a constant expression. */
        continue;
      }
    }

    Operand sources[current.source_count()];
    for (unsigned i = 0; i < current.source_count(); ++i) {
      if (current.source(i).is_constant()) {
        sources[i] = current.source(i);
      } else {
        const unsigned index = current.source(i).register_index();
        if (outputs[index].is_valid()) {
          sources[i] = outputs[index];
        } else {
          sources[i] = current.source(i);
        }
      }
    }

    if (current.result_count() == 0) {
      switch (current.source_count()) {
        case 1:
          result.append(current.opcode(), current.type(), {}, { sources[0] });
          break;

        case 2:
          result.append(current.opcode(), current.type(), {}, { sources[0], sources[1] });
          break;

        case 3:
          result.append(
            current.opcode(), current.type(), {}, { sources[0], sources[1], sources[2] });
          break;

        case 4:
          result.append(current.opcode(),
                        current.type(),
                        {},
                        { sources[0], sources[1], sources[2], sources[3] });
          break;

        default:
          assert(false && "Unhandled IR source count");
      }
    } else if (current.result_count() == 1) {
      switch (current.source_count()) {
        case 1:
          result.append(
            current.opcode(), current.type(), { current.result(0) }, { sources[0] });
          break;

        case 2:
          result.append(current.opcode(),
                        current.type(),
                        { current.result(0) },
                        { sources[0], sources[1] });
          break;

        case 3:
          result.append(current.opcode(),
                        current.type(),
                        { current.result(0) },
                        { sources[0], sources[1], sources[2] });
          break;

        case 4:
          result.append(current.opcode(),
                        current.type(),
                        { current.result(0) },
                        { sources[0], sources[1], sources[2], sources[3] });
          break;

        default:
          assert(false && "Unhandled IR source count");
      }
    } else {
      assert(false && "Unhandled IR result count");
    }
  }

  return result;
}

/*******************************************************************************
 * ir::Optimize::DeadCodeElimination
 ******************************************************************************/

DeadCodeElimination::DeadCodeElimination() {}
DeadCodeElimination::~DeadCodeElimination() {}

bool
DeadCodeElimination::instruction_has_side_effects(Opcode opcode) const
{
  switch (opcode) {
    case Opcode::Call:
    case Opcode::WriteGuest:
    case Opcode::Store:
    case Opcode::Exit:
      return true;
    default:
      return false;
  }
}

ExecutionUnit
DeadCodeElimination::execute(const ExecutionUnit &source)
{
  const u32 input_instruction_count = source.instructions().size();

  struct InstructionInfo {
    u32 index;
    ir::Instructions::const_iterator iterator;
    bool has_side_effects;
  };
  std::vector<InstructionInfo> instruction_info;
  instruction_info.reserve(input_instruction_count);

  // Maps SSA register -> instruction index
  std::vector<u32> source_to_instruction(source.register_count(), 0u);

  // Create 'random access' so we can walk the instruction data in reverse
  u32 index = 0;
  for (auto it = source.instructions().begin(); it != source.instructions().end();
       ++it, ++index) {
    InstructionInfo info { .index = index,
                           .iterator = it,
                           .has_side_effects =
                             instruction_has_side_effects(it->opcode()) };
    instruction_info.push_back(info);
  }

  // Track which registers go unused. We initially assume all registers (i.e. results) are
  // unused, and will determine in coming pass which ones we actually need to keep around.
  // This exploits the fact that the SSA instructions are already in a topological order,
  // so if we iterate them from the end to the beginning, we will visit instructions
  // before their sources have been computed.
  std::vector<bool> is_register_not_used(source.register_count(), true);

  for (i32 i = input_instruction_count - 1; i >= 0; --i) {
    const auto &it = instruction_info[i].iterator;

    // This instruction has side effects. It's sources have to be kept around.
    if (instruction_info[i].has_side_effects) {
      for (u32 j = 0; j < it->source_count(); ++j) {
        if (it->source(j).is_register()) {
          is_register_not_used[it->source(j).register_index()] = false;
        }
      }
    } else {
      // Instruction doesn't have side effects, however, if we know the result of this
      // instruction is a known dependency of something else later in execution graph,
      // then we need to mark sources to the instruction as being used.
      bool was_any_result_not_dead_code = false;
      for (u32 j = 0; j < it->result_count(); ++j) {
        if (!is_register_not_used[it->result(j).register_index()]) {
          was_any_result_not_dead_code = true;
          break;
        }
      }

      if (was_any_result_not_dead_code) {
        for (u32 j = 0; j < it->source_count(); ++j) {
          if (it->source(j).is_register()) {
            is_register_not_used[it->source(j).register_index()] = false;
          }
        }
      }
    }
  }

  // Now we walk forward through the graph. When we encounter an instruction isn't
  // eventually contributing to side-effects, we don't need to emit it in the
  // resulting graph. It's dead code.
  ExecutionUnit result;
  for (u32 i = 0; i < input_instruction_count; ++i) {
    const auto &info = instruction_info[i];

    // If this instruction itself has side effects, we definitely must emit it.
    if (info.has_side_effects) {
      result.add_instruction(*info.iterator);
      continue;
    }

    // Otherwise, need to check on whether results are used downstream. Check all the
    // results of this instruction to see if any of them are required/used.
    bool is_any_result_used = false;
    for (u32 j = 0; j < info.iterator->result_count(); ++j) {
      const u32 result_reg_index = info.iterator->result(j).register_index();
      if (!is_register_not_used[result_reg_index]) {
        is_any_result_used = true;
        break;
      }
    }

    if (is_any_result_used) {
      result.add_instruction(*info.iterator);
    }
  }

  // const u32 input_count = source.instructions().size();
  // const u32 result_count = result.instructions().size();
  // if(result_count < input_count) {
  //   printf("Dead code removal %u -> %u \n", input_count, result_count);
  // }

  return result;
}

}
}
}
