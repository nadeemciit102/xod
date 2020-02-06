
// clang-format off
{{#global}}
#include <Text.h>
{{/global}}
// clang-format on

struct State {
    uint8_t mem[sizeof(Text)];
    Text* text;

    int16_t x, y;

    char* str;
    size_t cap;
};

// clang-format off
{{ GENERATED_CODE }}
// clang-format on

void evaluate(Context ctx) {

    auto state = getState(ctx);
    auto gfx = getValue<input_GFX>(ctx);

    if (isSettingUp()) {
        state->text = new (state->mem) Text(gfx);

        state->cap = getValue<input_CAP>(ctx);
        state->str = new char[state->cap + 1];

        state->text->setText(state->str);
    }

    auto str = getValue<input_STR>(ctx);
    memset(state->str, '\0', state->cap + 1);
    dump(str, state->str);

    int16_t x = (int16_t)getValue<input_X>(ctx);
    int16_t y = (int16_t)getValue<input_Y>(ctx);

    if (isSettingUp() || state->x != x || state->y != y || isInputDirty<input_STR>(ctx)) {
        state->x = x;
        state->y = y;
        state->text->setCursor(x, y);
        emitValue<output_GFXU0027>(ctx, state->text);
    }

    if (isInputDirty<input_GFX>(ctx)) {
        emitValue<output_GFXU0027>(ctx, state->text);
    }
}
