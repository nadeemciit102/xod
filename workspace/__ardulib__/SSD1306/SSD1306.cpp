
#include "SSD1306.h"

SSD1306::SSD1306(TwoWire* wire, uint8_t i2cAddress, uint8_t width, uint8_t height) {
    _wire = wire ? wire : &Wire;
    _i2cAddress = i2cAddress;
    _width = width;
    _height = height;
    _buffer = new uint8_t[_width * _height / 8];
}

SSD1306::~SSD1306() {
    if (_buffer) {
        free(_buffer);
        _buffer = NULL;
    }
}

void SSD1306::_sendCommand(uint8_t command) {
    _wire->beginTransmission(_i2cAddress);
    _wire->write(0x80);
    _wire->write(command);
    _wire->endTransmission();
}

void SSD1306::begin() {
    _wire->begin();
    _sendCommand(SSD1306_DISPLAY_OFF);
    _sendCommand(SSD1306_SET_DISPLAY_CLOCK);
    _sendCommand(0x80);
    _sendCommand(SSD1306_SET_MULTIPLEX_RATIO);
    _sendCommand(0x3F);
    _sendCommand(SSD1306_SET_DISPLAY_OFFSET);
    _sendCommand(0x00);
    _sendCommand(SSD1306_SET_START_LINE | 0);
    _sendCommand(SSD1306_CHARGE_DCDC_PUMP);
    _sendCommand(0x14);
    _sendCommand(SSD1306_ADDR_MODE);
    _sendCommand(0x00);
    _sendCommand(SSD1306_SET_REMAP_L_TO_R);
    _sendCommand(SSD1306_SET_REMAP_T_TO_D);
    _sendCommand(SSD1306_SET_COM_PINS);
    _sendCommand(0x12);
    _sendCommand(SSD1306_SET_CONTRAST);
    _sendCommand(0xFF);
    _sendCommand(SSD1306_SET_PRECHARGE_PERIOD);
    _sendCommand(0xF1);
    _sendCommand(SSD1306_SET_VCOM_DESELECT);
    _sendCommand(0x40);
    _sendCommand(SSD1306_RAM_ON);
    _sendCommand(SSD1306_INVERT_OFF);
    _sendCommand(SSD1306_DISPLAY_ON);
}

void SSD1306::sendBuffer() {
    _sendCommand(SSD1306_ADDR_PAGE);
    _sendCommand(0);
    _sendCommand(_height / 8 - 1);
    _sendCommand(SSD1306_ADDR_COLUMN);
    _sendCommand(0);
    _sendCommand(_width - 1);
    for (uint16_t i = 0; i < _width * _height / 8; i++) {
        _wire->beginTransmission(_i2cAddress);
        _wire->write(0x40);

        for (uint8_t x = 0; x < 16; x++)
            _wire->write(_buffer[i++]);

        i--;
        _wire->endTransmission();
    }
}

void SSD1306::clearScreen() {
    memset(_buffer, 0, _width * _height / 8);
}

void SSD1306::renderScanlinePart(int16_t scanline, int16_t xmin, int16_t xmax, uint16_t* lineBuffer) {
    if ((scanline >= _height) || (scanline < 0))
        return;
    if ((xmin < 0) || (xmax < 0) || (xmin >= _width) || (xmax >= _width) || (xmin > xmax) || xmin == xmax)
        return;

    for (int16_t x = 0; x < _width; x++) {

        bool _color = lineBuffer[x] < 0x7FFF ? 0 : 1;

        uint8_t p = scanline / 8;
        uint16_t numByte = (p * 128) + x;
        uint8_t numBit = scanline % 8;

        switch (_color) {
        case 1:
            _buffer[numByte] |= 1 << numBit;
            break;
        case 0:
            _buffer[numByte] &= ~(1 << numBit);
            break;
        default:
            break;
        }
    }
}
