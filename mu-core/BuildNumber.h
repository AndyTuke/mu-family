#pragma once
#define BUILD_NUMBER 1489
// Live products return MU_VERSION_STRING from getVersionString(), pre-release products MU_VERSION_STRING_PRE.
// The compiled mu-core library reads neither: it is one library for every product.
#define MU_VERSION_BASE "1.2"
#define MU_VERSION_BASE_PRE "1.2"
#define MU_STR_(x) #x
#define MU_STR(x) MU_STR_(x)
#define MU_VERSION_STRING MU_VERSION_BASE ".0." MU_STR(BUILD_NUMBER)
#define MU_VERSION_STRING_PRE MU_VERSION_BASE_PRE ".0." MU_STR(BUILD_NUMBER)
