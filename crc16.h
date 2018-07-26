#ifndef _INCLUDED_CRC16_H_
#define _INCLUDED_CRC16_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus  */

uint16_t crc16(const uint8_t *buf, int len);

#ifdef __cplusplus
}
#endif /* __cplusplus  */
#endif
