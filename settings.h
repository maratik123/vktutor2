#ifndef SETTINGS_H
#define SETTINGS_H

class QWindow;
class QByteArray;

class Settings
{
public:
    static void saveSettings(const QWindow &w);
    static void loadSettings(QWindow &w);
    static void savePipelineCache(const QByteArray &cache);
    [[nodiscard]] static QByteArray loadPipelineCache();
};

#endif // SETTINGS_H
