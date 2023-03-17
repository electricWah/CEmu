#include "spi.h"
#include "bus.h"
#include "cpu.h"
#include "emu.h"
#include "schedule.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

spi_state_t spi;

static bool spi_scan_line(uint16_t row) {
    if (unlikely(row > SPI_LAST_ROW)) {
        spi.mode |= SPI_MODE_IGNORE;
        return false;
    }
    spi.mode &= ~SPI_MODE_IGNORE;
    if (unlikely(spi.mode & SPI_MODE_PARTIAL) &&
        spi.partialStart > spi.partialEnd ?
        spi.partialStart > row && row > spi.partialEnd :
        spi.partialStart > row || row > spi.partialEnd) {
        spi.mode |= SPI_MODE_BLANK;
    } else {
        spi.mode &= ~SPI_MODE_BLANK;
    }
    spi.row = spi.dstRow = spi.srcRow = row;
    if (unlikely(spi.mode & SPI_MODE_SCROLL)) {
        uint16_t top = spi.topArea, bot = SPI_LAST_ROW - spi.bottomArea;
        if (row >= top && row <= bot) {
            spi.srcRow += spi.scrollStart - top;
            if (spi.srcRow > bot) {
                spi.srcRow -= SPI_NUM_ROWS - spi.topArea - spi.bottomArea;
            }
            spi.srcRow &= 0x1FF;
        }
    }
    if (unlikely(spi.mac & SPI_MAC_VRO)) {
        spi.dstRow = SPI_LAST_ROW - spi.dstRow;
        spi.srcRow = SPI_LAST_ROW - spi.srcRow;
    }
    if (unlikely(spi.mac & SPI_MAC_HRO)) {
        spi.col = SPI_LAST_COL;
        spi.colDir = -1;
    } else {
        spi.col = 0;
        spi.colDir = 1;
    }
    return true;
}

bool spi_hsync(void) {
    return spi_scan_line(spi.row + 1);
}

static void spi_reset_mregs(void) {
    if (unlikely(spi.mac & SPI_MAC_RCX)) {
        spi.rowReg = spi.rowStart;
        spi.colReg = spi.colStart;
    } else {
        spi.rowReg = spi.colStart;
        spi.colReg = spi.rowStart;
    }
}

bool spi_vsync(void) {
    if (likely(spi.ifCtl & SPI_IC_CTRL_DATA)) {
        spi_reset_mregs();
    }
    return spi_scan_line(0);
}

bool spi_refresh_pixel(void) {
    uint8_t *pixel, red, green, blue;
    if (unlikely(spi.mode & SPI_MODE_IGNORE)) {
        return false;
    }
    if (unlikely(spi.mode & (SPI_MODE_SLEEP | SPI_MODE_OFF | SPI_MODE_BLANK))) {
        red = green = blue = ~0;
    } else {
        if (unlikely(spi.srcRow > SPI_LAST_ROW)) {
            red = bus_rand();
            green = bus_rand();
            blue = bus_rand();
        } else {
            pixel = spi.frame[spi.srcRow][spi.col];
            red = pixel[SPI_RED];
            green = pixel[SPI_GREEN];
            blue = pixel[SPI_BLUE];
        }
        if (!likely(spi.mac & SPI_MAC_BGR)) { /* eor */
            uint8_t temp = red;
            red = blue;
            blue = temp;
        }
        if (unlikely(spi.mode & SPI_MODE_INVERT)) {
            red = ~red;
            green = ~green;
            blue = ~blue;
        }
        if (unlikely(spi.mode & SPI_MODE_IDLE)) {
            red = (int8_t)red >> 7;
            green = (int8_t)green >> 7;
            blue = (int8_t)blue >> 7;
        }
    }
    pixel = spi.display[spi.col][spi.dstRow];
    pixel[SPI_RED] = red;
    pixel[SPI_GREEN] = green;
    pixel[SPI_BLUE] = blue;
    pixel[SPI_ALPHA] = ~0;
    spi.col += spi.colDir;
    if (unlikely(spi.col > SPI_LAST_COL)) {
        spi.mode |= SPI_MODE_IGNORE;
        return false;
    }
    return true;
}

