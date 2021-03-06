#pragma XOD evaluate_on_pin disable
#pragma XOD evaluate_on_pin enable input_UPD

struct State {
};

{{ GENERATED_CODE }}

void evaluate(Context ctx) {
    if (!isInputDirty<input_UPD>(ctx))
        return;

    const uint8_t port = getValue<input_PORT>(ctx);
    ::pinMode(port, INPUT_PULLUP);
    emitValue<output_SIG>(ctx, ::digitalRead(port));
    emitValue<output_DONE>(ctx, 1);
}

template<uint8_t port>
void evaluateTmpl(Context ctx) {
    static_assert(isValidDigitalPort(port), "must be a valid digital port");

    evaluate(ctx);
}
