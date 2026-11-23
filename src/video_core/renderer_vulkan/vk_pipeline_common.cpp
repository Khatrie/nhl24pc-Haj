// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstddef>
#include <cstring>
#include <utility>

#include "shader_recompiler/resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

Pipeline::Pipeline(const Instance& instance_, Scheduler& scheduler_, DescriptorHeap& desc_heap_,
                   const Shader::Profile& profile_, vk::PipelineCache pipeline_cache,
                   bool is_compute_ /*= false*/)
    : instance{instance_}, scheduler{scheduler_}, desc_heap{desc_heap_}, profile{profile_},
      is_compute{is_compute_} {}

Pipeline::~Pipeline() = default;

namespace {
union DescriptorPayload {
    vk::DescriptorBufferInfo buffer;
    vk::DescriptorImageInfo image;
};
constexpr size_t MaxTemplateDescriptors = Shader::NUM_BUFFERS + Shader::NUM_IMAGES +
                                          Shader::NUM_SAMPLERS;
} // namespace

void Pipeline::BuildDescriptorTemplateEntries(
    std::span<const vk::DescriptorSetLayoutBinding> bindings) {
    size_t offset = 0;
    for (const auto& binding : bindings) {
        descriptor_entries.push_back({
            .dstBinding = binding.binding,
            .descriptorCount = binding.descriptorCount,
            .descriptorType = binding.descriptorType,
            .offset = offset,
            .stride = sizeof(DescriptorPayload),
        });
        offset += binding.descriptorCount * sizeof(DescriptorPayload);
    }
    if (offset > MaxTemplateDescriptors * sizeof(DescriptorPayload)) {
        descriptor_entries.clear();
    }
}

void Pipeline::CreateDescriptorTemplate() {
    if (descriptor_entries.empty()) {
        return;
    }
    const vk::DescriptorUpdateTemplateCreateInfo info{
        .descriptorUpdateEntryCount = static_cast<u32>(descriptor_entries.size()),
        .pDescriptorUpdateEntries = descriptor_entries.data(),
        .templateType = uses_push_descriptors ? vk::DescriptorUpdateTemplateType::ePushDescriptors
                                              : vk::DescriptorUpdateTemplateType::eDescriptorSet,
        .descriptorSetLayout = uses_push_descriptors ? vk::DescriptorSetLayout{} : *desc_layout,
        .pipelineBindPoint =
            IsCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics,
        .pipelineLayout = *pipeline_layout,
        .set = 0,
    };
    auto [result, handle] = instance.GetDevice().createDescriptorUpdateTemplateUnique(info);
    if (result == vk::Result::eSuccess) {
        descriptor_template = std::move(handle);
    }
}

void Pipeline::BindResources(DescriptorWrites& set_writes,
                             const Shader::PushData& push_data) const {
    const auto cmdbuf = scheduler.CommandBuffer();
    const auto bind_point =
        IsCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;

    const auto stage_flags = IsCompute() ? vk::ShaderStageFlagBits::eCompute : AllGraphicsStageBits;
    cmdbuf.pushConstants(*pipeline_layout, stage_flags, 0u, sizeof(push_data), &push_data);

    // Bind descriptor set.
    if (set_writes.empty()) {
        return;
    }

    alignas(DescriptorPayload) std::byte payload[MaxTemplateDescriptors * sizeof(DescriptorPayload)];
    bool use_template = descriptor_template && set_writes.size() == descriptor_entries.size();
    if (use_template) {
        for (size_t i = 0; i < set_writes.size(); ++i) {
            const auto& write = set_writes[i];
            const auto& entry = descriptor_entries[i];
            if (write.dstBinding != entry.dstBinding || write.dstArrayElement != 0 ||
                write.descriptorCount != entry.descriptorCount ||
                write.descriptorType != entry.descriptorType) {
                use_template = false;
                break;
            }
            auto* dst = payload + entry.offset;
            for (u32 j = 0; j < write.descriptorCount; ++j, dst += entry.stride) {
                if (write.descriptorType == vk::DescriptorType::eStorageBuffer) {
                    std::memcpy(dst, &write.pBufferInfo[j], sizeof(vk::DescriptorBufferInfo));
                } else {
                    std::memcpy(dst, &write.pImageInfo[j], sizeof(vk::DescriptorImageInfo));
                }
            }
        }
    }

    if (uses_push_descriptors) {
        if (use_template) {
            cmdbuf.pushDescriptorSetWithTemplateKHR(*descriptor_template, *pipeline_layout, 0,
                                                   payload);
        } else {
            cmdbuf.pushDescriptorSetKHR(bind_point, *pipeline_layout, 0, set_writes);
        }
        return;
    }

    const auto desc_set = desc_heap.Commit(*desc_layout);
    if (use_template) {
        instance.GetDevice().updateDescriptorSetWithTemplate(desc_set, *descriptor_template,
                                                             payload);
    } else {
        for (auto& set_write : set_writes) {
            set_write.dstSet = desc_set;
        }
        instance.GetDevice().updateDescriptorSets(set_writes, {});
    }
    cmdbuf.bindDescriptorSets(bind_point, *pipeline_layout, 0, desc_set, {});
}

std::string Pipeline::GetDebugString() const {
    std::string stage_desc;
    for (const auto& stage : stages) {
        if (stage) {
            const auto shader_name = PipelineCache::GetShaderName(stage->hw_stage, stage->pgm_hash);
            if (stage_desc.empty()) {
                stage_desc = shader_name;
            } else {
                stage_desc = fmt::format("{},{}", stage_desc, shader_name);
            }
        }
    }
    return stage_desc;
}

} // namespace Vulkan
