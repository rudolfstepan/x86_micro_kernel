#ifndef REIST_C_ERRNO_H
#define REIST_C_ERRNO_H
#include <reist/libc.h>
#define ENOMEM 12
#define EBUSY 16
#define EINVAL 22
#define errno (*reist_libc_errno())
#endif
