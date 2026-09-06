#include "../src/rules.hpp"
#include <cassert>

int main() {
    assert(url_filter::decodeURL("https://example.com/foo?bar=baz+%75") ==
           "https://example.com/foo?bar=baz+u");
}
