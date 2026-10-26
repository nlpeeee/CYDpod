#pragma once
#include <cstdint>
inline uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t* p,uint32_t n){crc=~crc;while(n--){crc^=*p++;for(int i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}return ~crc;}
