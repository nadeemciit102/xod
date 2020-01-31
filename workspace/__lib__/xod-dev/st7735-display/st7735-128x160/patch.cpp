
#pragma XOD error_raise enable

struct State {
    uint8_t mem[sizeof(ST7735)];
};

using Type = ST7735*;

// clang-format off
{{ GENERATED_CODE }}
// clang-format on

void evaluate(Context ctx) {
    if (!isSettingUp())
        return;

    auto state = getState(ctx);

    uint8_t cs = getValue<input_CS>(ctx);
    uint8_t dc = getValue<input_DC>(ctx);
    uint8_t rst = getValue<input_RST>(ctx);

    if (!isValidDigitalPort(cs) || !isValidDigitalPort(dc) || !isValidDigitalPort(rst)) {
        raiseError(ctx);
        return;
    }

    uint8_t type = getValue<input_TYPE>(ctx);

    Type dev = new (state->mem) ST7735(cs, dc, rst, type);
    dev->init();

    emitValue<output_DEV>(ctx, dev);
}
