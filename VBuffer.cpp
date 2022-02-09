/*<editor-fold desc="MIT License">

Copyright(c) 2022 Oskar Homburg

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
</editor-fold> */

#include "VBuffer.hpp"

/*
 * Note about this pass: ideally this would be run in parallel with compute/post-proc work from the previous frame
 * to make better use of the hardware (run fixed function work parallel to compute),
 * but that would take a lot of refactoring in this application.
 */

VBuffer::VBuffer(uint32_t width, uint32_t height)
    : width(width), height(height)
{
    auto vertStage = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", "shaders/vbuffer.vert.spv");
    auto fragStage = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", "shaders/vbuffer.frag.spv");

    const auto &pushConstRanges = vertStage->getPushConstantRanges();
    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{}, pushConstRanges);

    vsg::ShaderStages shaderStages{vertStage, fragStage};

    auto vertexInputState = vsg::VertexInputState::create();
    // HACK: hardcoded vertex input settings from vsgXchange assimp loader
    vertexInputState->vertexBindingDescriptions.emplace_back(VkVertexInputBindingDescription{0, 12, VK_VERTEX_INPUT_RATE_VERTEX});
    vertexInputState->vertexAttributeDescriptions.emplace_back(VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
    vsg::GraphicsPipelineStates pipelineStates {
        vertexInputState,
        vsg::InputAssemblyState::create(), // default settings
        vsg::RasterizationState::create(), // default settings
        vsg::MultisampleState::create(), // default settings
        vsg::DepthStencilState::create(), // default settings
        vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{{
            VkPipelineColorBlendAttachmentState{VK_FALSE}
        }}),
        vsg::ViewportState::create(0, 0, width, height),
    };

    pipeline = vsg::GraphicsPipeline::create(pipelineLayout.get(), shaderStages, pipelineStates, 0);

    auto image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = VK_FORMAT_D32_SFLOAT;
    image->extent.width = width;
    image->extent.height = height;
    image->extent.depth = 1;
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    auto imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_DEPTH_BIT);
    auto imageInfo = vsg::ImageInfo::create(vsg::Sampler::create(), imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    depthBuffer = vsg::DescriptorImage::create(imageInfo, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = VK_FORMAT_R32_UINT;
    image->extent.width = width;
    image->extent.height = height;
    image->extent.depth = 1;
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
    imageInfo = vsg::ImageInfo::create(vsg::Sampler::create(), imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    imageInfo->sampler->minFilter = imageInfo->sampler->magFilter = VK_FILTER_NEAREST;
    visBuffer = vsg::DescriptorImage::create(imageInfo, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    viewProjectMatrixValue = vsg::mat4Value::create();
}

class VBufferRenderVisitor : public vsg::Visitor
{
    vsg::MatrixStack transform;
    uint32_t meshId = 1;
    uint32_t volId = 0x8000'0001;

    struct PushConst
    {
        float matModel3x4[12];
        uint32_t meshId;
    };

    using PushConstValue = vsg::Value<PushConst>;

public:
    vsg::ref_ptr<vsg::Commands> commands;

    VBufferRenderVisitor() = default;

    void apply(Object &object) override
    {
        object.traverse(*this);
    }

    void apply(vsg::Transform &t) override
    {
        transform.push(t);
        t.traverse(*this);
        transform.pop();
    }

    void apply(vsg::VertexIndexDraw &draw) override
    {
        auto pushVal = PushConstValue::create();
        auto modelMat = vsg::transpose(transform.top());
        for (size_t i = 0; i < 12; i++)
            pushVal->value().matModel3x4[i] = (float)modelMat.data()[i];
        pushVal->value().meshId = meshId;
        auto pushConst = vsg::PushConstants::create(VK_SHADER_STAGE_VERTEX_BIT, 64, pushVal);
        commands->addChild(pushConst);
        commands->addChild(vsg::ref_ptr(&draw));
        meshId += draw.instanceCount;
        if (meshId > 0x8000'0000) throw vsg::Exception{"Too many mesh ids used!"};
    }

    void apply(vsg::Volumetric &volumetric) override
    {
        auto pushVal = PushConstValue::create();
        auto modelMat = vsg::transpose(transform.top());
        for (size_t i = 0; i < 12; i++)
            pushVal->value().matModel3x4[i] = (float)modelMat.data()[i];
        pushVal->value().meshId = volId;
        auto pushConst = vsg::PushConstants::create(VK_SHADER_STAGE_VERTEX_BIT, 64, pushVal);
        commands->addChild(pushConst);
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays(vsg::DataList{vsg::floatArray::create({0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1})});
        draw->assignIndices(vsg::ushortArray::create({0,4,1,1,4,5, 4,6,5,5,6,7, 1,5,3,3,5,7, 0,2,4,4,2,6, 0,1,2,2,1,3, 2,3,6,6,3,7}));
        draw->instanceCount = 1;
        draw->indexCount = 36;
        commands->addChild(draw);
        volId++;
    }
};

void VBuffer::setScene(vsg::Node& scene)
{
    VBufferRenderVisitor visitor;
    visitor.commands = vsg::Commands::create();
    visitor.commands->addChild(vsg::BindGraphicsPipeline::create(pipeline));
    visitor.commands->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_VERTEX_BIT, 0, viewProjectMatrixValue));
    scene.accept(visitor);
    commands = visitor.commands;
    if (renderGraph) renderGraph->children = {commands};
}

void VBuffer::compile(vsg::Context &context) {
    depthBuffer->compile(context);
    visBuffer->compile(context);

    vsg::AttachmentDescription depthAttachment = vsg::defaultDepthAttachment(VK_FORMAT_D32_SFLOAT);
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vsg::AttachmentDescription visAttachment = vsg::defaultColorAttachment(VK_FORMAT_R32_UINT);
    visAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vsg::RenderPass::Attachments attachments{depthAttachment, visAttachment};
    vsg::RenderPass::Subpasses subpasses{
            vsg::SubpassDescription{
                    0, VK_PIPELINE_BIND_POINT_GRAPHICS, {}, {
                            VkAttachmentReference{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
                    }, {}, {
                            VkAttachmentReference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                    }, {}
            }
    };
    vsg::RenderPass::Dependencies dependencies{
        VkSubpassDependency{
            VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0
        },
        VkSubpassDependency{
                VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0
        },
    };

    auto renderPass = vsg::RenderPass::create(context.device, attachments, subpasses, dependencies);

    pipeline->renderPass = renderPass;
    pipeline->compile(context);

    renderGraph = vsg::RenderGraph::create();
    vsg::ImageViews imageViews{depthBuffer->imageInfoList[0]->imageView, visBuffer->imageInfoList[0]->imageView};
    renderGraph->framebuffer = vsg::Framebuffer::create(renderPass, imageViews, width, height, 1);
    renderGraph->renderArea = {0, 0, width, height};
    VkClearValue depthClear, visClear;
    depthClear.depthStencil.depth = 1.0f;
    depthClear.depthStencil.stencil = 0;
    visClear.color.uint32[0] = 0;
    visClear.color.uint32[1] = 0;
    visClear.color.uint32[2] = 0;
    visClear.color.uint32[3] = 0;
    renderGraph->clearValues = {depthClear, visClear};
    if (commands) renderGraph->addChild(commands);
}
