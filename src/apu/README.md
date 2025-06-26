# Discussion from emu-dev
Why is the ARM7DI core so slow?

- The audio block clock is 33.8688MHz, which is PLL multiplied by 2/3 to get 22.5792MHz.
- The ARM core is given access to 16b SDRAM every 4th clock cycle.
- The ARM core needs 2 clock cycles to read an instruction from RAM.
- There is no instruction cache, so the maximum instruction rate is 2.8224MHz.
- There are 44100 samples per second, so the ARM core can execute 64 instructions per sample
  in the best case when there is no additional memory access. 
- Note that I/O to AICA registers does not have this penalty.
