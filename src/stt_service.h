﻿/* Copyright (C) 2021 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef STT_SERVICE_H
#define STT_SERVICE_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "audio_source.h"
#include "dbus_stt_adaptor.h"
#include "deepspeech_wrapper.h"
#include "models_manager.h"

class stt_service : public QObject {
    Q_OBJECT

    // DBus
    Q_PROPERTY(int State READ dbus_state CONSTANT)
    Q_PROPERTY(bool Speech READ speech CONSTANT)
    Q_PROPERTY(QVariantMap Langs READ available_langs CONSTANT)
    Q_PROPERTY(QString DefaultLang READ default_lang WRITE set_default_lang)
    Q_PROPERTY(QVariantMap Models READ available_models CONSTANT)
    Q_PROPERTY(QString DefaultModel READ default_model WRITE set_default_model)
    Q_PROPERTY(int CurrentTask READ current_task_id CONSTANT)
    Q_PROPERTY(QVariantMap Translations READ translations CONSTANT)
   public:
    enum class state_type {
        unknown = 0,
        not_configured = 1,
        busy = 2,
        idle = 3,
        listening_manual = 4,
        listening_auto = 5,
        transcribing_file = 6,
        listening_single_sentence = 7
    };

    enum class speech_mode_type {
        automatic = 0,
        manual = 1,
        single_sentence = 2
    };

    enum class source_type { none = 0, mic = 1, file = 2 };

    enum class error_type { generic = 0, mic_source = 1, file_source = 2 };

    explicit stt_service(QObject *parent = nullptr);

    Q_INVOKABLE void download_model(const QString &id);
    Q_INVOKABLE void delete_model(const QString &id);

    Q_INVOKABLE int start_listen(stt_service::speech_mode_type mode,
                                 const QString &lang);
    Q_INVOKABLE int stop_listen(int task);
    Q_INVOKABLE int transcribe_file(const QString &file, const QString &lang);
    Q_INVOKABLE int cancel_file(int task);

   signals:
    void models_changed();
    void model_download_progress(const QString &id, double progress);
    void speech_changed();
    void speech_clear();
    void state_changed();
    void buff_ready();
    void transcribe_file_progress_changed(double progress, int task);
    void error(stt_service::error_type type);
    void file_transcribe_finished(int task);
    void intermediate_text_decoded(const QString &text, const QString &lang,
                                   int task);
    void text_decoded(const QString &text, const QString &lang, int task);
    void current_task_changed();

    // DBus
    void ErrorOccured(int code);
    void FileTranscribeFinished(int task);
    void FileTranscribeProgress(double progress, int task);
    void IntermediateTextDecoded(const QString &text, const QString &lang,
                                 int task);
    void StatePropertyChanged(int state);
    void TextDecoded(const QString &text, const QString &lang, int task);
    void SpeechPropertyChanged(bool speech);
    void LangsPropertyChanged(const QVariantMap &langs);
    void DefaultLangPropertyChanged(const QString &lang);
    void ModelsPropertyChanged(const QVariantMap &models);
    void DefaultModelPropertyChanged(const QString &model);
    void CurrentTaskPropertyChanged(int task);

   private:
    struct model_data_type {
        QString model_id;
        QString lang_id;
        QString name;
    };

    struct model_files_type {
        QString model_id;
        QString model_file;
        QString scorer_file;
    };

    struct task_type {
        int id = INVALID_TASK;
        speech_mode_type speech_mode;
        QString model_id;
    };

    inline static const QString DBUS_SERVICE_NAME{
        QStringLiteral("org.mkiol.Stt")};
    inline static const QString DBUS_SERVICE_PATH{QStringLiteral("/")};
    static const int SUCCESS = 0;
    static const int FAILURE = -1;
    static const int INVALID_TASK = -1;
    static const int DS_RESTART_TIME = 2000;           // 2s
    static const int KEEPALIVE_TIME = 60000;           // 60s
    static const int KEEPALIVE_TASK_TIME = 10000;      // 10s
    static const int SINGLE_SENTENCE_TIMEOUT = 10000;  // 10s

    int last_task_id = INVALID_TASK;
    std::unique_ptr<deepspeech_wrapper> ds;
    std::unique_ptr<audio_source> source;
    models_manager manager;
    std::map<QString, model_data_type> available_models_map;
    double progress_value = -1;
    state_type state_value = state_type::unknown;
    SttAdaptor stt_adaptor;
    QTimer ds_reset_timer;
    QTimer keepalive_timer;
    QTimer keepalive_current_task_timer;
    QTimer single_sentence_task_timer;
    int last_intermediate_text_task = INVALID_TASK;
    std::optional<task_type> previous_task;
    std::optional<task_type> current_task;
    std::optional<task_type> pending_task;

    QVariantMap available_models() const;
    QVariantMap available_langs() const;
    void handle_models_changed();
    void handle_text_decoded(const std::string &text);
    void handle_text_decoded(const QString &text, const QString &model_id,
                             int task_id);
    void handle_intermediate_text_decoded(const std::string &text);
    void handle_intermediate_text_decoded(const QString &text,
                                          const QString &model_id, int task_id);
    void handle_audio_available();
    void handle_speech_status_changed(bool speech_detected);
    void handle_processing_changed(bool processing);
    void handle_speech_clear();
    void handle_audio_error();
    void handle_audio_ended();
    QString restart_ds(speech_mode_type speech_mode, const QString &model_id);
    void restart_audio_source(const QString &source_file = {});
    void stop();
    [[nodiscard]] bool speech() const;
    [[nodiscard]] double transcribe_file_progress(int task) const;
    [[nodiscard]] source_type audio_source_type() const;
    void set_progress(double progress);
    std::optional<model_files_type> choose_model_files(
        const QString &model_id = {});
    inline bool recording() const { return static_cast<bool>(source); };
    inline state_type state() const { return state_value; };
    void refresh_status();
    static QString state_str(state_type state_value);
    inline int dbus_state() const { return static_cast<int>(state()); };
    void reset_ds_gracefully();
    void reset_ds();
    QString default_model() const;
    QString default_lang() const;
    QString test_default_model(const QString &lang) const;
    void set_default_model(const QString &model_id) const;
    void set_default_lang(const QString &lang_id) const;
    int next_task_id();
    inline int current_task_id() const {
        return current_task ? current_task->id : INVALID_TASK;
    }
    void handle_keepalive_timeout();
    void handle_task_timeout();
    QVariantMap translations() const;

    // DBus
    Q_INVOKABLE int StartListen(int mode, const QString &lang);
    Q_INVOKABLE int StopListen(int task);
    Q_INVOKABLE int CancelTranscribeFile(int task);
    Q_INVOKABLE int TranscribeFile(const QString &file, const QString &lang);
    Q_INVOKABLE double GetFileTranscribeProgress(int task);
    Q_INVOKABLE int KeepAliveService();
    Q_INVOKABLE int KeepAliveTask(int task);
    Q_INVOKABLE int Reload();
};

#endif  // STT_SERVICE_H
