﻿/* Copyright (C) 2021 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "models_manager.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonArray>
#include <QJsonObject>
#include <QByteArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QVariantList>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QEventLoop>
#include <QUrlQuery>

#include <fstream>
#include <utility>
#include <vector>
#include <iostream>
#include <string>
#include <functional>
#include <thread>
#include <sstream>

#include <lzma.h>
#include <zlib.h>
#include <archive.h>
#include <archive_entry.h>

#include "settings.h"
#include "info.h"

models_manager::models_manager(QObject *parent)
    : QObject(parent)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
#endif

    connect(settings::instance(), &settings::models_dir_changed, this,
            static_cast<bool (models_manager::*)()>(&models_manager::parse_models_file));

    parse_models_file();
}

models_manager::~models_manager()
{
    thread.join();
}

bool models_manager::ok() const
{
    return !models.empty();
}

std::vector<models_manager::lang_t> models_manager::langs() const
{
    std::vector<lang_t> list;

    QDir dir{settings::instance()->models_dir()};

    std::transform(models.cbegin(), models.cend(), std::back_inserter(list),
                [&dir](decltype(models)::value_type const& pair) {
        return lang_t{
            pair.first,
            pair.second.lang_id,
            pair.second.name,
            pair.second.scorer_file_name.isEmpty() ? "" : dir.filePath(pair.second.scorer_file_name),
            pair.second.scorer_file_name.isEmpty() ? "" : dir.filePath(pair.second.scorer_file_name),
            pair.second.experimental,
            pair.second.available,
            pair.second.downloading,
            pair.second.download_progress
        };
    });

    std::sort(list.begin(), list.end(), [](const lang_t &a, const lang_t &b) {
        if (a.experimental && !b.experimental) return false;
        if (!a.experimental && b.experimental) return true;
        return QString::compare(a.id, b.id, Qt::CaseInsensitive) < 0;
    });

    return list;
}

std::vector<models_manager::lang_t> models_manager::available_models() const
{
    std::vector<lang_t> list;

    QDir dir{settings::instance()->models_dir()};

    for (const auto& [id, model] : models) {
        const auto model_file = dir.filePath(model.file_name);
        if (model.available && QFile::exists(model_file))
            list.push_back({id,
                            model.lang_id,
                            model.name,
                            model_file,
                            model.scorer_file_name.isEmpty() ? "" : dir.filePath(model.scorer_file_name),
                            model.experimental,
                            model.available,
                            model.downloading,
                            model.download_progress});
    }

    return list;
}

bool models_manager::model_exists(const QString& id) const
{
    const auto it = models.find(id);

    return it != std::cend(models) &&
            QFile::exists(model_path(it->second.file_name));
}

void models_manager::download_model(const QString& id)
{
    download(id, download_type::all);
}

bool models_manager::model_scorer_same_url(const model_t& model)
{
    if (model.comp == comp_type::tarxz && model.comp == model.scorer_comp &&
            model.urls.size() == 1 && model.scorer_urls.size() == 1) {
        QUrl model_url{model.urls.front()};
        QUrl scorer_url{model.urls.front()};

        if (model_url.hasQuery() && QUrlQuery{model_url}.hasQueryItem("file") &&
            scorer_url.hasQuery() && QUrlQuery{scorer_url}.hasQueryItem("file")) {
            model_url.setQuery(QUrlQuery{});
            scorer_url.setQuery(QUrlQuery{});
            return model_url == scorer_url;
        }
    }

    return false;
}

void models_manager::download(const QString& id, download_type type, int part)
{
    if (type != download_type::all && type != download_type::scorer) {
        qWarning() << "incorrect dl type requested:" << static_cast<int>(type);
        return;
    }

    auto it = models.find(id);
    if (it == std::end(models)) {
        qWarning() << "no model with id:" << id;
        return;
    }

    auto& model = it->second;

    if (type == download_type::all && part < 0 && model_scorer_same_url(model)) {
        type = download_type::model_scorer;
    }

    const auto& urls = type == download_type::scorer ? model.scorer_urls : model.urls;

    if (part < 0) {
        if (type != download_type::scorer) model.downloaded_part_data = 0;
        if (urls.size() > 1) part = 0;
    }

    auto url = urls.at(part < 0 ? 0 : part);

    QString path, path_2, checksum, checksum_2, path_in_archive, path_in_archive_2;
    comp_type comp;
    auto next_type = download_type::none;
    const auto next_part = part < 0 || part >= static_cast<int>(urls.size()) - 1 ? -1 : part + 1;
    const bool scorer_exists = !model.scorer_file_name.isEmpty() && !model.scorer_checksum.isEmpty() && !url.isEmpty();
    const qint64 size = type != download_type::model_scorer && scorer_exists ? model.size + model.scorer_size : model.size;

    QNetworkRequest request{url};
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    if (type == download_type::all || type == download_type::model_scorer) {
        path = model_path(model.file_name);
        checksum = model.checksum;
        comp = model.comp;

        if (type == download_type::all) {
            type = download_type::model;
            if (next_part > 0) {
                next_type = download_type::all;
            } else if (scorer_exists) {
                next_type = download_type::scorer;
            }
        } else { // model_scorer
            path_2 = model_path(model.scorer_file_name);
            checksum_2 = model.scorer_checksum;
            next_type = download_type::none;
        }
    } else { // scorer
        path = model_path(model.scorer_file_name);
        checksum = model.scorer_checksum;
        comp = model.scorer_comp;
        next_type = next_part > 0 ? download_type::scorer : download_type::none;
    }

    if (comp == comp_type::tarxz && url.hasQuery()) {
        if (QUrlQuery query{url}; query.hasQueryItem("file")) {
            path_in_archive = query.queryItemValue("file");
            if (type == download_type::model_scorer) {
                path_in_archive_2 = QUrlQuery{model.scorer_urls.front()}.queryItemValue("file");
            }
            query.removeQueryItem("file");
            url.setQuery(query);
        }
    }

    auto out_file = new std::ofstream{download_filename(path, comp, part).toStdString(), std::ofstream::out};

    QNetworkReply* reply = nam.get(request);
    qDebug() << "downloading:" << url << "type:" << static_cast<int>(type);
    qDebug() << "out path:" << path << path_2;

    reply->setProperty("out_file", QVariant::fromValue(static_cast<void*>(out_file)));
    reply->setProperty("out_path", path);
    reply->setProperty("out_path_2", path_2);
    reply->setProperty("model_id", id);
    reply->setProperty("download_type", static_cast<int>(type));
    reply->setProperty("download_next_type", static_cast<int>(next_type));
    reply->setProperty("checksum", checksum);
    reply->setProperty("checksum_2", checksum_2);
    reply->setProperty("comp", static_cast<int>(comp));
    reply->setProperty("size", size);
    reply->setProperty("part", part);
    reply->setProperty("next_part", next_part);
    reply->setProperty("path_in_archive", path_in_archive);
    reply->setProperty("path_in_archive_2", path_in_archive_2);

    connect(reply, &QNetworkReply::downloadProgress, this, &models_manager::handle_download_progress);
    connect(reply, &QNetworkReply::finished, this, &models_manager::handle_download_finished);
    connect(reply, &QNetworkReply::readyRead, this, &models_manager::handle_download_ready_read);
    connect(reply, &QNetworkReply::sslErrors, this, &models_manager::handle_ssl_errors);

    model.downloading = true;
    if (part < 0) emit models_changed();
}

void models_manager::handle_ssl_errors(const QList<QSslError> &errors)
{
    qWarning() << "ssl error:" << errors;

    // workaround for outdated cert db on Jolla 1
    static_cast<QNetworkReply*>(sender())->ignoreSslErrors();
}

void models_manager::handle_download_ready_read()
{
    auto reply = qobject_cast<QNetworkReply*>(sender());

    if (reply->bytesAvailable() > 0) {
        auto data = reply->readAll();
        auto out_file = static_cast<std::ofstream*>(reply->property("out_file").value<void*>());
        out_file->write(data.data(), data.size());
    }
}

bool models_manager::xz_decode(const QString& file_in, const QString& file_out)
{
    qDebug() << "extracting xz archive:" << file_in;

    if (std::ifstream input{file_in.toStdString(), std::ios::in | std::ifstream::binary}) {
        if (std::ofstream output{file_out.toStdString(), std::ios::out | std::ifstream::binary}) {
            lzma_stream strm = LZMA_STREAM_INIT;
            if (lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, 0); ret != LZMA_OK) {
                qWarning() << "error initializing the xz decoder:" << ret;
                return false;
            }

            lzma_action action = LZMA_RUN;

            char buff_out[std::numeric_limits<unsigned short>::max()];
            char buff_in[std::numeric_limits<unsigned short>::max()];

            strm.next_in = reinterpret_cast<uint8_t*>(buff_in);
            strm.next_out = reinterpret_cast<uint8_t*>(buff_out);
            strm.avail_out = sizeof buff_out;
            strm.avail_in = 0;

            while (true) {
                if (strm.avail_in == 0 && input) {
                    strm.next_in = reinterpret_cast<uint8_t*>(buff_in);
                    input.read(buff_in, sizeof buff_in);
                    strm.avail_in = input.gcount();
                }

                if (!input) action = LZMA_FINISH;

                auto ret = lzma_code(&strm, action);

                if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
                    output.write(buff_out, sizeof buff_out - strm.avail_out);

                    strm.next_out = reinterpret_cast<uint8_t*>(buff_out);
                    strm.avail_out = sizeof buff_out;
                }

                if (ret == LZMA_STREAM_END) break;

                if (ret != LZMA_OK) {
                    qWarning() << "xz decoder error:" << ret;
                    lzma_end(&strm);
                    return false;
                }
            }

            lzma_end(&strm);

            return true;
        } else {
            qWarning() << "error opening out-file:" << file_out;
        }
    } else {
        qWarning() << "error opening in-file:" << file_in;
    }

    return false;
}

bool models_manager::join_part_files(const QString& file_out, int parts)
{
    qDebug() << "joining files:" << file_out;

    if (std::ofstream output{file_out.toStdString(), std::ios::out | std::ifstream::binary}) {
        for (int i = 0; i < parts; ++i) {
            const auto file_in = download_filename(file_out, comp_type::none, i);
            if (std::ifstream input{file_in.toStdString(), std::ios::in | std::ifstream::binary}) {
                char buff[std::numeric_limits<unsigned short>::max()];
                while (input) {
                    input.read(buff, sizeof buff);
                    output.write(buff, static_cast<int>(input.gcount()));
                }
            } else {
                qWarning() << "error opening in-file:" << file_in;
                return false;
            }
        }
    } else {
        qWarning() << "error opening out-file:" << file_out;
        return false;
    }

    return true;
}

bool models_manager::gz_decode(const QString& file_in, const QString& file_out)
{
    qDebug() << "extracting gz archive:" << file_in;

    if (std::ifstream input{file_in.toStdString(), std::ios::in | std::ifstream::binary}) {
        if (std::ofstream output{file_out.toStdString(), std::ios::out | std::ifstream::binary}) {
            z_stream strm;
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;

            if (int ret = inflateInit2(&strm, MAX_WBITS | 16); ret != Z_OK) {
                qWarning() << "error initializing the gzip decoder:" << ret;
                return false;
            }

            char buff_out[std::numeric_limits<unsigned short>::max()];
            char buff_in[std::numeric_limits<unsigned short>::max()];

            strm.next_in = reinterpret_cast<uint8_t*>(buff_in);
            strm.next_out = reinterpret_cast<uint8_t*>(buff_out);
            strm.avail_out = sizeof buff_out;
            strm.avail_out = sizeof buff_out;
            strm.avail_in = 0;

            while (true) {
                if (strm.avail_in == 0 && input) {
                    strm.next_in = reinterpret_cast<uint8_t*>(buff_in);
                    input.read(buff_in, sizeof buff_in);
                    strm.avail_in = input.gcount();
                }

                if (input.bad()) {
                    inflateEnd(&strm);
                    return false;
                }

                auto ret = inflate(&strm, Z_NO_FLUSH);

                if (strm.avail_out == 0 || ret == Z_STREAM_END) {
                    output.write(buff_out, sizeof buff_out - strm.avail_out);

                    strm.next_out = reinterpret_cast<uint8_t*>(buff_out);
                    strm.avail_out = sizeof buff_out;
                }

                if (ret == Z_STREAM_END) break;

                if (ret != Z_OK) {
                    qWarning() << "gzip decoder error:" << ret;
                    inflateEnd(&strm);
                    return false;
                }
            }

            inflateEnd(&strm);

            return true;
        } else {
            qWarning() << "error opening out-file:" << file_out;
        }
    } else {
        qWarning() << "error opening in-file:" << file_in;
    }

    return false;
}

// source: https://github.com/libarchive/libarchive/blob/master/examples/untar.c
static int copy_data(struct archive *ar, struct archive *aw)
{
    int r;
    const void *buff;
    size_t size;
#if ARCHIVE_VERSION_NUMBER >= 3000000
    int64_t offset;
#else
    off_t offset;
#endif

    for (;;) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return (ARCHIVE_OK);
        if (r != ARCHIVE_OK)
            return (r);
        r = archive_write_data_block(aw, buff, size, offset);
        if (r != ARCHIVE_OK) {
            return (r);
        }
    }
}

bool models_manager::tar_decode(const QString& file_in, std::map<QString, QString> &&files_out)
{
    qDebug() << "extracting tar archive:" << file_in;

    struct archive* a = archive_read_new();
    struct archive* ext = archive_write_disk_new();
    archive_read_support_format_tar(a);

    bool ok = true;

    if (archive_read_open_filename(a, file_in.toStdString().c_str(), 10240)) {
        qWarning() << "error opening in-file:" << file_in << archive_error_string(a);
        ok = false;
    } else {
        struct archive_entry* entry;
        while(true) {
            int ret = archive_read_next_header(a, &entry);
            if (ret == ARCHIVE_EOF) break;
            if (ret != ARCHIVE_OK) {
                qWarning() << "error archive_read_next_header:" << file_in << archive_error_string(a);
                ok = false;
                break;
            }

            const QString entry_path{archive_entry_pathname_utf8(entry)};

            qDebug() << "found file in tar archive:" << entry_path;

            if (const auto it = files_out.find(entry_path); it != files_out.cend()) {
                qDebug() << "extracting file:" << entry_path << "to" << it->second;

                const auto std_file_out = it->second.toStdString();
                archive_entry_set_pathname(entry, std_file_out.c_str());

                int ret = archive_write_header(ext, entry);
                if (ret != ARCHIVE_OK) {
                    qWarning() << "error archive_write_header:" << file_in << archive_error_string(ext);
                    ok = false;
                    break;
                }

                copy_data(a, ext);
                ret = archive_write_finish_entry(ext);
                if (ret != ARCHIVE_OK) {
                    qWarning() << "error archive_write_finish_entry:" << file_in << archive_error_string(ext);
                    ok = false;
                    break;
                }

                files_out.erase(it);
                if (files_out.empty()) break;
            }
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return ok;
}

bool models_manager::handle_download(const QString& path, const QString& checksum, const QString& path_in_archive,
                                     const QString& path_2, const QString& checksum_2, const QString& path_in_archive_2,
                                     comp_type comp, int parts)
{
    QEventLoop loop;

    bool ok = false;

    std::thread thread{[&] {
        if (parts > 1) {
            qDebug() << "joining parts:" << parts;
            join_part_files(download_filename(path, comp), parts);
            for (int i = 0; i < parts; ++i) QFile::remove(download_filename(path, comp, i));
        }

        auto comp_file = download_filename(path, comp);
        qDebug() << "total downloaded size:" << QFileInfo{comp_file}.size();

        bool ok_2 = true;

        if (comp != comp_type::none) {
            if (comp == comp_type::gz) {
                gz_decode(comp_file, path);
                QFile::remove(comp_file);
            } else if (comp == comp_type::xz) {
                xz_decode(comp_file, path);
                QFile::remove(comp_file);
            } else if (comp == comp_type::tarxz) {
                auto tar_file = download_filename(path, comp_type::tar);
                xz_decode(comp_file, tar_file);
                QFile::remove(comp_file);
                if (!path_in_archive_2.isEmpty() && !path_2.isEmpty()) {
                    tar_decode(tar_file, {{path_in_archive, path}, {path_in_archive_2, path_2}});
                    ok_2 = make_checksum(path_2) == checksum_2;
                    if (!ok_2) qWarning() << "checksum 2 is invalid";
                } else {
                    tar_decode(tar_file, {{path_in_archive, path}});
                }
                QFile::remove(tar_file);
            } else {
                QFile::remove(comp_file);
            }
        }

        if (ok_2) {
            ok = make_checksum(path) == checksum;
            if (!ok) qWarning() << "checksum is invalid";
        }

        loop.quit();
    }};

    loop.exec();
    thread.join();

    return ok;
}

void models_manager::handle_download_finished()
{
    auto reply = qobject_cast<QNetworkReply*>(sender());

    delete static_cast<std::ofstream*>(reply->property("out_file").value<void*>());

    const auto id = reply->property("model_id").toString();

    if (models.find(id) == models.cend()) throw std::runtime_error("invalid id");

    auto& model = models.at(id);
    const auto type = static_cast<download_type>(reply->property("download_type").toInt());
    const auto path = reply->property("out_path").toString();
    const auto comp = static_cast<comp_type>(reply->property("comp").toInt());
    const auto part = reply->property("part").toInt();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "download error:" << reply->error();
        QFile::remove(download_filename(path, comp, part));
        if (part > 0) for (int i = 0; i < part; ++i) QFile::remove(download_filename(path, comp, i));

        emit download_error(id);
    } else {
        const auto next_part = reply->property("next_part").toInt();

        if (next_part < 0) {
            const auto parts = type == download_type::scorer ? model.scorer_urls.size() : model.urls.size();
            const auto downloaded_part_data = QFileInfo{download_filename(path, comp, part)}.size();
            const auto path_2 = reply->property("out_path_2").toString();
            const auto checksum = reply->property("checksum").toString();
            const auto checksum_2 = reply->property("checksum_2").toString();
            const auto path_in_archive = reply->property("path_in_archive").toString();
            const auto path_in_archive_2 = reply->property("path_in_archive_2").toString();

            if (handle_download(path, checksum, path_in_archive, path_2, checksum_2, path_in_archive_2,
                                comp, parts)) {
                qDebug() << "successfully downloaded:" <<
                            (type == download_type::scorer ? "scorer" :
                             type == download_type::model_scorer ? "model and scorer" : "model")
                         << id;

                if (static_cast<download_type>(reply->property("download_next_type").toInt()) == download_type::scorer) {
                    model.downloaded_part_data += downloaded_part_data;
                    download(id, download_type::scorer);
                    reply->deleteLater();
                    return;
                } else {
                    model.available = true;
                    emit download_finished(id);
                }
            } else {
                QFile::remove(path);
                emit download_error(id);
            }
        } else {
            qDebug() << "successfully downloaded:" <<
                        (type == download_type::scorer ? "scorer" :
                         type == download_type::model_scorer ? "model and scorer" : "model")
                     << id << "part:" << part;
            model.downloaded_part_data += QFileInfo{download_filename(path, comp, part)}.size();
            download(id, static_cast<download_type>(reply->property("download_next_type").toInt()), next_part);
            reply->deleteLater();
            return;
        }
    }

    reply->deleteLater();

    model.downloading = false;
    model.download_progress = 0.0;
    model.downloaded_part_data = 0;
    emit models_changed();
}

QString models_manager::make_checksum(const QString &file)
{
    if (std::ifstream input{file.toStdString(), std::ios::in | std::ifstream::binary}) {
        auto checksum = crc32(0L, Z_NULL, 0);
        char buff[std::numeric_limits<unsigned short>::max()];
        while (input) {
            input.read(buff, sizeof buff);
            checksum = crc32(checksum, reinterpret_cast<unsigned char*>(buff),
                             static_cast<unsigned int>(input.gcount()));
        }
        std::stringstream ss; ss << std::hex << checksum;
        const auto hex = QString::fromStdString(ss.str());
        qDebug() << "crc checksum:" << hex << file;
        return hex;
    } else {
        qWarning() << "cannot open file:" << file;
    }

    return {};
}

void models_manager::handle_download_progress(qint64 received, qint64 real_total)
{
    const auto reply = qobject_cast<const QNetworkReply*>(sender());
    const auto id = reply->property("model_id").toString();

    auto total = reply->property("size").toLongLong();
    if (total <= 0) total = real_total;

    if (total > 0) {
        auto& model = models.at(id);
        const auto new_download_progress = static_cast<double>(received + model.downloaded_part_data)/total;
        if (new_download_progress - model.download_progress >= 0.01) {
            model.download_progress = new_download_progress;
            emit download_progress(id, new_download_progress);
        }
    }
}

void models_manager::delete_model(const QString &id)
{
    if (const auto it = models.find(id); it != std::cend(models)) {
        auto& model = it->second;
        QFile::remove(model_path(model.file_name));
        QFile::remove(model_path(model.scorer_file_name));
        model.available = false;
        model.download_progress = 0.0;
        model.downloaded_part_data = 0;
        emit models_changed();
    } else {
        qWarning() << "no model with id:" << id;
    }
}

void models_manager::backup_config(const QString& lang_models_file)
{
    QString backup_file;
    QFileInfo lang_models_file_i{lang_models_file};
    auto backup_file_stem = lang_models_file_i.dir().filePath(lang_models_file_i.baseName());

    int i = 0;
    do {
        backup_file = backup_file_stem + ".old" + (i == 0 ? "" : QString::number(i)) + ".json";
        ++i;
    } while (QFile::exists(backup_file) && i < 1000);

    qDebug() << "making lang models file backup to:" << backup_file;

    QFile::remove(backup_file);
    QFile::copy(lang_models_file, backup_file);
}

void models_manager::init_config()
{
    QFile default_models_file{":/config/" + models_file};
    if (!default_models_file.exists()) {
        qWarning() << "default models file does not exist";
        return;
    }
    if (!default_models_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "cannot open default models file";
        return;
    }

    const QString data_dir{QStandardPaths::writableLocation(QStandardPaths::DataLocation)};
    QFile models{QDir{data_dir}.filePath(models_file)};
    if (models.exists()) backup_config(QFileInfo{models}.absoluteFilePath());

    if (!models.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "cannot open models file";
        return;
    }

    models.write(default_models_file.readAll()
            .replace("%VERSION%", std::to_string(dsnote::CONF_VERSION).c_str()));
}

models_manager::comp_type models_manager::str2comp(const QString& str)
{
    if (!str.compare("gz", Qt::CaseInsensitive)) return comp_type::gz;
    if (!str.compare("xz", Qt::CaseInsensitive)) return comp_type::xz;
    if (!str.compare("tarxz", Qt::CaseInsensitive)) return comp_type::tarxz;
    return comp_type::none;
}

QString models_manager::download_filename(const QString& filename, comp_type comp, int part)
{
    QString ret_name = filename;

    switch (comp) {
    case comp_type::gz: ret_name += ".gz"; break;
    case comp_type::xz: ret_name += ".xz"; break;
    case comp_type::tar: ret_name += ".tar"; break;
    case comp_type::tarxz: ret_name += ".tar.xz"; break;
    default: ;
    }

    if (part > -1) {
        ret_name += QString(".part-%1").arg(part, 2, 10, QLatin1Char('0'));
    }

    return ret_name;
}

auto models_manager::check_models_file(const QJsonArray &models_jarray)
{
    std::map<QString, models_manager::model_t> models;

    QDir dir{settings::instance()->models_dir()};

    for (const auto& ele : models_jarray) {
        auto obj = ele.toObject();
        auto model_id = obj.value("model_id").toString();

        if (model_id.isEmpty()) {
            qWarning() << "empty model id in lang models file";
            continue;
        }

        if (models.find(model_id) != models.end()) {
            qWarning() << "duplicate model id in lang models file:" << model_id;
            continue;
        }

        auto lang_id = obj.value("lang_id").toString();
        if (lang_id.isEmpty()) {
            qWarning() << "empty lang id in lang models file";
            continue;
        }

        auto checksum = obj.value("checksum").toString();
        if (checksum.isEmpty()) {
            qWarning() << "checksum cannot be empty:" << model_id;
            continue;
        }

        auto file_name = obj.value("file_name").toString();
        if (file_name.isEmpty()) file_name = file_name_from_id(model_id);

        auto scorer_file_name = obj.value("scorer_file_name").toString();
        if (scorer_file_name.isEmpty()) scorer_file_name = scorer_file_name_from_id(model_id);

        model_t model {
            lang_id,
            obj.value("name").toString(),
            file_name,
            checksum,
            str2comp(obj.value("comp").toString()),
            {},
            obj.value("size").toString().toLongLong(),
            scorer_file_name,
            obj.value("scorer_checksum").toString(),
            str2comp(obj.value("scorer_comp").toString()),
            {},
            obj.value("scorer_size").toString().toLongLong(),
            obj.value("experimental").toBool(),
            false,
            false
        };

        auto urls = obj.value("urls").toArray();
        if (urls.isEmpty()) {
            qWarning() << "urls should be non empty array";
            continue;
        }
        for (auto url : urls) model.urls.emplace_back(url.toString());

        auto scorer_urls = obj.value("scorer_urls").toArray();
        for (auto url : scorer_urls) model.scorer_urls.emplace_back(url.toString());

        model.available = dir.exists(file_name) && checksum == make_checksum(dir.filePath(file_name)) &&
                ((dir.exists(scorer_file_name) && model.scorer_checksum == make_checksum(dir.filePath(scorer_file_name))) ||
                 model.scorer_urls.empty());

        models.insert({model_id, std::move(model)});
    }

    return models;
}

void models_manager::reload()
{
    if (!parse_models_file()) pending_reload = true;
}

bool models_manager::parse_models_file()
{
    bool expected = false;
    if (!busy_value.compare_exchange_strong(expected, true))
        return false;

    emit busy_changed();

    if (thread.joinable())
        thread.join();

    thread = std::thread{[this]{
        do {
            pending_reload = false;
            models = parse_models_file(false);
        } while (pending_reload);

        emit models_changed();
        busy_value.store(false);
        emit busy_changed();
    }};

    return true;
}

std::map<QString, models_manager::model_t> models_manager::parse_models_file(bool reset)
{
    std::map<QString, models_manager::model_t> models;

    const auto models_file_path = QDir{QStandardPaths::writableLocation(QStandardPaths::DataLocation)}.filePath(models_file);
    if (!QFile::exists(models_file_path)) init_config();

    if (std::ifstream input{models_file_path.toStdString(), std::ifstream::in | std::ifstream::binary}) {
        const std::vector<char> buff{std::istreambuf_iterator<char>{input}, {}};

        QJsonParseError err;
        auto json = QJsonDocument::fromJson(QByteArray::fromRawData(&buff[0], buff.size()), &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "error parsing json:" << err.errorString();
            input.close();
            if (!reset) {
                init_config();
                models = parse_models_file(true);
            }
        } else {
            auto version = json.object().value("version").toInt();

            if (version < dsnote::CONF_VERSION) {
                qWarning("version mismatch, has %d but requires %d", version, dsnote::CONF_VERSION);
                input.close();
                if (!reset) {
                    init_config();
                    models = parse_models_file(true);
                }
            } else {
                models = check_models_file(json.object().value("models").toArray());
            }
        }
    } else {
        qWarning() << "cannot open lang models file";
    }

    return models;
}

inline QString models_manager::file_name_from_id(const QString& id)
{
    return id + ".tflite";
}

inline QString models_manager::scorer_file_name_from_id(const QString& id)
{
    return id + ".scorer";
}

QString models_manager::model_path(const QString& file_name)
{
    return QDir{settings::instance()->models_dir()}.filePath(file_name);
}
