#include <cstdint>

#include "../include/crc32.hpp"

// 静态全局变量
static uint32_t crc_table[256];
static bool crc_init = false;

// CRC表初始化函数
static void init_table(){
    for(uint32_t i=0;i<256;i++){
        uint32_t c = i;
        for(int j=0;j<8;j++){
            if(c & 1) c = 0xEDB88320U ^ (c >> 1);
            else c = c >> 1;
        }
        crc_table[i] = c;
    }
    crc_init = true;
}

// CRC32计算函数
uint32_t crc32_calc(const void* data, size_t length){
    if(!crc_init) init_table();
    const uint8_t* buf = (const uint8_t*)data;
    uint32_t c = 0xFFFFFFFFU;
    for(size_t i=0;i<length;i++){
        c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}
