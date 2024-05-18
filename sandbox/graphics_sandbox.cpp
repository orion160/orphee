#include <filesystem>
#include <iostream>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <orphee/orphee.hpp>

#include "shader/util.hpp"

class GraphicsSandbox {
public:
  GraphicsSandbox(uint32_t iWidth, uint32_t iHeight) {
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

    /** Graphics **/
    /* vertex shader */
    auto vertexCode = shader::load(std::filesystem::path{"vertex.spv"});
    auto vertexModule = D.h.createShaderModule({{}, vertexCode});
    /* fragment shader */
    auto fragmentCode = shader::load(std::filesystem::path{"fragment.spv"});
    auto fragmentModule = D.h.createShaderModule({{}, fragmentCode});
    /* shader modules */
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{
        vk::PipelineShaderStageCreateInfo{
            {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main", {}},
        vk::PipelineShaderStageCreateInfo{
            {}, vk::ShaderStageFlagBits::eFragment, fragmentModule, "main", {}},
    };
    /* vertex input */
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{{}, {}, {}};
    /* input assembly */
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        {}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
    /* viewport */
    vk::PipelineViewportStateCreateInfo viewportInfo{{}, 1, {}, 1, {}};
    /* rasterization  */
    vk::PipelineRasterizationStateCreateInfo rasterizationInfo{
        {},
        vk::False,
        vk::False,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack,
        vk::FrontFace::eClockwise,
        vk::False,
        0.0F,
        0.0F,
        0.0F,
        1.0F};
    /* sampling */
    vk::PipelineMultisampleStateCreateInfo multisampleInfo{
        {}, vk::SampleCountFlagBits::e1, vk::False, 1.0F, {}, {}, {}};
    /* color blending */
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        vk::False,
        vk::BlendFactor::eOne,
        vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        vk::BlendFactor::eOne,
        vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    vk::PipelineColorBlendStateCreateInfo colorBlendInfo{
        {}, vk::False, vk::LogicOp::eCopy, colorBlendAttachment, {}};
    /* dynamic state */
    std::array<vk::DynamicState, 2> dynamicStates{vk::DynamicState::eViewport,
                                                  vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{{}, dynamicStates};
    /* pipeline layout */
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{{}, {}, {}};
    graphicsLayout = D.h.createPipelineLayout(pipelineLayoutInfo);
    /* dynamic rendering */
    vk::Format colorFormat{vk::Format::eB8G8R8A8Unorm};
    vk::PipelineRenderingCreateInfo renderingInfo{{}, colorFormat, {}, {}};
    /* graphics pipeline creation */
    vk::GraphicsPipelineCreateInfo graphicsInfo{{},
                                                shaderStages,
                                                &vertexInputInfo,
                                                &inputAssemblyInfo,
                                                {},
                                                &viewportInfo,
                                                &rasterizationInfo,
                                                &multisampleInfo,
                                                {},
                                                &colorBlendInfo,
                                                &dynamicStateInfo,
                                                graphicsLayout,
                                                {},
                                                {},
                                                {},
                                                {},
                                                &renderingInfo};

    graphicsPipeline = D.h.createGraphicsPipeline(nullptr, graphicsInfo);

    CP = D.h.createCommandPool(
        {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, Q->fIdx});

    CMD = std::move(
        D.h.allocateCommandBuffers({CP, vk::CommandBufferLevel::ePrimary, 1})
            .front());

    DrawFence = D.h.createFence({vk::FenceCreateFlagBits::eSignaled});
    ImageAvailable = D.h.createSemaphore({});
    RenderFinished = D.h.createSemaphore({});
  }

  ~GraphicsSandbox() {
    D.h.waitIdle();
    // SDL
    SDL_DestroyWindow(mWindow);
    SDL_Quit();
  }

  void run() {
    bool isRunning = true;
    while (isRunning) {
      SDL_Event event;
      while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
          isRunning = false;
        }
      }

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
      CMD.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
      vk::Viewport viewport{0.0F,
                            0.0F,
                            static_cast<float>(SC.extent.width),
                            static_cast<float>(SC.extent.height),
                            0.0F,
                            1.0F};
      CMD.setViewport(0, viewport);

      vk::Rect2D scissor{{0, 0}, SC.extent};
      CMD.setScissor(0, scissor);
      CMD.draw(3, 1, 0, 0);
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
  std::string mName{"Graphics Sandbox"};
  // SDL
  SDL_Window *mWindow;
  // Vulkan
  orphee::vkManager VK;
  vk::raii::SurfaceKHR S{nullptr};
  orphee::Device D;
  orphee::Swapchain SC;
  orphee::Queue *Q;
  // Graphics
  vk::raii::PipelineLayout graphicsLayout{nullptr};
  vk::raii::Pipeline graphicsPipeline{nullptr};
  // CMD
  vk::raii::CommandPool CP{nullptr};
  vk::raii::CommandBuffer CMD{nullptr};
  // SYNC
  vk::raii::Fence DrawFence{nullptr};
  vk::raii::Semaphore ImageAvailable{nullptr};
  vk::raii::Semaphore RenderFinished{nullptr};
};

int main(int argc, char **argv) {
  try {
    GraphicsSandbox sandbox{1080U, 720U};
    sandbox.run();
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
  }

  return 0;
}
