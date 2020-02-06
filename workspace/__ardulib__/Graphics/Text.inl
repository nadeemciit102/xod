
Text::Text(XGraphics* parent)
    : XGraphics(parent) {}

void Text::setCursor(int16_t x, int16_t y) {
    _cursor = XVector2<int16_t>(x, y);
}

void Text::setText(char* text) {
    _text = text;
}

void Text::setStyle(XColor* strokeColor) {
    _strokeColor = strokeColor;
}

void Text::renderScanline(XRenderer* renderer, int16_t scanline, uint16_t* buffer, size_t bufferSize) {

    // if (scanline != _a.y)
    //     return;

    // uint16_t color = xcolorTo565(_strokeColor ? *_strokeColor : getFGColor());

    // if (_a.x >= 0 && _a.x < bufferSize)
    //     buffer[_a.x] = color;
}
