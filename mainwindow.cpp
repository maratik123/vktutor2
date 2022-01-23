#include "mainwindow.h"

#include "vulkanrenderer.h"

#include <QDebug>

MainWindow::MainWindow(QWindow *parent)
    : QVulkanWindow{parent}
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setColorSpace(QSurfaceFormat::ColorSpace::sRGBColorSpace);
    setFormat(format);
}

QVulkanWindowRenderer *MainWindow::createRenderer()
{
    qDebug() << "Creating renderer";
    return new VulkanRenderer(this);
}
