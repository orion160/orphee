#include <array>
#include <iostream>
#include <random>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

#include <orphee/orphee.hpp>

#include "shader/util.hpp"

struct TInfo {
  uint32_t width;
  uint32_t height;
  float max_temp;
  float min_temp;
};

class App {
public:
  App(uint32_t iWidth, uint32_t iHeight) {
    // SDL
    SDL_Init(SDL_INIT_VIDEO);
    mWindow = SDL_CreateWindow(mName.c_str(), SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, iWidth, iHeight,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI |
                                   SDL_WINDOW_SHOWN);

    tInfo.width = iWidth;
    tInfo.height = iHeight;

    // Vulkan
    VK = orphee::vkManager{{.windowing = true}};

    VkSurfaceKHR surface{};
    SDL_Vulkan_CreateSurface(mWindow, *VK.instance, &surface);
    S = vk::raii::SurfaceKHR{VK.instance, surface};

    auto dR = VK.createDevice({
        .tag = "main",
        .count = 1,
        .capabilities = {vk::QueueFlagBits::eGraphics},
        .surface = surface,
    });
    if (!dR) {
      throw std::runtime_error("Failed to create device");
    }
    D = std::move(*dR);

    Q = D.queues.at("main0").get();

    uint32_t minInageCount = 2;
    int dw{};
    int dh{};
    SDL_Vulkan_GetDrawableSize(mWindow, &dw, &dh);
    vk::Extent2D swapchainExtent{static_cast<uint32_t>(dw),
                                 static_cast<uint32_t>(dh)};
    vk::Format swapchainFormat{vk::Format::eR8G8B8A8Unorm};
    vk::SwapchainCreateInfoKHR swapchainInfo{
        {},
        *S,
        minInageCount,
        swapchainFormat,
        vk::ColorSpaceKHR::eSrgbNonlinear,
        swapchainExtent,
        1,
        vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive,
        {},
        vk::SurfaceTransformFlagBitsKHR::eIdentity,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::PresentModeKHR::eFifo,
        vk::True,
        nullptr};

    auto swapchain = D.h.createSwapchainKHR(swapchainInfo);

    auto swapchainImages = swapchain.getImages();
    std::vector<vk::raii::ImageView> swapchainImageViews;
    swapchainImageViews.reserve(swapchainImages.size());

    for (const auto &image : swapchainImages) {
      swapchainImageViews.push_back(
          D.h.createImageView({{},
                               image,
                               vk::ImageViewType::e2D,
                               swapchainFormat,
                               {},
                               {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    }

    SC = orphee::Swapchain{swapchain, swapchainImages, swapchainImageViews,
                           swapchainFormat, swapchainExtent};

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL2_InitForVulkan(mWindow);
    ImGui::StyleColorsDark();

    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler,
                                    1};
    imguiDescriptorPool = D.h.createDescriptorPool(
        {vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSize});

    vk::PipelineRenderingCreateInfo pipelineRenderingInfo{
        {}, swapchainFormat, {}, {}};

    ImGui_ImplVulkan_InitInfo imguiVulkanInit{};
    imguiVulkanInit.Instance = *VK.instance;
    imguiVulkanInit.PhysicalDevice = *D.physical;
    imguiVulkanInit.Device = *D.h;
    imguiVulkanInit.QueueFamily = Q->fIdx;
    imguiVulkanInit.Queue = *Q->h;
    imguiVulkanInit.DescriptorPool = *imguiDescriptorPool;
    imguiVulkanInit.MinImageCount = minInageCount;
    imguiVulkanInit.ImageCount = static_cast<uint32_t>(SC.images.size());
    imguiVulkanInit.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    imguiVulkanInit.UseDynamicRendering = true;
    imguiVulkanInit.PipelineRenderingCreateInfo = pipelineRenderingInfo;
    ImGui_ImplVulkan_Init(&imguiVulkanInit);

    CP = D.h.createCommandPool(
        {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, Q->fIdx});

    CMD = std::move(
        D.h.allocateCommandBuffers({CP, vk::CommandBufferLevel::ePrimary, 1})
            .front());

    DrawFence = D.h.createFence({vk::FenceCreateFlagBits::eSignaled});
    ImageAvailable = D.h.createSemaphore({});
    RenderFinished = D.h.createSemaphore({});
    /* heat transfer */
    /* code */
    auto code = shader::load("heatTransfer.spv");
    auto computeModule = D.h.createShaderModule({{}, code});
    vk::PipelineShaderStageCreateInfo htComputeStageInfo{
        {}, vk::ShaderStageFlagBits::eCompute, computeModule, "main", {}};
    /* args */
    vk::DescriptorSetLayoutBinding htBufferCurrent{
        0, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eCompute};
    vk::DescriptorSetLayoutBinding htBufferTarget{
        1, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eCompute};

    std::array<vk::DescriptorSetLayoutBinding, 2> htBindings{htBufferCurrent,
                                                             htBufferTarget};

    htArgsLayout = D.h.createDescriptorSetLayout({{}, htBindings});

    vk::PushConstantRange htPC{vk::ShaderStageFlagBits::eCompute, 0,
                               sizeof(TInfo)};

    htLayout = D.h.createPipelineLayout({{}, *htArgsLayout, htPC});
    /****/
    heatTransfer = D.h.createComputePipeline(
        nullptr, {{}, htComputeStageInfo, htLayout, {}, {}});
    /* color mapping  */
    /* code */
    auto cmCode = shader::load("colorMapping.spv");
    auto cmModule = D.h.createShaderModule({{}, cmCode});
    vk::PipelineShaderStageCreateInfo cmComputeStageInfo{
        {}, vk::ShaderStageFlagBits::eCompute, cmModule, "main", {}};
    /* args */
    vk::DescriptorSetLayoutBinding cmBufferBinding{
        0, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eCompute};

    vk::DescriptorSetLayoutBinding cmImageBinding{
        1, vk::DescriptorType::eStorageImage, 1,
        vk::ShaderStageFlagBits::eCompute};

    std::vector<vk::DescriptorSetLayoutBinding> cmBindings{cmBufferBinding,
                                                           cmImageBinding};

    cmArgsLayout = D.h.createDescriptorSetLayout({{}, cmBindings});

    vk::PushConstantRange cmPC{vk::ShaderStageFlagBits::eCompute, 0,
                               sizeof(TInfo)};

    cmLayout = D.h.createPipelineLayout({{}, *cmArgsLayout, cmPC});
    /****/
    colorMapping = D.h.createComputePipeline(
        nullptr, {{}, cmComputeStageInfo, cmLayout, {}, {}});
    /* descriptor pool */
    std::vector<vk::DescriptorPoolSize> poolSizes{
        {vk::DescriptorType::eStorageBuffer, 3},
        {vk::DescriptorType::eStorageImage, 1}};
    DP = D.h.createDescriptorPool(
        {vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 2, poolSizes});
    /* descriptor set allocation */
    htArgs =
        std::move(D.h.allocateDescriptorSets({*DP, *htArgsLayout}).front());
    cmArgs =
        std::move(D.h.allocateDescriptorSets({*DP, *cmArgsLayout}).front());
    /* RT*/
    vk::BufferCreateInfo TReferenceBufferInfo{
        {},
        iWidth * iHeight * sizeof(float),
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferSrc |
            vk::BufferUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive,
        {}};

    VmaAllocationCreateInfo TReferenceAllocInfo{};
    TReferenceAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    Treference = D.createBuffer(TReferenceBufferInfo, TReferenceAllocInfo);

    vk::BufferCreateInfo TstagingBufferInfo{
        {},
        iWidth * iHeight * sizeof(float),
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferSrc,
        vk::SharingMode::eExclusive,
        {}};

    VmaAllocationCreateInfo TStagingAllocInfo{};
    TStagingAllocInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    TStagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    Tstaging = D.createBuffer(TstagingBufferInfo, TStagingAllocInfo);

    vk::BufferCreateInfo TbufferInfo{{},
                                     iWidth * iHeight * sizeof(float),
                                     vk::BufferUsageFlagBits::eStorageBuffer |
                                         vk::BufferUsageFlagBits::eTransferDst,
                                     vk::SharingMode::eExclusive,
                                     {}};
    VmaAllocationCreateInfo vbAllocateInfo{};
    vbAllocateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    T[0] = D.createBuffer(TbufferInfo, vbAllocateInfo);
    T[1] = D.createBuffer(TbufferInfo, vbAllocateInfo);

    vk::ImageCreateInfo imgInfo{{},
                                vk::ImageType::e2D,
                                vk::Format::eR8G8B8A8Unorm,
                                {iWidth, iHeight, 1},
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

    targetImg = D.createImage(imgInfo, imageAllocCreateInfo);

    targetImgView =
        D.h.createImageView({{},
                             targetImg.h,
                             vk::ImageViewType::e2D,
                             vk::Format::eR8G8B8A8Unorm,
                             {},
                             {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
    /* RT init */
    Tdata = std::make_unique<float[]>(iWidth * iHeight);

    // initialize Tdata to zero and set a 5% to a positive value and a 5% to a
    // negative value
    std::fill(Tdata.get(), Tdata.get() + iWidth * iHeight, 0.6F);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, iWidth * iHeight - 1);

    int radius2 = 140;
    for (int i = 0; i < 40; ++i) {
      auto k = dis(gen);
      for (int x = -radius2; x < radius2; ++x) {
        for (int y = -radius2; y < radius2; ++y) {
          if (x * x + y * y < radius2 * radius2) {
            Tdata[(k + x + y * iWidth) % (iWidth * iHeight)] = 200.0F;
          }
        }
      }
    }

    int radius1 = 10;
    for (int i = 0; i < 1000; ++i) {
      auto k = dis(gen);
      for (int x = -radius1; x < radius1; ++x) {
        for (int y = -radius1; y < radius1; ++y) {
          if (x * x + y * y < radius1 * radius1) {
            Tdata[(k + x + y * iWidth) % (iWidth * iHeight)] = 900.0F;
          }
        }
      }
    }

    /*
    for (int i = 0; i < iWidth * iHeight / 10; ++i) {
      Tdata[dis(gen)] = -100.0F;
    }
    */

    /*
    for (uint32_t i = 0; i < SC.extent.width * SC.extent.height; ++i) {
      Tdata[i] = tInfo.min_temp + (tInfo.max_temp - tInfo.min_temp) *
                                      static_cast<float>(std::rand()) /
                                      RAND_MAX;
    }
    */

    vmaCopyMemoryToAllocation(Tstaging.allocator, Tdata.get(),
                              Tstaging.allocation, 0, Tstaging.size);
  }

  ~App() {
    D.h.waitIdle();
    // ImGui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    // SDL
    SDL_DestroyWindow(mWindow);
    SDL_Quit();
  }

  void run() {
    bool isRunning = true;
    while (isRunning) {
      SDL_Event event;
      while (SDL_PollEvent(&event) != 0) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) {
          isRunning = false;
        }
      }

      draw();
    }
  }

private:
  void draw() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Render();

    const auto wfR = D.h.waitForFences(*DrawFence, vk::True, UINT64_MAX);
    if (wfR != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to wait for fence");
    }

    D.h.resetFences(*DrawFence);

    const auto aiR = SC.h.acquireNextImage(UINT64_MAX, ImageAvailable);
    if (aiR.first != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to acquire next image");
    }
    const auto imageIndex = aiR.second;

    /* heat transfer */
    /* binding 0 - current grid */
    vk::DescriptorBufferInfo htCurrentInfo{T[TIdx].h, 0, T[TIdx].size};
    /* binding 1 - target grid */
    vk::DescriptorBufferInfo htTargetInfo{T[(TIdx + 1) % 2].h, 0,
                                          T[(TIdx + 1) % 2].size};
    /****/
    std::array<vk::DescriptorBufferInfo, 2> htBufferInfos{htCurrentInfo,
                                                          htTargetInfo};
    vk::WriteDescriptorSet htWriteDescriptors{
        htArgs,        0, {}, vk::DescriptorType::eStorageBuffer, {},
        htBufferInfos, {}};

    D.h.updateDescriptorSets(htWriteDescriptors, {});
    /* color mapping */
    /* binding 0 - temperature grid */
    vk::DescriptorBufferInfo bInfo{T[(TIdx + 1) % 2].h, 0,
                                   T[(TIdx + 1) % 2].size};
    vk::WriteDescriptorSet writeBufferDescriptor{
        cmArgs, 0, {}, vk::DescriptorType::eStorageBuffer, {}, bInfo, {}};
    /* binding 1 - dst img */
    vk::DescriptorImageInfo imageInfo{
        {}, targetImgView, vk::ImageLayout::eGeneral};
    vk::WriteDescriptorSet writeImageDescriptor{
        cmArgs, 1, {}, vk::DescriptorType::eStorageImage, imageInfo, {}, {}};
    /****/
    std::array<vk::WriteDescriptorSet, 2> cmWriteDescriptors{
        writeBufferDescriptor, writeImageDescriptor};
    D.h.updateDescriptorSets(cmWriteDescriptors, {});

    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    CMD.begin(beginInfo);

    if (initSim) {
      vk::BufferMemoryBarrier2 toCopyBufferSrc{
          vk::PipelineStageFlagBits2::eNone,
          vk::AccessFlagBits2::eNone,
          vk::PipelineStageFlagBits2::eTransfer,
          vk::AccessFlagBits2::eTransferRead,
          Q->fIdx,
          Q->fIdx,
          Tstaging.h,
          0,
          vk::WholeSize};
      vk::BufferMemoryBarrier2 toCopyBufferDst{
          vk::PipelineStageFlagBits2::eNone,
          vk::AccessFlagBits2::eNone,
          vk::PipelineStageFlagBits2::eTransfer,
          vk::AccessFlagBits2::eTransferWrite,
          Q->fIdx,
          Q->fIdx,
          Treference.h,
          0,
          vk::WholeSize};

      std::array<vk::BufferMemoryBarrier2, 2> toCopyBufferBarriers{
          toCopyBufferSrc, toCopyBufferDst};
      CMD.pipelineBarrier2({{}, {}, toCopyBufferBarriers, {}});

      vk::BufferCopy2 stagingToBufferInfo{0, 0, Tstaging.size};
      vk::CopyBufferInfo2 copyBufferInfo{Tstaging.h, Treference.h,
                                         stagingToBufferInfo};
      CMD.copyBuffer2(copyBufferInfo);

      vk::BufferMemoryBarrier2 copyRefSrc{vk::PipelineStageFlagBits2::eCopy,
                                          vk::AccessFlagBits2::eTransferWrite,
                                          vk::PipelineStageFlagBits2::eCopy,
                                          vk::AccessFlagBits2::eTransferRead,
                                          Q->fIdx,
                                          Q->fIdx,
                                          Treference.h,
                                          0,
                                          vk::WholeSize};
      vk::BufferMemoryBarrier2 copyRefDst{vk::PipelineStageFlagBits2::eCopy,
                                          vk::AccessFlagBits2::eTransferWrite,
                                          vk::PipelineStageFlagBits2::eCopy,
                                          vk::AccessFlagBits2::eTransferWrite,
                                          Q->fIdx,
                                          Q->fIdx,
                                          T[TIdx].h,
                                          0,
                                          vk::WholeSize};

      std::array<vk::BufferMemoryBarrier2, 2> copyRefBarriers{copyRefSrc,
                                                              copyRefDst};

      vk::DependencyInfo toCopyToRef{{}, {}, copyRefBarriers, {}};
      CMD.pipelineBarrier2(toCopyToRef);

      initSim = false;
    } else {
      vk::BufferMemoryBarrier2 copyRefSrc{vk::PipelineStageFlagBits2::eNone,
                                          vk::AccessFlagBits2::eNone,
                                          vk::PipelineStageFlagBits2::eCopy,
                                          vk::AccessFlagBits2::eTransferRead,
                                          Q->fIdx,
                                          Q->fIdx,
                                          Treference.h,
                                          0,
                                          vk::WholeSize};
      vk::BufferMemoryBarrier2 copyRefDst{vk::PipelineStageFlagBits2::eNone,
                                          vk::AccessFlagBits2::eNone,
                                          vk::PipelineStageFlagBits2::eCopy,
                                          vk::AccessFlagBits2::eTransferWrite,
                                          Q->fIdx,
                                          Q->fIdx,
                                          T[TIdx].h,
                                          0,
                                          vk::WholeSize};

      std::array<vk::BufferMemoryBarrier2, 2> copyRefBarriers{copyRefSrc,
                                                              copyRefDst};

      vk::DependencyInfo toCopyToRef{{}, {}, copyRefBarriers, {}};
      CMD.pipelineBarrier2(toCopyToRef);
    }

    vk::BufferCopy2 refToCurCopy{0, 0, Treference.size};
    vk::CopyBufferInfo2 copyReftoCurInfo{Treference.h, T[TIdx].h, refToCurCopy};
    CMD.copyBuffer2(copyReftoCurInfo);

    vk::BufferMemoryBarrier2 toHeatTransferCurrent{
        vk::PipelineStageFlagBits2::eCopy,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead,
        Q->fIdx,
        Q->fIdx,
        T[TIdx].h,
        0,
        vk::WholeSize};

    vk::BufferMemoryBarrier2 toHeatTransferTarget{
        vk::PipelineStageFlagBits2::eCopy,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        Q->fIdx,
        Q->fIdx,
        T[(TIdx + 1) % 2].h,
        0,
        vk::WholeSize};

    std::array<vk::BufferMemoryBarrier2, 2> toHeatTransferBarriers{
        toHeatTransferCurrent, toHeatTransferTarget};
    vk::DependencyInfo toHeatTransferInfo{{}, {}, toHeatTransferBarriers, {}};
    CMD.pipelineBarrier2(toHeatTransferInfo);

    // heat transfer
    CMD.bindPipeline(vk::PipelineBindPoint::eCompute, heatTransfer);
    CMD.bindDescriptorSets(vk::PipelineBindPoint::eCompute, htLayout, 0,
                           *htArgs, {});
    CMD.pushConstants<TInfo>(htLayout, vk::ShaderStageFlagBits::eCompute, 0,
                             tInfo);
    CMD.dispatch(SC.extent.width, SC.extent.height, 1);

    vk::BufferMemoryBarrier2 toColorMapBuffer{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead |
            vk::AccessFlagBits2::eShaderStorageWrite,
        Q->fIdx,
        Q->fIdx,
        T[(TIdx + 1) % 2].h,
        0,
        vk::WholeSize};

    vk::ImageMemoryBarrier2 toColorMapImage{
        vk::PipelineStageFlagBits2::eNone,
        vk::AccessFlagBits2::eNone,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderWrite,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        Q->fIdx,
        Q->fIdx,
        targetImg.h,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};
    vk::DependencyInfo toColorMapInfo{
        {}, {}, toColorMapBuffer, toColorMapImage};
    CMD.pipelineBarrier2(toColorMapInfo);

    // color mapping
    CMD.bindPipeline(vk::PipelineBindPoint::eCompute, colorMapping);
    CMD.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cmLayout, 0,
                           *cmArgs, {});
    CMD.pushConstants<TInfo>(cmLayout, vk::ShaderStageFlagBits::eCompute, 0,
                             tInfo);

    CMD.dispatch(SC.extent.width, SC.extent.height, 1);

    vk::ImageMemoryBarrier2 toCopyImageSrc{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderWrite,
        vk::PipelineStageFlagBits2::eCopy,
        vk::AccessFlagBits2::eTransferRead,
        vk::ImageLayout::eGeneral,
        vk::ImageLayout::eTransferSrcOptimal,
        Q->fIdx,
        Q->fIdx,
        targetImg.h,
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};

    vk::ImageMemoryBarrier2 toCopyImageDst{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderWrite,
        vk::PipelineStageFlagBits2::eCopy,
        vk::AccessFlagBits2::eTransferWrite,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        Q->fIdx,
        Q->fIdx,
        SC.images[imageIndex],
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};

    std::array<vk::ImageMemoryBarrier2, 2> toCopyImageBarriers{toCopyImageSrc,
                                                               toCopyImageDst};
    vk::DependencyInfo toCopyImageInfo{{}, {}, {}, toCopyImageBarriers};
    CMD.pipelineBarrier2(toCopyImageInfo);

    vk::ImageCopy2 copyRegion{
        vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        {0, 0, 0},
        vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        {0, 0, 0},
        {SC.extent.width, SC.extent.height, 1}};

    vk::CopyImageInfo2 copyImageInfo{
        targetImg.h, vk::ImageLayout::eTransferSrcOptimal,
        SC.images[imageIndex], vk::ImageLayout::eTransferDstOptimal,
        copyRegion};

    CMD.copyImage2(copyImageInfo);

    /*
    vk::ImageMemoryBarrier2 toWriteBarrier{
        vk::PipelineStageFlagBits2::eCopy,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eColorAttachmentOptimal,
        Q->fIdx,
        Q->fIdx,
        SC.images[imageIndex],
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};
    vk::DependencyInfo toWriteInfo{{}, {}, {}, toWriteBarrier};
    CMD.pipelineBarrier2(toWriteInfo);

    vk::RenderingAttachmentInfo colorAttachmentInfo{
        SC.views[imageIndex],
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        {},
        {},
        vk::AttachmentLoadOp::eClear,
        vk::AttachmentStoreOp::eStore,
        {{0.0F, 0.0F, 0.0F, 0.0F}}};
    vk::RenderingInfo renderInfo{{},
                                 {{0, 0}, {SC.extent.width, SC.extent.height}},
                                 1,
                                 {},
                                 colorAttachmentInfo,
                                 {},
                                 {}};

    CMD.beginRendering(renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *CMD);
    CMD.endRendering();
    */

    vk::ImageMemoryBarrier2 toPresentBarrier{
        vk::PipelineStageFlagBits2::eCopy,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eNone,
        vk::AccessFlagBits2::eNone,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        Q->fIdx,
        Q->fIdx,
        SC.images[imageIndex],
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};
    vk::DependencyInfo toPresentInfo{{}, {}, {}, toPresentBarrier};
    CMD.pipelineBarrier2(toPresentInfo);

    CMD.end();

    vk::CommandBufferSubmitInfo cmdSubmitInfo{*CMD};
    vk::SemaphoreSubmitInfo swapchainWait{
        *ImageAvailable, {}, vk::PipelineStageFlagBits2::eAllCommands};
    vk::SemaphoreSubmitInfo renderSignal{
        *RenderFinished, {}, vk::PipelineStageFlagBits2::eAllCommands};
    vk::SubmitInfo2 submitInfo{{}, swapchainWait, cmdSubmitInfo, renderSignal};

    Q->h.submit2(submitInfo, *DrawFence);

    const auto pR = Q->h.presentKHR({*RenderFinished, *SC.h, imageIndex});
    if (pR != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to present image");
    }

    TIdx = (TIdx + 1) % 2;
  }

  // App
  std::string mName{"Heat Transfer"};
  // SDL
  SDL_Window *mWindow;
  // Vulkan
  orphee::vkManager VK;
  vk::raii::SurfaceKHR S{nullptr};
  orphee::Device D;
  orphee::Swapchain SC;
  orphee::Queue *Q;
  // ImGui
  vk::raii::DescriptorPool imguiDescriptorPool{nullptr};
  // render
  vk::raii::CommandPool CP{nullptr};
  vk::raii::CommandBuffer CMD{nullptr};
  vk::raii::Fence DrawFence{nullptr};
  vk::raii::Semaphore ImageAvailable{nullptr};
  vk::raii::Semaphore RenderFinished{nullptr};
  /* DP */
  vk::raii::DescriptorPool DP{nullptr};
  /* RT */
  /* host */
  std::unique_ptr<float[]> Tdata;
  /* device */
  orphee::vmaBuffer Tstaging{nullptr};
  orphee::vmaBuffer Treference{nullptr};
  std::array<orphee::vmaBuffer, 2> T{nullptr, nullptr};
  uint32_t TIdx = 0;
  orphee::vmaImage targetImg{nullptr};
  vk::raii::ImageView targetImgView{nullptr};
  /* heat transfer */
  vk::raii::Pipeline heatTransfer{nullptr};
  vk::raii::PipelineLayout htLayout{nullptr};
  vk::raii::DescriptorSetLayout htArgsLayout{nullptr};
  vk::raii::DescriptorSet htArgs{nullptr};
  /* color map */
  vk::raii::Pipeline colorMapping{nullptr};
  vk::raii::PipelineLayout cmLayout{nullptr};
  vk::raii::DescriptorSetLayout cmArgsLayout{nullptr};
  vk::raii::DescriptorSet cmArgs{nullptr};
  /* ht */
  TInfo tInfo{{}, {}, 0.0F, 1000.0F};
  bool initSim = true;
};

int main(int argc, char **argv) {
  try {
    App sandbox{1080U, 720U};
    sandbox.run();
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
  }

  return 0;
}
