// The sketch is auto-generated with XOD (https://xod.io).
//
// You can compile and upload it to an Arduino-compatible board with
// Arduino IDE.
//
// Rough code overview:
//
// - Configuration section
// - STL shim
// - Immutable list classes and functions
// - XOD runtime environment
// - Native node implementation
// - Program graph definition
//
// Search for comments fenced with '====' and '----' to navigate through
// the major code blocks.

#include <Arduino.h>
#include <inttypes.h>


/*=============================================================================
 *
 *
 * Configuration
 *
 *
 =============================================================================*/

// Uncomment to turn on debug of the program
//#define XOD_DEBUG

// Uncomment to trace the program runtime in the Serial Monitor
//#define XOD_DEBUG_ENABLE_TRACE


// Uncomment to make possible simulation of the program
//#define XOD_SIMULATION

#ifdef XOD_SIMULATION
#include <WasmSerial.h>
#define XOD_DEBUG_SERIAL WasmSerial
#else
#define XOD_DEBUG_SERIAL DEBUG_SERIAL
#endif

/*=============================================================================
 *
 *
 * STL shim. Provides implementation for vital std::* constructs
 *
 *
 =============================================================================*/

namespace xod {
namespace std {

template< class T > struct remove_reference      {typedef T type;};
template< class T > struct remove_reference<T&>  {typedef T type;};
template< class T > struct remove_reference<T&&> {typedef T type;};

template <class T>
typename remove_reference<T>::type&& move(T&& a) {
    return static_cast<typename remove_reference<T>::type&&>(a);
}

} // namespace std
} // namespace xod

/*=============================================================================
 *
 *
 * Basic XOD types
 *
 *
 =============================================================================*/
namespace xod {
#if __SIZEOF_FLOAT__ == 4
typedef float Number;
#else
typedef double Number;
#endif
typedef bool Logic;
typedef unsigned long TimeMs;
typedef uint8_t ErrorFlags;

struct Pulse {
  Pulse() {}
  Pulse(bool) {}
  Pulse(int) {}
};

struct XColor {
  uint8_t r, g, b;
};

} // namespace xod

/*=============================================================================
 *
 *
 * XOD-specific list/array implementations
 *
 *
 =============================================================================*/

#ifndef XOD_LIST_H
#define XOD_LIST_H

namespace xod {
namespace detail {

/*
 * Cursors are used internaly by iterators and list views. They are not exposed
 * directly to a list consumer.
 *
 * The base `Cursor` is an interface which provides the bare minimum of methods
 * to facilitate a single iteration pass.
 */
template<typename T> class Cursor {
  public:
    virtual ~Cursor() { }
    virtual bool isValid() const = 0;
    virtual bool value(T* out) const = 0;
    virtual void next() = 0;
};

template<typename T> class NilCursor : public Cursor<T> {
  public:
    virtual bool isValid() const { return false; }
    virtual bool value(T*) const { return false; }
    virtual void next() { }
};

} // namespace detail

/*
 * Iterator is an object used to iterate a list once.
 *
 * Users create new iterators by calling `someList.iterate()`.
 * Iterators are created on stack and are supposed to have a
 * short live, e.g. for a duration of `for` loop or node’s
 * `evaluate` function. Iterators can’t be copied.
 *
 * Implemented as a pimpl pattern wrapper over the cursor.
 * Once created for a cursor, an iterator owns that cursor
 * and will delete the cursor object once destroyed itself.
 */
template<typename T>
class Iterator {
  public:
    static Iterator<T> nil() {
        return Iterator<T>(new detail::NilCursor<T>());
    }

    Iterator(detail::Cursor<T>* cursor)
        : _cursor(cursor)
    { }

    ~Iterator() {
        if (_cursor)
            delete _cursor;
    }

    Iterator(const Iterator& that) = delete;
    Iterator& operator=(const Iterator& that) = delete;

    Iterator(Iterator&& it)
        : _cursor(it._cursor)
    {
        it._cursor = nullptr;
    }

    Iterator& operator=(Iterator&& it) {
        auto tmp = it._cursor;
        it._cursor = _cursor;
        _cursor = tmp;
        return *this;
    }

    operator bool() const { return _cursor->isValid(); }

    bool value(T* out) const {
        return _cursor->value(out);
    }

    T operator*() const {
        T out;
        _cursor->value(&out);
        return out;
    }

    Iterator& operator++() {
        _cursor->next();
        return *this;
    }

  private:
    detail::Cursor<T>* _cursor;
};

/*
 * An interface for a list view. A particular list view provides a new
 * kind of iteration over existing data. This way we can use list slices,
 * list concatenations, list rotations, etc without introducing new data
 * buffers. We just change the way already existing data is iterated.
 *
 * ListView is not exposed to a list user directly, it is used internally
 * by the List class. However, deriving a new ListView is necessary if you
 * make a new list/string processing node.
 */
template<typename T> class ListView {
  public:
    virtual Iterator<T> iterate() const = 0;
};

/*
 * The list as it seen by data consumers. Have a single method `iterate`
 * to create a new iterator.
 *
 * Implemented as pimpl pattern wrapper over a list view. Takes pointer
 * to a list view in constructor and expects the view will be alive for
 * the whole life time of the list.
 */
template<typename T> class List {
  public:
    constexpr List()
        : _view(nullptr)
    { }

    List(const ListView<T>* view)
        : _view(view)
    { }

    Iterator<T> iterate() const {
        return _view ? _view->iterate() : Iterator<T>::nil();
    }

    // pre 0.15.0 backward compatibility
    List* operator->() __attribute__ ((deprecated)) { return this; }
    const List* operator->() const __attribute__ ((deprecated)) { return this; }

  private:
    const ListView<T>* _view;
};

/*
 * A list view over an old good plain C array.
 *
 * Expects the array will be alive for the whole life time of the
 * view.
 */
template<typename T> class PlainListView : public ListView<T> {
  public:
    class Cursor : public detail::Cursor<T> {
      public:
        Cursor(const PlainListView* owner)
            : _owner(owner)
            , _idx(0)
        { }

        bool isValid() const override {
            return _idx < _owner->_len;
        }

        bool value(T* out) const override {
            if (!isValid())
                return false;
            *out = _owner->_data[_idx];
            return true;
        }

        void next() override { ++_idx; }

      private:
        const PlainListView* _owner;
        size_t _idx;
    };

  public:
    PlainListView(const T* data, size_t len)
        : _data(data)
        , _len(len)
    { }

    virtual Iterator<T> iterate() const override {
        return Iterator<T>(new Cursor(this));
    }

  private:
    friend class Cursor;
    const T* _data;
    size_t _len;
};

/*
 * A list view over a null-terminated C-String.
 *
 * Expects the char buffer will be alive for the whole life time of the view.
 * You can use string literals as a buffer, since they are persistent for
 * the program execution time.
 */
class CStringView : public ListView<char> {
  public:
    class Cursor : public detail::Cursor<char> {
      public:
        Cursor(const char* str)
            : _ptr(str)
        { }

        bool isValid() const override {
            return (bool)*_ptr;
        }

        bool value(char* out) const override {
            *out = *_ptr;
            return (bool)*_ptr;
        }

        void next() override { ++_ptr; }

      private:
        const char* _ptr;
    };

  public:
    CStringView(const char* str = nullptr)
        : _str(str)
    { }

    CStringView& operator=(const CStringView& rhs) {
        _str = rhs._str;
        return *this;
    }

    virtual Iterator<char> iterate() const override {
        return _str ? Iterator<char>(new Cursor(_str)) : Iterator<char>::nil();
    }

  private:
    friend class Cursor;
    const char* _str;
};

/*
 * A list view over two other lists (Left and Right) which first iterates the
 * left one, and when exhausted, iterates the right one.
 *
 * Expects both Left and Right to be alive for the whole view life time.
 */
template<typename T> class ConcatListView : public ListView<T> {
  public:
    class Cursor : public detail::Cursor<T> {
      public:
        Cursor(Iterator<T>&& left, Iterator<T>&& right)
            : _left(std::move(left))
            , _right(std::move(right))
        { }

        bool isValid() const override {
            return _left || _right;
        }

        bool value(T* out) const override {
            return _left.value(out) || _right.value(out);
        }

        void next() override {
            _left ? ++_left : ++_right;
        }

      private:
        Iterator<T> _left;
        Iterator<T> _right;
    };

  public:
    ConcatListView() { }

    ConcatListView(List<T> left, List<T> right)
        : _left(left)
        , _right(right)
    { }

    ConcatListView& operator=(const ConcatListView& rhs) {
        _left = rhs._left;
        _right = rhs._right;
        return *this;
    }

    virtual Iterator<T> iterate() const override {
        return Iterator<T>(new Cursor(_left.iterate(), _right.iterate()));
    }

  private:
    friend class Cursor;
    List<T> _left;
    List<T> _right;
};

//----------------------------------------------------------------------------
// Text string helpers
//----------------------------------------------------------------------------

using XString = List<char>;

/*
 * List and list view in a single pack. An utility used to define constant
 * string literals in XOD.
 */
class XStringCString : public XString {
  public:
    XStringCString(const char* str)
        : XString(&_view)
        , _view(str)
    { }

