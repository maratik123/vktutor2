#include "utils.h"

const QByteArrayList validationLayers = {
#ifndef NDEBUG
    QByteArrayLiteral("VK_LAYER_KHRONOS_validation")
#endif
};
