/* <editor-fold desc="MIT License">

Copyright(c) 2021 Oskar Homburg

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsgXchange/volumes.h>
#include <vsg/all.h>

#include <iostream>
#include <fstream>

using namespace vsgXchange;

xyz::xyz() = default;

bool xyz::getFeatures(Features& features) const
{
    features.extensionFeatureMap["xyz"] = static_cast<vsg::ReaderWriter::FeatureMask>(vsg::ReaderWriter::READ_FILENAME | vsg::ReaderWriter::READ_ISTREAM);
    return true;
}

vsg::ref_ptr<vsg::Object> xyz::read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> options) const
{
    vsg::Path filenameToUse = vsg::findFile(filename, options);
    std::ifstream file(filenameToUse, std::ifstream::in | std::ifstream::binary);
    return xyz::read(file, options);
}

vsg::ref_ptr<vsg::Object> xyz::read(std::istream& fin, vsg::ref_ptr<const vsg::Options> options) const
{
    uint32_t sizeX, sizeY, sizeZ;
    double voxelSizeX, voxelSizeY, voxelSizeZ;

    fin.read(reinterpret_cast<char*>(&sizeX), sizeof(uint32_t));
    fin.read(reinterpret_cast<char*>(&sizeY), sizeof(uint32_t));
    fin.read(reinterpret_cast<char*>(&sizeZ), sizeof(uint32_t));

    fin.read(reinterpret_cast<char*>(&voxelSizeX), sizeof(double));
    fin.read(reinterpret_cast<char*>(&voxelSizeY), sizeof(double));
    fin.read(reinterpret_cast<char*>(&voxelSizeZ), sizeof(double));

    std::vector<float> raw_data(static_cast<size_t>(sizeX * sizeY * sizeZ), 0.0f);
    fin.read(reinterpret_cast<char *>(raw_data.data()), sizeof(float) * raw_data.size());

    auto data = vsg::floatArray3D::create(sizeX, sizeY, sizeZ, vsg::Data::Layout{ VK_FORMAT_R32_SFLOAT });
    size_t i = 0;
    float maxVal = 0;

    // rearrange axes
    for (uint32_t x = 0; x < sizeX; x++)
    {
        for (uint32_t y = 0; y < sizeY; y++)
        {
            for (uint32_t z = 0; z < sizeZ; z++)
            {
                float d = raw_data[i++];
                data->at(x, y, z) = d;
                if (d > maxVal) maxVal = d;
            }
        }
    }

    // normalize data
    float rcpMaxVal = 1.0f / maxVal;
    for (float& d : *data)
    {
        d *= rcpMaxVal;
    }

    auto container = vsg::MatrixTransform::create();
    auto vol = vsg::Volumetric::create();
    vol->voxels = data;
    vol->box.minX = vol->box.minY = vol->box.minZ = 0;
    vol->box.maxX = vol->box.maxY = vol->box.maxZ = 1.0f;

    auto& mat = container->matrix;
    mat(0, 0) = static_cast<float>(voxelSizeX * sizeX);
    mat(1, 1) = static_cast<float>(voxelSizeY * sizeY);
    mat(2, 2) = static_cast<float>(voxelSizeZ * sizeZ);

    container->addChild(vol);
    return container;
}
