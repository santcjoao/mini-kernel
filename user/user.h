#pragma once
#include "common.h"

int syscall(int sysno, int arg0, int arg1, int arg2);
void putchar(char ch);
int getchar(void);
__attribute__((noreturn)) void exit(void);
int readfile(const char *filename, char *buf, int len);
int writefile(const char *filename, const char *buf, int len);
