# compiler

A tiny, from-scratch x86-64 assembler that reads a minimal custom assembly-like syntax and emits a **standalone, runnable ELF64 executable** directly — no external assembler, linker, or toolchain involved. Everything (parsing, "opcode" translation, and ELF header/segment construction) happens in a single C++ file.

> Written for Linux/x86-64. This is a learning project / toy compiler, not a production assembler — see [Limitations](#limitations) below.

## How it works

1. **Read source** — the input file is slurped into a string (`GetFileContents`).
2. **Tokenize & translate** (`InterpretAsm`) — the source is split into tokens (handling quoted strings and bracketed expressions) and translated word-by-word into raw bytes:
   - Recognized mnemonics/registers (`mov`, `rax`, `rdi`, `rsi`, `syscall`, `ret`, `nop`) map to fixed opcode bytes.
   - Numeric tokens are encoded as 8-byte little-endian immediates.
   - Quoted string literals (`"like this"`) are emitted as raw bytes with a trailing null terminator.
   - Source is split into `.code` and `.data` sections (`.data` is currently parsed but not yet emitted — see Limitations).
3. **Build the ELF** — a minimal `Elf64_Ehdr` (ELF header) and single `Elf64_Phdr` (`PT_LOAD` program header) are constructed by hand (`MakeElfHeader` / `MakePHeader`), pointing the entry point right after the headers.
4. **Link & write** — the header, program header, and generated code are concatenated and written straight to disk as the final executable (`my_program`). Because everything is resolved and laid out in one pass with no relocations or external symbols, this single step effectively acts as both the "compiler" and the "linker."

## Supported syntax

```asm
.code
mov rax 60
mov rdi 0
syscall
```

- `.code` — marks the start of the executable instruction section.
- `.data` — marks a data section (parsed, not yet written to the output — see Limitations).
- Registers: `rax`, `rdi`, `rsi`
- Instructions: `mov`, `syscall`, `ret`, `nop`
- Literals: decimal integers (encoded as 8-byte little-endian) and double-quoted strings (null-terminated)

## Requirements

- Linux (uses `<linux/elf.h>` and `<linux/elf-em.h>` from the kernel headers)
- A C++17 compiler (e.g. `g++`)

## Building

```bash
g++ -std=c++17 -o compiler compiler.cpp
```

## Usage

```bash
./compiler path/to/source.asm
```

This produces an executable named `my_program` in the current directory. Make it executable and run it:

```bash
chmod +x my_program
./my_program
```

### Example: minimal exit syscall

```asm
.code
mov rax 60
mov rdi 0
syscall
```

Compiling and running this produces a static ELF64 binary that calls `sys_exit(0)`.

## Limitations

This is a very early / intentionally minimal implementation:

- No real instruction encoder — mnemonics map to single fixed bytes rather than a proper x86-64 encoding scheme, so only the exact instructions listed above are supported.
- `.data` section content is parsed but currently discarded (not written into the output binary).
- No labels, jumps, branching, function calls, or arithmetic instructions.
- No relocations, symbol table, or section headers — the output is the smallest possible loadable ELF (headers + one `PT_LOAD` segment), not a fully spec-compliant object/executable.
- No error recovery — malformed input generally throws and aborts.
- Single translation unit, single input file — no multi-file linking in the traditional sense.

## Sources / references

- [Linux x86-64 syscall table](https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/)
- [Linux kernel `elf.h`](https://github.com/torvalds/linux/blob/master/include/uapi/linux/elf.h)