static void spi_update_pixel(uint8_t red, uint8_t green, uint8_t blue) {
    if (likely(spi.rowReg < 320 && spi.colReg < 240)) {
        uint8_t *pixel = spi.frame[spi.rowReg][spi.colReg];
        pixel[SPI_RED] = red;
        pixel[SPI_GREEN] = green;
        pixel[SPI_BLUE] = blue;
    }
    if (unlikely(spi.mac & SPI_MAC_RCX)) {
        if (unlikely(spi.colReg == spi.colEnd)) {
            if (unlikely(spi.rowReg == spi.rowEnd && spi.rowStart <= spi.rowEnd)) {
                spi.rowReg = spi.colReg = ~0;
            } else {
                spi.colReg = spi.colStart;
                spi.rowReg = (spi.rowReg + 1 - (spi.mac >> 6 & 2)) & 0x1FF;
            }
        } else if (spi.colReg < 0x100) {
            spi.colReg = (spi.colReg + 1 - (spi.mac >> 5 & 2)) & 0xFF;
        }
    } else {
        if (unlikely(spi.rowReg == spi.colEnd)) {
            if (unlikely(spi.colReg == spi.rowEnd && spi.rowStart <= spi.rowEnd)) {
                spi.rowReg = spi.colReg = ~0;
            } else {
                spi.rowReg = spi.colStart;
                spi.colReg = (spi.colReg + 1 - (spi.mac >> 5 & 2)) & 0xFF;
            }
        } else if (spi.rowReg < 0x200) {
            spi.rowReg = (spi.rowReg + 1 - (spi.mac >> 6 & 2)) & 0x1FF;
        }
    }
}

void spi_update_pixel_18bpp(uint8_t red, uint8_t green, uint8_t blue) {
    assert(red < 64 && green < 64 && blue < 64);
    spi_update_pixel(red << 2 | red >> 4, green << 2 | green >> 4, blue << 2 | blue >> 4);
}

void spi_update_pixel_16bpp(uint8_t red, uint8_t green, uint8_t blue) {
    assert(red < 32 && green < 64 && blue < 32);
    spi_update_pixel(spi.lut[red + 0], spi.lut[green + 32], spi.lut[blue + 96]);
}

void spi_update_pixel_12bpp(uint8_t red, uint8_t green, uint8_t blue) {
    assert(red < 16 && green < 16 && blue < 16);
    spi_update_pixel(spi.lut[(red << 1) + 0], spi.lut[(green << 2) + 32], spi.lut[(blue << 1) + 96]);
}

static void spi_sw_reset(void) {
    spi.cmd = 0;
    spi.fifo = 1;
    spi.param = 0;
    spi.gamma = 1;
    spi.mode = SPI_MODE_SLEEP | SPI_MODE_OFF;
    spi.colStart = 0;
    spi.colEnd = spi.mac & SPI_MAC_RCX ? SPI_LAST_COL : SPI_LAST_ROW;
    spi.rowStart = 0;
    spi.rowEnd = spi.mac & SPI_MAC_RCX ? SPI_LAST_ROW : SPI_LAST_COL;
    spi.topArea = 0;
    spi.scrollArea = SPI_NUM_ROWS;
    spi.bottomArea = 0;
    spi.partialStart = 0;
    spi.partialEnd = SPI_LAST_ROW;
    spi.scrollStart = 0;
    spi.tear = false;
}

static void spi_hw_reset(void) {
    spi.mac = 0;
    spi_sw_reset();
}

