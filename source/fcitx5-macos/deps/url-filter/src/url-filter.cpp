#include "url-filter.hpp"
#include "rules.hpp"

namespace url_filter {
std::string filterTrackingParameters(std::string url) {
    for (const auto &rule : url_filter::rules) {
        auto result = rule.transform(url);
        if (result != url) {
            return result;
        }
    }
    return url;
}
} // namespace url_filter
