#ifndef UTILS_H
#define UTILS_H

#include <QByteArrayList>

extern const QByteArrayList validationLayers;

constexpr bool enableValidationLayers =
#ifdef NDEBUG
    false
#else
    true
#endif
;

#endif // UTILS_H