  private:
    CStringView _view;
};

} // namespace xod

#endif

/*=============================================================================
 *
 *
 * Functions to work with memory
 *
 *
 =============================================================================*/

// Define the placement new operator for cores that do not provide their own.
// Note, this definition takes precedence over the existing one (if any). We found no C++ way
// to use the existing implementation _and_ this implementation if not yet defined.
template<typename T>
void* operator new(size_t, T* ptr) noexcept {
    return ptr;
}

/*=============================================================================
 *
 *
 * UART Classes, that wraps Serials
 *
 *
 =============================================================================*/

class HardwareSerial;
class SoftwareSerial;

namespace xod {

class Uart {
  private:
    long _baud;

  protected:
    bool _started = false;

  public:
    Uart(long baud) {
        _baud = baud;
    }

    virtual void begin() = 0;

    virtual void end() = 0;

    virtual void flush() = 0;

    virtual bool available() = 0;

    virtual bool writeByte(uint8_t) = 0;

    virtual bool readByte(uint8_t*) = 0;

    virtual SoftwareSerial* toSoftwareSerial() {
      return nullptr;
    }

    virtual HardwareSerial* toHardwareSerial() {
      return nullptr;
    }

    void changeBaudRate(long baud) {
      _baud = baud;
      if (_started) {
        end();
        begin();
      }
    }

    long getBaudRate() const {
      return _baud;
    }

    Stream* toStream() {
      Stream* stream = (Stream*) toHardwareSerial();
      if (stream) return stream;
      return (Stream*) toSoftwareSerial();
    }
};

class HardwareUart : public Uart {
  private:
    HardwareSerial* _serial;

  public:
    HardwareUart(HardwareSerial& hserial, uint32_t baud = 115200) : Uart(baud) {
      _serial = &hserial;
    }

    void begin();
    void end();
    void flush();

    bool available() {
      return (bool) _serial->available();
    }

    bool writeByte(uint8_t byte) {
      return (bool) _serial->write(byte);
    }

    bool readByte(uint8_t* out) {
      int data = _serial->read();
      if (data == -1) return false;
      *out = data;
      return true;
    }

    HardwareSerial* toHardwareSerial() {
      return _serial;
    }
};

void HardwareUart::begin() {
  _started = true;
  _serial->begin(getBaudRate());
};
void HardwareUart::end() {
  _started = false;
  _serial->end();
};
void HardwareUart::flush() {
  _serial->flush();
};

} // namespace xod

/*=============================================================================
 *
 *
 * Basic algorithms for XOD lists
 *
 *
 =============================================================================*/

#ifndef XOD_LIST_FUNCS_H
#define XOD_LIST_FUNCS_H



namespace xod {

/*
 * Folds a list from left. Also known as "reduce".
 */
template<typename T, typename TR>
TR foldl(List<T> xs, TR (*func)(TR, T), TR acc) {
    for (auto it = xs.iterate(); it; ++it)
        acc = func(acc, *it);
    return acc;
}

template<typename T> size_t lengthReducer(size_t len, T) {
    return len + 1;
}

/*
 * Computes length of a list.
 */
template<typename T> size_t length(List<T> xs) {
    return foldl(xs, lengthReducer<T>, (size_t)0);
}

template<typename T> T* dumpReducer(T* buff, T x) {
    *buff = x;
    return buff + 1;
}

/*
 * Copies a list content into a memory buffer.
 *
 * It is expected that `outBuff` has enough size to fit all the data.
 */
template<typename T> size_t dump(List<T> xs, T* outBuff) {
    T* buffEnd = foldl(xs, dumpReducer, outBuff);
    return buffEnd - outBuff;
}

/*
 * Compares two lists.
 */
template<typename T> bool equal(List<T> lhs, List<T> rhs) {
    auto lhsIt = lhs.iterate();
    auto rhsIt = rhs.iterate();

    for (; lhsIt && rhsIt; ++lhsIt, ++rhsIt) {
        if (*lhsIt != *rhsIt) return false;
    }

    return !lhsIt && !rhsIt;
}

template<typename T> bool operator == (List<T> lhs, List<T> rhs) {
  return equal(lhs, rhs);
}

} // namespace xod

#endif

/*=============================================================================
 *
 *
 * Format Numbers
 *
 *
 =============================================================================*/

/**
 * Provide `formatNumber` cross-platform number to string converter function.
 *
 * Taken from here:
 * https://github.com/client9/stringencoders/blob/master/src/modp_numtoa.c
 * Original function name: `modp_dtoa2`.
 *
 * Modified:
 * - `isnan` instead of tricky comparing and return "NaN"
 * - handle Infinity values and return "Inf" or "-Inf"
 * - return `OVF` and `-OVF` for numbers bigger than max possible, instead of using `sprintf`
 * - use `Number` instead of double
 * - if negative number rounds to zero, return just "0" instead of "-0"
 *
 * This is a replacement of `dtostrf`.
 */

#ifndef XOD_FORMAT_NUMBER_H
#define XOD_FORMAT_NUMBER_H

namespace xod {

/**
 * Powers of 10
 * 10^0 to 10^9
 */
static const Number powers_of_10[] = { 1, 10, 100, 1000, 10000, 100000, 1000000,
    10000000, 100000000, 1000000000 };

static void strreverse(char* begin, char* end) {
    char aux;
    while (end > begin)
        aux = *end, *end-- = *begin, *begin++ = aux;
};

size_t formatNumber(Number value, int prec, char* str) {
    if (isnan(value)) {
        strcpy(str, "NaN");
        return (size_t)3;
    }

    if (isinf(value)) {
        bool isNegative = value < 0;
        strcpy(str, isNegative ? "-Inf" : "Inf");
        return (size_t)isNegative ? 4 : 3;
    }

    /* if input is larger than thres_max return "OVF" */
    const Number thres_max = (Number)(0x7FFFFFFF);

    Number diff = 0.0;
    char* wstr = str;

    if (prec < 0) {
        prec = 0;
    } else if (prec > 9) {
        /* precision of >= 10 can lead to overflow errors */
        prec = 9;
    }

    /* we'll work in positive values and deal with the
	   negative sign issue later */
    int neg = 0;
    if (value < 0) {
        neg = 1;
        value = -value;
    }

    uint32_t whole = (uint32_t)value;
    Number tmp = (value - whole) * powers_of_10[prec];
    uint32_t frac = (uint32_t)(tmp);
    diff = tmp - frac;

    if (diff > 0.5) {
        ++frac;
        /* handle rollover, e.g.  case 0.99 with prec 1 is 1.0  */
        if (frac >= powers_of_10[prec]) {
            frac = 0;
            ++whole;
        }
    } else if (diff == 0.5 && prec > 0 && (frac & 1)) {
        /* if halfway, round up if odd, OR
		   if last digit is 0.  That last part is strange */
        ++frac;
        if (frac >= powers_of_10[prec]) {
            frac = 0;
            ++whole;
        }
    } else if (diff == 0.5 && prec == 0 && (whole & 1)) {
        ++frac;
        if (frac >= powers_of_10[prec]) {
            frac = 0;
            ++whole;
        }
    }

    if (value > thres_max) {
        if (neg) {
            strcpy(str, "-OVF");
            return (size_t)4;
        }
        strcpy(str, "OVF");
        return (size_t)3;
    }

    int has_decimal = 0;
    int count = prec;
    bool notzero = frac > 0;

    while (count > 0) {
        --count;
        *wstr++ = (char)(48 + (frac % 10));
        frac /= 10;
        has_decimal = 1;
    }

    if (frac > 0) {
        ++whole;
    }

    /* add decimal */
    if (has_decimal) {
        *wstr++ = '.';
    }

    notzero = notzero || whole > 0;

    /* do whole part
	 * Take care of sign conversion
	 * Number is reversed.
	 */
    do
        *wstr++ = (char)(48 + (whole % 10));
    while (whole /= 10);

    if (neg && notzero) {
        *wstr++ = '-';
    }
    *wstr = '\0';
    strreverse(str, wstr - 1);
    return (size_t)(wstr - str);
}

} // namespace xod
#endif


/*=============================================================================
 *
 *
 * Runtime
 *
 *
 =============================================================================*/

//----------------------------------------------------------------------------
// Debug routines
//----------------------------------------------------------------------------
// #ifndef DEBUG_SERIAL
#if defined(XOD_DEBUG) && !defined(DEBUG_SERIAL)
#  define DEBUG_SERIAL Serial
#endif

