// io.h
#ifndef IO_H
#define IO_H

#include <stdarg.h>
#include <stdint.h>


// Byte (8-Bit) lesen/schreiben
unsigned char inb(unsigned short port);
void outb(unsigned short port, unsigned char val);

// Wort (16-Bit) lesen/schreiben
unsigned short inw(unsigned short port);
void outw(unsigned short port, unsigned short val);

// Doppelwort (32-Bit) lesen/schreiben
unsigned int inl(unsigned short port);
void outl(unsigned short port, unsigned int val);

// Blockweises Lesen/Schreiben von Worten (16-Bit)
void insw(unsigned short port, void* addr, unsigned long count);
void outsw(unsigned short port, const void* buffer, unsigned long count);

// Blockweises Lesen/Schreiben von Bytes (8-Bit)
void insb(unsigned short port, void* addr, unsigned long count);
void outsb(unsigned short port, const void* buffer, unsigned long count);
#endif // IO_H