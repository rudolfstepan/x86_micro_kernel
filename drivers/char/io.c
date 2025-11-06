#include "io.h"

// General input/output handling

// Lese ein Byte (8-Bit) vom Port
unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Schreibe ein Byte (8-Bit) an den Port
void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Lese ein Wort (16-Bit) vom Port
unsigned short inw(unsigned short port) {
    unsigned short ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Schreibe ein Wort (16-Bit) an den Port
void outw(unsigned short port, unsigned short val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

// Lese einen Doppelwort (32-Bit) vom Port
unsigned int inl(unsigned short port) {
    unsigned int ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Schreibe einen Doppelwort (32-Bit) an den Port
void outl(unsigned short port, unsigned int val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

// Lese ein Array von Worten (16-Bit) vom Port
void insw(unsigned short port, void* addr, unsigned long count) {
    asm volatile ("rep insw"
                  : "+D"(addr), "+c"(count)
                  : "d"(port)
                  : "memory");
}

// Schreibe ein Array von Worten (16-Bit) an den Port
void outsw(unsigned short port, const void* buffer, unsigned long count) {
    asm volatile ("rep outsw"
                  : "+S"(buffer), "+c"(count)
                  : "d"(port)
                  : "memory");
}

// Lese ein Array von Bytes (8-Bit) vom Port
void insb(unsigned short port, void* addr, unsigned long count) {
    asm volatile ("rep insb"
                  : "+D"(addr), "+c"(count)
                  : "d"(port)
                  : "memory");
}

// Schreibe ein Array von Bytes (8-Bit) an den Port
void outsb(unsigned short port, const void* buffer, unsigned long count) {
    asm volatile ("rep outsb"
                  : "+S"(buffer), "+c"(count)
                  : "d"(port)
                  : "memory");
}