#if defined(XOD_DEBUG) && defined(XOD_DEBUG_ENABLE_TRACE)
#  define XOD_TRACE(x)      { DEBUG_SERIAL.print(x); DEBUG_SERIAL.flush(); }
#  define XOD_TRACE_LN(x)   { DEBUG_SERIAL.println(x); DEBUG_SERIAL.flush(); }
#  define XOD_TRACE_F(x)    XOD_TRACE(F(x))
#  define XOD_TRACE_FLN(x)  XOD_TRACE_LN(F(x))
#else
#  define XOD_TRACE(x)
#  define XOD_TRACE_LN(x)
#  define XOD_TRACE_F(x)
#  define XOD_TRACE_FLN(x)
#endif

//----------------------------------------------------------------------------
// PGM space utilities
//----------------------------------------------------------------------------
#define pgm_read_nodeid(address) (pgm_read_word(address))

/*
 * Workaround for bugs:
 * https://github.com/arduino/ArduinoCore-sam/pull/43
 * https://github.com/arduino/ArduinoCore-samd/pull/253
 * Remove after the PRs merge
 */
#if !defined(ARDUINO_ARCH_AVR) && defined(pgm_read_ptr)
#  undef pgm_read_ptr
#  define pgm_read_ptr(addr) (*(const void **)(addr))
#endif

namespace xod {
//----------------------------------------------------------------------------
// Global variables
//----------------------------------------------------------------------------

TimeMs g_transactionTime;
bool g_isSettingUp;
bool g_isEarlyDeferPass;

//----------------------------------------------------------------------------
// Metaprogramming utilities
//----------------------------------------------------------------------------

template<typename T> struct always_false {
    enum { value = 0 };
};

//----------------------------------------------------------------------------
// Forward declarations
//----------------------------------------------------------------------------

TimeMs transactionTime();
void runTransaction();

//----------------------------------------------------------------------------
// Engine (private API)
//----------------------------------------------------------------------------

namespace detail {

template<typename NodeT>
bool isTimedOut(const NodeT* node) {
    TimeMs t = node->timeoutAt;
    // TODO: deal with uint32 overflow
    return t && t < transactionTime();
}

template<typename NodeT>
void clearTimeout(NodeT* node) {
    node->timeoutAt = 0;
}

template<typename NodeT>
void clearStaleTimeout(NodeT* node) {
    if (isTimedOut(node))
        clearTimeout(node);
}

void printErrorToDebugSerial(uint16_t nodeId, ErrorFlags errorFlags) {
#if defined(XOD_DEBUG) || defined(XOD_SIMULATION)
    XOD_DEBUG_SERIAL.print(F("+XOD_ERR:"));
    XOD_DEBUG_SERIAL.print(g_transactionTime);
    XOD_DEBUG_SERIAL.print(':');
    XOD_DEBUG_SERIAL.print(nodeId);
    XOD_DEBUG_SERIAL.print(':');
    XOD_DEBUG_SERIAL.print(errorFlags, DEC);
    XOD_DEBUG_SERIAL.print('\r');
    XOD_DEBUG_SERIAL.print('\n');
#endif
}

} // namespace detail

//----------------------------------------------------------------------------
// Public API (can be used by native nodes’ `evaluate` functions)
//----------------------------------------------------------------------------

TimeMs transactionTime() {
    return g_transactionTime;
}

bool isSettingUp() {
    return g_isSettingUp;
}

bool isEarlyDeferPass() {
    return g_isEarlyDeferPass;
}

template<typename ContextT>
void setTimeout(ContextT* ctx, TimeMs timeout) {
    ctx->_node->timeoutAt = transactionTime() + timeout;
}

template<typename ContextT>
void clearTimeout(ContextT* ctx) {
    detail::clearTimeout(ctx->_node);
}

template<typename ContextT>
bool isTimedOut(const ContextT* ctx) {
    return detail::isTimedOut(ctx->_node);
}

constexpr bool isValidDigitalPort(uint8_t port) {
#if defined(__AVR__) && defined(NUM_DIGITAL_PINS)
    return port < NUM_DIGITAL_PINS;
#else
    return true;
#endif
}

constexpr bool isValidAnalogPort(uint8_t port) {
#if defined(__AVR__) && defined(NUM_ANALOG_INPUTS)
    return port >= A0 && port < A0 + NUM_ANALOG_INPUTS;
#else
    return true;
#endif
}

} // namespace xod

//----------------------------------------------------------------------------
// Entry point
//----------------------------------------------------------------------------
void setup() {
    // FIXME: looks like there is a rounding bug. Waiting for 100ms fights it
    delay(100);

#if defined(XOD_DEBUG) || defined(XOD_SIMULATION)
    XOD_DEBUG_SERIAL.begin(115200);
    XOD_DEBUG_SERIAL.setTimeout(10);
#endif
    XOD_TRACE_FLN("\n\nProgram started");

    xod::g_isSettingUp = true;
    xod::runTransaction();
    xod::g_isSettingUp = false;
}

void loop() {
    xod::runTransaction();
}

/*=============================================================================
 *
 *
 * Native node implementations
 *
 *
 =============================================================================*/

namespace xod {

//-----------------------------------------------------------------------------
// xod/core/cast-to-pulse(boolean) implementation
//-----------------------------------------------------------------------------
namespace xod__core__cast_to_pulse__boolean {

struct State {
  bool state = false;
};

struct Node {
    State state;
};

struct input_IN { };
struct output_OUT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_IN> { using T = Logic; };
template<> struct ValueType<output_OUT> { using T = Pulse; };

struct ContextObject {
    Node* _node;

    Logic _input_IN;

    bool _isOutputDirty_OUT : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_IN" \
            " output_OUT");
}

template<> Logic getValue<input_IN>(Context ctx) {
    return ctx->_input_IN;
}
template<> Pulse getValue<output_OUT>(Context ctx) {
    return Pulse();
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            "");
    return false;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void emitValue<output_OUT>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_OUT = true;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    State* state = getState(ctx);
    auto newValue = getValue<input_IN>(ctx);

    if (newValue == true && state->state == false)
        emitValue<output_OUT>(ctx, 1);

    state->state = newValue;
}

} // namespace xod__core__cast_to_pulse__boolean

//-----------------------------------------------------------------------------
// xod/core/continuously implementation
//-----------------------------------------------------------------------------
namespace xod__core__continuously {

struct State {
};

struct Node {
    TimeMs timeoutAt;
    State state;
};

struct output_TICK { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<output_TICK> { using T = Pulse; };

struct ContextObject {
    Node* _node;

    bool _isOutputDirty_TICK : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            "" \
            " output_TICK");
}

template<> Pulse getValue<output_TICK>(Context ctx) {
    return Pulse();
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            "");
    return false;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_TICK");
}

template<> void emitValue<output_TICK>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_TICK = true;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    emitValue<output_TICK>(ctx, 1);
    setTimeout(ctx, 0);
}

} // namespace xod__core__continuously

//-----------------------------------------------------------------------------
// xod/core/boot implementation
//-----------------------------------------------------------------------------
namespace xod__core__boot {

struct State {
};

struct Node {
    State state;
};

struct output_BOOT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<output_BOOT> { using T = Pulse; };

struct ContextObject {
    Node* _node;

    bool _isOutputDirty_BOOT : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            "" \
            " output_BOOT");
}

template<> Pulse getValue<output_BOOT>(Context ctx) {
    return Pulse();
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            "");
    return false;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_BOOT");
}

template<> void emitValue<output_BOOT>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_BOOT = true;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    emitValue<output_BOOT>(ctx, 1);
}

} // namespace xod__core__boot

//-----------------------------------------------------------------------------
// xod/core/any implementation
//-----------------------------------------------------------------------------
namespace xod__core__any {

struct State {
};

struct Node {
    State state;
};

struct input_IN1 { };
struct input_IN2 { };
struct output_OUT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_IN1> { using T = Pulse; };
template<> struct ValueType<input_IN2> { using T = Pulse; };
template<> struct ValueType<output_OUT> { using T = Pulse; };

struct ContextObject {
    Node* _node;

    bool _isInputDirty_IN1;
    bool _isInputDirty_IN2;

    bool _isOutputDirty_OUT : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_IN1 input_IN2" \
            " output_OUT");
}

template<> Pulse getValue<input_IN1>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<input_IN2>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<output_OUT>(Context ctx) {
    return Pulse();
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_IN1 input_IN2");
    return false;
}

template<> bool isInputDirty<input_IN1>(Context ctx) {
    return ctx->_isInputDirty_IN1;
}
template<> bool isInputDirty<input_IN2>(Context ctx) {
    return ctx->_isInputDirty_IN2;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void emitValue<output_OUT>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_OUT = true;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    bool p1 = isInputDirty<input_IN1>(ctx);
    bool p2 = isInputDirty<input_IN2>(ctx);
    if (p1 || p2)
        emitValue<output_OUT>(ctx, true);
}

} // namespace xod__core__any

