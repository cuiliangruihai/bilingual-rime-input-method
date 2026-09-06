/*
 * SPDX-FileCopyrightText: 2026 Codex
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "translation.h"
#include "rimeengine.h"
#include <curl/curl.h>
#include <fcitx-utils/log.h>
#include <fcitx/userinterface.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <utility>

extern char **environ;

namespace fcitx::rime {

std::string primaryTranslation(std::string_view translation) {
    std::string value(translation);
    if (const auto separator = value.find(';');
        separator != std::string::npos) {
        value.erase(separator);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

class TranslationProvider {
public:
    virtual ~TranslationProvider() = default;
    virtual std::string translate(std::string_view source) = 0;
};

namespace {

using json = nlohmann::json;

size_t writeData(char *data, size_t size, size_t count, void *userData) {
    auto *output = static_cast<std::string *>(userData);
    output->append(data, size * count);
    return size * count;
}

std::string envOr(std::string_view name, std::string fallback) {
    const auto *value = std::getenv(std::string(name).c_str());
    return value && *value ? value : std::move(fallback);
}

std::string trim(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(),
                             [&](unsigned char c) { return !isSpace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char c) { return !isSpace(c); })
                    .base(),
                value.end());
    return value;
}

std::string cleanTranslation(std::string value) {
    value = trim(std::move(value));
    if (value.starts_with("```") && value.ends_with("```")) {
        value = trim(value.substr(3, value.size() - 6));
    }
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = trim(value.substr(1, value.size() - 2));
    }
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (value == "N/A" || value == "n/a" || value == "No translation" ||
        value == "no translation" || value == "无" ||
        lower.find("nothing to translate") != std::string::npos ||
        lower.find("no meaningful translation") != std::string::npos ||
        lower.find("cannot translate") != std::string::npos ||
        lower.find("can't translate") != std::string::npos ||
        lower.find("unable to translate") != std::string::npos) {
        return {};
    }
    return value;
}

bool containsCjk(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        uint32_t codePoint = 0;
        size_t width = 0;
        if (first < 0x80) {
            ++i;
            continue;
        }
        if ((first & 0xe0) == 0xc0 && i + 1 < text.size()) {
            codePoint = first & 0x1f;
            width = 2;
        } else if ((first & 0xf0) == 0xe0 && i + 2 < text.size()) {
            codePoint = first & 0x0f;
            width = 3;
        } else if ((first & 0xf8) == 0xf0 && i + 3 < text.size()) {
            codePoint = first & 0x07;
            width = 4;
        } else {
            ++i;
            continue;
        }
        for (size_t j = 1; j < width; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xc0) != 0x80) {
                width = 0;
                break;
            }
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if (width != 0 &&
            ((codePoint >= 0x3400 && codePoint <= 0x4dbf) ||
             (codePoint >= 0x4e00 && codePoint <= 0x9fff) ||
             (codePoint >= 0xf900 && codePoint <= 0xfaff))) {
            return true;
        }
        i += width != 0 ? width : 1;
    }
    return false;
}

std::optional<std::string> postJson(const std::string &url, const json &body,
                                    const std::string &authorization = {}) {
    static const auto curlInitialized = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)curlInitialized;

    CURL *curl = curl_easy_init();
    if (!curl) {
        return std::nullopt;
    }

    std::string response;
    const auto request = body.dump();
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!authorization.empty()) {
        const auto header = "Authorization: Bearer " + authorization;
        headers = curl_slist_append(headers, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 250L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 4500L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || status < 200 || status >= 300) {
        return std::nullopt;
    }
    return response;
}

bool looksLikeSentence(std::string_view text) {
    size_t cjkCount = 0;
    bool hasSentencePunctuation = false;
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            if (text[i] == '.' || text[i] == ',' || text[i] == '!' ||
                text[i] == '?' || text[i] == ':' || text[i] == ';' ||
                text[i] == '\n') {
                hasSentencePunctuation = true;
            }
            ++i;
            continue;
        }
        uint32_t codePoint = 0;
        size_t width = 0;
        if ((first & 0xe0) == 0xc0 && i + 1 < text.size()) {
            codePoint = first & 0x1f;
            width = 2;
        } else if ((first & 0xf0) == 0xe0 && i + 2 < text.size()) {
            codePoint = first & 0x0f;
            width = 3;
        } else if ((first & 0xf8) == 0xf0 && i + 3 < text.size()) {
            codePoint = first & 0x07;
            width = 4;
        }
        if (width == 0 || i + width > text.size()) {
            ++i;
            continue;
        }
        for (size_t j = 1; j < width; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xc0) != 0x80) {
                width = 0;
                break;
            }
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if (width != 0 &&
            ((codePoint >= 0x3400 && codePoint <= 0x4dbf) ||
             (codePoint >= 0x4e00 && codePoint <= 0x9fff) ||
             (codePoint >= 0xf900 && codePoint <= 0xfaff))) {
            ++cjkCount;
        }
        if (width != 0 &&
            (codePoint == 0x3002 || codePoint == 0xff01 ||
             codePoint == 0xff1f || codePoint == 0xff1a ||
             codePoint == 0xff1b || codePoint == 0x3001)) {
            hasSentencePunctuation = true;
        }
        i += width != 0 ? width : 1;
    }
    return cjkCount >= 6 || hasSentencePunctuation;
}

class AppleTranslationProvider final : public TranslationProvider {
public:
    ~AppleTranslationProvider() override { stop(); }

    std::string translate(std::string_view source) override {
        if (!start()) {
            return {};
        }
        std::string request(source);
        std::replace(request.begin(), request.end(), '\n', ' ');
        // Apple's on-device translator is conservative with isolated words
        // and short phrases.  A Chinese full stop gives it enough sentence
        // context to translate candidates such as "应用于", while the
        // punctuation is removed from the English result below.
        const bool addedContext = !looksLikeSentence(source);
        if (addedContext) {
            request.append("\xE3\x80\x82");
        }
        request.push_back('\n');
        size_t written = 0;
        while (written < request.size()) {
            const auto count = ::write(inputFd_, request.data() + written,
                                       request.size() - written);
            if (count <= 0) {
                stop();
                return {};
            }
            written += static_cast<size_t>(count);
        }

        std::string response;
        char ch = 0;
        while (response.size() < 4096) {
            struct pollfd descriptor{outputFd_, POLLIN, 0};
            // The first call can include macOS Translation service warm-up.
            // This worker is off the input thread, so a longer timeout does
            // not add latency to typing and prevents the first candidate
            // from being permanently shown with an empty English row.
            if (::poll(&descriptor, 1, 5000) <= 0 ||
                !(descriptor.revents & POLLIN)) {
                stop();
                return {};
            }
            const auto count = ::read(outputFd_, &ch, 1);
            if (count <= 0) {
                stop();
                return {};
            }
            if (ch == '\n') {
                break;
            }
            response.push_back(ch);
        }
        auto translation = cleanTranslation(std::move(response));
        if (addedContext && !translation.empty() && translation.back() == '.') {
            translation.pop_back();
            translation = trim(std::move(translation));
        }
        return translation;
    }

private:
    bool start() {
        if (pid_ > 0) {
            return true;
        }

        const auto helper = envOr(
            "RIME_APPLE_TRANSLATION_HELPER",
            "/Library/Input Methods/Fcitx5.app/Contents/MacOS/Fcitx5TranslationHelper");
        int inputPipe[2] = {-1, -1};
        int outputPipe[2] = {-1, -1};
        if (::pipe(inputPipe) != 0 || ::pipe(outputPipe) != 0) {
            if (inputPipe[0] >= 0) {
                ::close(inputPipe[0]);
                ::close(inputPipe[1]);
            }
            return false;
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, inputPipe[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, inputPipe[1]);
        posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
        char *const argv[] = {const_cast<char *>(helper.c_str()), nullptr};
        pid_t child = 0;
        const auto result = posix_spawn(&child, helper.c_str(), &actions,
                                        nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(inputPipe[0]);
        ::close(outputPipe[1]);
        if (result != 0) {
            ::close(inputPipe[1]);
            ::close(outputPipe[0]);
            return false;
        }
        inputFd_ = inputPipe[1];
        outputFd_ = outputPipe[0];
        // A helper can exit when macOS unloads the Translation service. Avoid
        // turning that normal failure into a SIGPIPE that kills Fcitx5.
        ::signal(SIGPIPE, SIG_IGN);
        pid_ = child;
        return true;
    }

    void stop() {
        if (inputFd_ >= 0) {
            ::close(inputFd_);
            inputFd_ = -1;
        }
        if (outputFd_ >= 0) {
            ::close(outputFd_);
            outputFd_ = -1;
        }
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
            ::waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
    }

    pid_t pid_ = -1;
    int inputFd_ = -1;
    int outputFd_ = -1;
};

/**
 * Optional online provider. The normal path is the Rime schema's
 * dict_comment_filter, which reads CC-CEDICT locally and supplies candidate
 * comments synchronously. This provider is only used when the user explicitly
 * sets RIME_TRANSLATOR_PROVIDER=online and supplies an endpoint.
 */
