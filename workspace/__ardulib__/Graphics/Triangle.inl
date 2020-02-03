
void fillBottomFlatTrianglePart(XVector2<int16_t> a, XVector2<int16_t> b, XVector2<int16_t> c, int16_t scanline, uint16_t* buffer, size_t bufferSize, uint16_t color) {
    
    float invslopeBA = (b.x - a.x) / (b.y - a.y);
    float invslopeCA = (c.x - a.x) / (c.y - a.y);

    float curxBA = a.x;
    float curxCA = a.x;

    for (int16_t line = a.y; line <= b.y; line++) {

        if (line == scanline) {

            int16_t curxMin = min((int16_t)curxBA, (int16_t)curxCA);
            int16_t curxMax = max((int16_t)curxBA, (int16_t)curxCA);

            for (int16_t x = curxMin; x <= curxMax; x++)
                if (x >= 0 && x < bufferSize)
                    buffer[x] = color;

        } else {
            curxBA += invslopeBA;
            curxCA += invslopeCA;
        }
    }
}

void fillTopFlatTrianglePart(XVector2<int16_t> a, XVector2<int16_t> b, XVector2<int16_t> c, int16_t scanline, uint16_t* buffer, size_t bufferSize, uint16_t color) {
    
    float invslopeCA = (c.x - a.x) / (c.y - a.y);
    float invslopeCB = (c.x - b.x) / (c.y - b.y);

    float curxCA = c.x;
    float curxCB = c.x;

    for (int16_t line = c.y; line > b.y; line--) {

        if (line == scanline) {

            int16_t curxMin = min((int16_t)curxCA, (int16_t)curxCB);
            int16_t curxMax = max((int16_t)curxCA, (int16_t)curxCB);

            for (int16_t x = curxMin; x <= curxMax; x++)
                if (x >= 0 && x < bufferSize)
                    buffer[x] = color;

        } else {
            curxCA -= invslopeCA;
            curxCB -= invslopeCB;
        }
    }
}

TriangleOutline::TriangleOutline(XGraphics* parent)
    : XGraphics(parent) {}

void TriangleOutline::setPosition(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    _a = XVector2<int16_t>(x0, y0);
    _b = XVector2<int16_t>(x1, y1);
    _c = XVector2<int16_t>(x2, y2);
}

void TriangleOutline::setStyle(XColor* strokeColor) {
    _strokeColor = strokeColor;
}

void TriangleOutline::renderScanline(XRenderer* renderer, int16_t scanline, uint16_t* buffer, size_t bufferSize) {

    int16_t lineMinY = min(_c.y, min(_a.y, _b.y));
    int16_t lineMaxY = max(_c.y, max(_a.y, _b.y));

    if (scanline > lineMaxY || scanline < lineMinY)
        return;

    uint16_t color = xcolorTo565(_strokeColor ? *_strokeColor : getFGColor());

    putLineOnScanline(_a, _b, scanline, buffer, bufferSize, color);
    putLineOnScanline(_b, _c, scanline, buffer, bufferSize, color);
    putLineOnScanline(_c, _a, scanline, buffer, bufferSize, color);
}

TriangleSolid::TriangleSolid(XGraphics* parent)
    : XGraphics(parent) {}

void TriangleSolid::setPosition(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {

    // Sort Y coordinates by order.
    if (y0 > y1) {
        swapInt16(y0, y1);
        swapInt16(x0, x1);
    }
    if (y1 > y2) {
        swapInt16(y2, y1);
        swapInt16(x2, x1);
    }
    if (y0 > y1) {
        swapInt16(y0, y1);
        swapInt16(x0, x1);
  }

    _a = XVector2<int16_t>(x0, y0);
    _b = XVector2<int16_t>(x1, y1);
    _c = XVector2<int16_t>(x2, y2);
}

void TriangleSolid::setStyle(XColor* fillColor) {
    _fillColor = fillColor;
}

void TriangleSolid::renderScanline(XRenderer* renderer, int16_t scanline, uint16_t* buffer, size_t bufferSize) {

    int16_t lineMinY = min(_c.y, min(_a.y, _b.y));
    int16_t lineMaxY = max(_c.y, max(_a.y, _b.y));

    if (scanline > lineMaxY || scanline < lineMinY)
        return;

    uint16_t color = xcolorTo565(_fillColor ? *_fillColor : getFGColor());

    if (_b.y == _c.y) {
        // Trivial case for the triangle with flat bottom base.
        fillBottomFlatTrianglePart(_a, _b, _c, scanline, buffer, bufferSize, color);
    } else if (_a.y == _b.y) {
        // Trivial case for the triangle with flat top base.
        fillTopFlatTrianglePart(_a, _b, _c, scanline, buffer, bufferSize, color);
    } else {
        // Regular triangle case. Split the triangle in two parts.
        XVector2 d = XVector2<int16_t>((int16_t)(_a.x + ((float)(_b.y - _a.y) / (float)(_c.y - _a.y)) * (_c.x - _a.x)), _b.y);
        fillBottomFlatTrianglePart(_a, _b, d, scanline, buffer, bufferSize, color);
        fillTopFlatTrianglePart(_b, d, _c, scanline, buffer, bufferSize, color);
    }

}
