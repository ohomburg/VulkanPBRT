#include "A_SVGF.hpp"

/*
Copyright 2021 Oskar Homburg

Adapted in part from a_svgf, Copyright (c) 2018, Christoph Schied
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Karlsruhe Institute of Technology nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

vsg::ref_ptr<vsg::ImageInfo> createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage = 0)
{
    auto image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = format;
    image->extent.width = width;
    image->extent.height = height;
    image->extent.depth = 1;
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage = VK_IMAGE_USAGE_STORAGE_BIT | usage;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    auto view = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
    view->viewType = VK_IMAGE_VIEW_TYPE_2D;

    return vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, view, VK_IMAGE_LAYOUT_GENERAL);
}

A_SVGF::A_SVGF(uint32_t width, uint32_t height, vsg::ref_ptr<GBuffer> gBuffer,
               vsg::ref_ptr<IlluminationBuffer> illuBuffer, vsg::ref_ptr<AccumulationBuffer> accBuffer,
               vsg::ref_ptr<GradientProjector> gradProjector)
    : width(width), height(height)
{
    PerPass<const char*> shaderNames{"shaders/a-svgf/CreateGradientSamples.comp.spv",
                        "shaders/a-svgf/AtrousGradient.comp.spv",
                        "shaders/a-svgf/TemporalAccumulation.comp.spv",
                        "shaders/a-svgf/EstimateVariance.comp.spv",
                        "shaders/a-svgf/Atrous.comp.spv"};

    auto shaderStages = shaderNames.map<vsg::ref_ptr<vsg::ShaderStage>>([](const auto &name) {
        return vsg::ShaderStage::read(VK_SHADER_STAGE_COMPUTE_BIT, "main", name);
    });

    shaderStages.atrous->specializationConstants = vsg::ShaderStage::SpecializationConstants{
        {0, vsg::intValue::create(FilterKernel)}
    };

    vsg::DescriptorSetLayoutBindings layoutBindings0 = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {14, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
    };

    vsg::DescriptorSetLayoutBindings layoutBindings1 = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_ALL, nullptr},
    };

    auto setLayout0 = vsg::DescriptorSetLayout::create(layoutBindings0), setLayout1 = vsg::DescriptorSetLayout::create(layoutBindings1);

    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{setLayout0, setLayout1},
         vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ASvgfPushConst)}});

    pipelines = shaderStages.map<vsg::ref_ptr<vsg::ComputePipeline>>([&](const auto& shader) {
        return vsg::ComputePipeline::create(pipelineLayout, shader);
    });

    bindPipelines = pipelines.map<vsg::ref_ptr<vsg::BindComputePipeline>>([](const auto& pipeline) {
        return vsg::BindComputePipeline::create(pipeline);
    });

    // create internal resources.
    uint32_t gradWidth = (width + GradientDownsample - 1) / GradientDownsample, gradHeight = (height + GradientDownsample - 1) / GradientDownsample;
    diffA1 = createImage(gradWidth, gradHeight, VK_FORMAT_R32G32B32A32_SFLOAT);
    diffA2 = createImage(gradWidth, gradHeight, VK_FORMAT_R32G32B32A32_SFLOAT);
    diffB1 = createImage(gradWidth, gradHeight, VK_FORMAT_R32G32B32A32_SFLOAT);
    diffB2 = createImage(gradWidth, gradHeight, VK_FORMAT_R32G32B32A32_SFLOAT);
    accum_color = createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
    accum_moments = createImage(width, height, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    accum_moments_prev = createImage(width, height, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    accum_histlen = createImage(width, height, VK_FORMAT_R16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    accum_histlen_prev = createImage(width, height, VK_FORMAT_R16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    varA = createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    varB = createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    color_hist = createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    vsg::Descriptors desc0 {
        vsg::DescriptorImage::create(/*irradiance*/illuBuffer->illuminationImages[0]->imageInfoList, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(/*prev irrad*/accBuffer->prevIllu->imageInfoList, 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(/*grad block sample xy*/gradProjector->gradientSamples->imageInfoList, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(gBuffer->albedo->imageInfoList, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(/*color*/illuBuffer->illuminationImages[0]->imageInfoList, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(color_hist, 5, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accBuffer->motion->imageInfoList, 6, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(gBuffer->depth->imageInfoList, 7, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accBuffer->prevDepth->imageInfoList, 8, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(accum_moments_prev, 9, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accum_histlen_prev, 10, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(gBuffer->normal->imageInfoList, 11, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accBuffer->prevNormal->imageInfoList, 12, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(gradProjector->mergedVisBuffer->imageInfoList, 13, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(gradProjector->prevVisBuffer->imageInfoList, 14, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
    };

    vsg::Descriptors desc1A {
        vsg::DescriptorImage::create(diffA1, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(diffA2, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(diffB1, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(diffB2, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accum_color, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accum_moments, 5, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accum_histlen, 6, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(varA, 7, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(varB, 8, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
    };
    vsg::Descriptors desc1B { // the same, but all the A/Bs switched around.
        vsg::DescriptorImage::create(diffB1, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(diffB2, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(diffA1, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(diffA2, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accum_color, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accum_moments, 5, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(accum_histlen, 6, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(varB, 7, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(varA, 8, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
    };

    auto ds0 = vsg::DescriptorSet::create(setLayout0, desc0);
    auto ds1A = vsg::DescriptorSet::create(setLayout1, desc1A);
    auto ds1B = vsg::DescriptorSet::create(setLayout1, desc1B);
    bindDescriptorSet0 = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, ds0);
    bindDescriptorSet1A = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, ds1A);
    bindDescriptorSet1B = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 1, ds1B);
}

void A_SVGF::compile(vsg::Context &ctx)
{
    for (auto &desc : bindDescriptorSet0->descriptorSet->descriptors)
        desc->compile(ctx);
    for (auto &desc : bindDescriptorSet1A->descriptorSet->descriptors)
        desc->compile(ctx);
    for (auto &desc : bindDescriptorSet1B->descriptorSet->descriptors)
        desc->compile(ctx);
}

void A_SVGF::addDispatchToCommandGraph(vsg::ref_ptr<vsg::Commands> commandGraph)
{
    // Passes to run:
    // 1. Create Gradient Samples
    // 2. Atrous Gradient
    // 3. Temporal Accumulation
    // 4. Estimate Variance
    // 5. Atrous

    auto pushConstVal = vsg::Value<ASvgfPushConst>::create(ASvgfPushConst{
        vsg::vec2{0, 0},
        0,
        0,
        GradientDownsample,
        TemporalAlpha,
        ModulateAlbedo,
    });

    auto tileWidth = (width + 7) / 8, tileHeight = (height + 7) / 8;
    auto gradWidth = (width + GradientDownsample - 1) / GradientDownsample, gradHeight = (height + GradientDownsample - 1) / GradientDownsample;
    auto gradTileWidth = (gradWidth + 7) / 8, gradTileHeight = (gradHeight + 7) / 8;

    // 1. Create Gradient Samples
    commandGraph->addChild(bindPipelines.createGradSamples);
    commandGraph->addChild(bindDescriptorSet0);
    commandGraph->addChild(bindDescriptorSet1A);
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstVal));
    commandGraph->addChild(vsg::Dispatch::create(gradTileWidth, gradTileHeight, 1));

    auto pipelineBarrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
    commandGraph->addChild(pipelineBarrier);

    // 2. Atrous Gradient
    commandGraph->addChild(bindPipelines.atrousGrad);
    for (int i = 0; i < DiffAtrousIterations; i++)
    {
        // swap the diff textures around each iteration.
        commandGraph->addChild(bindDescriptorSet1A);
        std::swap(bindDescriptorSet1A, bindDescriptorSet1B);

        pushConstVal = vsg::Value<ASvgfPushConst>::create(ASvgfPushConst{
                vsg::vec2{0, 0},
                i,
                1 << i,
                GradientDownsample,
                TemporalAlpha,
                ModulateAlbedo,
        });
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstVal));
        commandGraph->addChild(vsg::Dispatch::create(gradTileWidth, gradTileHeight, 1));
        commandGraph->addChild(pipelineBarrier);
    }

    // 3. Temporal Accumulation
    commandGraph->addChild(bindPipelines.tempAccum);
    commandGraph->addChild(bindDescriptorSet1A);
    commandGraph->addChild(vsg::Dispatch::create(tileWidth, tileHeight, 1));
    commandGraph->addChild(pipelineBarrier);

    // 4. Estimate Variance
    commandGraph->addChild(bindPipelines.estVariance);
    commandGraph->addChild(vsg::Dispatch::create(tileWidth, tileHeight, 1));
    commandGraph->addChild(pipelineBarrier);

    if (NumIterations == 0)
    {
        auto copyCmd = vsg::CopyImage::create();
        copyCmd->srcImage = (DiffAtrousIterations & 1) ? varB->imageView->image : varA->imageView->image;
        copyCmd->srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        copyCmd->dstImage = color_hist->imageView->image;
        copyCmd->dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        copyCmd->regions = {VkImageCopy{
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {0, 0, 0},
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {0, 0, 0},
                {width, height, 1}
        }};
        commandGraph->addChild(copyCmd);
    }

    // 5. Atrous
    commandGraph->addChild(bindPipelines.atrous);
    for (int i = 0; i < NumIterations; i++)
    {
        if (HistoryTap == i - 1)
        {
            auto copyCmd = vsg::CopyImage::create();
            copyCmd->srcImage = ((DiffAtrousIterations + i) & 1) ? varB->imageView->image : varA->imageView->image;
            copyCmd->srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
            copyCmd->dstImage = color_hist->imageView->image;
            copyCmd->dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
            copyCmd->regions = {VkImageCopy{
                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    {0, 0, 0},
                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    {0, 0, 0},
                    {width, height, 1}
            }};
            commandGraph->addChild(copyCmd);
        }

        // swap the textures around each iteration.
        commandGraph->addChild(bindDescriptorSet1A);
        std::swap(bindDescriptorSet1A, bindDescriptorSet1B);

        pushConstVal = vsg::Value<ASvgfPushConst>::create(ASvgfPushConst{
                vsg::vec2{0, 0},
                i,
                1 << i,
                GradientDownsample,
                TemporalAlpha,
                ModulateAlbedo && i == NumIterations - 1,
        });
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstVal));
        commandGraph->addChild(vsg::Dispatch::create(tileWidth, tileHeight, 1));
        commandGraph->addChild(pipelineBarrier);
    }

    if (NumIterations > 0 && HistoryTap == NumIterations - 1)
    {
        auto copyCmd = vsg::CopyImage::create();
        copyCmd->srcImage = ((DiffAtrousIterations + NumIterations) & 1) ? varB->imageView->image : varA->imageView->image;
        copyCmd->srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        copyCmd->dstImage = color_hist->imageView->image;
        copyCmd->dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
        copyCmd->regions = {VkImageCopy{
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {0, 0, 0},
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {0, 0, 0},
                {width, height, 1}
        }};
        commandGraph->addChild(copyCmd);
    }

    // copy accum to prev
    auto copyCmd = vsg::CopyImage::create();
    copyCmd->srcImage = accum_histlen->imageView->image;
    copyCmd->srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    copyCmd->dstImage = accum_histlen_prev->imageView->image;
    copyCmd->dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    copyCmd->regions = {VkImageCopy{
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {width, height, 1}
    }};
    commandGraph->addChild(copyCmd);
    copyCmd = vsg::CopyImage::create();
    copyCmd->srcImage = accum_moments->imageView->image;
    copyCmd->srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    copyCmd->dstImage = accum_moments_prev->imageView->image;
    copyCmd->dstImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    copyCmd->regions = {VkImageCopy{
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {width, height, 1}
    }};
    commandGraph->addChild(copyCmd);
}

vsg::ref_ptr<vsg::DescriptorImage> A_SVGF::getFinalDescriptorImage() const
{
    auto img = ((DiffAtrousIterations + NumIterations) & 1) ? varB : varA;
    return vsg::DescriptorImage::create(img, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
}

void A_SVGF::updateImageLayouts(vsg::Context &context)
{
    auto barr = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

    VkImageSubresourceRange rr{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (auto& img : {diffA1, diffA2, diffB1, diffB2, accum_color, accum_moments, accum_histlen, accum_moments_prev, accum_histlen_prev, varA, varB, color_hist})
    {
        barr->add(vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 0, img->imageView->image, rr));
    }

    context.commands.emplace_back(barr);
}

// clear command
class ClearColorImage : public vsg::Inherit<vsg::Command, ClearColorImage>
{
public:
    ClearColorImage() = default;

    ClearColorImage(vsg::ref_ptr<vsg::Image> image, VkImageLayout layout, vsg::vec4 color)
        : image(std::move(image)), layout(layout), value()
    {
        value.float32[0] = color[0];
        value.float32[1] = color[1];
        value.float32[2] = color[2];
        value.float32[3] = color[3];
        ranges.push_back(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS});
    }

    ClearColorImage(vsg::ref_ptr<vsg::Image> image, VkImageLayout layout, vsg::uivec4 color)
            : image(std::move(image)), layout(layout), value()
    {
        value.uint32[0] = color[0];
        value.uint32[1] = color[1];
        value.uint32[2] = color[2];
        value.uint32[3] = color[3];
        ranges.push_back(VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS});
    }

    vsg::ref_ptr<vsg::Image> image{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkClearColorValue value{};
    std::vector<VkImageSubresourceRange> ranges{};

    void record(vsg::CommandBuffer &commandBuffer) const override
    {
        vkCmdClearColorImage(commandBuffer, image->vk(commandBuffer.deviceID), layout, &value, static_cast<uint32_t>(ranges.size()), ranges.data());
    }
};

GradientProjector::GradientProjector(vsg::ref_ptr<VBuffer> vBuffer)
    : width(vBuffer->width), height(vBuffer->height)
{
    auto image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = VK_FORMAT_R32G32B32A32_SFLOAT;
    image->extent.width = width;
    image->extent.height = height;
    image->extent.depth = 1;
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    auto imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
    auto imageInfo = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, imageView, VK_IMAGE_LAYOUT_GENERAL);
    mergedVisBuffer = vsg::DescriptorImage::create(imageInfo, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = VK_FORMAT_R32G32B32A32_SFLOAT;
    image->extent.width = width;
    image->extent.height = height;
    image->extent.depth = 1;
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
    imageInfo = vsg::ImageInfo::create(vsg::Sampler::create(), imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    prevVisBuffer = vsg::DescriptorImage::create(imageInfo, 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = VK_FORMAT_R32_UINT;
    image->extent.width = (width + GradientDownsample - 1) / GradientDownsample;
    image->extent.height = (height + GradientDownsample - 1) / GradientDownsample;
    image->extent.depth = 1;
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
    imageInfo = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, imageView, VK_IMAGE_LAYOUT_GENERAL);
    gradientSamples = vsg::DescriptorImage::create(imageInfo, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    auto shaderCreateImg = vsg::ShaderStage::read(VK_SHADER_STAGE_COMPUTE_BIT, "main", "shaders/a-svgf/GradientImg.comp.spv");
    auto shaderProject = vsg::ShaderStage::read(VK_SHADER_STAGE_COMPUTE_BIT, "main", "shaders/a-svgf/GradientForwardProject.comp.spv");

    shaderProject->specializationConstants = vsg::ShaderStage::SpecializationConstants{
            {0, vsg::intValue::create(GradientDownsample)},
            {1, vsg::intValue::create(width)},
            {2, vsg::intValue::create(height)},
    };

    auto bindingMap = shaderProject->getDescriptorSetLayoutBindingsMap();
    auto dsetLayout = vsg::DescriptorSetLayout::create(bindingMap.begin()->second.bindings);
    auto descriptorSet = vsg::DescriptorSet::create(dsetLayout, vsg::Descriptors{
        vsg::DescriptorImage::create(vBuffer->depthBuffer->imageInfoList[0], 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        prevVisBuffer,
        vsg::DescriptorImage::create(vBuffer->visBuffer->imageInfoList[0], 2, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        mergedVisBuffer,
        gradientSamples,
    });
    auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsetLayout}, vsg::PushConstantRanges{
        {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GradientProjectPushConst)}
    });
    bindProject = vsg::BindComputePipeline::create(vsg::ComputePipeline::create(pipelineLayout, shaderProject));
    bindDescriptorSetP = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, descriptorSet);

    pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsetLayout}, vsg::PushConstantRanges{});
    bindCreateImg = vsg::BindComputePipeline::create(vsg::ComputePipeline::create(pipelineLayout, shaderCreateImg));
    bindDescriptorSetCI = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, descriptorSet);
}

void GradientProjector::addDispatchToCommandGraph(vsg::ref_ptr<vsg::Commands> commandGraph)
{
    auto barrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0);
    // transition previous buffer
    auto prevBufferBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    prevBufferBarrier->image = prevVisBuffer->imageInfoList[0]->imageView->image;
    prevBufferBarrier->subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier->add(prevBufferBarrier);
    commandGraph->addChild(barrier);

    auto copyCmd = vsg::CopyImage::create();
    copyCmd->regions = {VkImageCopy{
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {0, 0, 0},
            {width, height, 1}
    }};
    copyCmd->srcImage = mergedVisBuffer->imageInfoList[0]->imageView->image;
    copyCmd->srcImageLayout = VK_IMAGE_LAYOUT_GENERAL;
    copyCmd->dstImage = prevVisBuffer->imageInfoList[0]->imageView->image;
    copyCmd->dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    commandGraph->addChild(copyCmd);

    commandGraph->addChild(ClearColorImage::create(gradientSamples->imageInfoList[0]->imageView->image, VK_IMAGE_LAYOUT_GENERAL, vsg::uivec4{0, 0, 0, 0}));

    barrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
    // transition previous buffer
    prevBufferBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    prevBufferBarrier->image = prevVisBuffer->imageInfoList[0]->imageView->image;
    prevBufferBarrier->subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier->add(prevBufferBarrier);
    // data dependency barrier on current buffer
    auto currBufferBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    currBufferBarrier->image = mergedVisBuffer->imageInfoList[0]->imageView->image;
    currBufferBarrier->subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier->add(currBufferBarrier);
    // data dependency barrier on gradient samples buffer
    auto gradBufferBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    gradBufferBarrier->image = gradientSamples->imageInfoList[0]->imageView->image;
    gradBufferBarrier->subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier->add(gradBufferBarrier);
    commandGraph->addChild(barrier);

    commandGraph->addChild(bindCreateImg);
    commandGraph->addChild(bindDescriptorSetCI);
    commandGraph->addChild(vsg::Dispatch::create((width + 7) / 8, (height + 7) / 8, 1));

    barrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
    // data dependency barrier on current buffer
    currBufferBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    currBufferBarrier->image = mergedVisBuffer->imageInfoList[0]->imageView->image;
    currBufferBarrier->subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier->add(currBufferBarrier);
    commandGraph->addChild(barrier);

    commandGraph->addChild(bindProject);
    commandGraph->addChild(bindDescriptorSetP);
    pushConstValue = vsg::Value<GradientProjectPushConst>::create(GradientProjectPushConst{
        vsg::mat4(),
        0
    });
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstValue));
    auto gradWidth = (width + GradientDownsample - 1) / GradientDownsample, gradHeight = (height + GradientDownsample - 1) / GradientDownsample;
    auto gradTileWidth = (gradWidth + 7) / 8, gradTileHeight = (gradHeight + 7) / 8;
    commandGraph->addChild(vsg::Dispatch::create(gradTileWidth, gradTileHeight, 1));

    barrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0);
    // data dependency barrier on current buffer
    currBufferBarrier = vsg::ImageMemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    currBufferBarrier->image = mergedVisBuffer->imageInfoList[0]->imageView->image;
    currBufferBarrier->subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier->add(currBufferBarrier);
    commandGraph->addChild(barrier);
}

void GradientProjector::compile(vsg::Context& context)
{
    for (auto& desc : bindDescriptorSetCI->descriptorSet->descriptors)
        desc->compile(context);
    for (auto& desc : bindDescriptorSetP->descriptorSet->descriptors)
        desc->compile(context);
}

void GradientProjector::updateImageLayouts(vsg::Context &context) const
{
    auto barr = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);

    VkImageSubresourceRange rr{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barr->add(vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 0, mergedVisBuffer->imageInfoList[0]->imageView->image, rr));
    barr->add(vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0, prevVisBuffer->imageInfoList[0]->imageView->image, rr));
    barr->add(vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 0, gradientSamples->imageInfoList[0]->imageView->image, rr));

    context.commands.emplace_back(barr);
}

void GradientProjector::updateDescriptor(vsg::BindDescriptorSet *descSet, const vsg::BindingMap &bindingMap) const
{
    int mergedIdx = vsg::ShaderStage::getSetBindingIndex(bindingMap, "merged_vbuf").second;
    auto merged = vsg::DescriptorImage::create(mergedVisBuffer->imageInfoList, mergedIdx);
    merged->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descSet->descriptorSet->descriptors.push_back(merged);
}

void GradientProjector::updatePushConstants(vsg::dmat4 projMatrixD, vsg::dmat4 viewMatrixD, unsigned int frameNum) {
    vsg::mat4 projMatrix{projMatrixD};
    vsg::mat4 viewMatrix{viewMatrixD};
    pushConstValue->value().frameNum = frameNum;
    vsg::mat4 normalizeMat(1.0f / (float)width, 0, 0, 0,
                           0, 1.0f / (float)height, 0, 0,
                           0, 0, 1, 0,
                           0, 0, 0, 1);
    pushConstValue->value().reprojectionMatrix = projMatrix * viewMatrix * inverse(prevViewMatrix) * inverse(prevProjMatrix) * normalizeMat;
    prevProjMatrix = projMatrix;
    prevViewMatrix = viewMatrix;
}
