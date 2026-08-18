/**
 * @file arch/x86/paging.h
 * @brief Kompatibilitätsweiterleitung der Paging-API.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: Der Forwarder bewahrt den kanonischen Pagingvertrag.
 */
#ifndef ARCH_X86_PAGING_FORWARD_H
#define ARCH_X86_PAGING_FORWARD_H

#include "mm/paging.h"

#endif
