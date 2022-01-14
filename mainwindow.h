#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QVulkanWindow>
#include <QCloseEvent>

class MainWindow : public QVulkanWindow
{
    Q_OBJECT

public:
    MainWindow(MainWindow const &) = delete;
    MainWindow(MainWindow &&) = delete;
    MainWindow operator=(MainWindow) = delete;
    MainWindow operator=(MainWindow &&) = delete;
    explicit MainWindow(QWindow *parent = nullptr);
    ~MainWindow() override = default; 
};
#endif // MAINWINDOW_H
