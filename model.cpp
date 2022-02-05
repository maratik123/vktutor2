#include "model.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "externals/tinyobjloader/tiny_obj_loader.h"

#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QVector>

namespace {
class DataStreamBuf
        : public std::streambuf
{
public:
    explicit DataStreamBuf(QFile *file)
        : m_ds{file}
        , m_buffer{}
    {}

    int_type underflow() override;

private:
    std::array<char, 1024> m_buffer;
    QDataStream m_ds;
};

DataStreamBuf::int_type DataStreamBuf::underflow()
{
    if (gptr() == egptr()) {
        auto size = m_ds.readRawData(m_buffer.data(), static_cast<int>(m_buffer.size()));
        if (size == -1) {
            throw std::runtime_error("can not read data to buffer");
        }
        setg(m_buffer.begin(), m_buffer.begin(), m_buffer.begin() + size);
        if (gptr() == egptr()) {
            return std::char_traits<char>::eof();
        }
    }
    return std::char_traits<char>::to_int_type(*gptr());
}

class MaterialDirReader : public tinyobj::MaterialReader
{
public:
    explicit MaterialDirReader(QDir *dir)
        : m_dir{dir}
    {}
    bool operator()(const std::string &matId,
                    std::vector<tinyobj::material_t> *materials,
                    std::map<std::string, int> *matMap,
                    std::string *warn,
                    std::string *err) override;

private:
    QDir *m_dir;
};

bool MaterialDirReader::operator()(const std::string &matId,
                                   std::vector<tinyobj::material_t> *materials,
                                   std::map<std::string, int> *matMap,
                                   std::string *warn,
                                   std::string *err)
{
    QString fileName{std::move(QString::fromStdString(matId))};
    QFile file{m_dir->filePath(fileName)};
    qDebug() << "Load material: " << file;
    if (!file.open(QFile::OpenModeFlag::ReadOnly)) {
        *warn += "Material file \"";
        *warn += m_dir->filePath(fileName).toStdString();
        *warn += "\" not found\n";
        return false;
    }
    DataStreamBuf dsbuf{&file};
    std::istream in{&dsbuf};
    tinyobj::LoadMtl(matMap, materials, &in, warn, err);
    return true;
}
}

Model Model::loadModel(const QString &baseDirName, const QString &fileName)
{
    tinyobj::attrib_t attrib{};
    std::vector<tinyobj::shape_t> shapes{};

    {
        QDir baseDir{baseDirName};
        QFile file{baseDir.filePath(fileName)};
        qDebug() << "Load model: " << file;
        if (!file.open(QFile::OpenModeFlag::ReadOnly)) {
            throw std::runtime_error("File can not be opened");
        }
        file.open(QFile::OpenModeFlag::ReadOnly);
        DataStreamBuf dsbuf{&file};
        std::istream in{&dsbuf};

        std::string warn{};
        std::string err{};

        MaterialDirReader mr{&baseDir};

        std::vector<tinyobj::material_t> materials{};

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &in, &mr)) {
            throw std::runtime_error("Warn: " + warn + ", err: " + err);
        }
        if (!warn.empty()) {
            qDebug() << "Warn: " << warn.c_str();
        }
        if (!err.empty()) {
            qDebug() << "Err: " << err.c_str();
        }
    }

    Model result{};
    QHash<Vertex, uint32_t> uniqueVertices{};

    for(const auto &shape : shapes) {
        for (const auto &index : shape.mesh.indices) {
            Vertex vertex{};

            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            vertex.color = {1.0F, 1.0F, 1.0F};

            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0F - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            auto iUniqueVertices = uniqueVertices.constFind(vertex);

            if (iUniqueVertices == uniqueVertices.cend()) {
                iUniqueVertices = static_cast<decltype(iUniqueVertices)>(uniqueVertices.insert(vertex, result.vertices.size()));
                result.vertices.push_back(vertex);
            }

            result.indices.push_back(iUniqueVertices.value());
        }
    }

    qDebug() << "Vertices: " << result.vertices.size();
    qDebug() << "Indices: " << result.indices.size();

    return result;
}
