#include "io.h"

// General input/output handling
unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void insw(unsigned short port, void* addr, unsigned long count) {
    asm volatile ("rep insw" : "+D" (addr), "+c" (count) : "d" (port) : "memory");
}

void outsw(unsigned short port, void* buffer, unsigned long count) {
    const unsigned short* words = (const unsigned short*)buffer;
    for (unsigned long i = 0; i < count; i++) {
        asm volatile ("outsw" : : "d"(port), "S"(words++));
    }
}