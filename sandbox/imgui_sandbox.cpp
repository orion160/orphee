#include <iostream>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

#include <orphee/orphee.hpp>

class ImGuiSandox {
public:
  ImGuiSandox(uint32_t iWidth, uint32_t iHeight) {
    // SDL
    SDL_Init(SDL_INIT_VIDEO);
    mWindow = SDL_CreateWindow(mName.c_str(), SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, iWidth, iHeight,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI |
                                   SDL_WINDOW_SHOWN);

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
    vk::Format swapchainFormat{vk::Format::eB8G8R8A8Unorm};
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
    auto descriptorPool = D.h.createDescriptorPool(
        {vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSize});
    imguiDescriptorPool = std::move(descriptorPool);

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
  }

  ~ImGuiSandox() {
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

      vk::CommandBufferBeginInfo beginInfo{
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
      CMD.begin(beginInfo);

      vk::ImageMemoryBarrier2 toWriteBarrier{
          vk::PipelineStageFlagBits2::eNone,
          vk::AccessFlagBits2::eNone,
          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          vk::AccessFlagBits2::eColorAttachmentWrite,
          vk::ImageLayout::eUndefined,
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
          {{0.0F, 0.0F, 0.0F, 1.0F}}};
      vk::RenderingInfo renderInfo{
          {},
          {{0, 0}, {SC.extent.width, SC.extent.height}},
          1,
          {},
          colorAttachmentInfo,
          {},
          {}};

      CMD.beginRendering(renderInfo);
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *CMD);
      CMD.endRendering();

      vk::ImageMemoryBarrier2 toPresentBarrier{
          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          vk::AccessFlagBits2::eColorAttachmentWrite,
          vk::PipelineStageFlagBits2::eNone,
          vk::AccessFlagBits2::eNone,
          vk::ImageLayout::eColorAttachmentOptimal,
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
      vk::SubmitInfo2 submitInfo{
          {}, swapchainWait, cmdSubmitInfo, renderSignal};

      Q->h.submit2(submitInfo, *DrawFence);

      const auto pR = Q->h.presentKHR({*RenderFinished, *SC.h, imageIndex});
      if (pR != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present image");
      }
    }
  }

private:
  // App
  std::string mName{"ImGui Sandox"};
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
};

int main(int argc, char **argv) {
  try {
    ImGuiSandox sandbox{1080U, 720U};
    sandbox.run();
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
  }

  return 0;
}
