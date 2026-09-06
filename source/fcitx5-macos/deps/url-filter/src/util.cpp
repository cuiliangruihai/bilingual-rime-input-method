#include <regex>
#include <sstream>

namespace url_filter {

// https://developer.android.com/reference/java/net/URLDecoder
std::string urlDecode(const std::string &url) {
    std::ostringstream decoded;
    decoded << std::hex;

    for (auto it = url.begin(); it != url.end(); ++it) {
        char c = *it;
        if (c == '%') {
            if (std::distance(it, url.end()) >= 2) {
                std::string hex = {*(it + 1), *(it + 2)};
                decoded << static_cast<char>(std::stoi(hex, nullptr, 16));
                std::advance(it, 2);
            } else {
                continue;
            }
        } else if (c == '+') {
            decoded << ' ';
        } else {
            decoded << c;
        }
    }

    return decoded.str();
}

std::string decodeURL(const std::string &url) {
    auto temp = std::regex_replace(url, std::regex("\\+"), "%2B");
    return std::regex_replace(urlDecode(temp), std::regex("%2B"), "+");
}

std::vector<std::regex> compileRules(const std::vector<std::string> &rules) {
    std::vector<std::regex> compiledRules;
    compiledRules.reserve(rules.size());
    std::transform(rules.begin(), rules.end(),
                   std::back_inserter(compiledRules),
                   [](const std::string &rule) { return std::regex(rule); });
    return compiledRules;
}
} // namespace url_filter
