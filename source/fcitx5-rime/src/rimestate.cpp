/*
 * SPDX-FileCopyrightText: 2017~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "rimestate.h"
#include "rimeaction.h"
#include "rimecandidate.h"
#include "rimeengine.h"
#include "rimesession.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/stringutils.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <rime_api.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fcitx::rime {

namespace {

bool emptyExceptAux(const InputPanel &inputPanel) {

    return inputPanel.preedit().empty() && inputPanel.clientPreedit().empty() &&
           (!inputPanel.candidateList() || inputPanel.candidateList()->empty());
}

} // namespace

RimeState::RimeState(RimeEngine *engine, InputContext &ic)
    : engine_(engine), ic_(ic) {}

RimeState::~RimeState() {}

RimeSessionId RimeState::session(bool requestNewSession) {
    if (!session_ && requestNewSession) {
        auto [sessionHolder, isNewSession] =
            engine_->sessionPool().requestSession(&ic_);
        session_ = sessionHolder;
        if (isNewSession) {
            restore();
            // Rime can persist ascii_mode in the user database.  This input
            // method is intentionally Chinese-first: users should be able to
            // type pinyin immediately after switching to Fcitx5, while the
            // normal Rime shortcut can still switch to Latin mode later.
            engine_->api()->set_option(session_->id(), RIME_ASCII_MODE, false);
        } else {
            savedCurrentSchema_.clear();
            savedOptions_.clear();
        }
    }
    if (!session_) {
        return 0;
    }

    return session_->id();
}

void RimeState::clear() {
    if (auto session = this->session()) {
        engine_->api()->clear_composition(session);
    }
}

void RimeState::activate() { maybeSyncProgramNameToSession(); }

std::string RimeState::subMode() {
    std::string result;
    getStatus([&result](const RimeStatus &status) {
        if (status.is_disabled) {
            result = "\xe2\x8c\x9b";
        } else if (status.is_ascii_mode) {
            result = _("Latin Mode");
        } else if (status.schema_name && status.schema_name[0] != '.') {
            result = status.schema_name;
        }
    });
    return result;
}

std::string RimeState::subModeLabel() {
    std::string result;
    getStatus([&result](const RimeStatus &status) {
        if (status.is_disabled) {
            result = "";
        } else if (status.is_ascii_mode) {
            result = "A";
        } else if (status.schema_name && status.schema_name[0] != '.') {
            result = status.schema_name;
            if (!result.empty() &&
                utf8::lengthValidated(result) != utf8::INVALID_LENGTH) {
                result = result.substr(
                    0, std::distance(result.begin(),
                                     utf8::nextChar(result.begin())));
            }
        }
    });
    return result;
}

std::string RimeState::currentSchema() {
    std::string schema;
    getStatus([&schema](const RimeStatus &status) {
        if (status.schema_id) {
            schema = status.schema_id;
        }
    });
    return schema;
}

void RimeState::toggleLatinMode() {
    auto *api = engine_->api();
    if (api->is_maintenance_mode()) {
        return;
    }

    Bool oldValue = api->get_option(session(), RIME_ASCII_MODE);
    api->set_option(session(), RIME_ASCII_MODE, !oldValue);
}

void RimeState::setLatinMode(bool latin) {
    auto *api = engine_->api();
    if (api->is_maintenance_mode()) {
        return;
    }
    api->set_option(session(), RIME_ASCII_MODE, latin);
}

void RimeState::recordChinese(std::string_view text) {
    if (engine_->learningLogEnabled()) {
        engine_->learningLog().append(text);
    }
}

void RimeState::commitAndRecord(InputContext *inputContext,
                                std::string_view text) {
    recordChinese(text);
    inputContext->commitString(std::string(text));
}

void RimeState::selectSchema(const std::string &schema) {
    auto *api = engine_->api();
    if (api->is_maintenance_mode()) {
        return;
    }
    api->set_option(session(), RIME_ASCII_MODE, false);
    api->select_schema(session(), schema.data());
}

void RimeState::commitSelected(InputContext *inputContext,
                               std::string_view chinese,
                               std::string_view translation, bool english) {
    // Record the original Chinese even when the user chooses to output its
    // English translation.  The learning pipeline will later decide how to
    // group and analyze the day's text.
    recordChinese(chinese);
    if (!english) {
        inputContext->commitString(std::string(chinese));
        return;
    }

    std::string englishText(translation);
    if (englishText.empty()) {
        if (auto cached = engine_->translations().lookup(chinese);
            cached && !cached->empty()) {
            englishText = *cached;
        } else {
            engine_->translations().request(std::string(chinese),
                                            inputContext);
        }
    }
    englishText = primaryTranslation(std::move(englishText));

    // A missing translation is kept as a safe Chinese fallback.  The
    // candidate remains selectable and the asynchronous cache can fill the
    // English row for the next composition.
    inputContext->commitString(englishText.empty() ? std::string(chinese)
                                                   : englishText);
}

void RimeState::keyEvent(KeyEvent &event) {
    changedOptions_.clear();
    auto *ic = event.inputContext();
    const bool optionKey = event.rawKey().sym() == FcitxKey_Alt_L ||
                           event.rawKey().sym() == FcitxKey_Alt_R ||
                           event.key().sym() == FcitxKey_Alt_L ||
                           event.key().sym() == FcitxKey_Alt_R;
    // For key-release, composeResult will always be empty string, which feed
    // into engine directly.
    std::string composeResult;
    if (!optionKey && !event.key().states().testAny(
            KeyStates{KeyState::Ctrl, KeyState::Super}) &&
        !event.isRelease()) {
        auto compose =
            engine_->instance()->processComposeString(&ic_, event.key().sym());
        if (!compose) {
            event.filterAndAccept();
            return;
        }
        composeResult = *compose;
    }

    auto *api = engine_->api();
    if (api->is_maintenance_mode()) {
        return;
    }
    auto session = this->session();
    if (!session) {
        return;
    }

    const auto keyStates = event.key().states();
    const auto rawStates = event.rawKey().states();
    const bool optionHeld = keyStates.test(KeyState::Alt) ||
                             rawStates.test(KeyState::Alt);

    // A modifier-only event is used to give immediate visual feedback while
    // Option is held. Do not swallow Option when there is no active menu, so
    // application-level Option shortcuts remain available.
    if (optionKey) {
        bool hasCandidates = false;
        RIME_STRUCT(RimeContext, context);
        if (api->get_context(session, &context)) {
            hasCandidates = context.menu.num_candidates > 0;
            api->free_context(&context);
        }
        const bool wasPreviewing = englishPreview_;
        englishPreview_ = !event.isRelease();
        if (hasCandidates || wasPreviewing) {
            event.filterAndAccept();
            updateUI(ic, false);
            return;
        }
        englishPreview_ = false;
    } else if (!optionHeld) {
        englishPreview_ = false;
    }

    // Option+Space and Option+number are explicit English alternatives to
    // Rime's normal Chinese selection keys. Handling them here makes the
    // behavior independent of the active schema's modifier bindings.
    if (optionHeld && !event.isRelease()) {
        const auto sym = event.rawKey().sym();
        int candidateIndex = -1;
        if (sym == FcitxKey_space) {
            RIME_STRUCT(RimeContext, context);
            if (api->get_context(session, &context)) {
                candidateIndex = context.menu.highlighted_candidate_index >= 0
                                     ? context.menu.highlighted_candidate_index
                                     : 0;
                if (candidateIndex >= context.menu.num_candidates) {
                    candidateIndex = -1;
                }
                std::string translation;
                if (candidateIndex >= 0 && context.menu.candidates &&
                    context.menu.candidates[candidateIndex].comment) {
                    translation =
                        context.menu.candidates[candidateIndex].comment;
                }
                if (candidateIndex >= 0) {
                    api->select_candidate_on_current_page(session,
                                                          candidateIndex);
                    RIME_STRUCT(RimeCommit, commit);
                    if (api->get_commit(session, &commit)) {
                        commitSelected(ic, commit.text, translation, true);
                        api->free_commit(&commit);
                        engine_->instance()->resetCompose(ic);
                    }
                    api->free_context(&context);
                    event.filterAndAccept();
                    updateUI(ic, false);
                    return;
                }
                api->free_context(&context);
            }
        } else {
            int digit = -1;
            if (sym >= FcitxKey_1 && sym <= FcitxKey_9) {
                digit = static_cast<int>(sym - FcitxKey_1);
            } else if (sym == FcitxKey_0) {
                digit = 9;
            }
            if (digit >= 0) {
                RIME_STRUCT(RimeContext, context);
                if (api->get_context(session, &context)) {
                    int index = digit;
                    if (context.menu.select_keys &&
                        context.menu.select_keys[0]) {
                        const char typed = static_cast<char>('1' + digit);
                        if (digit == 9) {
                            if (const char *found = std::strchr(
                                    context.menu.select_keys, '0')) {
                                index = static_cast<int>(
                                    found - context.menu.select_keys);
                            }
                        } else if (const char *found = std::strchr(
                                       context.menu.select_keys, typed)) {
                            index = static_cast<int>(found -
                                                     context.menu.select_keys);
                        }
                    }
                    if (index >= 0 && index < context.menu.num_candidates) {
                        std::string translation;
                        if (context.menu.candidates &&
                            context.menu.candidates[index].comment) {
                            translation =
                                context.menu.candidates[index].comment;
                        }
                        api->select_candidate_on_current_page(session, index);
                        RIME_STRUCT(RimeCommit, commit);
                        if (api->get_commit(session, &commit)) {
                            commitSelected(ic, commit.text, translation, true);
                            api->free_commit(&commit);
                            engine_->instance()->resetCompose(ic);
                        }
                        api->free_context(&context);
                        event.filterAndAccept();
                        updateUI(ic, false);
                        return;
                    }
                    api->free_context(&context);
                }
            }
        }
    }

    maybeSyncProgramNameToSession();
    lastMode_ = subMode();

    std::string lastSchema = currentSchema();
    auto states = event.rawKey().states() &
                  KeyStates{KeyState::Mod1, KeyState::CapsLock, KeyState::Shift,
                            KeyState::Ctrl, KeyState::Super};
    if (states.test(KeyState::Super)) {
        // IBus uses virtual super mask.
        states |= KeyState::Super2;
    }
    uint32_t intStates = states;
    if (event.isRelease()) {
        // IBUS_RELEASE_MASK
        intStates |= (1 << 30);
    }
    if (!composeResult.empty()) {
        event.filterAndAccept();
        auto length = utf8::lengthValidated(composeResult);
        bool result = false;
        if (length == 1) {
            auto c = utf8::getChar(composeResult);
            auto sym = Key::keySymFromUnicode(c);
            if (sym != FcitxKey_None) {
                result = api->process_key(session, sym, intStates);
            }
        }
        if (!result) {
            commitPreedit(ic);
            commitAndRecord(ic, composeResult);
            clear();
        }
    } else {
        auto result =
            api->process_key(session, event.rawKey().sym(), intStates);
        if (result) {
            event.filterAndAccept();
        }
    }

    RIME_STRUCT(RimeCommit, commit);
    if (api->get_commit(session, &commit)) {
        commitSelected(ic, commit.text, {}, optionHeld && !event.isRelease());
        api->free_commit(&commit);
        engine_->instance()->resetCompose(ic);
    }

    updateUI(ic, event.isRelease());
    if (!event.isRelease() && !lastSchema.empty() &&
        lastSchema == currentSchema() && ic->inputPanel().empty() &&
        !changedOptions_.empty()) {
        showChangedOptions();
    }
}

void RimeState::selectCandidate(InputContext *inputContext, int idx,
                                bool global, std::string_view translation,
                                bool english) {
    auto *api = engine_->api();
    if (api->is_maintenance_mode()) {
        return;
    }
    auto session = this->session();
    if (!session) {
        return;
    }
    if (global) {
        api->select_candidate(session, idx);
    } else {
        api->select_candidate_on_current_page(session, idx);
    }
    RIME_STRUCT(RimeCommit, commit);
    if (api->get_commit(session, &commit)) {
        commitSelected(inputContext, commit.text, translation, english);
        api->free_commit(&commit);
    }
    updateUI(inputContext, false);
}

#ifndef FCITX_RIME_NO_DELETE_CANDIDATE
void RimeState::deleteCandidate(int idx, bool global) {
    auto *api = engine_->api();
    if (api->is_maintenance_mode()) {
        return;
    }
    auto session = this->session();
    if (!session) {
        return;
    }
    if (global) {
        api->delete_candidate(session, idx);
    } else {
        api->delete_candidate_on_current_page(session, idx);
    }
    updateUI(&ic_, false);
}
#endif

bool RimeState::getStatus(
    const std::function<void(const RimeStatus &)> &callback) {
    auto *api = engine_->api();
    auto session = this->session();
    if (!session) {
        return false;
    }
    RIME_STRUCT(RimeStatus, status);
    if (!api->get_status(session, &status)) {
        return false;
    }
    callback(status);
    api->free_status(&status);
    return true;
}

Text preeditFromRimeContext(const RimeContext &context, TextFormatFlags flag,
                            TextFormatFlags highlightFlag) {
    Text preedit;

    do {
        if (context.composition.length == 0) {
            break;
        }

        // validation.
        if (!(context.composition.sel_start >= 0 &&
              context.composition.sel_start <= context.composition.sel_end &&
              context.composition.sel_end <= context.composition.length)) {
            break;
        }

        /* converted text */
        if (context.composition.sel_start > 0) {
            preedit.append(std::string(context.composition.preedit,
                                       context.composition.sel_start),
                           flag);
        }

        /* converting candidate */
        if (context.composition.sel_start < context.composition.sel_end) {
            preedit.append(
                std::string(
                    &context.composition.preedit[context.composition.sel_start],
                    &context.composition.preedit[context.composition.sel_end]),
                flag | highlightFlag);
        }

        /* remaining input to convert */
        if (context.composition.sel_end < context.composition.length) {
            preedit.append(
                std::string(
                    &context.composition.preedit[context.composition.sel_end],
                    &context.composition.preedit[context.composition.length]),
                flag);
        }

        preedit.setCursor(context.composition.cursor_pos);
    } while (0);

    return preedit;
}

