#ifndef _ERROR_HEADER
#define _ERROR_HEADER

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

inline void panic(const char *msg)
{
  perror(msg);
  exit(1);
}

#endif
