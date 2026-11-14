#pragma once
#ifdef _WIN32
#include <ctime>
// Winsock's timeval uses a 32-bit long on Windows. The ESP time owner uses
// time_t; keep a 64-bit epoch in this isolated host translation unit.
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval { time_t tv_sec; long tv_usec; };
#endif
#else
#include_next <sys/time.h>
#endif
