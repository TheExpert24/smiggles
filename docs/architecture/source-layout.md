# Source Layout

Smiggles keeps source files grouped by responsibility:

- `core/`: kernel entry and shared core code
- `arch/`: GDT, TSS, interrupts, and context switching
- `mm/`: physical and virtual memory
- `proc/`: processes, scheduler, signals, and ELF loading
- `fs/`: VFS, inode, block, path, and file descriptor code
- `ui/`: text and framebuffer display, shell applications
- `drivers/`: disk, PCI, network, audio, and input drivers
- `net/`: network protocols and sockets
- `sys/`: syscall dispatch
- `include/`: subsystem public headers

The current build still keeps implementation files in `myos/` for compatibility.
New subsystem headers in `include/` should be added incrementally before source files are moved.

Rules for new code:

- Include the narrow subsystem header, not all of `kernel.h`, when possible.
- Keep public cross-subsystem types in `include/process.h` or `include/syscall.h`.
- Put only implementation-local declarations in `.c` files.
- Group Makefile sources by responsibility using `CORE_SOURCES`, `ARCH_SOURCES`, `MM_SOURCES`, `PROCESS_SOURCES`, `FS_SOURCES`, `UI_SOURCES`, `DRIVER_SOURCES`, `NET_SOURCES`, and `UTIL_SOURCES`.
