#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/* CRC-32/ISO-HDLC (zlib, Ethernet): poly 0x04C11DB7 reflected, init/xorout 0xFFFFFFFF.
 * Chainable like zlib: crc = CRC32_Update(0, a, n); crc = CRC32_Update(crc, b, m); */
uint32_t CRC32_Update(uint32_t crc, const void *data, size_t len);

#endif
