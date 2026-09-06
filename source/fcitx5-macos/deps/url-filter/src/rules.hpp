#ifndef URL_FILTER_RULES_H
#define URL_FILTER_RULES_H

#include <algorithm>
#include <regex>
#include <vector>

namespace url_filter {

std::string decodeURL(const std::string &url);

std::vector<std::regex> compileRules(const std::vector<std::string> &rules);

// https://github.com/fcitx5-android/fcitx5-android/blob/ea2d5c8a439a4bc400ae976c5ded278f39c5ce12/plugin/clipboard-filter/src/main/java/org/fcitx/fcitx5/android/plugin/clipboard_filter/ClearURLs.kt
class Rule {
  public:
    Rule(std::string pattern, std::vector<std::string> rules,
         std::vector<std::string> rawRules, std::vector<std::string> exceptions,
         std::vector<std::string> redirects)
        : pattern(pattern), rules(rules), rawRules(compileRules(rawRules)),
          exceptions(compileRules(exceptions)),
          redirects(compileRules(redirects)) {}

    std::string transform(std::string url) const {
        if (!std::regex_search(url, pattern)) {
            return url;
        }
        for (const auto &exception : exceptions) {
            if (std::regex_search(url, exception)) {
                return url;
            }
        }
        for (const auto &redirect : redirects) {
            std::smatch matchResult;
            if (std::regex_search(url, matchResult, redirect) &&
                matchResult.size() > 1) {
                url = decodeURL(matchResult[1].str());
            }
        }
        for (const auto &rawRule : rawRules) {
            url = std::regex_replace(url, rawRule, "");
        }
        for (const auto &rule : rules) {
            url = std::regex_replace(
                url, std::regex("(?:&amp;|[/?#&])" + rule + "=[^&]*"), "");
        }
        return url;
    }

  private:
    std::regex pattern;
    std::vector<std::string> rules;
    std::vector<std::regex> rawRules;
    std::vector<std::regex> exceptions;
    std::vector<std::regex> redirects;
};

extern std::vector<Rule> rules;
} // namespace url_filter
#endif
