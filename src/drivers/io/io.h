// io.h
#ifndef IO_H
#define IO_H

#include <stdarg.h>

unsigned char inb(unsigned short port);
void outb(unsigned short port, unsigned char val);
void insw(unsigned short port, const void* addr, unsigned long count);
void outsw(unsigned short port, const void* buffer, unsigned long count);


#endif // IO_H