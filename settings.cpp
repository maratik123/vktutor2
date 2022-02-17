#include "settings.h"

#include <QDebug>
#include <QSettings>
#include <QWindow>

namespace {
const QString geometry = QStringLiteral("geometry");
const QString windowState = QStringLiteral("windowState");
const QString mainWindow = QStringLiteral("mainWindow");
const QString graphics = QStringLiteral("graphics");
const QString pipelineCache = QStringLiteral("pipelineCache");
const QString pipelineCacheLayoutVersionName = QStringLiteral("pipelineCacheLayoutVersion");
constexpr int defaultWidth = 800;
constexpr int defaultHeight = 600;
constexpr QSize defaultSize{defaultWidth, defaultHeight};
constexpr QRect defaultGeometry{QPoint{0, 0}, defaultSize};

[[nodiscard]] constexpr Qt::WindowStates filterWindowStates(Qt::WindowStates windowStates)
{
    return windowStates & ~(Qt::WindowState::WindowActive | Qt::WindowState::WindowFullScreen);
}
}

void Settings::saveSettings(const QWindow &w)
{
    QSettings settings{};
    settings.beginGroup(mainWindow);
    settings.setValue(geometry, w.geometry());
    settings.setValue(windowState, static_cast<Qt::WindowStates::Int>(filterWindowStates(w.windowStates())));
    settings.endGroup();
    qDebug() << "Save window settings to: " << settings.fileName();
}

void Settings::loadSettings(QWindow &w)
{
    QSettings settings{};
    qDebug() << "Load window state from: " << settings.fileName();
    settings.beginGroup(mainWindow);
    w.setGeometry(settings.value(geometry, defaultGeometry).toRect());
    w.setWindowStates(filterWindowStates(static_cast<Qt::WindowStates>(settings
                                                                        .value(windowState, Qt::WindowState::WindowNoState)
                                                                        .toInt())));
    settings.endGroup();
}

enum class PipelineCacheLayoutVersion : int
{
    PLAIN = 0,
    COMPRESS = 1,
    COMPRESS_B64 = 2
};

constexpr PipelineCacheLayoutVersion pipelineCacheLayoutVersion = PipelineCacheLayoutVersion::COMPRESS_B64;

void Settings::savePipelineCache(const QByteArray &cache)
{
    QSettings settings{};
    settings.beginGroup(graphics);
    QByteArray convertedCache{cache};
    if (pipelineCacheLayoutVersion == PipelineCacheLayoutVersion::COMPRESS || pipelineCacheLayoutVersion == PipelineCacheLayoutVersion::COMPRESS_B64) {
        convertedCache = qCompress(convertedCache, 9);
        if (pipelineCacheLayoutVersion == PipelineCacheLayoutVersion::COMPRESS_B64) {
            convertedCache = convertedCache.toBase64();
        }
    }
    settings.setValue(pipelineCache, convertedCache);
    settings.setValue(pipelineCacheLayoutVersionName, static_cast<int>(pipelineCacheLayoutVersion));
    settings.endGroup();
    qDebug() << "Save pipeline cache to: " << settings.fileName();
}

QByteArray Settings::loadPipelineCache()
{
    QSettings settings{};
    qDebug() << "Load pipeline cache from: " << settings.fileName();
    settings.beginGroup(graphics);
    auto result = settings.value(pipelineCache).toByteArray();
    auto version = static_cast<PipelineCacheLayoutVersion>(settings.value(pipelineCacheLayoutVersionName).toInt());
    switch (version) {
    case PipelineCacheLayoutVersion::PLAIN:
        break;
    case PipelineCacheLayoutVersion::COMPRESS_B64:
        if (auto b64Result = QByteArray::fromBase64Encoding(result, QByteArray::Base64Option::AbortOnBase64DecodingErrors)) {
            result = *b64Result;
        } else {
            qDebug() << "Failed to process base64";
            result = {};
        }
        // Passthrough to next case
    case PipelineCacheLayoutVersion::COMPRESS:
        result = qUncompress(result);
        break;
    default:
        qDebug() << "Unknown pipeline cache layout version: " << static_cast<int>(version);
        result = {};
        break;
    }
    if (result.isEmpty()) {
        qDebug() << "Can not fetch stored pipeline cache";
    }
    settings.endGroup();
    return result;
}