static void spi_write_cmd(uint8_t value) {
    spi.cmd = value;
    spi.param = 0;

    switch (spi.cmd) {
        case 0x00:
            break;
        case 0x01:
            spi_sw_reset();
            break;
        case 0x10:
            spi.mode |= SPI_MODE_SLEEP;
            break;
        case 0x11:
            spi.mode &= ~SPI_MODE_SLEEP;
            break;
        case 0x12:
            spi.mode |= SPI_MODE_PARTIAL;
            spi.scrollStart = 0;
            break;
        case 0x13:
            spi.mode &= ~(SPI_MODE_PARTIAL | SPI_MODE_SCROLL);
            spi.scrollStart = 0;
            break;
        case 0x20:
            spi.mode &= ~SPI_MODE_INVERT;
            break;
        case 0x21:
            spi.mode |= SPI_MODE_INVERT;
            break;
        case 0x28:
            spi.mode |= SPI_MODE_OFF;
            break;
        case 0x29:
            spi.mode &= ~SPI_MODE_OFF;
            break;
        case 0x2C:
            spi_reset_mregs();
            break;
        case 0x34:
            spi.tear = false;
            break;
        case 0x35:
            spi.tear = true;
            break;
        case 0x38:
            spi.mode &= ~SPI_MODE_IDLE;
            break;
        case 0x39:
            spi.mode |= SPI_MODE_IDLE;
            break;
        default:
            break;
    }
}

