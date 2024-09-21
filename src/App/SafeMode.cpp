/***************************************************************************
 *   Copyright (c) 2024 Benjamin Nauck <benjamin@nauck.se>                 *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License (LGPL)   *
 *   as published by the Free Software Foundation; either version 2 of     *
 *   the License, or (at your option) any later version.                   *
 *   for detail see the LICENCE text file.                                 *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with FreeCAD; if not, write to the Free Software        *
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
 *   USA                                                                   *
 *                                                                         *
 ***************************************************************************/

#include <QDateTime>
#include <QDebug>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "FCConfig.h"
#include "Application.h"

#include "SafeMode.h"

static QTemporaryDir * tempDir = nullptr;

static std::string _getBootFailDetectionFileName() {
    // We need a place to store the boot file that is the same every time
    // It is ok for this file to disappear, especially on a reboot
    auto const staticTempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return staticTempDir.toStdString() + PATHSEP + "FREECAD_BOOT_NOT_COMPLETE";
}

static std::string _bootFileContent() {
    auto &config = App::GetApplication().Config();

    auto fileContent = config["BuildRevision"];
    if (config.count("BuildRevisionBranch")) {
        fileContent += " " + config["BuildRevisionBranch"];
    }
    if (config.count("BuildRevisionHash")) {
        fileContent += " " + config["BuildRevisionHash"];
    }
    return fileContent;
}

static bool _didBootFailRecently() {
    auto const filename = _getBootFailDetectionFileName();

    // Check modification date is not too old
    {
        QFileInfo fileInfo(QString::fromStdString(filename));
        if (!fileInfo.exists()) {
            return false;
        }
        auto const currentTime = QDateTime::currentDateTime();
        auto const fileLastModified = fileInfo.lastModified();
        auto const timeDifference = fileLastModified.secsTo(currentTime);
        auto const secondsPerHour = 3600;
        if (timeDifference > 12 * secondsPerHour) {
            return false;
        }
    }

    // Check that the file was created for the same version
    {
        QFile file(QString::fromStdString(filename));
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            return false;
        }
        QTextStream in(&file);
        QString content = in.readAll();
        if (content != QString::fromStdString(_bootFileContent())) {
            return false;
        }
    }

    // If all checks has passed
    return true;
}

static bool _createTemporaryBaseDir() {
    tempDir = new QTemporaryDir();
    if (!tempDir->isValid()) {
        delete tempDir;
        tempDir = nullptr;
    }
    return tempDir;
}

static void _createBootFile()
{
    auto const bootFilePath = _getBootFailDetectionFileName();
    QFile bootFile(QString::fromStdString(bootFilePath));
    if (!bootFile.open(QFile::WriteOnly)) {
        return;
    }
    QTextStream stream(&bootFile);
    stream << QString::fromStdString(_bootFileContent());
}

static void _replaceDirs()
{
    auto &config = App::GetApplication().Config();

    auto const temp_base = tempDir->path().toStdString();
    auto const dirs = {
        "UserAppData",
        "UserConfigPath",
        "UserCachePath",
        "AppTempPath",
        "UserMacroPath",
        "UserHomePath",
    };

    for (auto const d : dirs) {
        auto const path = temp_base + PATHSEP + d + PATHSEP;
        auto const qpath = QString::fromStdString(path);
        QDir().mkpath(qpath);
        config[d] = path;
    }
}

void SafeMode::BootUpComplete()
{
    auto const bootFilePath = _getBootFailDetectionFileName();
    QFile bootFile(QString::fromStdString(bootFilePath));
    bootFile.remove();
}

void SafeMode::InitializeSafeMode(bool forceSafeMode)
{
    auto const bootFailedPreviously = _didBootFailRecently();
    _createBootFile();
    if (bootFailedPreviously || forceSafeMode) {
        if (_createTemporaryBaseDir()) {
            if (bootFailedPreviously) {
                qWarning() << "Failed boot detected, entering safe mode!";
            }
            _replaceDirs();
        }
    }
}

bool SafeMode::SafeModeEnabled() {
    return tempDir;
}

void SafeMode::Destruct() {
    delete tempDir;
}