//-----------------------------------------------------------------------------
// xod/core/clock implementation
//-----------------------------------------------------------------------------
namespace xod__core__clock {

struct State {
  TimeMs nextTrig;
};

struct Node {
    TimeMs timeoutAt;
    State state;
};

struct input_EN { };
struct input_IVAL { };
struct input_RST { };
struct output_TICK { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_EN> { using T = Logic; };
template<> struct ValueType<input_IVAL> { using T = Number; };
template<> struct ValueType<input_RST> { using T = Pulse; };
template<> struct ValueType<output_TICK> { using T = Pulse; };

struct ContextObject {
    Node* _node;

    Logic _input_EN;
    Number _input_IVAL;

    bool _isInputDirty_EN;
    bool _isInputDirty_RST;

    bool _isOutputDirty_TICK : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_EN input_IVAL input_RST" \
            " output_TICK");
}

template<> Logic getValue<input_EN>(Context ctx) {
    return ctx->_input_EN;
}
template<> Number getValue<input_IVAL>(Context ctx) {
    return ctx->_input_IVAL;
}
template<> Pulse getValue<input_RST>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<output_TICK>(Context ctx) {
    return Pulse();
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_EN input_RST");
    return false;
}

template<> bool isInputDirty<input_EN>(Context ctx) {
    return ctx->_isInputDirty_EN;
}
template<> bool isInputDirty<input_RST>(Context ctx) {
    return ctx->_isInputDirty_RST;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_TICK");
}

template<> void emitValue<output_TICK>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_TICK = true;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    State* state = getState(ctx);
    TimeMs tNow = transactionTime();
    auto ival = getValue<input_IVAL>(ctx);
    if (ival < 0) ival = 0;
    TimeMs dt = ival * 1000;
    TimeMs tNext = tNow + dt;

    auto isEnabled = getValue<input_EN>(ctx);
    auto isRstDirty = isInputDirty<input_RST>(ctx);

    if (isTimedOut(ctx) && isEnabled && !isRstDirty) {
        emitValue<output_TICK>(ctx, 1);
        state->nextTrig = tNext;
        setTimeout(ctx, dt);
    }

    if (isRstDirty || isInputDirty<input_EN>(ctx)) {
        // Handle enable/disable/reset
        if (!isEnabled) {
            // Disable timeout loop on explicit false on EN
            state->nextTrig = 0;
            clearTimeout(ctx);
        } else if (state->nextTrig < tNow || state->nextTrig > tNext) {
            // Start timeout from scratch
            state->nextTrig = tNext;
            setTimeout(ctx, dt);
        }
    }
}

} // namespace xod__core__clock

//-----------------------------------------------------------------------------
// xod/core/delay implementation
//-----------------------------------------------------------------------------
namespace xod__core__delay {

struct State {
};

struct Node {
    TimeMs timeoutAt;
    Logic output_ACT;
    State state;
};

struct input_T { };
struct input_SET { };
struct input_RST { };
struct output_DONE { };
struct output_ACT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_T> { using T = Number; };
template<> struct ValueType<input_SET> { using T = Pulse; };
template<> struct ValueType<input_RST> { using T = Pulse; };
template<> struct ValueType<output_DONE> { using T = Pulse; };
template<> struct ValueType<output_ACT> { using T = Logic; };

struct ContextObject {
    Node* _node;

    Number _input_T;

    bool _isInputDirty_SET;
    bool _isInputDirty_RST;

    bool _isOutputDirty_DONE : 1;
    bool _isOutputDirty_ACT : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_T input_SET input_RST" \
            " output_DONE output_ACT");
}

template<> Number getValue<input_T>(Context ctx) {
    return ctx->_input_T;
}
template<> Pulse getValue<input_SET>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<input_RST>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<output_DONE>(Context ctx) {
    return Pulse();
}
template<> Logic getValue<output_ACT>(Context ctx) {
    return ctx->_node->output_ACT;
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_SET input_RST");
    return false;
}

template<> bool isInputDirty<input_SET>(Context ctx) {
    return ctx->_isInputDirty_SET;
}
template<> bool isInputDirty<input_RST>(Context ctx) {
    return ctx->_isInputDirty_RST;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_DONE output_ACT");
}

template<> void emitValue<output_DONE>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_DONE = true;
}
template<> void emitValue<output_ACT>(Context ctx, Logic val) {
    ctx->_node->output_ACT = val;
    ctx->_isOutputDirty_ACT = true;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    if (isInputDirty<input_RST>(ctx)) {
        clearTimeout(ctx);
        emitValue<output_ACT>(ctx, false);
    } else if (isInputDirty<input_SET>(ctx)) {
        TimeMs dt = getValue<input_T>(ctx) * 1000;
        setTimeout(ctx, dt);
        emitValue<output_ACT>(ctx, true);
    } else if (isTimedOut(ctx)) {
        emitValue<output_DONE>(ctx, true);
        emitValue<output_ACT>(ctx, false);
    }
}

} // namespace xod__core__delay

//-----------------------------------------------------------------------------
// xod/core/count implementation
//-----------------------------------------------------------------------------
namespace xod__core__count {

struct State {
};

struct Node {
    Number output_OUT;
    State state;
};

struct input_STEP { };
struct input_INC { };
struct input_RST { };
struct output_OUT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_STEP> { using T = Number; };
template<> struct ValueType<input_INC> { using T = Pulse; };
template<> struct ValueType<input_RST> { using T = Pulse; };
template<> struct ValueType<output_OUT> { using T = Number; };

struct ContextObject {
    Node* _node;

    Number _input_STEP;

    bool _isInputDirty_INC;
    bool _isInputDirty_RST;

    bool _isOutputDirty_OUT : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_STEP input_INC input_RST" \
            " output_OUT");
}

template<> Number getValue<input_STEP>(Context ctx) {
    return ctx->_input_STEP;
}
template<> Pulse getValue<input_INC>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<input_RST>(Context ctx) {
    return Pulse();
}
template<> Number getValue<output_OUT>(Context ctx) {
    return ctx->_node->output_OUT;
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_INC input_RST");
    return false;
}

template<> bool isInputDirty<input_INC>(Context ctx) {
    return ctx->_isInputDirty_INC;
}
template<> bool isInputDirty<input_RST>(Context ctx) {
    return ctx->_isInputDirty_RST;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void emitValue<output_OUT>(Context ctx, Number val) {
    ctx->_node->output_OUT = val;
    ctx->_isOutputDirty_OUT = true;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    Number count = getValue<output_OUT>(ctx);

    if (isInputDirty<input_RST>(ctx))
        count = 0;
    else if (isInputDirty<input_INC>(ctx))
        count += getValue<input_STEP>(ctx);

    emitValue<output_OUT>(ctx, count);
}

} // namespace xod__core__count

//-----------------------------------------------------------------------------
// xod/core/greater implementation
//-----------------------------------------------------------------------------
namespace xod__core__greater {

//#pragma XOD dirtieness disable

struct State {
};

struct Node {
    Logic output_OUT;
    State state;
};

struct input_IN1 { };
struct input_IN2 { };
struct output_OUT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_IN1> { using T = Number; };
template<> struct ValueType<input_IN2> { using T = Number; };
template<> struct ValueType<output_OUT> { using T = Logic; };

struct ContextObject {
    Node* _node;

    Number _input_IN1;
    Number _input_IN2;

};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_IN1 input_IN2" \
            " output_OUT");
}

template<> Number getValue<input_IN1>(Context ctx) {
    return ctx->_input_IN1;
}
template<> Number getValue<input_IN2>(Context ctx) {
    return ctx->_input_IN2;
}
template<> Logic getValue<output_OUT>(Context ctx) {
    return ctx->_node->output_OUT;
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            "");
    return false;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void emitValue<output_OUT>(Context ctx, Logic val) {
    ctx->_node->output_OUT = val;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    auto lhs = getValue<input_IN1>(ctx);
    auto rhs = getValue<input_IN2>(ctx);
    emitValue<output_OUT>(ctx, lhs > rhs);
}

} // namespace xod__core__greater

//-----------------------------------------------------------------------------
// xod/core/cast-to-string(number) implementation
//-----------------------------------------------------------------------------
namespace xod__core__cast_to_string__number {

//#pragma XOD dirtieness disable

struct State {
    char str[16];
    CStringView view;
    State() : view(str) { }
};

struct Node {
    XString output_OUT;
    State state;
};

struct input_IN { };
struct output_OUT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_IN> { using T = Number; };
template<> struct ValueType<output_OUT> { using T = XString; };

struct ContextObject {
    Node* _node;

    Number _input_IN;

};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_IN" \
            " output_OUT");
}

template<> Number getValue<input_IN>(Context ctx) {
    return ctx->_input_IN;
}
template<> XString getValue<output_OUT>(Context ctx) {
    return ctx->_node->output_OUT;
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            "");
    return false;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void emitValue<output_OUT>(Context ctx, XString val) {
    ctx->_node->output_OUT = val;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

void evaluate(Context ctx) {
    auto state = getState(ctx);
    auto num = getValue<input_IN>(ctx);
    formatNumber(num, 2, state->str);
    emitValue<output_OUT>(ctx, XString(&state->view));
}

} // namespace xod__core__cast_to_string__number