static void spi_write_param(uint8_t value) {
    uint8_t word_param = spi.param >> 1;
    uint8_t bit_offset = ~spi.param << 3 & 8;

    switch (spi.cmd) {
        case 0x26:
            if (spi.param == 0) {
                spi.gamma = value;
            }
            break;
        case 0x2A:
            switch (word_param) {
                case 0:
                    write8(spi.colStart, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                case 1:
                    write8(spi.colEnd, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                default:
                    break;
            }
            break;
        case 0x2B:
            switch (word_param) {
                case 0:
                    write8(spi.rowStart, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                case 1:
                    write8(spi.rowEnd, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                default:
                    break;
            }
            break;
        case 0x2C:
        case 0x3C:
            if (unlikely(!(spi.ifCtl & SPI_IC_CTRL_DATA))) {
                switch (spi.ifBpp & 7) {
                    default:
                    case 6: /* 18bpp */
                        switch (spi.param % 3) {
                            case 0:
                                spi.ifBlue = value >> 2;
                                break;
                            case 1:
                                spi.ifGreen = value >> 2;
                                break;
                            case 2:
                                spi.ifRed = value >> 2;
                                spi_update_pixel_18bpp(spi.ifRed, spi.ifGreen, spi.ifBlue);
                                break;
                        }
                        break;
                    case 5: /* 16bpp */
                        switch (spi.param % 2) {
                            case 0:
                                spi.ifBlue = value >> 3;
                                spi.ifGreen = value << 3 & 0x38;
                                break;
                            case 1:
                                spi.ifGreen |= value >> 5;
                                spi.ifRed = value & 0x1F;
                                spi_update_pixel_16bpp(spi.ifRed, spi.ifGreen, spi.ifBlue);
                                break;
                        }
                        break;
                    case 3: /* 12bpp */
                        switch (spi.param % 3) {
                            case 0:
                                spi.ifBlue = value >> 4;
                                spi.ifGreen = value & 0xF;
                                break;
                            case 1:
                                spi.ifRed = value >> 4;
                                spi_update_pixel_12bpp(spi.ifRed, spi.ifGreen, spi.ifBlue);
                                spi.ifBlue = value & 0xF;
                                break;
                            case 2:
                                spi.ifGreen = value >> 4;
                                spi.ifRed = value & 0xF;
                                spi_update_pixel_12bpp(spi.ifRed, spi.ifGreen, spi.ifBlue);
                                break;
                        }
                        break;
                }
            }
            break;
        case 0x2D:
            break;
        case 0x30:
            switch (word_param) {
                case 0:
                    write8(spi.partialStart, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                case 1:
                    write8(spi.partialEnd, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                default:
                    break;
            }
            break;
        case 0x33:
            switch (word_param) {
                case 0:
                    write8(spi.topArea, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                case 1:
                    write8(spi.scrollArea, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                case 2:
                    write8(spi.bottomArea, bit_offset, value & 0x1FF >> bit_offset);
                    break;
                default:
                    break;
            }
            break;
        case 0x36:
            if (spi.param == 0) {
                spi.mac = value;
            }
            break;
        case 0x37:
            switch (word_param) {
                case 0:
                    write8(spi.scrollStart, bit_offset, value & 0x1FF >> bit_offset);
                    spi.mode |= SPI_MODE_SCROLL;
                    break;
                default:
                    break;
            }
            break;
        case 0x3A:
            switch (word_param) {
                case 0:
                    spi.ifBpp = value;
                    break;
                default:
                    break;
            }
            break;
        case 0xB0:
            switch (word_param) {
                case 0:
                    spi.ifCtl = value;
                    break;
                case 1:
                    break;
                default:
                    break;
            }
            break;
        case 0xE0:
            spi.gammaCorrection[0][spi.param] = value;
            break;
        case 0xE1:
            spi.gammaCorrection[1][spi.param] = value;
            break;
        default:
            break;
    }

    spi.param++;
}

/* Read from the SPI range of ports */
static uint8_t spi_read(const uint16_t pio, bool peek) {
    uint8_t value = 0;
    (void)peek;
    if (pio < 0x18) {
        value = spi.regs[pio];
    } else if (pio == 0x18) {
        switch (spi.regs[0x12]) {
            case 0: // LCD?
                break;
            case 1: // ARM?
                if (!asic.revM) {
                    break;
                }
                if (spi.dataLength) {
                    value = *spi.data++;
                    spi.dataLength--;
                } else if (!memcmp(spi.cbw, "USBC", 4)) {
                    memcpy(spi.csw, spi.cbw, 8);
                    spi.csw[3] = 'S';
                    memset(spi.csw + 8, 0, 5);
                    spi.cswIndex = 0;
                    switch (spi.cbw[15]) {
                        case 0x12: { // INQUIRY
                            static const uint8_t inquiry[] = { 0xA5, 0x00, 0x80, 0x03, 0x02, 0x1F, 0x00, 0x00, 0x00, 0x54, 0x49, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x50, 0x79, 0x74, 0x68, 0x6F, 0x6E, 0x20, 0x41, 0x64, 0x61, 0x70, 0x74, 0x65, 0x72, 0x20, 0x20, 0x33, 0x2E, 0x30, 0x30, 0x00, 0xA5, 0x55, 0x53, 0x42, 0x53, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
                            spi.data = inquiry;
                            spi.dataLength = sizeof(inquiry);
                            break;
                        }
                    }
                }
                spi.cbw[spi.cbwIndex = 0] = 0;
                break;
        }
    }
    return value;
}

/* Write to the SPI range of ports */
static void spi_write(const uint16_t pio, const uint8_t byte, bool poke) {
    (void)poke;

    if (pio < 0x18) {
        spi.regs[pio] = byte;
    } else if (pio == 0x18) {
        switch (spi.regs[0x12]) {
            case 0: // LCD?
                spi.fifo = spi.fifo << 3 | (byte & 7);
                if (spi.fifo & 0x200) {
                    if (spi.fifo & 0x100) {
                        spi_write_param(spi.fifo);
                    } else {
                        spi_write_cmd(spi.fifo);
                    }
                    spi.fifo = 1;
                }
                break;
            case 1: // ARM?
                if (!asic.revM) {
                    break;
                }
                spi.cbw[spi.cbwIndex++ & 0x1F] = byte;
                break;
        }
    }
}

static const eZ80portrange_t pspi = {
    .read  = spi_read,
    .write = spi_write
};


void spi_reset(void) {
    uint8_t i = 0, c;
    memset(&spi, 0, sizeof(spi));
    spi.regs[0x0C] = 0xF2;
    spi_hw_reset();
    for (c = 0; c < 1 << 5; c++) {
        spi.lut[i++] = c << 3 | c >> 2;
    }
    for (c = 0; c < 1 << 6; c++) {
        spi.lut[i++] = c << 2 | c >> 4;
    }
    for (c = 0; c < 1 << 5; c++) {
        spi.lut[i++] = c << 3 | c >> 2;
    }
}

eZ80portrange_t init_spi(void) {
    spi_reset();
    gui_console_printf("[CEmu] Initialized Serial Peripheral Interface...\n");
    return pspi;
}

bool spi_save(FILE *image) {
    return fwrite(&spi, sizeof(spi), 1, image) == 1;
}

bool spi_restore(FILE *image) {
    return fread(&spi, sizeof(spi), 1, image) == 1;
}
