#include "utils.h"

#include <QDebug>
#include <QFile>

const QByteArrayList vulkanLayers{
#ifndef NDEBUG
    QByteArrayLiteral("VK_LAYER_KHRONOS_validation")
#endif
};


[[nodiscard]] QByteArray readFile(const QString &fileName)
{
    qDebug() << "readFile: " << fileName;
    QFile file{fileName};
    if (!file.open(QIODevice::OpenModeFlag::ReadOnly)) {
        qDebug() << "file not found: " << fileName;
        throw std::runtime_error{"file not found"};
    }
    return file.readAll();
}
