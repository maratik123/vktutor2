#ifndef MODEL_H
#define MODEL_H

#include <QVector>
#include "vertex.h"

struct Model
{
    QVector<ModelVertex> vertices;
    QVector<uint32_t> indices;

    [[nodiscard]] static Model loadModel(const QString &baseDirName, const QString &fileName);
};


#endif // MODEL_H
