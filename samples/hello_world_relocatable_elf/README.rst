.. zephyr:code-sample:: hello_world_relocatable_elf
   :name: Hello World (Relocatable ELF)

   Print "Hello World" from a self-relocating PIE image and verify its
   runtime address matches (or differs from) its link-time address.

Overview
********

A variant of the :zephyr:code-sample:`hello_world` sample built as a
self-relocating position-independent executable. It patches the address
``main()`` was linked at into the built ELF, then at runtime prints both
that linked address and the address ``main()`` is actually running at, so
the two can be compared to confirm self-relocation worked.

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world_relocatable_elf
   :host-os: unix
   :board: qemu_riscv32
   :goals: run
   :compact:

To build for another board, change "qemu_riscv32" above to that board's name.

Sample Output
=============

.. code-block:: console

    Hello World! qemu_riscv32/qemu_virt_riscv32
    main() linked at:  0x...
    main() running at: 0x...

Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.

Testing Self-Relocation with a Shifted LMA
===========================================

QEMU's ELF loader places each segment at its LMA (the ``PhysAddr`` column in
``readelf -l`` output), not its VMA, so shifting a build's LMA is a simple way
to make QEMU load and run the image at an address other than the one it was
linked for, without changing any code. This exercises the self-relocation
path and lets the sample's two printed addresses ("main() linked at" and
"main() running at") actually differ.

Using ``llvm-objcopy`` from a CPULLVM toolchain install
(``<cpullvm-toolchain>/bin/llvm-objcopy``), shift the LMA of every section in
the built ELF by an offset, for example ``0x1000``:

.. code-block:: console

   llvm-objcopy --change-section-lma='*+0x1000' <build-dir>/zephyr/zephyr.elf

This edits the ELF in place, so keep a copy of the unmodified file if you
need to re-run with a different offset, and rebuild before repeating this
step. Then run the modified image as usual, e.g. ``west build -t run``, and
compare the two addresses the sample prints; they should differ by the
offset used above.

