#include "closeeventfilter.h"
#include "mainwindow.h"
#include "settings.h"
#include "utils.h"

#include <QGuiApplication>
#include <QDebug>
#include <QLoggingCategory>
#include <QSettings>
#include <QVulkanInstance>
#include <QWindow>

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
                     &cef, [](const QObject *obj, const QEvent *) { Settings::saveSettings(*qobject_cast<const QWindow *>(obj)); });

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(QSurfaceFormat::ColorSpace::sRGBColorSpace);

    MainWindow w{};
    w.setFormat(format);
    w.setTitle(applicationName);
    w.installEventFilter(&cef);
    w.setVulkanInstance(&inst);
    Settings::loadSettings(w);
    w.show();

    return QGuiApplication::exec();
}
