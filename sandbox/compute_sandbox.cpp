#include <filesystem>
#include <fstream>
#include <iostream>

#include <ImfArray.h>
#include <ImfRgbaFile.h>

#include <orphee/orphee.hpp>

class ComputeSandbox {
public:
  ComputeSandbox(uint32_t imageWidth, uint32_t imageHeight)
      : imgExtent{imageWidth, imageHeight} {
    // Vulkan
    VK = orphee::vkManager{{.windowing = false}};

    auto dR = VK.createDevice({
        .tag = "main",
        .count = 1,
        .capabilities = {vk::QueueFlagBits::eCompute},
    });
    if (!dR) {
      throw std::runtime_error("Failed to create device");
    }
    D = std::move(*dR);

    Q = D.queues.at("main0").get();
    // CMD
    CP = D.h.createCommandPool(
        {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, Q->fIdx});

    CMD = std::move(
        D.h.allocateCommandBuffers({CP, vk::CommandBufferLevel::ePrimary, 1})
            .front());
    // SYNC
    vk::SemaphoreTypeCreateInfo semaphoreTypeInfo{vk::SemaphoreType::eTimeline,
                                                  0};
    timelineSemaphore = D.h.createSemaphore({{}, &semaphoreTypeInfo});
    // Compute pipeline
    /** CODE **/
    std::vector<uint32_t> code;
    {
      std::filesystem::path file{"simple.spv"};
      std::ifstream shader{file, std::ios::binary};

      const auto fileSize = std::filesystem::file_size(file);
      const size_t words = fileSize / sizeof(uint32_t);

      code.resize(words);

      shader.read(reinterpret_cast<char *>(code.data()), fileSize);
    }
    auto computeModule = D.h.createShaderModule({{}, code});
    vk::PipelineShaderStageCreateInfo computeStageInfo{
        {}, vk::ShaderStageFlagBits::eCompute, computeModule, "main", {}};
    /** CODE **/
    /** args - inputs/outputs **/
    /* descriptor set & push constants <-> IO */
    vk::DescriptorSetLayoutBinding imageBinding{
        0, vk::DescriptorType::eStorageImage, 1,
        vk::ShaderStageFlagBits::eCompute};

    auto computeDescriptorLayout =
        D.h.createDescriptorSetLayout({{}, imageBinding});

    computeLayout =
        D.h.createPipelineLayout({{}, *computeDescriptorLayout, {}});
    /** args - inputs/outputs **/
    computePipeline = D.h.createComputePipeline(
        nullptr, {{}, computeStageInfo, computeLayout, {}, {}});

    vk::DescriptorPoolSize imageDescriptorPool{
        vk::DescriptorType::eStorageImage, 1};

    descriptorPool = D.h.createDescriptorPool(
        {vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1,
         imageDescriptorPool});
    /** RT **/
    vk::ImageCreateInfo imgInfo{{},
                                vk::ImageType::e2D,
                                vk::Format::eR16G16B16A16Sfloat,
                                {imgExtent.width, imgExtent.height, 1},
                                1,
                                1,
                                vk::SampleCountFlagBits::e1,
                                vk::ImageTiling::eOptimal,
                                vk::ImageUsageFlagBits::eTransferSrc |
                                    vk::ImageUsageFlagBits::eTransferDst |
                                    vk::ImageUsageFlagBits::eStorage |
                                    vk::ImageUsageFlagBits::eColorAttachment,
                                vk::SharingMode::eExclusive,
                                {},
                                {},
                                vk::ImageLayout::eUndefined};

    VmaAllocationCreateInfo imageAllocCreateInfo{};
    imageAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCreateImage(D.allocator, reinterpret_cast<VkImageCreateInfo *>(&imgInfo),
                   &imageAllocCreateInfo, reinterpret_cast<VkImage *>(&img),
                   &imgAlloc, nullptr);

    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(D.allocator, imgAlloc, &allocInfo);

    VmaAllocationCreateInfo bufferAllocCreateInfo{};
    bufferAllocCreateInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    bufferAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vk::BufferCreateInfo bufferInfo{{},
                                    allocInfo.size,
                                    vk::BufferUsageFlagBits::eTransferDst,
                                    vk::SharingMode::eExclusive,
                                    {}};
    vmaCreateBuffer(
        D.allocator, reinterpret_cast<VkBufferCreateInfo *>(&bufferInfo),
        &bufferAllocCreateInfo, reinterpret_cast<VkBuffer *>(&imgBuffer),
        &imgBufferAlloc, nullptr);

    imgView =
        D.h.createImageView({{},
                             img,
                             vk::ImageViewType::e2D,
                             vk::Format::eR16G16B16A16Sfloat,
                             {},
                             {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});

    computeDescriptorSet = std::move(
        D.h.allocateDescriptorSets({descriptorPool, *computeDescriptorLayout})
            .front());

    vk::DescriptorImageInfo imageInfo{{}, imgView, vk::ImageLayout::eGeneral};
    vk::WriteDescriptorSet writeDescriptor{computeDescriptorSet,
                                           0,
                                           {},
                                           vk::DescriptorType::eStorageImage,
                                           imageInfo,
                                           {},
                                           {}};
    D.h.updateDescriptorSets(writeDescriptor, {});
  }

  ~ComputeSandbox() {
    D.h.waitIdle();
    vmaDestroyImage(D.allocator, img, imgAlloc);
    vmaDestroyBuffer(D.allocator, imgBuffer, imgBufferAlloc);
  }

  void run() {
    CMD.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    CMD.bindPipeline(vk::PipelineBindPoint::eCompute, *computePipeline);
    CMD.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computeLayout, 0,
                           *computeDescriptorSet, {});

    vk::ImageMemoryBarrier2 toComputeBarrier{
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        Q->fIdx,
        Q->fIdx,
        img,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};
    vk::DependencyInfo toPresentInfo{{}, {}, {}, toComputeBarrier};
    CMD.pipelineBarrier2(toPresentInfo);
    CMD.dispatch(std::ceil(imgExtent.width / 16.0),
                 std::ceil(imgExtent.height / 16.0), 1);

    vk::BufferImageCopy2 copyRegion{
        0,
        0,
        0,
        vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        {0, 0, 0},
        {imgExtent.width, imgExtent.height, 1}};

    vk::CopyImageToBufferInfo2 copyImageToBuffer{img, vk::ImageLayout::eGeneral,
                                                 imgBuffer, copyRegion};
    CMD.copyImageToBuffer2(copyImageToBuffer);
    CMD.end();

    vk::CommandBufferSubmitInfo cmdSubmit{*CMD};
    vk::SemaphoreSubmitInfo signalSemaphore{timelineSemaphore, 1, {}, {}};
    vk::SubmitInfo2 info{{}, {}, cmdSubmit, signalSemaphore};
    Q->h.submit2(info);

    std::array<uint64_t, 1> waitValue{1};
    const auto wR =
        D.h.waitSemaphores({{}, *timelineSemaphore, waitValue}, UINT64_MAX);
    if (wR != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to wait for semaphore");
    }

    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(D.allocator, imgBufferAlloc, &allocInfo);

    auto pixels = std::make_unique<char[]>(allocInfo.size);

    vmaCopyAllocationToMemory(D.allocator, imgBufferAlloc, allocInfo.offset,
                              pixels.get(), allocInfo.size);

    Imf::RgbaOutputFile file("out.exr", imgExtent.width, imgExtent.height,
                             Imf::WRITE_RGBA);
    file.setFrameBuffer(reinterpret_cast<Imf::Rgba *>(pixels.get()), 1,
                        imgExtent.width);
    file.writePixels(imgExtent.height);
  }

private:
  // Vulkan
  orphee::vkManager VK;
  orphee::Device D;
  orphee::Queue *Q;
  /* CMD */
  vk::raii::CommandPool CP{nullptr};
  vk::raii::CommandBuffer CMD{nullptr};
  /* SYNC */
  vk::raii::Semaphore timelineSemaphore{nullptr};
  /* Compute */
  vk::raii::PipelineLayout computeLayout{nullptr};
  vk::raii::Pipeline computePipeline{nullptr};
  /* CMD STATE */
  vk::raii::DescriptorPool descriptorPool{nullptr};
  vk::raii::DescriptorSet computeDescriptorSet{nullptr};
  /* Compute RT */
  vk::Image img;
  vk::Extent2D imgExtent;
  vk::raii::ImageView imgView{nullptr};
  VmaAllocation imgAlloc;
  vk::Buffer imgBuffer;
  VmaAllocation imgBufferAlloc;
};

int main(int argc, char **argv) {
  try {
    ComputeSandbox sandbox{1920, 1080};
    sandbox.run();
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
  }

  return 0;
}
