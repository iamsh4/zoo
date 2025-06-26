
In order to run tests from `https://github.com/riscv-software-src/riscv-tests`

```shell
TEST_REPO="riscv-tests/isa"
riscv64-linux-gnu-gcc -I$TEST_REPO/macros/scalar/ -I. -c -S $TEST_REPO/rv64ui/addi.S | grep -v "/home/" > addi.S
riscv64-linux-gnu-as addi.S -o addi.elf
riscv64-linux-gnu-objcopy -O binary addi.elf addi.bin
```

To disassemble the binary
```shell
riscv64-linux-gnu-objdump -d addi.elf
```
