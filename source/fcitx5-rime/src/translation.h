/*
 * SPDX-FileCopyrightText: 2026 Codex
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_RIME_TRANSLATION_H_
#define _FCITX_RIME_TRANSLATION_H_

#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/inputcontext.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fcitx::rime {

class RimeEngine;
class TranslationProvider;

// Keep candidate display and English output consistent when a dictionary
// returns several meanings separated by semicolons.
std::string primaryTranslation(std::string_view translation);

/**
 * Small cache used by the Rime candidate list for an optional local/online
 * translation provider.
 *
 * The normal translation path is dict_comment_filter.lua, which reads
 * CC-CEDICT locally and places exact dictionary definitions in candidate
 * comments. The default fallback uses Apple's local Translation framework
 * through Fcitx5TranslationHelper for short phrases and sentences. An online
 * HTTP provider remains available only when explicitly selected.
 */
class TranslationCache {
public:
    explicit TranslationCache(RimeEngine *engine);
    ~TranslationCache();

    TranslationCache(const TranslationCache &) = delete;
    TranslationCache &operator=(const TranslationCache &) = delete;

    std::optional<std::string> lookup(std::string_view source) const;
    void remember(std::string source, std::string translation);
    void request(std::string source, InputContext *inputContext);
    std::string translateNow(std::string_view source);
    void stop();

private:
    struct Job {
        std::string source;
        TrackableObjectReference<InputContext> inputContext;
    };

    void workerLoop();
    void save(std::string source, std::string translation);

    struct CacheEntry {
        std::string value;
        std::chrono::steady_clock::time_point expiresAt;
    };

    RimeEngine *engine_;
    std::unique_ptr<TranslationProvider> provider_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<std::string, CacheEntry> translations_;
    std::unordered_set<std::string> pending_;
    std::deque<Job> jobs_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace fcitx::rime

#endif // _FCITX_RIME_TRANSLATION_H_