//-----------------------------------------------------------------------------
// xod/common-hardware/text-lcd-16x2 implementation
//-----------------------------------------------------------------------------
namespace xod__common_hardware__text_lcd_16x2 {

//#pragma XOD evaluate_on_pin disable
//#pragma XOD evaluate_on_pin enable input_UPD
//#pragma XOD error_raise enable

// --- Enter global namespace ---
}}
#include <LiquidCrystal.h>

namespace xod {
namespace xod__common_hardware__text_lcd_16x2 {
// --- Back to local namespace ---
struct State {
    LiquidCrystal* lcd;
};

union NodeErrors {
    struct {
        bool output_DONE : 1;
    };

    ErrorFlags flags;
};

struct Node {
    NodeErrors errors;
    State state;
};

struct input_RS { };
struct input_EN { };
struct input_D4 { };
struct input_D5 { };
struct input_D6 { };
struct input_D7 { };
struct input_L1 { };
struct input_L2 { };
struct input_UPD { };
struct output_DONE { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_RS> { using T = uint8_t; };
template<> struct ValueType<input_EN> { using T = uint8_t; };
template<> struct ValueType<input_D4> { using T = uint8_t; };
template<> struct ValueType<input_D5> { using T = uint8_t; };
template<> struct ValueType<input_D6> { using T = uint8_t; };
template<> struct ValueType<input_D7> { using T = uint8_t; };
template<> struct ValueType<input_L1> { using T = XString; };
template<> struct ValueType<input_L2> { using T = XString; };
template<> struct ValueType<input_UPD> { using T = Pulse; };
template<> struct ValueType<output_DONE> { using T = Pulse; };

struct ContextObject {
    Node* _node;

    uint8_t _input_RS;
    uint8_t _input_EN;
    uint8_t _input_D4;
    uint8_t _input_D5;
    uint8_t _input_D6;
    uint8_t _input_D7;
    XString _input_L1;
    XString _input_L2;

    bool _isInputDirty_UPD;

    bool _isOutputDirty_DONE : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_RS input_EN input_D4 input_D5 input_D6 input_D7 input_L1 input_L2 input_UPD" \
            " output_DONE");
}

template<> uint8_t getValue<input_RS>(Context ctx) {
    return ctx->_input_RS;
}
template<> uint8_t getValue<input_EN>(Context ctx) {
    return ctx->_input_EN;
}
template<> uint8_t getValue<input_D4>(Context ctx) {
    return ctx->_input_D4;
}
template<> uint8_t getValue<input_D5>(Context ctx) {
    return ctx->_input_D5;
}
template<> uint8_t getValue<input_D6>(Context ctx) {
    return ctx->_input_D6;
}
template<> uint8_t getValue<input_D7>(Context ctx) {
    return ctx->_input_D7;
}
template<> XString getValue<input_L1>(Context ctx) {
    return ctx->_input_L1;
}
template<> XString getValue<input_L2>(Context ctx) {
    return ctx->_input_L2;
}
template<> Pulse getValue<input_UPD>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<output_DONE>(Context ctx) {
    return Pulse();
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_UPD");
    return false;
}

template<> bool isInputDirty<input_UPD>(Context ctx) {
    return ctx->_isInputDirty_UPD;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_DONE");
}

template<> void emitValue<output_DONE>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_DONE = true;
    ctx->_node->errors.output_DONE = false;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

template<typename OutputT> void raiseError(Context ctx) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_DONE");
}

template<> void raiseError<output_DONE>(Context ctx) {
    ctx->_node->errors.output_DONE = true;
    ctx->_isOutputDirty_DONE = true;
}

void raiseError(Context ctx) {
    ctx->_node->errors.output_DONE = true;
    ctx->_isOutputDirty_DONE = true;
}

void printLine(LiquidCrystal* lcd, uint8_t lineIndex, XString str) {
    lcd->setCursor(0, lineIndex);
    uint8_t whitespace = 16;
    for (auto it = str->iterate(); it; ++it, --whitespace)
        lcd->write(*it);

    // Clear the rest of the line
    while (whitespace--)
        lcd->write(' ');
}

void evaluate(Context ctx) {
    if (!isInputDirty<input_UPD>(ctx))
        return;

    State* state = getState(ctx);
    auto lcd = state->lcd;
    if (!state->lcd) {
        auto rsPort = getValue<input_RS>(ctx);
        auto enPort = getValue<input_EN>(ctx);
        auto d4Port = getValue<input_D4>(ctx);
        auto d5Port = getValue<input_D5>(ctx);
        auto d6Port = getValue<input_D6>(ctx);
        auto d7Port = getValue<input_D7>(ctx);

        if (
            !isValidDigitalPort(rsPort) ||
            !isValidDigitalPort(enPort) ||
            !isValidDigitalPort(d4Port) ||
            !isValidDigitalPort(d5Port) ||
            !isValidDigitalPort(d6Port) ||
            !isValidDigitalPort(d7Port)
        ) {
            raiseError(ctx);
            return;
        }

        state->lcd = lcd = new LiquidCrystal(
            (int)getValue<input_RS>(ctx),
            (int)getValue<input_EN>(ctx),
            (int)getValue<input_D4>(ctx),
            (int)getValue<input_D5>(ctx),
            (int)getValue<input_D6>(ctx),
            (int)getValue<input_D7>(ctx));

        lcd->begin(16, 2);
    }

    printLine(lcd, 0, getValue<input_L1>(ctx));
    printLine(lcd, 1, getValue<input_L2>(ctx));

    emitValue<output_DONE>(ctx, 1);
}

} // namespace xod__common_hardware__text_lcd_16x2

//-----------------------------------------------------------------------------
// xod/core/defer(pulse) implementation
//-----------------------------------------------------------------------------
namespace xod__core__defer__pulse {

//#pragma XOD error_catch enable
//#pragma XOD error_raise enable

struct State {
    bool shouldRaiseAtTheNextDeferOnlyRun = false;
    bool shouldPulseAtTheNextDeferOnlyRun = false;
};

union NodeErrors {
    struct {
        bool output_OUT : 1;
    };

    ErrorFlags flags;
};

struct Node {
    NodeErrors errors;
    TimeMs timeoutAt;
    State state;
};

struct input_IN { };
struct output_OUT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_IN> { using T = Pulse; };
template<> struct ValueType<output_OUT> { using T = Pulse; };

struct ContextObject {
    Node* _node;
    uint8_t _error_input_IN;

    bool _isInputDirty_IN;

