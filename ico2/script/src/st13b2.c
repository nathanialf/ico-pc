#include "typedef.h"
#include "generator.h"

void actSt13b2Generator(GObj *volatile self)
{
    Generator_Mask(self);
    if (!ICO_JUN26) {
        Generator_ResetCount(self);
    }
}
