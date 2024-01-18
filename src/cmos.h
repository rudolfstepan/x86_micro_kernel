#ifndef CMOS_H
#define CMOS_H

#define CMOS_ADDRESS    0x70
#define CMOS_DATA       0x71

int readFromCMOS(int reg);
void getDate(int* year, int* month, int* day);
void getTime(int* hours, int* minutes, int* seconds);

#endif // CMOS_H