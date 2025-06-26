#!/usr/bin/env python3

from string import Template
import sys

opcode_template_0 = Template("""\
struct OP_$pattern {
  static constexpr unsigned cycles = $op_cycles;
  static constexpr u16 op_encoding = 0b$op_encoding;
  static constexpr u16 op_mask = 0b$op_mask;

  u16 opcode;\
""")

opcode_template_1 = Template("""
  static constexpr unsigned ${letter}_bits = $bits;
  u16 $letter() const { return (opcode & 0b$mask) >> $offset; }\
""")

opcode_template_2 = Template("""\
};

template <> void
SH4::execute_instruction<OP_$pattern>(const u16 opcode)
{
  typedef OP_$pattern OP;
  const OP data{opcode}; (void) data;
  if (0 /* illegal in delay slot */) { /* TODO Generate exception. */ }
  $execute
}

template <> std::string
Debugger::raw_disassemble<OP_$pattern>(const u16 opcode, const u32 PC)
{
  typedef OP_$pattern OP;
  const OP data{opcode}; (void) data;
  $disassemble
}
""")

opcode_template_3 = Template("""\
template <> bool
SH4Assembler::translate_instruction<OP_$pattern>(const u16 opcode, const u32 PC, const u32 flags)
{
  typedef OP_$pattern OP;
  const OP data{opcode}; (void) data;
  $ir
}
""")

def make_mask(source, charset):
    result = ''
    for char in source:
        if char in charset:
            result += '1'
        else:
            result += '0'
    return result

with open(sys.argv[1], 'r') as stream:
    lines = [ x.strip('\r\n') for x in stream.readlines() ]

top_level = []
current = None
in_level = 0
key = None

for line in lines:
    clean_line = line.lstrip(' ')
    level = (len(line) - len(clean_line)) / 2

    if len(line) == 0 or line[0] == '#':
      continue

    if level == 0:
        assert clean_line[-1] == ':'

        if current is not None:
            top_level.append(current)
            current = None

        current = { clean_line[:-1] : {} }
        in_level = 1
        continue

    if level == 1:
        assert in_level > 0
        assert ':' in clean_line

        key, value = clean_line.split(':', 1)
        value = value.strip()

        if len(value) == 0:
            current[key] = []
            in_level = 2
        else:
            current[key] = value

    if level >= 2:
        assert in_level == 2
        assert key is not None

        current[key].append(line[in_level*2:].rstrip())

if current is not None:
    top_level.append(current)
    current = None

decode_table = []
for i in range(65536):
    decode_table.append(0)

opcode_table = []

#
# Header
#
print('#include <cmath>')
print('')
print('#include "shared/bitmanip.h"')
print('#include "shared/print.h"')
print('#include "shared/types.h"')
print('#include "guest/sh4/sh4.h"')
print('#include "guest/sh4/sh4_ir.h"')
print('#include "guest/sh4/sh4_debug.h"')
print('#include "guest/sh4/sh4_opcode.h"')
print('')
print('#ifdef _WIN64')
print('#pragma warning(disable : 4100)')
print('#endif')
print('')
print('namespace cpu {')
print('')
print('using Type = ::fox::ir::Type;')
print('using Operand = ::fox::ir::Operand;')
print('')

#
# Generate opcode definitions and implementation logic
#
for entry in top_level:
    pattern = entry['FORMAT']
    op_encoding = make_mask(pattern, '1')
    op_mask = make_mask(pattern, '01')
    op_cycles = int(entry['CYCLES'])
    execute = entry['EXECUTE']

    if 'DISASSEMBLE' in entry:
        disassemble = entry['DISASSEMBLE']
    else:
        disassemble = [ 'return "' + pattern + '";' ]

    kwargs = {
        'pattern': pattern,
        'op_cycles': entry['CYCLES'],
        'op_encoding': op_encoding,
        'op_mask': op_mask,
        'execute': '\n'.join(execute),
        'disassemble': '\n'.join(disassemble),
        'ir': '' if not 'IR' in entry else '\n  '.join(entry['IR'])
    }

    print(opcode_template_0.substitute(**kwargs))
    for letter in [ 'm', 'n', 'i', 'd' ]:
        if letter not in pattern:
            continue
        mask = make_mask(pattern, letter)
        offset = 0 if not '1' in mask else 15 - mask.rindex('1')
        bits = mask.count('1')
        print(opcode_template_1.substitute(letter=letter, mask=mask,
                                            offset=offset, bits=bits))
    print(opcode_template_2.substitute(**kwargs))

    if 'IR' in entry:
        print(opcode_template_3.substitute(**kwargs))

    encoding_int = int(op_encoding, 2)
    mask_int = int(op_mask, 2)
    opcode_table.append(pattern)
    for i in range(65536):
        if (i & mask_int) == encoding_int:
            assert decode_table[i] == 0
            decode_table[i] = len(opcode_table)

print('')

assert len(opcode_table) <= 256
print('Total Opcodes:', len(opcode_table), file=sys.stderr)
print('    Implemented:', len([ True for x in top_level if 'Unimplemented' not in x['EXECUTE'][0] ]), file=sys.stderr)
print('    With Disasm:', len([ True for x in top_level if 'DISASSEMBLE' in x ]), file=sys.stderr)
print('    With IR:    ', len([ True for x in top_level if 'IR' in x ]), file=sys.stderr)

#
# Generate opcode table
#
print('const std::vector<cpu::Opcode> SH4::opcode_table = {')
print('  cpu::Opcode{ NULL, [](u16, u32){ return std::string("????"); }, NULL, NO_FLAGS }')
for entry in top_level:
    pattern = entry['FORMAT']
    flags = ' | '.join(entry['FLAGS'].split(' '))
    cycles = int(entry['CYCLES'])
    if 'IR' in entry:
        print('  ,cpu::Opcode{ &SH4::execute_instruction<OP_%s>, &Debugger::raw_disassemble<OP_%s>, &SH4Assembler::translate_instruction<OP_%s>, %s, %d }' % (pattern, pattern, pattern, flags, cycles))
    else:
        print('  ,cpu::Opcode{ &SH4::execute_instruction<OP_%s>, &Debugger::raw_disassemble<OP_%s>, nullptr, %s, %d }' % (pattern, pattern, flags, cycles))
print('};')
print('')

#
# Generate decode table
#
for i in range(len(top_level)):
    entry = top_level[i]

print('const u8 SH4::decode_table[65536] = {')
for i in range(65536):
    print(str(decode_table[i]) + ',', end=' ')
    if (i % 128) == 127:
        print('')

print('};')
print('')

#
# Footer
#
print('}')

#printtop_level
