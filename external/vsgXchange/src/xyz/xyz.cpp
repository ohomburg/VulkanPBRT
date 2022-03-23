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

xyz::xyz(bool use16bit) : use16bit(use16bit) {}

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

uint32_t floatToBits(float f)
{
    uint32_t ret;
    std::memcpy(&ret, &f, sizeof(float));
    return ret;
}

uint16_t floatToHalf(float f)
{
    auto bits = floatToBits(f);
    uint16_t sign = (bits >> 16) & 0x8000;
    int exp = int((bits >> 23) & 0xFF) - 127;
    uint16_t m = (bits >> 13) & 0x3FF; // FIXME: inaccurate, always rounds down
    uint16_t expb = uint16_t(exp + 15) << 10;
    if (exp > 15) { m = 0; expb = 31; } // inf
    if (exp < -14) { m = 0; expb = 0; } // subnormal -> zero
    return sign | expb | m;
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

    // swap y/z to convert coordinate systems
    auto data = vsg::floatArray3D::create(sizeX, sizeZ, sizeY, vsg::Data::Layout{ VK_FORMAT_R32_SFLOAT });
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
                // swap y/z again
                data->at(x, z, y) = d;
                if (d > maxVal) maxVal = d;
            }
        }
    }

    // normalize data
    float rcpMaxVal = 1.0f / maxVal;
    vsg::ref_ptr<vsg::ushortArray3D> data16;
    if (use16bit) data16 = vsg::ushortArray3D::create(sizeX, sizeZ, sizeY, vsg::Data::Layout{ VK_FORMAT_R16_SFLOAT });
    for (size_t j = 0; j < data->size(); j++)
    {
        data->at(j) *= rcpMaxVal;
        if (use16bit)
            data16->at(j) = floatToHalf(data->at(j));
    }

    auto container = vsg::MatrixTransform::create();
    auto vol = vsg::Volumetric::create();
    if (use16bit)
        vol->voxels = data16;
    else
        vol->voxels = data;
    vol->box.minX = vol->box.minY = vol->box.minZ = 0;
    vol->box.maxX = vol->box.maxY = vol->box.maxZ = 1.0f;

    auto& mat = container->matrix;
    // swap y/z here as well
    mat(0, 0) = static_cast<float>(voxelSizeX * sizeX);
    mat(1, 1) = static_cast<float>(voxelSizeZ * sizeZ);
    mat(2, 2) = static_cast<float>(voxelSizeY * sizeY);
    mat(3, 0) = -0.5f * mat(0, 0);
    mat(3, 1) = -0.5f * mat(1, 1);
    mat(3, 2) = -0.5f * mat(2, 2);

    container->addChild(vol);
    return container;
}
