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

constexpr int pipelineCacheLayoutVersion = 1;

void Settings::savePipelineCache(const QByteArray &cache)
{
    QSettings settings{};
    settings.beginGroup(graphics);
    auto compressedCache = qCompress(cache, 9);
    settings.setValue(pipelineCache, compressedCache);
    settings.setValue(pipelineCacheLayoutVersionName, pipelineCacheLayoutVersion);
    settings.endGroup();
    qDebug() << "Save pipeline cache to: " << settings.fileName();
}

QByteArray Settings::loadPipelineCache()
{
    QSettings settings{};
    qDebug() << "Load pipeline cache from: " << settings.fileName();
    settings.beginGroup(graphics);
    QByteArray result{};
    auto version = settings.value(pipelineCacheLayoutVersionName).toInt();
    if (version == pipelineCacheLayoutVersion) {
        result = settings.value(pipelineCache).toByteArray();
        result = qUncompress(result);
        if (result.isEmpty()) {
            qDebug() << "Can not fetch stored pipeline cache";
        }
    } else {
        qDebug() << "Stored version: " << version << ", expected: " << pipelineCacheLayoutVersion << ", discarding";
    }
    settings.endGroup();
    return result;
}
