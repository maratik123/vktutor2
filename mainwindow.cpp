#include "mainwindow.h"

#include "vulkanrenderer.h"

#include <QDebug>

MainWindow::MainWindow(QWindow *parent)
    : QVulkanWindow{parent}
{
}

QVulkanWindowRenderer *MainWindow::createRenderer()
{
    qDebug() << "Creating renderer";
    return new VulkanRenderer(this);
}
