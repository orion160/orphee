#include <SDL_video.h>
#include <orphee/App.hpp>
#include <orphee/RenderStage.hpp>
#include <orphee/vkManager.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>

#include <SDL.h>
#include <SDL_vulkan.h>

class SanboxStage : public orphee::RenderStage {
public:
  SanboxStage(const orphee::vkDeviceDesc &dd,
              std::unique_ptr<orphee::vkSwapChainDesc> sd)
      : mDevice{dd.device}, mSD{std::move(sd)},
        mSwapchainImages{mSD->swapchain.getImages()}, mG0{dd.queues.at("G0")} {
    vk::CommandPoolCreateInfo commandPoolInfo{
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, mG0->fIdx};
    mCP = std::make_unique<vk::raii::CommandPool>(
        mDevice->createCommandPool(commandPoolInfo));

    vk::CommandBufferAllocateInfo cmdBufferInfo{
        *mCP, vk::CommandBufferLevel::ePrimary, 1};

    mCMD = std::make_unique<vk::raii::CommandBuffer>(
        std::move(mDevice->allocateCommandBuffers(cmdBufferInfo).front()));

    vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
    mDrawFence =
        std::make_unique<vk::raii::Fence>(mDevice->createFence(fenceInfo));

    vk::SemaphoreCreateInfo swapchainSemaphoreInfo{};
    mSwapchainSemaphore = std::make_unique<vk::raii::Semaphore>(
        mDevice->createSemaphore(swapchainSemaphoreInfo));

    vk::SemaphoreCreateInfo renderSemaphoreInfo{};
    mRenderSemaphore = std::make_unique<vk::raii::Semaphore>(
        mDevice->createSemaphore(renderSemaphoreInfo));
  }

  ~SanboxStage() override { mDevice->waitIdle(); }

  void draw() override {
    const auto wfR = mDevice->waitForFences(**mDrawFence, VK_TRUE, UINT64_MAX);
    if (wfR != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to wait for fence");
    }

    mDevice->resetFences(**mDrawFence);

    const auto aiR =
        mSD->swapchain.acquireNextImage(UINT64_MAX, **mSwapchainSemaphore);
    if (aiR.first != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to acquire next image");
    }

    const auto imageIndex = aiR.second;

    // record
    mCMD->reset();
    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    mCMD->begin(beginInfo);

    // to write
    vk::ImageMemoryBarrier2 toWriteBarrier{
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        mG0->fIdx,
        mG0->fIdx,
        mSwapchainImages[imageIndex],
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};
    vk::DependencyInfo toWriteInfo{{}, {}, {}, toWriteBarrier};
    mCMD->pipelineBarrier2(toWriteInfo);

    vk::ClearColorValue clearColor(
        std::array<float, 4>{1.0F, 0.0F, 0.0F, 1.0F});
    vk::ImageSubresourceRange subresourceRange{vk::ImageAspectFlagBits::eColor,
                                               0, vk::RemainingMipLevels, 0,
                                               vk::RemainingArrayLayers};

    mCMD->clearColorImage(mSwapchainImages[imageIndex],
                          vk::ImageLayout::eGeneral, clearColor,
                          subresourceRange);

    // to present
    vk::ImageMemoryBarrier2 toPresentBarrier{
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryWrite,
        vk::PipelineStageFlagBits2::eAllCommands,
        vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
        vk::ImageLayout::eGeneral,
        vk::ImageLayout::ePresentSrcKHR,
        mG0->fIdx,
        mG0->fIdx,
        mSwapchainImages[imageIndex],
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                  vk::RemainingMipLevels, 0,
                                  vk::RemainingArrayLayers}};
    vk::DependencyInfo toPresentInfo{{}, {}, {}, toPresentBarrier};
    mCMD->pipelineBarrier2(toPresentInfo);

    mCMD->end();

    // submit
    vk::CommandBufferSubmitInfo cmdSubmitInfo{**mCMD};
    vk::SemaphoreSubmitInfo swapchainWait{
        **mSwapchainSemaphore,
        {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput};
    vk::SemaphoreSubmitInfo renderSignal{
        **mRenderSemaphore, {}, vk::PipelineStageFlagBits2::eAllGraphics};
    vk::SubmitInfo2 submitInfo{{}, swapchainWait, cmdSubmitInfo, renderSignal};
    mG0->queue.submit2(submitInfo, **mDrawFence);

    // present
    vk::PresentInfoKHR presentInfo{**mRenderSemaphore, *mSD->swapchain,
                                   imageIndex};
    const auto pR = mG0->queue.presentKHR(presentInfo);
    if (pR != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to present image");
    }
  }

