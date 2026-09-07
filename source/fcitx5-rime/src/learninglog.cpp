/*
 * SPDX-FileCopyrightText: 2026 Codex
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "learninglog.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <system_error>

namespace fcitx::rime {

namespace {

using json = nlohmann::json;

bool containsChinese(std::string_view text) {
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

std::string localDate() {
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
    if (!localtime_r(&now, &local)) {
        return "1970-01-01";
    }
    char buffer[sizeof("0000-00-00")];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local) == 0) {
        return "1970-01-01";
    }
    return buffer;
}

} // namespace

DailyLearningLog::DailyLearningLog()
    : directory_(StandardPaths::global().userDirectory(
                     StandardPathsType::PkgData) /
                 "rime" / "learning") {
    worker_ = std::thread([this] { workerLoop(); });
}

DailyLearningLog::~DailyLearningLog() { stop(); }

void DailyLearningLog::append(std::string_view text) {
    if (text.empty() || !containsChinese(text)) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        pending_.emplace_back(text);
    }
    condition_.notify_one();
}

void DailyLearningLog::workerLoop() {
    while (true) {
        std::string text;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock,
                            [this] { return stopping_ || !pending_.empty(); });
            if (stopping_ && pending_.empty()) {
                return;
            }
            text = std::move(pending_.front());
            pending_.pop_front();
        }
        appendToCurrentDay(std::move(text));
    }
}

void DailyLearningLog::appendToCurrentDay(std::string text) {
    const auto date = localDate();
    if (!loaded_ || loadedDate_ != date) {
        if (!loadCurrentDay(date)) {
            return;
        }
    }

    if (!currentText_.empty() && currentText_.back() != '\n') {
        currentText_.push_back('\n');
    }
    currentText_.append(std::move(text));
    if (!writeCurrentDay(date)) {
        FCITX_WARN() << "Unable to write daily learning log for " << date;
    }
}

bool DailyLearningLog::loadCurrentDay(std::string_view date) {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        FCITX_WARN() << "Unable to create learning log directory: "
                     << error.message();
        return false;
    }

    const auto path = directory_ / (std::string(date) + ".json");
    currentText_.clear();
    std::ifstream input(path);
    if (input) {
        try {
            const auto document = json::parse(input);
            if (document.is_object() && document.contains("text") &&
                document["text"].is_string()) {
                currentText_ = document["text"].get<std::string>();
            }
        } catch (const std::exception &exception) {
            FCITX_WARN() << "Unable to parse daily learning log " << path
                         << ": " << exception.what();
            return false;
        }
    }

    loadedDate_ = date;
    loaded_ = true;
    return true;
}

bool DailyLearningLog::writeCurrentDay(std::string_view date) {
    const auto path = directory_ / (std::string(date) + ".json");
    const auto temporaryPath = directory_ / (std::string(date) + ".json.tmp");
    const json document = {{"date", std::string(date)},
                           {"text", currentText_}};

    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output) {
            return false;
        }
        output << document.dump(2) << '\n';
        if (!output.good()) {
            return false;
        }
    }

    // Learning data is personal text. Keep the file readable only by the
    // current user even when the system umask is permissive.
    ::chmod(temporaryPath.c_str(), S_IRUSR | S_IWUSR);

    std::error_code error;
    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        FCITX_WARN() << "Unable to atomically replace learning log " << path
                     << ": " << error.message();
        return false;
    }
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
    return true;
}

void DailyLearningLog::stop() {
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
