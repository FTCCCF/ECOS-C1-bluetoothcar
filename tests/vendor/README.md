PicoRV32 source: retroSoC/retroSoC commit 303705c550e4c6cfdbf07c418f871b039fbbfbb4,
rtl/mini/core/picorv32.v, copied from the workspace's existing C1 reference.

The copyright and permission notice remains intact in picorv32.v. The test
instantiation matches C1's core wrapper: barrel shifter, MUL, FAST_MUL, DIV,
compressed ISA, no IRQ. The standalone bus model does not establish physical
SRAM/flash latency, board clock accuracy, or microphone edge timing.
