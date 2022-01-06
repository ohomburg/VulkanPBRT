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

vsg::ref_ptr<vsg::ImageInfo> createImage(uint32_t width, uint32_t height, VkFormat format)
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
    image->usage = VK_IMAGE_USAGE_STORAGE_BIT;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    auto view = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
    view->viewType = VK_IMAGE_VIEW_TYPE_2D;

    return vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, view, VK_IMAGE_LAYOUT_GENERAL);
}

A_SVGF::A_SVGF(uint32_t width, uint32_t height, vsg::ref_ptr<GBuffer> gBuffer, vsg::ref_ptr<IlluminationBuffer> illuBuffer, vsg::ref_ptr<AccumulationBuffer> accBuffer)
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
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
        {13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr},
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
    accum_color_prev = createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
    accum_moments = createImage(width, height, VK_FORMAT_R32G32_SFLOAT);
    accum_moments_prev = createImage(width, height, VK_FORMAT_R32G32_SFLOAT);
    accum_histlen = createImage(width, height, VK_FORMAT_R16_SFLOAT);
    accum_histlen_prev = createImage(width, height, VK_FORMAT_R16_SFLOAT);
    varA = createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
    varB = createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT);

    vsg::Descriptors desc0 {
        // TODO: find the correct inputs for the question-marked entries
        vsg::DescriptorImage::create(/*unfiltered color?*/illuBuffer->illuminationImages[0]->imageInfoList, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(accBuffer->prevIllu->imageInfoList, 1, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(/*gradient positions?*/accBuffer->motion->imageInfoList, 2, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(gBuffer->albedo->imageInfoList, 3, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(/*color?*/illuBuffer->illuminationImages[0]->imageInfoList, 4, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(/*prev color? schied has color history buffer*/accBuffer->prevIllu->imageInfoList, 5, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(accBuffer->motion->imageInfoList, 6, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(gBuffer->depth->imageInfoList, 7, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(accBuffer->prevDepth->imageInfoList, 8, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(accum_moments_prev, 9, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(accum_histlen_prev, 10, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(gBuffer->normal->imageInfoList, 11, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        vsg::DescriptorImage::create(accBuffer->prevNormal->imageInfoList, 12, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        /*TODO: 13, 14: curr and prev VBuffer (mesh ids)*/
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
    for (auto &img : {diffA1, diffA2, diffB1, diffB2, accum_color, accum_moments, accum_histlen, accum_color_prev, accum_moments_prev, accum_histlen_prev, varA, varB})
    {
        img->imageView->compile(ctx);
    }
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
    commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_ALL, 0, pushConstVal));
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
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_ALL, 0, pushConstVal));
        commandGraph->addChild(vsg::Dispatch::create(gradTileWidth, gradTileHeight, 1));
        commandGraph->addChild(pipelineBarrier);
    }

    // 3. Temporal Accumulation
    // TODO: double check mapping of prev accumulation textures.
    commandGraph->addChild(bindPipelines.tempAccum);
    commandGraph->addChild(bindDescriptorSet1A);
    commandGraph->addChild(vsg::Dispatch::create(tileWidth, tileHeight, 1));

    // 4. Estimate Variance
    commandGraph->addChild(bindPipelines.estVariance);
    commandGraph->addChild(vsg::Dispatch::create(tileWidth, tileHeight, 1));

    // TODO: blit source to color history unfiltered (does not exist yet)

    // 5. Atrous
    commandGraph->addChild(bindPipelines.atrous);
    for (int i = 0; i < NumIterations; i++)
    {
        // TODO: blit to color history if (i-1)==HistoryTap

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
        commandGraph->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_ALL, 0, pushConstVal));
        commandGraph->addChild(vsg::Dispatch::create(tileWidth, tileHeight, 1));
        commandGraph->addChild(pipelineBarrier);
    }

    // TODO: blit to target?
}

vsg::ref_ptr<vsg::DescriptorImage> A_SVGF::getFinalDescriptorImage() const
{
    return vsg::DescriptorImage::create(varA, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
}
