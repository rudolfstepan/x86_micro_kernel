#ifndef REIST_TEXT_PRIVATE_WCHAR_H
#define REIST_TEXT_PRIVATE_WCHAR_H
#include <stddef.h>
#define wctomb reist_text_wctomb
int reist_text_wctomb(char *,wchar_t);
#endif
