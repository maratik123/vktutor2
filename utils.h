#ifndef UTILS_H
#define UTILS_H

#include <QByteArrayList>

extern const QByteArrayList vulkanLayers;

constexpr bool enableValidationLayers =
#ifdef NDEBUG
    false
#else
    true
#endif
;

[[nodiscard]] QByteArray readFile(const QString &fileName);

#endif // UTILS_H
