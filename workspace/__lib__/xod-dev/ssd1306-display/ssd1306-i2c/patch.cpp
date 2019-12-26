#pragma XOD error_raise enable

struct State {
    uint8_t mem[sizeof(SSD1306)];
};

using Type = SSD1306*;

// clang-format off
{{ GENERATED_CODE }}
// clang-format on

void evaluate(Context ctx) {
    if (!isSettingUp())
        return;

    auto state = getState(ctx);

    auto i2c = getValue<input_I2C>(ctx);
    uint8_t addr = getValue<input_ADDR>(ctx);
    uint8_t width = (uint8_t)getValue<input_W>(ctx);
    uint8_t height = (uint8_t)getValue<input_H>(ctx);

    if (addr > 127) {
        raiseError(ctx);
        return;
    }

    Type dev = new (state->mem) SSD1306(i2c, addr, width, height);
    dev->begin();
    dev->clearScreen();

    emitValue<output_DEV>(ctx, dev);
}
