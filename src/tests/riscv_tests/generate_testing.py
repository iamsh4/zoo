#!/usr/bin/env python3

import subprocess
import sys
import os

def find_files_with_suffix(directory, suffix):
  for root, dirs, files in os.walk(directory):
    for file in files:
      if file.endswith(suffix):
        yield os.path.join(root, file)

def filename_without_extension(file_path):
  # Get the file name with extension
  file_name_with_extension = os.path.basename(file_path)
  # Remove the extension
  return os.path.splitext(file_name_with_extension)[0]

if __name__ == '__main__':
  if len(sys.argv) < 2:
    print(f"usage: {sys.argv[0]} <directory of riscv-tests repo>")
    sys.exit(1)

  script_directory = os.path.dirname(os.path.abspath(__file__))
  output_dir = f"{script_directory}/test_binaries"
  os.makedirs(output_dir, exist_ok=True)

  test_repo = os.path.realpath(sys.argv[1])
  test_isa_dir = test_repo + '/isa/rv32ui/'

  assert os.path.isdir(script_directory), "???"
  assert os.path.isdir(test_isa_dir), f"test repo dir '{test_isa_dir}' not found"

  # Find .S files from the repo, compile each to binary in our output folder 
  for file_path in find_files_with_suffix(test_isa_dir, '.S'):
    name = filename_without_extension(file_path)

    cmd = f"riscv64-linux-gnu-gcc -march=rv32g -mabi=ilp32 -static -mcmodel=medany -fvisibility=hidden -nostdlib -nostartfiles -I{test_repo}/isa/macros/scalar/ -I{script_directory} -T{script_directory}/link.ld {file_path} -o {output_dir}/{name}.elf"
    subprocess.check_call(cmd, shell=True)

    subprocess.check_call(f"riscv64-linux-gnu-objcopy -O binary --only-section=.text {output_dir}/{name}.elf {output_dir}/{name}.bin", shell=True)
    os.unlink(f"{output_dir}/{name}.elf")
  
  # Now we have all the assembly files. 
  # Generate an include file which can be included in test_rv32.cpp

  with open(f"{script_directory}/gtest_generated.h", "w") as f:
    files = list(find_files_with_suffix(output_dir, '.bin'))
    files = sorted(files)
    for file_path in files:
      name = filename_without_extension(file_path)
      test_name = f"RV32_OFFICIAL_TEST_{name.upper()}"
      file_path_relative = os.path.relpath(file_path, f"{script_directory}/../../..")

      f.write(f"""TEST_F(RV32, {test_name})
{{
  run_test_bin("{file_path_relative}", 4096);
}}\n\n""")
