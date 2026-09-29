#ifndef FONT5X7_INCLUDED
#define FONT5X7_INCLUDED

#include <stdint.h>

#define FONT5X7_W 5U
#define FONT5X7_H 7U

const uint8_t *FONT5X7_Glyph(uint32_t codepoint);

#endif
