#pragma once

// Reimplementation of the Blockwise multi order feature regression method presented in https://webpages.tuni.fi/foi/papers/Koskela-TOG-2019-Blockwise_Multi_Order_Feature_Regression_for_Real_Time_Path_Tracing_Reconstruction.pdf
// This version is converted to vulkan and optimized, such that it uses less kernels, and does require accumulated images to be given to it.
// Further the spacial features used for feature fitting are in screen space to reduce calculation efforts.

#include "../TAA/taa.hpp"

#include <vsg/all.h>


class BMFR: public vsg::Inherit<vsg::Object, BMFR>{
public:
    uint depthBinding = 0, normalBinding = 1, materialBinding = 2, albedoBinding = 3, motionBinding = 4, sampleBinding = 5, sampledDenIlluBinding = 6, finalBinding = 7, noisyBinding = 8, denoisedBinding = 9, featureBufferBinding = 10, weightsBinding = 11;
    uint amtOfFeatures = 13;
    
    uint width, height, workWidth, workHeight, fittingKernel, widthPadded, heightPadded;
    vsg::ref_ptr<GBuffer> gBuffer;
    vsg::ref_ptr<vsg::Sampler> sampler;
    vsg::ref_ptr<vsg::BindComputePipeline> bindPrePipeline, bindFitPipeline, bindPostPipeline;
    vsg::ref_ptr<vsg::ComputePipeline> bmfrPrePipeline, bmfrFitPipeline, bmfrPostPipeline;
    vsg::ref_ptr<vsg::DescriptorImage> accumulatedIllumination, finalIllumination, featureBuffer, rMat, weights;
    vsg::ref_ptr<vsg::BindDescriptorSet> bindDescriptorSet;
    vsg::ref_ptr<Taa> taaPipeline;

