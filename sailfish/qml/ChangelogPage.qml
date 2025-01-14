/* Copyright (C) 2021 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: root

    allowedOrientations: Orientation.All

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        contentHeight: content.height

        Column {
            id: content

            width: root.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Changes")
            }

            SectionHeader {
                text: qsTr("Version %1").arg("1.8")
            }

            LogItem {
                title: "New languages: Finnish, Mongolian (experimental), Estonian (experimental)"
                description: "New models are configured for download directly from the app. " +
                             "Unfortunately experimental models provide very bad accuracy " +
                             "and most likely they are not suitable for any use right now."
            }

            LogItem {
                title: "Improved model for Polish language: 'Polski (mkiol)'"
                description: "New model is slower than 'Polski (Jaco)' but provides larger vocabulary. " +
                             "Improved language model was trained on text from polish Wikipedia and 5K random movie subtitles."
            }

            LogItem {
                title: "Experimental German medical model: 'Deutsch (med)'"
                description: "New language model is similar to 'Deutsch (Aashish Agarwal)' " +
                             "but is able to recognize enhanced medical vocabulary."
            }

            LogItem {
                title: "New models for English: 'English (Coqui Huge Vocabulary)', 'English (Coqui Large Vocabulary)'"
                description: "New models provide larger vocabulary for English language."
            }

            LogItem {
                title: "Improved languages browser"
                description: "Available models are grouped by languages and also it is possible to search by name."
            }

            LogItem {
                title: "Support for SFOS 4.4"
                description: "Sandboxing is explicitly disabled which allows you to run the application on latest SFOS version."
            }

            SectionHeader {
                text: qsTr("Version %1").arg("1.6")
            }

            LogItem {
                title: "New listening mode: One sentence"
                description: "Following listening modes are available: " +
                             "One sentence (Clicking on the bottom panel starts listening, which ends when the first sentence is recognized), " +
                             "Press and hold (Pressing and holding on the bottom panel enables listening. When you stop holding, listening will turn off), " +
                             "Always on (Listening is always turn on). " +
                             "The default is 'One sentence'. You can change listening mode in the Settings."
            }

            LogItem {
                title: "Cover action"
                description: "When 'One sentence' mode is set, cover displays action to enable/cancel listening."
            }

            LogItem {
                title: "Improved language viewer"
                description: "Language viewer has a search bar and hide/show experimental models option."
            }

            LogItem {
                title: "Coqui STT lib update"
                description: "STT library has been updated to the most recent version (1.1.0)."
            }

            LogItem {
                title: "Bug fixes and performance improvements"
                description: "Many minor bugs were fixed. App starts quicker even with multiple languages enabled."
            }

            SectionHeader {
                text: qsTr("Version %1").arg("1.5")
            }

            LogItem {
                title: "Catalan language model"
                description: "Catalan model is configured for download directly from the app."
            }

            LogItem {
                title: "Many new experimental models"
                description: "New section in Language menu with experimental models for various languages. " +
                             "Most of these models provide very bad accuracy and some are redundant. They are for experiments and testing. " +
                             "Following new experimental models are configured: " +
                             "English (Huge Vocabulary), Dutch, Yoruba, Amharic, Basque, Turkish, Thai, " +
                             "Slovenian, Romanian, Portuguese, Latvian, Indonesian, Greek, Hungarian."
            }

            LogItem {
                title: "Bug fixes"
                description: "Previous version did not work on ARM64 devices. Hopefully this problem is now resolved."
            }

            SectionHeader {
                text: qsTr("Version %1").arg("1.4")
            }

            LogItem {
                title: "Russian and Ukrainian language models"
                description: "Russian and Ukrainian models are configured for download directly from the app."
            }

            LogItem {
                title: "Speech-to-text D-Bus service"
                description: "Integration with 3rd-party applications is now possible thanks to D-Bus interface and service."
            }

            SectionHeader {
                text: qsTr("Version %1").arg("1.3")
            }

            LogItem {
                title: "Czech language model and translation"
                description: "Thanks to community member contribution, Czech language is now supported!"
            }

            LogItem {
                title: "New language models for French and Italian."
                description: "Additional models come from Common Voice and Mozilla Italia projects."
            }

            SectionHeader {
                text: qsTr("Version %1").arg("1.2")
            }

            LogItem {
                title: "Transcribe audio file option"
                description: "New option to transcribe speech from audio file was added. " +
                             "Following file formats are supported: wav, mp3, ogg, flac, m4a, aac, opus."
            }

            SectionHeader {
                text: qsTr("Version %1").arg("1.0")
            }

            LogItem {
                title: "DeepSpeech lib update"
                description: "DeepSpeech library was updated to version '0.10.0-alpha.3'. " +
                             "Thanks to this update speech recognition accuracy is much better now."
            }

            LogItem {
                title: "Support for Jolla 1, Jolla C and PinePhone"
                description: "DeepSpeech library update made possible to run app on more devices. " +
                             "Unfortunately only ARM-based devices are supported therefore app still " +
                             "does not work on Jolla Tablet."
            }

            LogItem {
                title: "Minor UI improvements"
                description: "Translation has been polished and few UI glitches have been fixed."
            }

            Spacer {}
        }
    }

    VerticalScrollDecorator {
        flickable: flick
    }
}