void RimeState::updatePreedit(InputContext *ic, const RimeContext &context) {
    PreeditMode mode = ic->capabilityFlags().test(CapabilityFlag::Preedit)
                           ? *engine_->config().preeditMode
                           : PreeditMode::No;

    switch (mode) {
    case PreeditMode::No:
        ic->inputPanel().setPreedit(preeditFromRimeContext(
            context, TextFormatFlag::NoFlag, TextFormatFlag::NoFlag));
        ic->inputPanel().setClientPreedit(Text());
        break;
    case PreeditMode::CommitPreview: {
        ic->inputPanel().setPreedit(preeditFromRimeContext(
            context, TextFormatFlag::NoFlag, TextFormatFlag::NoFlag));
        if (context.composition.length > 0 && context.commit_text_preview) {
            Text clientPreedit;
            clientPreedit.append(context.commit_text_preview,
                                 TextFormatFlag::Underline);
            if (*engine_->config().preeditCursorPositionAtBeginning) {
                clientPreedit.setCursor(0);
            } else {
                clientPreedit.setCursor(clientPreedit.textLength());
            }
            ic->inputPanel().setClientPreedit(clientPreedit);
        } else {
            ic->inputPanel().setClientPreedit(Text());
        }
    } break;
    case PreeditMode::ComposingText: {
        // Keep the composing text in the input panel as well as in the
        // client-side preedit.  macOS uses clientPreedit for inline display,
        // while the custom bilingual web panel reads inputPanel.preedit().
        // Without both values the host shows the marked text but the panel's
        // large preedit box remains hidden.
        ic->inputPanel().setPreedit(
            preeditFromRimeContext(context, TextFormatFlag::NoFlag,
                                   TextFormatFlag::NoFlag));
        const TextFormatFlag highlightFlag =
            *engine_->config().preeditCursorPositionAtBeginning
                ? TextFormatFlag::HighLight
                : TextFormatFlag::NoFlag;
        Text clientPreedit = preeditFromRimeContext(
            context, TextFormatFlag::Underline, highlightFlag);
        if (*engine_->config().preeditCursorPositionAtBeginning) {
            clientPreedit.setCursor(0);
        }
        ic->inputPanel().setClientPreedit(clientPreedit);
    } break;
    }
}

