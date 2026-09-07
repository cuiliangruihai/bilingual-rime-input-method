/*
 * SPDX-FileCopyrightText: 2026 Codex
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _FCITX_RIME_LEARNINGLOG_H_
#define _FCITX_RIME_LEARNINGLOG_H_

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace fcitx::rime {

/**
 * Appends committed Chinese text to one local JSON file per calendar day.
 *
 * The input thread only enqueues text. File parsing and atomic writes happen
 * on a worker thread so learning capture never blocks normal typing.
 */
class DailyLearningLog {
public:
    DailyLearningLog();
    ~DailyLearningLog();

    DailyLearningLog(const DailyLearningLog &) = delete;
    DailyLearningLog &operator=(const DailyLearningLog &) = delete;

    void append(std::string_view text);
    void stop();

private:
    void workerLoop();
    void appendToCurrentDay(std::string text);
    bool loadCurrentDay(std::string_view date);
    bool writeCurrentDay(std::string_view date);

    std::filesystem::path directory_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::string> pending_;
    bool stopping_ = false;
    std::thread worker_;

    std::string loadedDate_;
    std::string currentText_;
    bool loaded_ = false;
};

} // namespace fcitx::rime

#endif // _FCITX_RIME_LEARNINGLOG_H_

