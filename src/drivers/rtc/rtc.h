#ifndef RTC_H
#define RTC_H


void getDate(int* year, int* month, int* day);
void getTime(int* hours, int* minutes, int* seconds);

void setDate(int year, int month, int day);
void setTime(int hours, int minutes, int seconds);

#endif // RTC_H