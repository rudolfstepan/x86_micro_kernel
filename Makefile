# the makefile for the kernel is not yet complete
# please use the build.sh script instead for building the kernel
# use the run.sh script for running the kernel on qemu

# NASM=nasm
# GCC=gcc
# LD=ld
# CFLAGS=-m32 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -fno-builtin -fno-stack-protector -O2 -Wall -Wextra -g
# LDFLAGS=-m elf_i386 -T linker.ld -nostdlib
# SRC_DIR=src
# INCLUDE_DIR=include
# BUILD_DIR=build

# # Define source files
# SOURCES = source1.c source2.c

# # Convert .c files in SOURCES to .o files in build directory
# OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)

# # Target executable
# TARGET = $(BUILD_DIR)/my_executable

# # Default target
# all: $(TARGET)

# $(TARGET): $(OBJECTS)
#     gcc $(OBJECTS) -o $(TARGET)

# $(BUILD_DIR)/%.o: %.c
#     gcc -c $< -o $@