
#include "XST7735.h"

inline uint16_t swapColor565(uint16_t color) {
    return (color << 11) | (color & 0x07E0) | (color >> 11);
}

ST7735::ST7735(uint8_t cs, uint8_t dc, uint8_t rst, uint8_t type) {
    _cs = cs;
    _rs = dc;
    _rst = rst;
    _type = type;
    _width = ST7735_TFTWIDTH;
    _height = ST7735_TFTHEIGHT;
}

void ST7735::writeCmd(uint8_t cmd) {

#ifdef SPI_HAS_TRANSACTION
    SPI.beginTransaction(spisettings);
#endif

#ifdef __ARDUINO_ARC__
    digitalWrite(_rs, LOW);
#else
    *rsport &= ~rspinmask;
#endif

    *csport &= ~cspinmask;
    SPI.transfer(cmd);
    *csport |= cspinmask;

#ifdef SPI_HAS_TRANSACTION
    SPI.endTransaction();
#endif
}

void ST7735::writeData(uint8_t data) {

#ifdef SPI_HAS_TRANSACTION
    SPI.beginTransaction(spisettings);
#endif

#ifdef __ARDUINO_ARC__
    digitalWrite(_rs, HIGH);
#else
    *rsport |= rspinmask;
#endif

    *csport &= ~cspinmask;
    SPI.transfer(data);
    *csport |= cspinmask;

#ifdef SPI_HAS_TRANSACTION
    SPI.endTransaction();
#endif
}

void ST7735::setAddrWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    writeCmd(ST7735_CASET);
    writeData(0x00);
    writeData(x0 + _colstart);
    writeData(0x00);
    writeData(x1 + _colstart);

    writeCmd(ST7735_RASET);
    writeData(0x00);
    writeData(y0 + _rowstart);
    writeData(0x00);
    writeData(y1 + _rowstart);

    writeCmd(ST7735_RAMWR);
}

void ST7735::commandList(const uint8_t* addr) {

    uint8_t numCommands = pgm_read_byte(addr++); // Number of commands to follow
    uint8_t numArgs;
    uint16_t ms;

    while (numCommands--) { // For each command...
        writeCmd(pgm_read_byte(addr++)); // Read, issue command
        numArgs = pgm_read_byte(addr++); // Number of args to follow
        ms = numArgs & DELAY; // If hibit set, delay follows args
        numArgs &= ~DELAY; // Mask out delay bit
        while (numArgs--) { // For each argument...
            writeData(pgm_read_byte(addr++)); // Read, issue argument
        }

        if (ms) {
            ms = pgm_read_byte(addr++); // Read post-command delay time (ms)
            if (ms == 255)
                ms = 500; // If 255, delay for 500 ms
            delay(ms);
        }
    }
}

void ST7735::commonInit(const uint8_t* cmdList) {

    _colstart = _rowstart = 0; // May be overridden in init func

    pinMode(_rs, OUTPUT);
    pinMode(_cs, OUTPUT);
    csport = portOutputRegister(digitalPinToPort(_cs));
    cspinmask = digitalPinToBitMask(_cs);
    rsport = portOutputRegister(digitalPinToPort(_rs));
    rspinmask = digitalPinToBitMask(_rs);

    SPI.begin();

#ifdef SPI_HAS_TRANSACTION
    spisettings = SPISettings(4000000L, MSBFIRST, SPI_MODE0);
#else
#if defined(ARDUINO_ARCH_SAM)
    SPI.setClockDivider(24); // 4 MHz (half speed)
#else
    SPI.setClockDivider(SPI_CLOCK_DIV4); // 4 MHz (half speed)
#endif
    SPI.setBitOrder(MSBFIRST);
    SPI.setDataMode(SPI_MODE0);
#endif // SPI_HAS_TRANSACTION

    // toggle RST low to reset; CS low so it'll listen to us
    *csport &= ~cspinmask;
    if (_rst) {
        pinMode(_rst, OUTPUT);
        digitalWrite(_rst, HIGH);
        delay(500);
        digitalWrite(_rst, LOW);
        delay(500);
        digitalWrite(_rst, HIGH);
        delay(500);
    }

    if (cmdList)
        commandList(cmdList);
}

void ST7735::init() {
    switch (_type) {
        case 0: // G Type
            commonInit(Gcmd);
            break;
        case 1: // B Type
            commonInit(Bcmd);
            break;
        case 2: // RG type
            commonInit(Rcmd1);
            commandList(Rcmd2green);
            _colstart = 2;
            _rowstart = 1;
            commandList(Rcmd3);
            break;
        case 3: // RR Type
            commonInit(Rcmd1);
            commandList(Rcmd2red);
            commandList(Rcmd3);
            break;
        default:
            break;
    }
}

void ST7735::renderScanlinePart(int16_t scanline, int16_t xmin, int16_t xmax, uint16_t* lineBuffer) {

    if ((scanline >= _height) || (scanline < 0))
        return;
    if ((xmin < 0) || (xmax < 0) || (xmin >= _width) || (xmax >= _width) || (xmin > xmax) || xmin == xmax)
        return;

    if (_type == 2) // RG type
        for (uint16_t x = 0; x <= xmax - xmin; x++)
            lineBuffer[x] = swapColor565(lineBuffer[x]);

    setAddrWindow(xmin, scanline, xmax, scanline);

#ifdef SPI_HAS_TRANSACTION
    SPI.beginTransaction(spisettings);
#endif

#ifdef __ARDUINO_ARC__
    digitalWrite(_rs, HIGH);
#else
    *rsport |= rspinmask;
#endif

    *csport &= ~cspinmask;
    for (uint16_t x = 0; x <= xmax - xmin; x++) {
        uint8_t hi = lineBuffer[x] >> 8;
        uint8_t lo = lineBuffer[x];
        SPI.transfer(hi);
        SPI.transfer(lo);
    }
    *csport |= cspinmask;

#ifdef SPI_HAS_TRANSACTION
    SPI.endTransaction();
#endif
}
