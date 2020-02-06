
#ifndef TEXT_H
#define TEXT_H

#include "XGraphics.h"

/*
 * Represents a text given by cursor point (x, y) .
 * The `setStyle` method sets up text color otherwise
 * the default foreground color of the scene is used.
 */
class Text : public XGraphics {
private:
    XVector2<int16_t> _cursor;
    char* _text;

    XColor* _strokeColor = nullptr;

public:
    Text(XGraphics* parent);

    void setCursor(int16_t x, int16_t y);
    void setText(char* text);
    void setStyle(XColor* strokeColor);

    void renderScanline(XRenderer* renderer, int16_t scanline, uint16_t* buffer, size_t bufferSize);
};

#include "Text.inl"

#endif // TEXT_H
