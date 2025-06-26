
import subprocess
from pathlib import Path
import sys

assert len(sys.argv) > 1, "Must supply input C file"
input_file = Path(sys.argv[1])

# arm-none-eabi-gcc function.c -c -o function.o -fomit-frame-pointer -O2

subprocess.check_call(f'arm-none-eabi-gcc {str(input_file)} -march=armv7-a -c -o function.o -fomit-frame-pointer -O0', shell=True)
p = subprocess.check_output(f'arm-none-eabi-objdump -S function.o', shell=True).decode("utf-8")

print(p)

words = []

# Split lines, get the sections that have the 
lines = p.split('\n')
for line in lines:
  # Lines containing binary look like
  # 0:\te2800001 \tadd\tr0, r0, #
  if '\t' in line:
    word = line.split('\t')[1].strip()
    words.append(word)

N = len(words) 
print(f'const std::array<u32, {N}> program_words = ' + '{')
for word in words:
  print(f"  0x{word}u,")
print('};')
