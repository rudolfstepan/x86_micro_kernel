// io.h
#ifndef IO_H
#define IO_H

#include <stdarg.h>
#include <stdint.h>


// Byte (8-Bit) lesen/schreiben
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %w1, %b0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %b0, %w1" : : "a"(val), "Nd"(port));
}

// Wort (16-Bit) lesen/schreiben
static inline unsigned short inw(unsigned short port) {
    unsigned short ret;
    asm volatile ("inw %w1, %w0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(unsigned short port, unsigned short val) {
    asm volatile ("outw %w0, %w1" : : "a"(val), "Nd"(port));
}

// Doppelwort (32-Bit) lesen/schreiben
static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    asm volatile ("inl %w1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(unsigned short port, unsigned int val) {
    asm volatile ("outl %0, %w1" : : "a"(val), "Nd"(port));
}

// Blockweises Lesen/Schreiben von Worten (16-Bit)
static inline void insw(unsigned short port, void* addr, unsigned long count) {
    asm volatile ("rep insw"
                  : "+D"(addr), "+c"(count)
                  : "d"(port)
                  : "memory");
}

static inline void outsw(unsigned short port, const void* buffer, unsigned long count) {
    asm volatile ("rep outsw"
                  : "+S"(buffer), "+c"(count)
                  : "d"(port)
                  : "memory");
}

// Blockweises Lesen/Schreiben von Bytes (8-Bit)
static inline void insb(unsigned short port, void* addr, unsigned long count) {
    asm volatile ("rep insb"
                  : "+D"(addr), "+c"(count)
                  : "d"(port)
                  : "memory");
}

static inline void outsb(unsigned short port, const void* buffer, unsigned long count) {
    asm volatile ("rep outsb"
                  : "+S"(buffer), "+c"(count)
                  : "d"(port)
                  : "memory");
}

#endif // IO_H