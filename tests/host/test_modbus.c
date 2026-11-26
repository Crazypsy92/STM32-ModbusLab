#include "modbus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t frame(uint8_t *p, size_t n)
{
    uint16_t crc = Mb_Crc16(p, n);
    p[n++] = (uint8_t)crc;
    p[n++] = (uint8_t)(crc >> 8);
    return n;
}
static void request(MbDevice *d, uint8_t *q, size_t n, uint8_t fc, uint8_t ex)
{
    uint8_t a[256];
    size_t len = Mb_Process(d, 1, q, frame(q,n), a);
    assert(len >= 5 && Mb_Crc16(a,len)==0);
    assert(a[0]==1 && a[1]==(uint8_t)(fc | (ex ? 0x80 : 0)));
    if(ex) assert(len==5 && a[2]==ex);
}
int main(void)
{
    MbDevice d;
    uint8_t q[256]={1,3,0,0,0,2,0xc4,0x0b}, a[256];
    Mb_Init(&d);
    assert(Mb_Crc16((const uint8_t *)"123456789",9)==0x4b37);
    assert(Mb_Crc16(q,8)==0);
    assert(Mb_Process(&d,1,q,8,a)==9);
    const uint8_t expected[]={1,3,4,0,1,0,10,0x2b,0xf4};
    assert(memcmp(a,expected,sizeof(expected))==0);
    uint8_t coils[256]={1,5,0,0,0xff,0};
    request(&d,coils,6,5,0); assert(d.coils==1);
    uint8_t rc[256]={1,1,0,0,0,8};
    request(&d,rc,6,1,0);
    uint8_t di[256]={1,2,0,0,0,4};
    d.discrete=5; request(&d,di,6,2,0);
    uint8_t ir[256]={1,4,0,6,0,2};
    request(&d,ir,6,4,0);
    uint8_t hr[256]={1,6,0,1,0,20};
    request(&d,hr,6,6,0); assert(d.holding[1]==20);
    uint8_t mc[256]={1,15,0,0,0,8,1,5};
    request(&d,mc,8,15,0); assert(d.coils==5);
    uint8_t mr[256]={1,16,0,0,0,2,4,0,2,0,25};
    request(&d,mr,11,16,0); assert(d.holding[0]==2 && d.holding[1]==25);
    uint8_t invalid[256]={1,16,0,0,0,2,4,0,1,0,0};
    request(&d,invalid,11,16,3); assert(d.holding[0]==2 && d.holding[1]==25);
    uint8_t addr[256]={1,3,0,8,0,1}; request(&d,addr,6,3,2);
    uint8_t unknown[256]={1,0x41}; request(&d,unknown,2,0x41,1);
    q[7]^=1; assert(Mb_Process(&d,1,q,8,a)==0);
    uint8_t broadcast[256]={0,5,0,0,0,0};
    assert(Mb_Process(&d,1,broadcast,frame(broadcast,6),a)==0 && d.coils==4);
    uint8_t other[256]={2,5,0,0,0xff,0};
    assert(Mb_Process(&d,1,other,frame(other,6),a)==0 && d.coils==4);
    puts("Modbus core vectors PASS");
    return 0;
}
