/* Private musl numerical adapter, not a public endian/POSIX header. */
#ifndef REIST_MATH_PRIVATE_ENDIAN_H
#define REIST_MATH_PRIVATE_ENDIAN_H
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __BYTE_ORDER __BYTE_ORDER__
#endif
