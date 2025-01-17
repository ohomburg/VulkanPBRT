#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2019 Thomas Hogarth

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsg/commands/Command.h>
#include <vsg/core/Value.h>
#include <vsg/maths/mat4.h>
#include <vsg/state/BufferInfo.h>
#include <vsg/state/Descriptor.h>
#include <vsg/vk/DeviceMemory.h>
#include <variant>

namespace vsg
{

    class VSG_DECLSPEC AccelerationGeometry : public Inherit<Object, AccelerationGeometry>
    {
    public:
        AccelerationGeometry(Allocator* allocator = nullptr);

        void compile(Context& context);

        operator VkAccelerationStructureGeometryKHR() const { return _geometry; }

        struct Triangles
        {
            ref_ptr<Data> verts;
            ref_ptr<Data> indices;
        };
        struct AABBs
        {
            ref_ptr<Data> boxes;
        };

        std::variant<std::monostate, Triangles, AABBs> geometry;

    protected:
        // compiled data
        struct CompiledTriangles
        {
            ref_ptr<BufferInfo> _vertexBuffer;
            ref_ptr<BufferInfo> _indexBuffer;
        };

        struct CompiledAABBs
        {
            ref_ptr<BufferInfo> _aabbBuffer;
        };

        std::variant<CompiledTriangles, CompiledAABBs> _compiled;

        VkAccelerationStructureGeometryKHR _geometry;
    };

    using AccelerationGeometries = std::vector<ref_ptr<AccelerationGeometry>>;

} // namespace vsg