    BMFR(uint width, uint height, uint workWidth, uint workHeight, vsg::ref_ptr<GBuffer> gBuffer, vsg::ref_ptr<IlluminationBuffer> illuBuffer, vsg::ref_ptr<AccumulationBuffer> accBuffer, uint fittingKernel = 256):
    width(width),
    height(height),
    workWidth(workWidth),
    workHeight(workHeight),
    fittingKernel(fittingKernel),
    widthPadded((width / workWidth + 1) * workWidth),
    heightPadded((height / workHeight + 1) * workHeight),
    gBuffer(gBuffer),
    sampler(vsg::Sampler::create())
    {
        if(!illuBuffer.cast<IlluminationBufferFinalDemodulated>()) return;        //demodulated illumination needed 
        auto illumination = illuBuffer;
        std::string preShaderPath = "shaders/bmfrPre.comp.spv";
        std::string fitShaderPath = "shaders/bmfrFit.comp.spv";
        std::string postShaderPath = "shaders/bmfrPost.comp.spv";
        auto preComputeStage = vsg::ShaderStage::readSpv(VK_SHADER_STAGE_COMPUTE_BIT, "main", preShaderPath);
        auto fitComputeStage = vsg::ShaderStage::readSpv(VK_SHADER_STAGE_COMPUTE_BIT, "main", fitShaderPath);
        auto postComputeStage = vsg::ShaderStage::readSpv(VK_SHADER_STAGE_COMPUTE_BIT, "main", postShaderPath);
        preComputeStage->specializationConstants = vsg::ShaderStage::SpecializationConstants{
            {0, vsg::intValue::create(width)},
            {1, vsg::intValue::create(height)},
            {2, vsg::intValue::create(workWidth)},
            {3, vsg::intValue::create(workHeight)},
            {4, vsg::intValue::create(workWidth)}};

        fitComputeStage->specializationConstants = vsg::ShaderStage::SpecializationConstants{
            {0, vsg::intValue::create(width)},
            {1, vsg::intValue::create(height)},
            {2, vsg::intValue::create(fittingKernel)},
            {3, vsg::intValue::create(1)},
            {4, vsg::intValue::create(workWidth)}};
        
        postComputeStage->specializationConstants = vsg::ShaderStage::SpecializationConstants{
            {0, vsg::intValue::create(width)},
            {1, vsg::intValue::create(height)},
            {2, vsg::intValue::create(workWidth)},
            {3, vsg::intValue::create(workHeight)},
            {4, vsg::intValue::create(workWidth)}};

        // denoised illuminatino accumulation
        auto image = vsg::Image::create();
        image->imageType = VK_IMAGE_TYPE_2D;
        image->format = VK_FORMAT_R16G16B16A16_SFLOAT;
        image->extent.width = width;
        image->extent.height = height;
        image->extent.depth = 1;
        image->mipLevels = 1;
        image->arrayLayers = 2;
        image->samples = VK_SAMPLE_COUNT_1_BIT;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        image->usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
        imageView->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vsg::ImageInfo imageInfo = {nullptr, imageView, VK_IMAGE_LAYOUT_GENERAL};
        accumulatedIllumination = vsg::DescriptorImage::create(imageInfo, denoisedBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        imageInfo.sampler = sampler;
        auto sampledAccIllu = vsg::DescriptorImage::create(imageInfo, sampledDenIlluBinding, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        image = vsg::Image::create();
        image->imageType = VK_IMAGE_TYPE_2D;
        image->format = VK_FORMAT_R16G16B16A16_SFLOAT;
        image->extent.width = width;
        image->extent.height = height;
        image->extent.depth = 1;
        image->mipLevels = 1;
        image->arrayLayers = 1;
        image->samples = VK_SAMPLE_COUNT_1_BIT;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        image->usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
        imageInfo = {nullptr, imageView, VK_IMAGE_LAYOUT_GENERAL};
        finalIllumination = vsg::DescriptorImage::create(imageInfo, finalBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        //feature buffer image array
        image = vsg::Image::create();
        image->imageType = VK_IMAGE_TYPE_2D;
        image->format = VK_FORMAT_R16_SFLOAT;
        image->extent.width = widthPadded;
        image->extent.height = heightPadded;
        image->extent.depth = 1;
        image->mipLevels = 1;
        image->arrayLayers = amtOfFeatures;
        image->samples = VK_SAMPLE_COUNT_1_BIT;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        image->usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
        imageView->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        imageInfo = {nullptr, imageView, VK_IMAGE_LAYOUT_GENERAL};
        featureBuffer = vsg::DescriptorImage::create(imageInfo, featureBufferBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        
        //weights buffer
        image = vsg::Image::create();
        image->imageType = VK_IMAGE_TYPE_2D;
        image->format = VK_FORMAT_R32_SFLOAT;
        image->extent.width = widthPadded / workWidth;
        image->extent.height = heightPadded / workHeight;
        image->extent.depth = 1;
        image->mipLevels = 1;
        image->arrayLayers = (amtOfFeatures - 3) * 3;
        image->samples = VK_SAMPLE_COUNT_1_BIT;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        image->usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageView = vsg::ImageView::create(image, VK_IMAGE_ASPECT_COLOR_BIT);
        imageView->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        imageInfo = {nullptr, imageView, VK_IMAGE_LAYOUT_GENERAL};
        weights = vsg::DescriptorImage::create(imageInfo, weightsBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        // descriptor set layout
        vsg::DescriptorSetLayoutBindings descriptorBindings{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {11, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
        };
        auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);
        // filling descriptor set
        vsg::Descriptors descriptors{
            vsg::DescriptorImage::create(gBuffer->depth->imageInfoList[0], depthBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            vsg::DescriptorImage::create(gBuffer->normal->imageInfoList[0], normalBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            vsg::DescriptorImage::create(gBuffer->material->imageInfoList[0], materialBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            vsg::DescriptorImage::create(gBuffer->albedo->imageInfoList[0], albedoBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            vsg::DescriptorImage::create(accBuffer->motion->imageInfoList[0], motionBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            vsg::DescriptorImage::create(accBuffer->spp->imageInfoList[0], sampleBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            vsg::DescriptorImage::create(illumination->illuminationImages[1]->imageInfoList[0], noisyBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            accumulatedIllumination,
            finalIllumination,
            sampledAccIllu,
            featureBuffer,
            weights
        };
        auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, descriptors);

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RayTracingPushConstants)}});
        bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, descriptorSet);

        bmfrPrePipeline = vsg::ComputePipeline::create(pipelineLayout, preComputeStage);
        bmfrFitPipeline = vsg::ComputePipeline::create(pipelineLayout, fitComputeStage);
        bmfrPostPipeline = vsg::ComputePipeline::create(pipelineLayout, postComputeStage);
        bindPrePipeline = vsg::BindComputePipeline::create(bmfrPrePipeline);
        bindFitPipeline = vsg::BindComputePipeline::create(bmfrFitPipeline);
        bindPostPipeline = vsg::BindComputePipeline::create(bmfrPostPipeline);

        taaPipeline = Taa::create(width, height, workWidth, workHeight, gBuffer, accBuffer, finalIllumination);
    }

    void addDispatchToCommandGraph(vsg::ref_ptr<vsg::Commands> commandGraph, vsg::ref_ptr<vsg::PushConstants> pushConstants){
        auto pipelineBarrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT);
        uint dispatchX = widthPadded / workWidth, dispatchY = heightPadded / workHeight;
        // pre pipeline
        commandGraph->addChild(bindPrePipeline);
        commandGraph->addChild(bindDescriptorSet);
        commandGraph->addChild(pushConstants);
        commandGraph->addChild(vsg::Dispatch::create(dispatchX, dispatchY, 1));
        commandGraph->addChild(pipelineBarrier);
        
        // fit pipeline
        commandGraph->addChild(bindFitPipeline);
        commandGraph->addChild(bindDescriptorSet);
        commandGraph->addChild(pushConstants);
        commandGraph->addChild(vsg::Dispatch::create(dispatchX, dispatchY, 1));
        commandGraph->addChild(pipelineBarrier);

        // post pipeline
        commandGraph->addChild(bindPostPipeline);
        commandGraph->addChild(bindDescriptorSet);
        commandGraph->addChild(pushConstants);
        commandGraph->addChild(vsg::Dispatch::create(dispatchX, dispatchY, 1));
        commandGraph->addChild(pipelineBarrier);

        taaPipeline->addDispatchToCommandGraph(commandGraph);
    }

    void compileImages(vsg::Context& context){
        accumulatedIllumination->compile(context);
        finalIllumination->compile(context);
        taaPipeline->compile(context);
        featureBuffer->compile(context);
        weights->compile(context);
    }

    void updateImageLayouts(vsg::Context& context){
        VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0 , 1, 0, 2};
        auto accIlluLayout = vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 0, accumulatedIllumination->imageInfoList[0].imageView->image, resourceRange);
        resourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0 , 1, 0, 1};
        auto finalIluLayout = vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 0, finalIllumination->imageInfoList[0].imageView->image, resourceRange);
        resourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0 , 1, 0, amtOfFeatures};
        auto featureBufferLayout = vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 0, featureBuffer->imageInfoList[0].imageView->image, resourceRange);
        resourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0 , 1, 0, (amtOfFeatures - 3) * 3};
        auto weightsLayout = vsg::ImageMemoryBarrier::create(VK_ACCESS_NONE_KHR, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, 0, weights->imageInfoList[0].imageView->image, resourceRange);

        auto pipelineBarrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT,
            accIlluLayout, finalIluLayout, featureBufferLayout, weightsLayout);
        context.commands.push_back(pipelineBarrier);
        taaPipeline->updateImageLayouts(context);
    }
};
