Bare Metal x86 Kernel

Overview

This project is a minimalistic bare-metal kernel for x86 systems, developed in C. It is designed to boot using GRUB and provides a basic Linux-like shell for user interaction. The kernel allows for directory listing, program loading, running, and more. It serves as an educational resource for understanding low-level system programming on x86 architecture.

Features

Boot with GRUB: Utilizes the GRUB bootloader for initializing the system in a protected mode.
Basic Shell Interface: Includes a simple shell interface for user commands, resembling a Linux environment.
File System Operations: Supports basic file system operations like directory listing (ls), file viewing, etc.
Program Execution: Capable of loading and executing programs from the filesystem.
Memory Management: Implements basic memory management for program execution.
Interrupt Handling: Basic handling of hardware and software interrupts.

Custom program header for executables

.PRG is a file extension which identifies a program as executable, similar to DOS .com or .exe
An executable has a prg start header with meta information such an entrypoint and more.
During linking the header is autom. calculated and linked to the binary executable.
When loading the kernel looks into the header and use the information for execution.
At the moment this is simple and rudimential.

Development environment
- linux or WSL2 in Windows
- vscode (Visual Studio Code)
- GCC (GNU Compiler Collection)
- NASM (Netwide Assembler)
- QEMU (Quick EMUlator) for running the kernel in a virtualized environment

dont forget to habe grub2 installed on your machine

Custom toolchain

to support development of programs for this kernel a minimalistic c toolchain has been created.
some custom c files eg stdio.h, stdlib.h are available which are wrapper for the underlaying 
methods to access the systems hardware like io, vga...
For example when writing your own c program, and include the stdio.h from the toolchain,
the printf command and others will work like in a standard c environment.

Exemple programs are cli_dir.c which can be used as a kind of hello world.

Usage

Once the kernel is booted, you will be greeted with a simple shell. You can perform various operations like listing directories, viewing files, and running programs. Example commands include:

Supported filesystem

fat32

ls - Lists files in the current directory
cd <path> - Change directory
mkdir <dirname> - Create directoy
rmdir <dirname> - Delete directoy

mkfile <filename> - Create file
rmfile <filename> - Delete file

open <filename> - Displays the content of a file
run <program> - Executes a program
load <program> - Load a program in the memory
sys <address> - Execute a program in memory at the specified address

dump <start_address> <end_address> - Hex dump of a given range in memory
