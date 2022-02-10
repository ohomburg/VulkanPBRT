#pragma once
/*<editor-fold desc="MIT License">

Copyright(c) 2022 Oskar Homburg

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
</editor-fold> */

#include "vsg/all.h"

class VBuffer : public vsg::Inherit<vsg::Object, VBuffer> {
public:
    VBuffer(uint32_t width, uint32_t height);

    uint32_t width, height;
    vsg::ref_ptr<vsg::DescriptorImage> depthBuffer, visBuffer;
    vsg::ref_ptr<vsg::mat4Value> viewProjectMatrixValue;
    vsg::ref_ptr<vsg::RenderGraph> renderGraph;

    void setScene(vsg::Node& scene);
    void compile(vsg::Context& context);

private:
    vsg::ref_ptr<vsg::GraphicsPipeline> pipeline;
    vsg::ref_ptr<vsg::Commands> commands;
};