class OnlineHttpProvider final : public TranslationProvider {
public:
    std::string translate(std::string_view source) override {
        const auto endpoint =
            envOr("RIME_ONLINE_TRANSLATION_URL", "");
        if (endpoint.empty()) {
            return {};
        }
        const auto body = json{{"text", source},
                               {"source", "zh"},
                               {"target", "en"}};
        const auto response = postJson(
            endpoint, body, envOr("RIME_ONLINE_TRANSLATION_TOKEN", ""));
        if (!response) {
            return {};
        }
        try {
            const auto value = json::parse(*response);
            for (const auto *key : {"translation", "translatedText"}) {
                if (value.contains(key) && value[key].is_string()) {
                    return cleanTranslation(value[key].get<std::string>());
                }
            }
            if (value.contains("data") && value["data"].is_object() &&
                value["data"].contains("translation") &&
                value["data"]["translation"].is_string()) {
                return cleanTranslation(
                    value["data"]["translation"].get<std::string>());
            }
        } catch (const std::exception &e) {
            FCITX_WARN() << "Unable to parse online translation response: "
                         << e.what();
        }
        return {};
    }
};

std::unique_ptr<TranslationProvider> createProvider() {
    const auto provider = envOr("RIME_TRANSLATOR_PROVIDER", "apple");
    if (provider == "online") {
        return std::make_unique<OnlineHttpProvider>();
    }
    if (provider == "apple" || provider == "hybrid") {
        return std::make_unique<AppleTranslationProvider>();
    }
    // CC-CEDICT is handled by dict_comment_filter.lua in the active Rime
    // schema. Explicitly selecting dictionary disables all fallbacks.
    return nullptr;
}

} // namespace

