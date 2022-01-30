#include "closeeventfilter.h"
#include "constlatin1string.h"
#include "mainwindow.h"
#include "utils.h"

#include <QGuiApplication>
#include <QDebug>
#include <QLoggingCategory>
#include <QSettings>
#include <QVulkanInstance>
#include <QWindow>

namespace {
const QString geometry = QStringLiteral("geometry");
const QString windowState = QStringLiteral("windowState");
const QString mainWindow = QStringLiteral("mainWindow");
constexpr int defaultWidth = 800;
constexpr int defaultHeight = 600;
constexpr QSize defaultSize{defaultWidth, defaultHeight};
constexpr QRect defaultGeometry{QPoint{0, 0}, defaultSize};

[[nodiscard]] constexpr Qt::WindowStates filterWindowStates(Qt::WindowStates windowStates)
{
    return windowStates & ~(Qt::WindowState::WindowActive | Qt::WindowState::WindowFullScreen);
}

void saveSettings(const QWindow *w)
{
    QSettings settings{};
    settings.beginGroup(mainWindow);
    settings.setValue(geometry, w->geometry());
    settings.setValue(windowState, static_cast<Qt::WindowStates::Int>(filterWindowStates(w->windowStates())));
    settings.endGroup();
    qDebug() << "Save window settings to: " << settings.fileName();
}

void loadSettings(QWindow *w)
{
    QSettings settings{};
    qDebug() << "Load window state from: " << settings.fileName();
    settings.beginGroup(mainWindow);
    w->setGeometry(settings.value(geometry, defaultGeometry).toRect());
    w->setWindowStates(filterWindowStates(static_cast<Qt::WindowStates>(settings
                                                                        .value(windowState, Qt::WindowState::WindowNoState)
                                                                        .toInt())));
    settings.endGroup();
}
}

int main(int argc, char *argv[])
{
    QLoggingCategory::setFilterRules(QStringLiteral("qt.vulkan=true"));

    QCoreApplication::setOrganizationName(QStringLiteral("maratik"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("maratik.name"));
    QString applicationName = QStringLiteral("vktutor2");
    QCoreApplication::setApplicationName(applicationName);

    QGuiApplication a{argc, argv};

    QVulkanInstance inst{};
    inst.setApiVersion(QVersionNumber{1, 0, 0});
    if constexpr (enableValidationLayers) {
        inst.setLayers(validationLayers);
    }
    if (!inst.create()) {
        qDebug() << "Vulkan is not available: " << inst.errorCode();
        return 1;
    }

    CloseEventFilter cef{};

    QObject::connect(&cef, &CloseEventFilter::close,
                     &cef, [](const QObject *obj, const QEvent *) { saveSettings(qobject_cast<const QWindow *>(obj)); });

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(QSurfaceFormat::ColorSpace::sRGBColorSpace);

    MainWindow w{};
    w.setFormat(format);
    w.setTitle(applicationName);
    w.installEventFilter(&cef);
    w.setVulkanInstance(&inst);
    w.setWidth(defaultWidth);
    w.setHeight(defaultHeight);
    loadSettings(&w);
    w.show();

    return QGuiApplication::exec();
}
