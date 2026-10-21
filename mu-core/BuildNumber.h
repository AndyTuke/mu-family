#pragma once
#define BUILD_NUMBER 1432
// Live products use this base; pre-release products override MU_VERSION_BASE per target.
#ifndef MU_VERSION_BASE
#define MU_VERSION_BASE "1.1"
#endif
#define MU_STR_(x) #x
#define MU_STR(x) MU_STR_(x)
#define MU_VERSION_STRING MU_VERSION_BASE ".0." MU_STR(BUILD_NUMBER)
