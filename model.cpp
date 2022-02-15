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
class DataStreamBuf final
        : public std::streambuf
{
public:
    explicit DataStreamBuf(QFile *file)
        : m_ds{file}
        , m_buffer{2048, char{}}
    {}

    int_type underflow() override;

private:
    QByteArray m_buffer;
    QDataStream m_ds;
};

DataStreamBuf::int_type DataStreamBuf::underflow()
{
    if (gptr() == egptr()) {
        auto size = m_ds.readRawData(m_buffer.data(), static_cast<int>(m_buffer.size()));
        if (size == -1) {
            throw std::runtime_error{"can not read data to buffer"};
        }
        setg(m_buffer.begin(), m_buffer.begin(), m_buffer.begin() + size);
        if (gptr() == egptr()) {
            return std::char_traits<char>::eof();
        }
    }
    return std::char_traits<char>::to_int_type(*gptr());
}

class MaterialDirReader final
        : public tinyobj::MaterialReader
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
            throw std::runtime_error{"File can not be opened"};
        }
        DataStreamBuf dsbuf{&file};
        std::istream in{&dsbuf};

        std::string warn{};
        std::string err{};

        MaterialDirReader mr{&baseDir};

        std::vector<tinyobj::material_t> materials{};

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &in, &mr)) {
            std::string message{"Warn: "};
            message += warn;
            message += ", err: ";
            message += err;
            throw std::runtime_error{message};
        }
        if (!warn.empty()) {
            qDebug() << "Warn: " << warn.c_str();
        }
        if (!err.empty()) {
            qDebug() << "Err: " << err.c_str();
        }
    }

    Model result{};
    QHash<TexVertex, uint32_t> uniqueVertices{};

    qDebug() << "Shapes: " << shapes.size();
    for (const auto &shape : shapes) {
        auto meshSize = static_cast<int>(shape.mesh.indices.size());
        qDebug() << "Mesh indices: " << meshSize;
        result.indices.reserve(result.indices.size() + meshSize);
        result.vertices.reserve(result.vertices.size() + meshSize);
        for (const auto &index : shape.mesh.indices) {
            auto vi = 3 * index.vertex_index;
            auto ni = 3 * index.normal_index;
            auto ti = 2 * index.texcoord_index;
            TexVertex vertex{
                {
                    attrib.vertices[vi + 0],
                    attrib.vertices[vi + 1],
                    attrib.vertices[vi + 2]
                },
                glm::normalize(glm::vec3{
                    attrib.normals[ni + 0],
                    attrib.normals[ni + 1],
                    attrib.normals[ni + 2]
                }),
                {
                    attrib.texcoords[ti + 0],
                    1.0F - attrib.texcoords[ti + 1]
                }
            };

            if (auto iUniqueVertices = uniqueVertices.constFind(vertex);
                    iUniqueVertices != uniqueVertices.cend()) {
                result.indices << iUniqueVertices.value();
                continue;
            }
            auto verticesSize = result.vertices.size();
            uniqueVertices.insert(vertex, verticesSize);
            result.indices << verticesSize;
            result.vertices << vertex;
        }
    }
    result.vertices.squeeze();

    qDebug() << "Vertices: " << result.vertices.size() << " (" << result.vertices.size() * sizeof(decltype(result.vertices)::value_type) << " bytes )";
    qDebug() << "Indices: " << result.indices.size() << " (" << result.indices.size() * sizeof(decltype(result.indices)::value_type) << " bytes )";

    return result;
}
