#ifndef MODEL_H
#define MODEL_H

#include <QVector>
#include "texvertex.h"

struct Model
{
    QVector<TexVertex> vertices;
    QVector<uint32_t> indices;

    [[nodiscard]] static Model loadModel(const QString &baseDirName, const QString &fileName);
};


#endif // MODEL_H
