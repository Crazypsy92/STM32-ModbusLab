#include "modbus.h"
#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}
uint16_t Mb_Crc16(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xffffu;
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xa001u : 0u));
    }
    return crc;
}
void Mb_Init(MbDevice *d)
{
    memset(d, 0, sizeof(*d));
    d->holding[0] = 1u;
    d->holding[1] = 10u;
    d->holding[2] = 500u;
    d->holding[4] = 100u;
}
static int value_ok(uint16_t address, uint16_t value)
{
    switch (address) {
    case 0: return value <= 2u;
    case 1: case 4: return value >= 1u && value <= 1000u;
    case 2: return value <= 1000u;
    case 3: return value <= 255u;
    default: return 0;
    }
}
static int in_range(uint16_t a, uint16_t n, uint16_t limit)
{
    return a < limit && n <= (uint16_t)(limit - a);
}
static void coil_set(MbDevice *d, uint16_t a, unsigned value)
{
    const uint8_t bit = (uint8_t)(1u << a);
    if (value) d->coils |= bit;
    else d->coils &= (uint8_t)~bit;
}
static void hr_set(MbDevice *d, uint16_t a, uint16_t v)
{
    d->holding[a] = v;
    if (a == 3u) d->coils = (uint8_t)v;
}
static size_t finish(MbDevice *d, uint8_t *tx, size_t n)
{
    uint16_t crc = Mb_Crc16(tx, n);
    tx[n++] = (uint8_t)crc;
    tx[n++] = (uint8_t)(crc >> 8);
    ++d->responses;
    return n;
}
size_t Mb_Process(MbDevice *d, uint8_t unit, const uint8_t *rx, size_t len,
                  uint8_t tx[MB_RTU_MAX_ADU])
{
    uint8_t fc, ex = 0u;
    uint16_t a = 0u, n = 0u;
    size_t out = 0u;
    int write, broadcast;
    ++d->requests;
    if (len < 4u || len > MB_RTU_MAX_ADU ||
        Mb_Crc16(rx, len - 2u) != ((uint16_t)rx[len-2u] | ((uint16_t)rx[len-1u] << 8))) {
        ++d->errors;
        return 0u;
    }
    if (rx[0] != unit && rx[0] != 0u) return 0u;
    fc = rx[1];
    broadcast = rx[0] == 0u;
    write = fc == 5u || fc == 6u || fc == 15u || fc == 16u;
    if (broadcast && !write) return 0u;
    tx[0] = unit; tx[1] = fc;
    d->holding[3] = d->coils;
    d->input[3] = (uint16_t)d->requests;
    d->input[4] = (uint16_t)d->responses;
    d->input[5] = (uint16_t)d->errors;
    d->input[6] = d->discrete;
    d->input[7] = d->coils;

    if (fc >= 1u && fc <= 6u) {
        if (len != 8u) { ex = 3u; goto done; }
        a = be16(rx + 2); n = be16(rx + 4);
        if (fc <= 4u) {
            const uint16_t limit = fc == 1u ? MB_COIL_COUNT :
                fc == 2u ? MB_DI_COUNT : MB_REG_COUNT;
            if (n == 0u || n > (fc <= 2u ? 2000u : 125u)) { ex = 3u; goto done; }
            if (!in_range(a, n, limit)) { ex = 2u; goto done; }
            if (fc <= 2u) {
                const uint8_t bits = fc == 1u ? d->coils : d->discrete;
                tx[2] = 1u; tx[3] = (uint8_t)((bits >> a) & ((1u << n) - 1u));
                out = 4u;
            } else {
                const uint16_t *regs = fc == 3u ? d->holding : d->input;
                tx[2] = (uint8_t)(2u * n);
                for (uint16_t j = 0; j < n; ++j) put16(tx + 3u + 2u*j, regs[a+j]);
                out = 3u + 2u*n;
            }
        } else {
            if (fc == 5u) {
                if (a >= MB_COIL_COUNT) { ex = 2u; goto done; }
                if (n != 0u && n != 0xff00u) { ex = 3u; goto done; }
                coil_set(d, a, n != 0u);
            } else {
                if (a >= 5u) { ex = 2u; goto done; }
                if (!value_ok(a, n)) { ex = 3u; goto done; }
                hr_set(d, a, n);
            }
            memcpy(tx + 2, rx + 2, 4u); out = 6u;
        }
    } else if (fc == 15u || fc == 16u) {
        unsigned bytes;
        if (len < 9u) { ex = 3u; goto done; }
        a = be16(rx+2); n = be16(rx+4);
        if (n == 0u || n > (fc == 15u ? 1968u : 123u)) { ex = 3u; goto done; }
        bytes = fc == 15u ? (n+7u)/8u : 2u*n;
        if (rx[6] != bytes || len != 9u + bytes) { ex = 3u; goto done; }
        if (!in_range(a, n, fc == 15u ? MB_COIL_COUNT : 5u)) { ex = 2u; goto done; }
        if (fc == 16u) {
            for (uint16_t j = 0; j < n; ++j)
                if (!value_ok((uint16_t)(a+j), be16(rx+7u+2u*j))) { ex = 3u; goto done; }
            for (uint16_t j = 0; j < n; ++j) hr_set(d, (uint16_t)(a+j), be16(rx+7u+2u*j));
        } else {
            for (uint16_t j = 0; j < n; ++j) coil_set(d, (uint16_t)(a+j), (rx[7u+j/8u] >> (j%8u)) & 1u);
        }
        memcpy(tx+2, rx+2, 4u); out = 6u;
    } else {
        ex = 1u;
    }
done:
    if (ex) {
        ++d->errors;
        tx[1] = (uint8_t)(fc | 0x80u); tx[2] = ex; out = 3u;
    } else if (write) {
        d->holding[3] = d->coils;
        ++d->holding[5];
        d->input[7] = d->coils;
    }
    return broadcast ? 0u : finish(d, tx, out);
}