    bool _isOutputDirty_OUT : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_IN" \
            " output_OUT");
}

template<> Pulse getValue<input_IN>(Context ctx) {
    return Pulse();
}
template<> Pulse getValue<output_OUT>(Context ctx) {
    return Pulse();
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_IN");
    return false;
}

template<> bool isInputDirty<input_IN>(Context ctx) {
    return ctx->_isInputDirty_IN;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void emitValue<output_OUT>(Context ctx, Pulse val) {
    ctx->_isOutputDirty_OUT = true;
    if (isEarlyDeferPass()) ctx->_node->errors.output_OUT = false;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

template<typename OutputT> void raiseError(Context ctx) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void raiseError<output_OUT>(Context ctx) {
    ctx->_node->errors.output_OUT = true;
    ctx->_isOutputDirty_OUT = true;
}

void raiseError(Context ctx) {
    ctx->_node->errors.output_OUT = true;
    ctx->_isOutputDirty_OUT = true;
}

template<typename InputT> uint8_t getError(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_IN");
    return 0;
}

template<> uint8_t getError<input_IN>(Context ctx) {
    return ctx->_error_input_IN;
}

void evaluate(Context ctx) {
    auto state = getState(ctx);

    if (isEarlyDeferPass()) {
        if (state->shouldRaiseAtTheNextDeferOnlyRun) {
            raiseError<output_OUT>(ctx);
            state->shouldRaiseAtTheNextDeferOnlyRun = false;
        }

        if (state->shouldPulseAtTheNextDeferOnlyRun) {
            emitValue<output_OUT>(ctx, true);
            state->shouldPulseAtTheNextDeferOnlyRun = false;
        }
    } else {
        if (getError<input_IN>(ctx)) {
            state->shouldRaiseAtTheNextDeferOnlyRun = true;
        } else if (isInputDirty<input_IN>(ctx)) {
            state->shouldPulseAtTheNextDeferOnlyRun = true;
        }

        setTimeout(ctx, 0);
    }
}

} // namespace xod__core__defer__pulse

//-----------------------------------------------------------------------------
// xod/core/defer(boolean) implementation
//-----------------------------------------------------------------------------
namespace xod__core__defer__boolean {

//#pragma XOD error_catch enable
//#pragma XOD error_raise enable

struct State {
    bool shouldRaiseAtTheNextDeferOnlyRun = false;
};

union NodeErrors {
    struct {
        bool output_OUT : 1;
    };

    ErrorFlags flags;
};

struct Node {
    NodeErrors errors;
    TimeMs timeoutAt;
    Logic output_OUT;
    State state;
};

struct input_IN { };
struct output_OUT { };

template<typename PinT> struct ValueType { using T = void; };
template<> struct ValueType<input_IN> { using T = Logic; };
template<> struct ValueType<output_OUT> { using T = Logic; };

struct ContextObject {
    Node* _node;
    uint8_t _error_input_IN;

    Logic _input_IN;

    bool _isOutputDirty_OUT : 1;
};

using Context = ContextObject*;

template<typename PinT> typename ValueType<PinT>::T getValue(Context ctx) {
    static_assert(always_false<PinT>::value,
            "Invalid pin descriptor. Expected one of:" \
            " input_IN" \
            " output_OUT");
}

template<> Logic getValue<input_IN>(Context ctx) {
    return ctx->_input_IN;
}
template<> Logic getValue<output_OUT>(Context ctx) {
    return ctx->_node->output_OUT;
}

template<typename InputT> bool isInputDirty(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            "");
    return false;
}

template<typename OutputT> void emitValue(Context ctx, typename ValueType<OutputT>::T val) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void emitValue<output_OUT>(Context ctx, Logic val) {
    ctx->_node->output_OUT = val;
    ctx->_isOutputDirty_OUT = true;
    if (isEarlyDeferPass()) ctx->_node->errors.output_OUT = false;
}

State* getState(Context ctx) {
    return &ctx->_node->state;
}

template<typename OutputT> void raiseError(Context ctx) {
    static_assert(always_false<OutputT>::value,
            "Invalid output descriptor. Expected one of:" \
            " output_OUT");
}

template<> void raiseError<output_OUT>(Context ctx) {
    ctx->_node->errors.output_OUT = true;
    ctx->_isOutputDirty_OUT = true;
}

void raiseError(Context ctx) {
    ctx->_node->errors.output_OUT = true;
    ctx->_isOutputDirty_OUT = true;
}

template<typename InputT> uint8_t getError(Context ctx) {
    static_assert(always_false<InputT>::value,
            "Invalid input descriptor. Expected one of:" \
            " input_IN");
    return 0;
}

template<> uint8_t getError<input_IN>(Context ctx) {
    return ctx->_error_input_IN;
}

void evaluate(Context ctx) {
    auto state = getState(ctx);

    if (isEarlyDeferPass()) {
        if (state->shouldRaiseAtTheNextDeferOnlyRun) {
            raiseError<output_OUT>(ctx);
            state->shouldRaiseAtTheNextDeferOnlyRun = false;
        } else {
            emitValue<output_OUT>(ctx, getValue<output_OUT>(ctx));
        }
    } else {
        if (getError<input_IN>(ctx)) {
            state->shouldRaiseAtTheNextDeferOnlyRun = true;
        } else {
            // save the value for reemission on deferred-only evaluation pass
            emitValue<output_OUT>(ctx, getValue<input_IN>(ctx));
        }

        setTimeout(ctx, 0);
    }
}

} // namespace xod__core__defer__boolean

} // namespace xod


/*=============================================================================
 *
 *
 * Main loop components
 *
 *
 =============================================================================*/

namespace xod {

// Define/allocate persistent storages (state, timeout, output data) for all nodes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

constexpr uint8_t node_1_output_VAL = 8;

constexpr uint8_t node_2_output_VAL = 9;

constexpr uint8_t node_3_output_VAL = 10;

constexpr uint8_t node_4_output_VAL = 11;

constexpr uint8_t node_5_output_VAL = 12;

constexpr uint8_t node_6_output_VAL = 13;

constexpr Number node_8_output_VAL = 1;

constexpr Number node_10_output_VAL = 1;

constexpr Logic node_11_output_VAL = true;

constexpr Number node_12_output_VAL = 1;

constexpr Number node_13_output_VAL = 1;

constexpr Number node_14_output_VAL = 10;

constexpr Logic node_17_output_ACT = false;

constexpr Number node_18_output_OUT = 0;

constexpr Number node_19_output_OUT = 0;

constexpr Logic node_20_output_OUT = false;

constexpr XString node_21_output_OUT = XString();

constexpr XString node_22_output_OUT = XString();

constexpr Logic node_25_output_OUT = false;

#pragma GCC diagnostic pop

struct TransactionState {
    bool node_0_isNodeDirty : 1;
    bool node_0_isOutputDirty_OUT : 1;
    bool node_0_hasUpstreamError : 1;
    bool node_7_isNodeDirty : 1;
    bool node_7_isOutputDirty_TICK : 1;
    bool node_9_isNodeDirty : 1;
    bool node_9_isOutputDirty_BOOT : 1;
    bool node_15_isNodeDirty : 1;
    bool node_15_isOutputDirty_OUT : 1;
    bool node_15_hasUpstreamError : 1;
    bool node_16_isNodeDirty : 1;
    bool node_16_isOutputDirty_TICK : 1;
    bool node_17_isNodeDirty : 1;
    bool node_17_isOutputDirty_DONE : 1;
    bool node_17_hasUpstreamError : 1;
    bool node_18_isNodeDirty : 1;
    bool node_18_isOutputDirty_OUT : 1;
    bool node_18_hasUpstreamError : 1;
    bool node_19_isNodeDirty : 1;
    bool node_19_isOutputDirty_OUT : 1;
    bool node_19_hasUpstreamError : 1;
    bool node_20_isNodeDirty : 1;
    bool node_20_isOutputDirty_OUT : 1;
    bool node_20_hasUpstreamError : 1;
    bool node_21_isNodeDirty : 1;
    bool node_21_hasUpstreamError : 1;
    bool node_22_isNodeDirty : 1;
    bool node_22_hasUpstreamError : 1;
    bool node_23_isNodeDirty : 1;
    bool node_23_hasUpstreamError : 1;
    bool node_24_isNodeDirty : 1;
    bool node_24_isOutputDirty_OUT : 1;
    bool node_24_hasUpstreamError : 1;
    bool node_25_isNodeDirty : 1;
    bool node_25_isOutputDirty_OUT : 1;
    bool node_25_hasUpstreamError : 1;
    TransactionState() {
        node_0_isNodeDirty = true;
        node_0_isOutputDirty_OUT = false;
        node_7_isNodeDirty = true;
        node_7_isOutputDirty_TICK = false;
        node_9_isNodeDirty = true;
        node_9_isOutputDirty_BOOT = false;
        node_15_isNodeDirty = true;
        node_15_isOutputDirty_OUT = false;
        node_16_isNodeDirty = true;
        node_16_isOutputDirty_TICK = false;
        node_17_isNodeDirty = true;
        node_17_isOutputDirty_DONE = false;
        node_18_isNodeDirty = true;
        node_18_isOutputDirty_OUT = true;
        node_19_isNodeDirty = true;
        node_19_isOutputDirty_OUT = true;
        node_20_isNodeDirty = true;
        node_21_isNodeDirty = true;
        node_22_isNodeDirty = true;
        node_23_isNodeDirty = true;
        node_24_isNodeDirty = true;
        node_24_isOutputDirty_OUT = false;
        node_25_isNodeDirty = true;
        node_25_isOutputDirty_OUT = true;
    }
};

TransactionState g_transaction;

xod__core__cast_to_pulse__boolean::Node node_0 = {
    xod__core__cast_to_pulse__boolean::State() // state default
};
xod__core__continuously::Node node_7 = {
    0, // timeoutAt
    xod__core__continuously::State() // state default
};
xod__core__boot::Node node_9 = {
    xod__core__boot::State() // state default
};
xod__core__any::Node node_15 = {
    xod__core__any::State() // state default
};
xod__core__clock::Node node_16 = {
    0, // timeoutAt
    xod__core__clock::State() // state default
};
xod__core__delay::Node node_17 = {
    0, // timeoutAt
    node_17_output_ACT, // output ACT default
    xod__core__delay::State() // state default
};
xod__core__count::Node node_18 = {
    node_18_output_OUT, // output OUT default
    xod__core__count::State() // state default
};
xod__core__count::Node node_19 = {
    node_19_output_OUT, // output OUT default
    xod__core__count::State() // state default
};
xod__core__greater::Node node_20 = {
    node_20_output_OUT, // output OUT default
    xod__core__greater::State() // state default
};
xod__core__cast_to_string__number::Node node_21 = {
    node_21_output_OUT, // output OUT default
    xod__core__cast_to_string__number::State() // state default
};
xod__core__cast_to_string__number::Node node_22 = {
    node_22_output_OUT, // output OUT default
    xod__core__cast_to_string__number::State() // state default
};
xod__common_hardware__text_lcd_16x2::Node node_23 = {
    false, // DONE has no errors on start
    xod__common_hardware__text_lcd_16x2::State() // state default
};
xod__core__defer__pulse::Node node_24 = {
    false, // OUT has no errors on start
    0, // timeoutAt
    xod__core__defer__pulse::State() // state default
};
xod__core__defer__boolean::Node node_25 = {
    false, // OUT has no errors on start
    0, // timeoutAt
    node_25_output_OUT, // output OUT default
    xod__core__defer__boolean::State() // state default
};

#if defined(XOD_DEBUG) || defined(XOD_SIMULATION)
namespace detail {
void handleTweaks() {
    if (XOD_DEBUG_SERIAL.available() > 0 && XOD_DEBUG_SERIAL.find("+XOD:", 5)) {
        int tweakedNodeId = XOD_DEBUG_SERIAL.parseInt();

        switch (tweakedNodeId) {
        }

        XOD_DEBUG_SERIAL.find('\n');
    }
}
} // namespace detail
#endif

void handleDefers() {
    {
        if (g_transaction.node_24_isNodeDirty) {

            XOD_TRACE_F("Trigger defer node #");
            XOD_TRACE_LN(24);

            xod__core__defer__pulse::ContextObject ctxObj;
            ctxObj._node = &node_24;
            ctxObj._isInputDirty_IN = false;

            ctxObj._error_input_IN = 0;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__defer__pulse::NodeErrors previousErrors = node_24.errors;

            node_24.errors.output_OUT = false;

            xod__core__defer__pulse::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_24_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            if (previousErrors.flags != node_24.errors.flags) {
                detail::printErrorToDebugSerial(24, node_24.errors.flags);

                // if an error was just raised or cleared from an output,
                // mark nearest downstream error catchers as dirty
                if (node_24.errors.output_OUT != previousErrors.output_OUT) {
                    g_transaction.node_24_isNodeDirty = true;
                }

                // if a pulse output was cleared from error, mark downstream nodes as dirty
                // (no matter if a pulse was emitted or not)
                if (previousErrors.output_OUT && !node_24.errors.output_OUT) {
                    g_transaction.node_15_isNodeDirty = true;
                }
            }

            // mark downstream nodes dirty
            g_transaction.node_15_isNodeDirty |= g_transaction.node_24_isOutputDirty_OUT;

            g_transaction.node_24_isNodeDirty = false;
            detail::clearTimeout(&node_24);
        }

        // propagate the error hold by the defer node
        if (node_24.errors.flags) {
            if (node_24.errors.output_OUT) {
                g_transaction.node_15_hasUpstreamError = true;
            }
        }
    }
    {
        if (g_transaction.node_25_isNodeDirty) {

            XOD_TRACE_F("Trigger defer node #");
            XOD_TRACE_LN(25);

            xod__core__defer__boolean::ContextObject ctxObj;
            ctxObj._node = &node_25;

            ctxObj._input_IN = node_20.output_OUT;

            ctxObj._error_input_IN = 0;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__defer__boolean::NodeErrors previousErrors = node_25.errors;

            xod__core__defer__boolean::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_25_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            if (previousErrors.flags != node_25.errors.flags) {
                detail::printErrorToDebugSerial(25, node_25.errors.flags);

                // if an error was just raised or cleared from an output,
                // mark nearest downstream error catchers as dirty
                if (node_25.errors.output_OUT != previousErrors.output_OUT) {
                    g_transaction.node_25_isNodeDirty = true;
                }

            }

            // mark downstream nodes dirty
            g_transaction.node_0_isNodeDirty |= g_transaction.node_25_isOutputDirty_OUT;

            g_transaction.node_25_isNodeDirty = false;
            detail::clearTimeout(&node_25);
        }

        // propagate the error hold by the defer node
        if (node_25.errors.flags) {
            if (node_25.errors.output_OUT) {
                g_transaction.node_0_hasUpstreamError = true;
            }
        }
    }
}

void runTransaction() {
    g_transactionTime = millis();

    XOD_TRACE_F("Transaction started, t=");
    XOD_TRACE_LN(g_transactionTime);

#if defined(XOD_DEBUG) || defined(XOD_SIMULATION)
    detail::handleTweaks();
#endif

    // Check for timeouts
    g_transaction.node_7_isNodeDirty |= detail::isTimedOut(&node_7);
    g_transaction.node_16_isNodeDirty |= detail::isTimedOut(&node_16);
    g_transaction.node_17_isNodeDirty |= detail::isTimedOut(&node_17);
    g_transaction.node_24_isNodeDirty |= detail::isTimedOut(&node_24);
    g_transaction.node_25_isNodeDirty |= detail::isTimedOut(&node_25);

    // defer-* nodes are always at the very bottom of the graph, so no one will
    // recieve values emitted by them. We must evaluate them before everybody
    // else to give them a chance to emit values.
    //
    // If trigerred, keep only output dirty, not the node itself, so it will
    // evaluate on the regular pass only if it receives a new value again.
    if (!isSettingUp()) {
        g_isEarlyDeferPass = true;
        handleDefers();
        g_isEarlyDeferPass = false;
    }

    // Evaluate all dirty nodes
    { // xod__core__cast_to_pulse__boolean #0

        if (g_transaction.node_0_hasUpstreamError) {
            g_transaction.node_18_hasUpstreamError = true;
        } else if (g_transaction.node_0_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(0);

            xod__core__cast_to_pulse__boolean::ContextObject ctxObj;
            ctxObj._node = &node_0;

            // copy data from upstream nodes into context
            ctxObj._input_IN = node_25.output_OUT;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__cast_to_pulse__boolean::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_0_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            // mark downstream nodes dirty
            g_transaction.node_18_isNodeDirty |= g_transaction.node_0_isOutputDirty_OUT;
        }

    }
    { // xod__core__continuously #7
        if (g_transaction.node_7_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(7);

            xod__core__continuously::ContextObject ctxObj;
            ctxObj._node = &node_7;

            // copy data from upstream nodes into context

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_TICK = false;

            xod__core__continuously::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_7_isOutputDirty_TICK = ctxObj._isOutputDirty_TICK;

            // mark downstream nodes dirty
            g_transaction.node_23_isNodeDirty |= g_transaction.node_7_isOutputDirty_TICK;
        }

    }
    { // xod__core__boot #9
        if (g_transaction.node_9_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(9);

            xod__core__boot::ContextObject ctxObj;
            ctxObj._node = &node_9;

            // copy data from upstream nodes into context

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_BOOT = false;

            xod__core__boot::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_9_isOutputDirty_BOOT = ctxObj._isOutputDirty_BOOT;

            // mark downstream nodes dirty
            g_transaction.node_15_isNodeDirty |= g_transaction.node_9_isOutputDirty_BOOT;
        }

    }
    { // xod__core__any #15

        if (g_transaction.node_15_hasUpstreamError) {
            g_transaction.node_17_hasUpstreamError = true;
        } else if (g_transaction.node_15_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(15);

            xod__core__any::ContextObject ctxObj;
            ctxObj._node = &node_15;

            // copy data from upstream nodes into context

            ctxObj._isInputDirty_IN1 = g_transaction.node_24_isOutputDirty_OUT;
            ctxObj._isInputDirty_IN2 = g_transaction.node_9_isOutputDirty_BOOT;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__any::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_15_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            // mark downstream nodes dirty
            g_transaction.node_17_isNodeDirty |= g_transaction.node_15_isOutputDirty_OUT;
        }

    }
    { // xod__core__clock #16
        if (g_transaction.node_16_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(16);

            xod__core__clock::ContextObject ctxObj;
            ctxObj._node = &node_16;

            // copy data from upstream nodes into context
            ctxObj._input_EN = node_11_output_VAL;
            ctxObj._input_IVAL = node_12_output_VAL;

            ctxObj._isInputDirty_RST = false;
            ctxObj._isInputDirty_EN = g_isSettingUp;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_TICK = false;

            xod__core__clock::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_16_isOutputDirty_TICK = ctxObj._isOutputDirty_TICK;

            // mark downstream nodes dirty
            g_transaction.node_18_isNodeDirty |= g_transaction.node_16_isOutputDirty_TICK;
        }

    }
    { // xod__core__delay #17

        if (g_transaction.node_17_hasUpstreamError) {
            g_transaction.node_19_hasUpstreamError = true;
            g_transaction.node_24_hasUpstreamError = true;
        } else if (g_transaction.node_17_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(17);

            xod__core__delay::ContextObject ctxObj;
            ctxObj._node = &node_17;

            // copy data from upstream nodes into context
            ctxObj._input_T = node_8_output_VAL;

            ctxObj._isInputDirty_RST = false;
            ctxObj._isInputDirty_SET = g_transaction.node_15_isOutputDirty_OUT;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_DONE = false;
            ctxObj._isOutputDirty_ACT = false;

            xod__core__delay::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_17_isOutputDirty_DONE = ctxObj._isOutputDirty_DONE;

            // mark downstream nodes dirty
            g_transaction.node_19_isNodeDirty |= g_transaction.node_17_isOutputDirty_DONE;
            g_transaction.node_24_isNodeDirty |= g_transaction.node_17_isOutputDirty_DONE;
        }

    }
    { // xod__core__count #18

        if (g_transaction.node_18_hasUpstreamError) {
            g_transaction.node_20_hasUpstreamError = true;
            g_transaction.node_21_hasUpstreamError = true;
        } else if (g_transaction.node_18_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(18);

            xod__core__count::ContextObject ctxObj;
            ctxObj._node = &node_18;

            // copy data from upstream nodes into context
            ctxObj._input_STEP = node_13_output_VAL;

            ctxObj._isInputDirty_INC = g_transaction.node_16_isOutputDirty_TICK;
            ctxObj._isInputDirty_RST = g_transaction.node_0_isOutputDirty_OUT;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__count::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_18_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            // mark downstream nodes dirty
            g_transaction.node_20_isNodeDirty |= g_transaction.node_18_isOutputDirty_OUT;
            g_transaction.node_21_isNodeDirty |= g_transaction.node_18_isOutputDirty_OUT;
        }

    }
    { // xod__core__count #19

        if (g_transaction.node_19_hasUpstreamError) {
            g_transaction.node_22_hasUpstreamError = true;
        } else if (g_transaction.node_19_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(19);

            xod__core__count::ContextObject ctxObj;
            ctxObj._node = &node_19;

            // copy data from upstream nodes into context
            ctxObj._input_STEP = node_10_output_VAL;

            ctxObj._isInputDirty_RST = false;
            ctxObj._isInputDirty_INC = g_transaction.node_17_isOutputDirty_DONE;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__count::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_19_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            // mark downstream nodes dirty
            g_transaction.node_22_isNodeDirty |= g_transaction.node_19_isOutputDirty_OUT;
        }

    }
    { // xod__core__greater #20

        if (g_transaction.node_20_hasUpstreamError) {
            g_transaction.node_25_hasUpstreamError = true;
        } else if (g_transaction.node_20_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(20);

            xod__core__greater::ContextObject ctxObj;
            ctxObj._node = &node_20;

            // copy data from upstream nodes into context
            ctxObj._input_IN1 = node_18.output_OUT;
            ctxObj._input_IN2 = node_14_output_VAL;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`

            xod__core__greater::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction

            // mark downstream nodes dirty
            g_transaction.node_25_isNodeDirty = true;
        }

    }
    { // xod__core__cast_to_string__number #21

        if (g_transaction.node_21_hasUpstreamError) {
            g_transaction.node_23_hasUpstreamError = true;
        } else if (g_transaction.node_21_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(21);

            xod__core__cast_to_string__number::ContextObject ctxObj;
            ctxObj._node = &node_21;

            // copy data from upstream nodes into context
            ctxObj._input_IN = node_18.output_OUT;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`

            xod__core__cast_to_string__number::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction

            // mark downstream nodes dirty
        }

    }
    { // xod__core__cast_to_string__number #22

        if (g_transaction.node_22_hasUpstreamError) {
            g_transaction.node_23_hasUpstreamError = true;
        } else if (g_transaction.node_22_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(22);

            xod__core__cast_to_string__number::ContextObject ctxObj;
            ctxObj._node = &node_22;

            // copy data from upstream nodes into context
            ctxObj._input_IN = node_19.output_OUT;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`

            xod__core__cast_to_string__number::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction

            // mark downstream nodes dirty
        }

    }
    { // xod__common_hardware__text_lcd_16x2 #23

        if (g_transaction.node_23_hasUpstreamError) {
        } else if (g_transaction.node_23_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(23);

            xod__common_hardware__text_lcd_16x2::ContextObject ctxObj;
            ctxObj._node = &node_23;

            // copy data from upstream nodes into context
            ctxObj._input_RS = node_1_output_VAL;
            ctxObj._input_EN = node_2_output_VAL;
            ctxObj._input_D4 = node_3_output_VAL;
            ctxObj._input_D5 = node_4_output_VAL;
            ctxObj._input_D6 = node_5_output_VAL;
            ctxObj._input_D7 = node_6_output_VAL;
            ctxObj._input_L1 = node_21.output_OUT;
            ctxObj._input_L2 = node_22.output_OUT;

            ctxObj._isInputDirty_UPD = g_transaction.node_7_isOutputDirty_TICK;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_DONE = false;

            xod__common_hardware__text_lcd_16x2::NodeErrors previousErrors = node_23.errors;

            node_23.errors.output_DONE = false;

            xod__common_hardware__text_lcd_16x2::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction

            if (previousErrors.flags != node_23.errors.flags) {
                detail::printErrorToDebugSerial(23, node_23.errors.flags);

                // if an error was just raised or cleared from an output,
                // mark nearest downstream error catchers as dirty
                if (node_23.errors.output_DONE != previousErrors.output_DONE) {
                }

                // if a pulse output was cleared from error, mark downstream nodes as dirty
                // (no matter if a pulse was emitted or not)
                if (previousErrors.output_DONE && !node_23.errors.output_DONE) {
                }
            }

            // mark downstream nodes dirty
        }

        // propagate errors hold by the node outputs
        if (node_23.errors.flags) {
            if (node_23.errors.output_DONE) {
            }
        }
    }
    { // xod__core__defer__pulse #24
        if (g_transaction.node_24_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(24);

            xod__core__defer__pulse::ContextObject ctxObj;
            ctxObj._node = &node_24;

            ctxObj._error_input_IN = 0;

            // copy data from upstream nodes into context

            ctxObj._isInputDirty_IN = g_transaction.node_17_isOutputDirty_DONE;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__defer__pulse::NodeErrors previousErrors = node_24.errors;

            xod__core__defer__pulse::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_24_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            if (previousErrors.flags != node_24.errors.flags) {
                detail::printErrorToDebugSerial(24, node_24.errors.flags);

                // if an error was just raised or cleared from an output,
                // mark nearest downstream error catchers as dirty
                if (node_24.errors.output_OUT != previousErrors.output_OUT) {
                    g_transaction.node_24_isNodeDirty = true;
                }

                // if a pulse output was cleared from error, mark downstream nodes as dirty
                // (no matter if a pulse was emitted or not)
                if (previousErrors.output_OUT && !node_24.errors.output_OUT) {
                    g_transaction.node_15_isNodeDirty = true;
                }
            }

            // mark downstream nodes dirty
            g_transaction.node_15_isNodeDirty |= g_transaction.node_24_isOutputDirty_OUT;
        }

        // propagate errors hold by the node outputs
        if (node_24.errors.flags) {
            if (node_24.errors.output_OUT) {
                g_transaction.node_15_hasUpstreamError = true;
            }
        }
    }
    { // xod__core__defer__boolean #25
        if (g_transaction.node_25_isNodeDirty) {
            XOD_TRACE_F("Eval node #");
            XOD_TRACE_LN(25);

            xod__core__defer__boolean::ContextObject ctxObj;
            ctxObj._node = &node_25;

            ctxObj._error_input_IN = 0;

            // copy data from upstream nodes into context
            ctxObj._input_IN = node_20.output_OUT;

            // initialize temporary output dirtyness state in the context,
            // where it can be modified from `raiseError` and `emitValue`
            ctxObj._isOutputDirty_OUT = false;

            xod__core__defer__boolean::NodeErrors previousErrors = node_25.errors;

            xod__core__defer__boolean::evaluate(&ctxObj);

            // transfer possibly modified dirtiness state from context to g_transaction
            g_transaction.node_25_isOutputDirty_OUT = ctxObj._isOutputDirty_OUT;

            if (previousErrors.flags != node_25.errors.flags) {
                detail::printErrorToDebugSerial(25, node_25.errors.flags);

                // if an error was just raised or cleared from an output,
                // mark nearest downstream error catchers as dirty
                if (node_25.errors.output_OUT != previousErrors.output_OUT) {
                    g_transaction.node_25_isNodeDirty = true;
                }

                // if a pulse output was cleared from error, mark downstream nodes as dirty
                // (no matter if a pulse was emitted or not)
            }

            // mark downstream nodes dirty
            g_transaction.node_0_isNodeDirty |= g_transaction.node_25_isOutputDirty_OUT;
        }

        // propagate errors hold by the node outputs
        if (node_25.errors.flags) {
            if (node_25.errors.output_OUT) {
                g_transaction.node_0_hasUpstreamError = true;
            }
        }
    }

    // Clear dirtieness and timeouts for all nodes and pins
    memset(&g_transaction, 0, sizeof(g_transaction));

    detail::clearStaleTimeout(&node_7);
    detail::clearStaleTimeout(&node_16);
    detail::clearStaleTimeout(&node_17);
    detail::clearStaleTimeout(&node_24);
    detail::clearStaleTimeout(&node_25);

    XOD_TRACE_F("Transaction completed, t=");
    XOD_TRACE_LN(millis());
}

} // namespace xod
