#include "../src/url-filter.hpp"
#include <cassert>

void test_dummy() {
    assert(url_filter::filterTrackingParameters("dummy") == "dummy");
}

void test_exception() {
    assert(url_filter::filterTrackingParameters(
               "https://www.google.com/mail/u/?ved=0") ==
           "https://www.google.com/mail/u/");
    assert(url_filter::filterTrackingParameters(
               "https://mail.google.com/mail/u/?ved=0") ==
           "https://mail.google.com/mail/u/?ved=0");
}

void test_redirect() {
    assert(url_filter::filterTrackingParameters(
               "https://www.google.com/url?q=https://example.com") ==
           "https://example.com");
    assert(url_filter::filterTrackingParameters(
               "https://www.google.com/url?q=http%3A%2F%2Fexample.com") ==
           "http://example.com");
}

void test_raw_rule() {
    assert(url_filter::filterTrackingParameters(
               "https://www.pantip.com/foo/#lead233") ==
           "https://www.pantip.com/foo/");
}

int main() {
    test_dummy();
    test_exception();
    test_redirect();
    test_raw_rule();
}
