// Host-side unit test for improv_wifi_busware::SerialFilter.
//
// Build + run:
//   g++ -std=c++17 -Wall -Wextra -O2
//       -I components/improv_wifi_busware/include
//       components/improv_wifi_busware/src/serial_filter.cpp
//       tests/test_serial_filter.cpp
//       -o /tmp/test_serial_filter && /tmp/test_serial_filter
//
// The filter is pure C++ with no hardware deps, so a host run exercises the
// state machine far more thoroughly than any on-device smoke test could.

#include "improv_wifi/serial_filter.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using improv_wifi_busware::SerialFilter;

namespace {

struct Sink {
    std::vector<uint8_t> bytes;
    std::vector<size_t>  callLens;   // length of each invocation, for atomicity checks
};

void consoleSinkFn(const uint8_t* data, size_t len, void* user) {
    auto* s = static_cast<Sink*>(user);
    s->bytes.insert(s->bytes.end(), data, data + len);
    s->callLens.push_back(len);
}

void improvSinkFn(const uint8_t* data, size_t len, void* user) {
    auto* s = static_cast<Sink*>(user);
    s->bytes.insert(s->bytes.end(), data, data + len);
    s->callLens.push_back(len);
}

int g_failed = 0;
int g_total  = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_total;                                                             \
        if (!(cond)) {                                                         \
            ++g_failed;                                                        \
            std::fprintf(stderr, "FAIL [%s:%d] %s — %s\n",                     \
                         __FILE__, __LINE__, msg, #cond);                      \
        }                                                                      \
    } while (0)

// Build a well-formed Improv frame: magic + ver + type + len + payload + csum.
// Checksum content does not matter for the filter (it doesn't validate),
// but we use a real value so the test resembles real traffic.
std::vector<uint8_t> makeFrame(uint8_t type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f = {'I','M','P','R','O','V', 0x01, type, static_cast<uint8_t>(payload.size())};
    f.insert(f.end(), payload.begin(), payload.end());
    uint8_t cs = 0;
    for (auto b : f) cs = static_cast<uint8_t>(cs + b);
    f.push_back(cs);
    return f;
}

void wireUp(SerialFilter& f, Sink& con, Sink& imp) {
    f.setSinks(consoleSinkFn, &con, improvSinkFn, &imp);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_pureConsole() {
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    const char* line = "V\n";
    f.feed(reinterpret_cast<const uint8_t*>(line), 2);
    CHECK(imp.bytes.empty(),         "no improv bytes for plain console traffic");
    CHECK(con.bytes.size() == 2,     "console got both bytes");
    CHECK(con.bytes[0] == 'V',       "console byte 0");
    CHECK(con.bytes[1] == '\n',      "console byte 1");
}

void test_fullFrame() {
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    auto frame = makeFrame(0x01, {0x00});  // GET_DEVICE_INFO has no payload, but use len=1 for variety
    f.feed(frame.data(), frame.size());
    CHECK(con.bytes.empty(),         "console silent during a clean Improv frame");
    CHECK(imp.bytes == frame,        "improv sink got the entire frame verbatim");
}

void test_frameByteByByte() {
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    auto frame = makeFrame(0x03, {'a','b','c'});
    for (auto b : frame) f.feed(&b, 1);
    CHECK(con.bytes.empty(),         "console silent for byte-by-byte frame");
    CHECK(imp.bytes == frame,        "improv sink reassembled byte-by-byte frame");
}

void test_partialMagic_strayI() {
    // User types "I<enter>" on the console: filter must hold back the 'I' but
    // ultimately deliver both 'I' and '\n' to the console.
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    const uint8_t in[] = {'I', '\n'};
    f.feed(in, 2);
    CHECK(imp.bytes.empty(),               "no improv on stray-I path");
    CHECK(con.bytes.size() == 2,           "console got both stray bytes back");
    CHECK(con.bytes[0] == 'I',             "stray 'I' delivered first");
    CHECK(con.bytes[1] == '\n',            "stray '\\n' delivered second");
}

void test_partialMagic_IM_then_X() {
    // 'I' then 'M' then a non-'P' must flush "IM" plus the breaking byte.
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    const uint8_t in[] = {'I', 'M', 'X'};
    f.feed(in, 3);
    CHECK(imp.bytes.empty(),               "no improv on IMX");
    CHECK(con.bytes.size() == 3,           "console got all three bytes");
    CHECK(con.bytes[0] == 'I' && con.bytes[1] == 'M' && con.bytes[2] == 'X',
          "console got IMX in order");
}

void test_consoleEcho_containingMagic() {
    // Reproduce the exact bug the filter was created to avoid: the Improv host
    // sends a frame, the device's console echoes back something containing the
    // literal magic. Both pieces hit the filter as a single stream, but only
    // the first (the real frame) must be classified as Improv.
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    auto frame = makeFrame(0x01, {});
    std::vector<uint8_t> stream = frame;
    const char* echo = "? (IMPROV<garbage> is unknown)\n";
    stream.insert(stream.end(), echo, echo + std::strlen(echo));
    // Note: the 'IMPROV' substring inside the echo IS magic and thus IS routed
    // to the improv sink — the filter cannot guess that the echo is "not real".
    // The lib's parser is the one that decides whether such input is a valid
    // RPC; classifying it as Improv-bound is the correct, deterministic choice.
    // What matters here: the filter does not get confused into discarding bytes
    // entirely, and the *real* preceding frame reaches the improv sink intact.
    f.feed(stream.data(), stream.size());
    // First N bytes of imp.bytes must be the real frame.
    CHECK(imp.bytes.size() >= frame.size(), "improv sink received at least the real frame");
    CHECK(std::equal(frame.begin(), frame.end(), imp.bytes.begin()),
          "real frame is the prefix of improv-sink bytes");
}

void test_nullSinks_noCrash() {
    SerialFilter f;  // sinks not set -> nullptr
    const uint8_t in[] = {'X','I','M','Y'};
    f.feed(in, sizeof(in));   // must not crash
    CHECK(true, "feed() with null sinks did not crash");

    // Now set only console sink.
    Sink con; Sink imp;
    f.setSinks(consoleSinkFn, &con, nullptr, nullptr);
    auto frame = makeFrame(0x01, {});
    f.feed(frame.data(), frame.size());
    CHECK(con.bytes.empty(),     "improv frame with null improv-sink is dropped");
    CHECK(imp.bytes.empty(),     "improv sink not invoked when null");
}

void test_reset_dropsHeldMagic() {
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    const uint8_t pre[] = {'I','M'};
    f.feed(pre, 2);  // held back, not yet flushed
    CHECK(con.bytes.empty(),     "partial magic held back");
    f.reset();
    const uint8_t post[] = {'X'};
    f.feed(post, 1);
    CHECK(con.bytes.size() == 1, "after reset, prior 'IM' is discarded");
    CHECK(con.bytes[0] == 'X',   "post-reset byte routed to console");
}

void test_consecutiveFrames() {
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    auto a = makeFrame(0x01, {});
    auto b = makeFrame(0x02, {0xaa, 0xbb});
    std::vector<uint8_t> stream;
    stream.insert(stream.end(), a.begin(), a.end());
    stream.insert(stream.end(), b.begin(), b.end());
    f.feed(stream.data(), stream.size());
    CHECK(con.bytes.empty(),     "no console bytes between two back-to-back frames");
    CHECK(imp.bytes == stream,   "both frames reached the improv sink in order");
}

void test_zeroLengthPayload() {
    // len=0 means: payload empty, just one checksum byte. Verify the filter
    // transitions Header -> Body(1) -> Idle correctly.
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    auto frame = makeFrame(0x01, {});
    f.feed(frame.data(), frame.size());
    CHECK(imp.bytes == frame,    "zero-payload frame goes to improv");
    // Now feed a console byte after — must NOT be interpreted as in-frame.
    const uint8_t after[] = {'X'};
    f.feed(after, 1);
    CHECK(con.bytes.size() == 1 && con.bytes[0] == 'X',
          "post-frame byte routed to console (state returned to Idle)");
}

void test_largePayload_255() {
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    std::vector<uint8_t> payload(255, 0x42);
    auto frame = makeFrame(0x03, payload);
    f.feed(frame.data(), frame.size());
    CHECK(imp.bytes == frame,    "max-len (255) payload reached improv intact");
    CHECK(con.bytes.empty(),     "no console bleed during max-len frame");
}

void test_strayI_betweenFrames() {
    // Sequence: real frame, then user types 'I' then a non-magic byte, then
    // another real frame. The 'I' must end up on the console, both frames
    // intact on improv.
    SerialFilter f; Sink con, imp; wireUp(f, con, imp);
    auto a = makeFrame(0x01, {});
    auto b = makeFrame(0x02, {0xcc});
    std::vector<uint8_t> stream = a;
    stream.push_back('I');
    stream.push_back('Q');   // breaks magic immediately at position 1
    stream.insert(stream.end(), b.begin(), b.end());
    f.feed(stream.data(), stream.size());
    std::vector<uint8_t> expectedImp = a;
    expectedImp.insert(expectedImp.end(), b.begin(), b.end());
    CHECK(imp.bytes == expectedImp,    "improv sink got both real frames concatenated");
    CHECK(con.bytes.size() == 2,       "console got 'I' and 'Q'");
    CHECK(con.bytes[0] == 'I' && con.bytes[1] == 'Q',
          "console bytes in order");
}

}  // namespace

int main() {
    test_pureConsole();
    test_fullFrame();
    test_frameByteByByte();
    test_partialMagic_strayI();
    test_partialMagic_IM_then_X();
    test_consoleEcho_containingMagic();
    test_nullSinks_noCrash();
    test_reset_dropsHeldMagic();
    test_consecutiveFrames();
    test_zeroLengthPayload();
    test_largePayload_255();
    test_strayI_betweenFrames();

    std::printf("%d/%d checks passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
