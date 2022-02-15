#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QVulkanWindow>

class MainWindow final : public QVulkanWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWindow *parent = nullptr);
    QVulkanWindowRenderer *createRenderer() override;
};
#endif // MAINWINDOW_H
