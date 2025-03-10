#include <stdlib.h>
#include <stdint.h>

//  --> http://www.libpng.org/pub/png/spec/1.2/PNG-CRCAppendix.html
//  --> http://www.libpng.org/pub/png/spec/1.2/PNG-Filters.html

unsigned long crc_table[256];
int crc_table_computed = 0;

void make_crc_table(void);
unsigned long update_crc(unsigned long, unsigned char *, int);
unsigned long crc(unsigned char *, int);
uint8_t paeth_predictor(uint8_t, uint8_t, uint8_t);

void make_crc_table(void){
    unsigned long c;
    int n, k;

    for (n = 0; n < 256; n++) {
        c = (unsigned long) n;
        for (k = 0; k < 8; k++) {
        if (c & 1)
            c = 0xedb88320L ^ (c >> 1);
        else
            c = c >> 1;
        }
        crc_table[n] = c;
    }
}

unsigned long update_crc(unsigned long crc, unsigned char *buf, int len){
    unsigned long c = crc;
    int n;

    if (!crc_table_computed)
        make_crc_table();
    for (n = 0; n < len; n++) {
        c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
unsigned long crc(unsigned char *buf, int len){
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

// a = left, b = up, c = left_up
uint8_t paeth_predictor(uint8_t left, uint8_t up, uint8_t left_up){
    int32_t p = left + up - left_up;
    int32_t pa = abs(p - left);
    int32_t pb = abs(p - up);
    int32_t pc = abs(p - left_up);

    if((pa <= pb) && (pa <= pc))
        return left;
    else if (pb <= pc)
        return up;
    else
        return left_up;
}
