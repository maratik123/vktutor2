#include "closeeventfilter.h"
#include "constlatin1string.h"
#include "mainwindow.h"

#include <QGuiApplication>
#include <QDebug>
#include <QSettings>
#include <QWindow>
#include <QVulkanInstance>

namespace {
constexpr ConstLatin1String geometry{"geometry"};
constexpr ConstLatin1String windowState{"windowState"};
constexpr int defaultWitdh = 640;
constexpr int defaultHeight = 480;
constexpr QRect defaultGeometry{QPoint{0, 0}, QSize{defaultWitdh, defaultHeight}};

void saveSettings(const QWindow *w) {
    QSettings settings;
    settings.setValue(geometry, w->geometry());
    settings.setValue(windowState, static_cast<Qt::WindowStates::Int>(w->windowStates()));
    qDebug() << "Save window settings to: " << settings.fileName();
}

void loadSettings(QWindow *w) {
    QSettings settings;
    qDebug() << "Load window state from: " << settings.fileName();
    w->setGeometry(settings.value(geometry, defaultGeometry).toRect());
    w->setWindowStates(static_cast<Qt::WindowStates>(settings.value(windowState, Qt::WindowNoState).toInt()));
}
}

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QLatin1String{"maratik"});
    QCoreApplication::setOrganizationDomain(QLatin1String{"maratik.name"});
    QCoreApplication::setApplicationName(QLatin1String{"vktutor2"});
    QGuiApplication a{argc, argv};

    QVulkanInstance inst;
    if (!inst.create()) {
        qDebug() << "Vulkan is not available";
        return 1;
    }

    CloseEventFilter cef;

    QObject::connect(&cef, &CloseEventFilter::close,
                     &cef, [](const QObject *obj, QEvent *) {
        saveSettings(qobject_cast<const QWindow *>(obj));
    });

    MainWindow w;
    w.installEventFilter(&cef);
    w.setVulkanInstance(&inst);
    loadSettings(&w);
    w.show();
    return QGuiApplication::exec();
}
