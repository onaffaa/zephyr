.. zephyr:code-sample:: hello_world
   :name: Hello World

   Print "Hello World" to the console.

Overview
********

A simple sample that can be used with any :ref:`supported board <boards>` and
prints "Hello World" to the console.

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :host-os: unix
   :board: qemu_riscv32
   :goals: run
   :compact:

To build for another board, change "qemu_riscv32" above to that board's name.

Sample Output
=============

.. code-block:: console

    Hello World! qemu_riscv32/qemu_virt_riscv32

Exit QEMU by pressing :kbd:`CTRL+A` :kbd:`x`.