  void resize() override {}

private:
  std::shared_ptr<vk::raii::Device> mDevice;
  std::unique_ptr<orphee::vkSwapChainDesc> mSD;
  std::vector<vk::Image> mSwapchainImages;
  /*
  On the future algorithms can be done with queue pools - aka mGPool - so work
  can be coordinated on Graphics queues
  */
  std::shared_ptr<orphee::vkQueueDesc> mG0;
  std::unique_ptr<vk::raii::CommandPool> mCP;
  std::unique_ptr<vk::raii::CommandBuffer> mCMD;
  std::unique_ptr<vk::raii::Fence> mDrawFence;
  std::unique_ptr<vk::raii::Semaphore> mSwapchainSemaphore;
  std::unique_ptr<vk::raii::Semaphore> mRenderSemaphore;
};

class Sandbox : public orphee::App {
public:
  Sandbox(const std::string sandboxName, uint32_t iWidth = 1080U,
          uint32_t iHeight = 720U)
      : mName{sandboxName}, mWidth{iWidth}, mHeight{iHeight} {
    SDL_Init(SDL_INIT_VIDEO);
    mWindow = SDL_CreateWindow("VK Sandbox", SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, iWidth, iHeight,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);

    unsigned int extensionCount{};
    SDL_Vulkan_GetInstanceExtensions(mWindow, &extensionCount, nullptr);
    std::vector<const char *> SDL_extensions(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(mWindow, &extensionCount,
                                     SDL_extensions.data());

    orphee::CharSet vkLayers;
    vkLayers.insert("VK_LAYER_KHRONOS_validation");

    orphee::CharSet vkExtensions;
    vkExtensions.insert(SDL_extensions.begin(), SDL_extensions.end());

    orphee::vkInit oInit{"Sandbox", 0, 0, vkLayers, vkExtensions};

    oVK = std::make_unique<orphee::vkManager>(oInit);

    mSurface = std::make_unique<vk::raii::SurfaceKHR>(oVK->createSurface(
        [w = mWindow](const vk::raii::Instance &I) -> vk::raii::SurfaceKHR {
          VkSurfaceKHR s{};
          SDL_Vulkan_CreateSurface(w, static_cast<VkInstance>(*I),
                                   static_cast<VkSurfaceKHR *>(&s));
          return vk::raii::SurfaceKHR{I, s};
        }));

    const auto dR = oVK->createDevice([]() -> orphee::vkDeviceReqs {
      orphee::CharSet ex;
      ex.insert({VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                 VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME});

      vk::PhysicalDeviceVulkan13Features f13;
      f13.setDynamicRendering(vk::True);
      f13.setSynchronization2(vk::True);
      orphee::FC f{{}, {}, {}, f13};

      return {ex,
              f,
              {{"G0", vk::QueueFlagBits::eGraphics},
               {"C0", vk::QueueFlagBits::eCompute},
               {"T0", vk::QueueFlagBits::eTransfer}}};
    });
    if (!dR) {
      throw std::runtime_error("Failed to create device");
    }

    mDD = *dR;

    vk::SwapchainCreateInfoKHR swapchainInfo{
        {},
        *mSurface,
        2,
        vk::Format::eB8G8R8A8Unorm,
        vk::ColorSpaceKHR::eSrgbNonlinear,
        {iWidth, iHeight},
        1,
        vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive,
        {},
        vk::SurfaceTransformFlagBitsKHR::eIdentity,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::PresentModeKHR::eFifo,
        VK_TRUE,
        nullptr};
    auto swapchain = mDD.device->createSwapchainKHR(swapchainInfo);

    mStage = std::make_unique<SanboxStage>(
        mDD, std::make_unique<orphee::vkSwapChainDesc>(std::move(swapchain)));
  }

  ~Sandbox() override {
    SDL_DestroyWindow(mWindow);
    SDL_Quit();
  }

  void run() override {
    SDL_Event event;
    bool isRunning{true};

    while (isRunning) {
      while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
          isRunning = false;
        }
      }
      mStage->draw();
    }
  }

private:
  // State
  std::string mName;
  uint32_t mWidth;
  uint32_t mHeight;

  // SDL
  SDL_Window *mWindow;

  // Graphics
  std::unique_ptr<orphee::vkManager> oVK;
  std::unique_ptr<vk::raii::SurfaceKHR> mSurface;
  orphee::vkDeviceDesc mDD;
  std::unique_ptr<orphee::RenderStage> mStage;
};

int main(int argc, char **argv) {
  try {
    Sandbox sandbox{"Sandbox"};
    sandbox.run();
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
  }
  return 0;
}
