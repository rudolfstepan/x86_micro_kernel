#ifndef REIST_CPP_PROFILE_H
#define REIST_CPP_PROFILE_H

/* Profile 1: opt-in freestanding C++20; public OS interfaces remain C ABI.
 * No allocator initialization, constructors or exit registration at startup.
 * Call reist_libc_init_process with an explicit budget before dynamic new.
 * Ordinary allocation failure terminates this process, without unwinding.
 * Use std::nothrow for a recoverable allocation failure. */
#define REIST_CPP_PROFILE_VERSION 1
#define REIST_CPP_ALLOCATION_FAILURE 71
#define REIST_CPP_INVALID_VIRTUAL_CALL 72

#endif
