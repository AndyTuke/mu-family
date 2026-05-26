#include "MuTheme.h"

MuTheme& MuTheme::current() noexcept
{
    static MuTheme instance;
    return instance;
}