TranslationCache::TranslationCache(RimeEngine *engine)
    : engine_(engine), provider_(createProvider()) {
    if (provider_) {
        worker_ = std::thread([this] { workerLoop(); });
    }
}

TranslationCache::~TranslationCache() { stop(); }

std::optional<std::string>
TranslationCache::lookup(std::string_view source) const {
    std::lock_guard lock(mutex_);
    auto iter = translations_.find(std::string(source));
    if (iter == translations_.end() ||
        iter->second.expiresAt <= std::chrono::steady_clock::now()) {
        return std::nullopt;
    }
    return iter->second.value;
}

void TranslationCache::remember(std::string source, std::string translation) {
    if (source.empty() || translation.empty()) {
        return;
    }
    save(std::move(source), std::move(translation));
}

void TranslationCache::request(std::string source, InputContext *inputContext) {
    // Dictionary comments are still preferred and are supplied synchronously
    // by Rime.  Every remaining Chinese candidate is queued so short words
    // and phrases also receive a translation instead of silently staying
    // blank.  The provider remains asynchronous, so typing is never blocked.
    if (!provider_ || source.empty() || !containsCjk(source) ||
        !inputContext) {
        return;
    }
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (auto iter = translations_.find(source);
        iter != translations_.end()) {
        if (iter->second.expiresAt > now) {
            return;
        }
        translations_.erase(iter);
    }
    if (pending_.contains(source) || stopping_) {
        return;
    }
    pending_.insert(source);
    jobs_.push_back({std::move(source), inputContext->watch()});
    condition_.notify_one();
}

std::string TranslationCache::translateNow(std::string_view source) {
    // Candidate selection must never block on a network or local-model call.
    // Translation is requested by the candidate list and completed by the
    // worker thread; this method is retained as a non-blocking compatibility
    // wrapper for callers outside that path.
    if (auto cached = lookup(source)) {
        return *cached;
    }
    return {};
}

void TranslationCache::save(std::string source, std::string translation) {
    std::lock_guard lock(mutex_);
    pending_.erase(source);
    const auto ttl = translation.empty() ? std::chrono::seconds(10)
                                         : std::chrono::hours(24);
    translations_[std::move(source)] =
        {std::move(translation), std::chrono::steady_clock::now() + ttl};
}

void TranslationCache::workerLoop() {
    if (!provider_) {
        return;
    }
    while (true) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock,
                            [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        auto translation = provider_->translate(job.source);
        save(job.source, translation);

        auto inputContext = job.inputContext;
        engine_->eventDispatcher().scheduleWithContext(
            inputContext, [inputContext] {
                if (auto *ic = inputContext.get()) {
                    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
                }
            });
    }
}

void TranslationCache::stop() {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace fcitx::rime