void RimeState::updateUI(InputContext *ic, bool keyRelease) {
    auto &inputPanel = ic->inputPanel();
    if (!keyRelease) {
        inputPanel.reset();
    }
    bool oldEmptyExceptAux = emptyExceptAux(inputPanel);

    do {
        auto *api = engine_->api();
        if (api->is_maintenance_mode()) {
            return;
        }
        auto session = this->session();
        if (!api->find_session(session)) {
            return;
        }

        RIME_STRUCT(RimeContext, context);
        if (!api->get_context(session, &context)) {
            break;
        }

        updatePreedit(ic, context);

        if (context.menu.num_candidates) {
            ic->inputPanel().setCandidateList(
                std::make_unique<RimeCandidateList>(engine_, ic, context));
        } else {
            ic->inputPanel().setCandidateList(nullptr);
        }

        api->free_context(&context);
    } while (false);

    ic->updatePreedit();
    // HACK: for show input method information.
    // Since we don't use aux, which is great for this hack.
    bool newEmptyExceptAux = emptyExceptAux(inputPanel);
    // If it's key release and old information is not "empty", do the rest of
    // "reset".
    if (keyRelease && !newEmptyExceptAux) {
        inputPanel.setAuxUp(Text());
        inputPanel.setAuxDown(Text());
    }
    if (newEmptyExceptAux && lastMode_ != subMode()) {
        engine_->instance()->showInputMethodInformation(ic);
        ic->updateUserInterface(UserInterfaceComponent::StatusArea);
    }

    if (!keyRelease || !oldEmptyExceptAux || !newEmptyExceptAux) {
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
}

void RimeState::release() { session_.reset(); }

void RimeState::commitInput(InputContext *ic) {
    if (auto *api = engine_->api()) {
        if (const char *input = api->get_input(this->session())) {
            if (std::strlen(input) > 0) {
                commitAndRecord(ic, input);
            }
        }
    }
}

void RimeState::commitComposing(InputContext *ic) {
    if (auto *api = engine_->api()) {
        RIME_STRUCT(RimeContext, context);
        auto session = this->session();
        if (!api->get_context(session, &context)) {
            return;
        }
        if (context.composition.length > 0) {
            commitAndRecord(ic, context.composition.preedit);
        }
        api->free_context(&context);
    }
}

void RimeState::commitPreedit(InputContext *ic) {
    if (auto *api = engine_->api()) {
        RIME_STRUCT(RimeContext, context);
        auto session = this->session();
        if (!api->get_context(session, &context)) {
            return;
        }
        if (context.composition.length > 0 && context.commit_text_preview) {
            commitAndRecord(ic, context.commit_text_preview);
        }
        api->free_context(&context);
    }
}

void RimeState::snapshot() {
    if (!session(false)) {
        return;
    }
    getStatus([this](const RimeStatus &status) {
        if (!status.schema_id) {
            return;
        }
        savedCurrentSchema_ = status.schema_id;
        savedOptions_.clear();
        if (savedCurrentSchema_.empty()) {
            return;
        }

        savedOptions_ = snapshotOptions(savedCurrentSchema_);
    });
}

std::vector<std::string> RimeState::snapshotOptions(const std::string &schema) {
    if (schema.empty()) {
        return {};
    }
    std::vector<std::string> savedOptions;
    const auto &optionActions = engine_->optionActions();
    auto iter = optionActions.find(schema);
    if (iter == optionActions.end()) {
        return {};
    }
    for (const auto &option : iter->second) {
        if (auto savedOption = option->snapshotOption(&ic_)) {
            savedOptions.push_back(std::move(*savedOption));
        }
    }
    return savedOptions;
}

void RimeState::restore() {
    if (savedCurrentSchema_.empty()) {
        return;
    }
    if (!engine_->schemas().count(savedCurrentSchema_)) {
        return;
    }

    selectSchema(savedCurrentSchema_);
    for (const auto &option : savedOptions_) {
        if (option.starts_with("!")) {
            engine_->api()->set_option(session(), option.c_str() + 1, false);
        } else {
            engine_->api()->set_option(session(), option.c_str(), true);
        }
    }
}

void RimeState::maybeSyncProgramNameToSession() {
    // The program name is guranteed to be const through the Input Context
    // lifetime. There is no need to update it if the policy is not "All".
    if (engine_->sessionPool().propertyPropagatePolicy() !=
        PropertyPropagatePolicy::All) {
        return;
    }

    if (session_) {
        session_->setProgramName(ic_.program());
    }
}

void RimeState::addChangedOption(std::string_view option) {
    changedOptions_.push_back(std::string(option));
}
void RimeState::showChangedOptions() {

    std::string schema = currentSchema();
    if (schema.empty()) {
        return;
    }
    const auto &optionActions = engine_->optionActions();
    auto iter = optionActions.find(schema);
    if (iter == optionActions.end()) {
        return;
    }
    const auto &actions = iter->second;

    std::string labels;
    std::unordered_set<RimeOptionAction *> actionSet;
    std::vector<RimeOptionAction *> actionList;

    auto extractOptionName = [](std::string_view &option) {
        const bool state = (option.front() != '!');
        if (!state) {
            option.remove_prefix(1);
        }
        return state;
    };

    for (std::string_view option : changedOptions_) {
        if (option.empty()) {
            continue;
        }
        extractOptionName(option);
        // Skip internal options.
        if (option.starts_with("_")) {
            continue;
        }

        // This is hard coded latin-mode.
        if (option == "ascii_mode") {
            continue;
        }

        // Filter by action, so we know this option belongs to current schema.
        auto actionIter = std::find_if(
            actions.begin(), actions.end(),
            [option](const std::unique_ptr<RimeOptionAction> &action) {
                return action->checkOptionName(option);
            });
        if (actionIter == actions.end()) {
            continue;
        }
        if (actionSet.count(actionIter->get())) {
            continue;
        }
        actionSet.insert(actionIter->get());
        actionList.push_back(actionIter->get());
    }

    for (auto *action : actionList) {
        // Snapshot again, so SelectAction will return the current active value.
        auto label = action->optionLabel(&ic_);
        if (label.empty()) {
            continue;
        }
        if (!labels.empty()) {
            labels.append("|");
        }
        labels.append(label);
    }
    if (!labels.empty()) {
        engine_->instance()->showCustomInputMethodInformation(&ic_, labels);
    }
}
} // namespace fcitx::rime
