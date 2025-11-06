#ifndef RTC_H
#define RTC_H


extern void read_date(int* year, int* month, int* day);
extern void read_time(int* hours, int* minutes, int* seconds);

extern void write_date(int year, int month, int day);
extern void write_time(int hours, int minutes, int seconds);

#endif // RTC_H