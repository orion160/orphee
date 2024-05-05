#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include <orphee/orphee.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

class Sandbox {
public:
  Sandbox(std::string appName, uint32_t iWidth, uint32_t iHeight)
      : mName{std::move(appName)} {
    // SDL
    SDL_Init(SDL_INIT_VIDEO);
    mWindow = SDL_CreateWindow(appName.c_str(), SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, iWidth, iHeight,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI |
                                   SDL_WINDOW_SHOWN);

    // Vulkan
    unsigned int extensionCount{};
    SDL_Vulkan_GetInstanceExtensions(mWindow, &extensionCount, nullptr);
    std::vector<const char *> SDL_extensions(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(mWindow, &extensionCount,
                                     SDL_extensions.data());

    orphee::CharSet vkLayers;
    vkLayers.insert("VK_LAYER_KHRONOS_validation");

    orphee::CharSet vkExtensions;
    vkExtensions.insert(SDL_extensions.begin(), SDL_extensions.end());

    mVK = std::make_unique<orphee::vkManager>(
        orphee::vkInit{mName, 0U, 0U, vkLayers, vkExtensions});

    // create surface
    VkSurfaceKHR s{};
    SDL_Vulkan_CreateSurface(mWindow, *mVK->instance, &s);
    mSurface = vk::raii::SurfaceKHR{mVK->instance, s};

    // create device
    orphee::CharSet dExtensions;
    dExtensions.insert({VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME});

    vk::PhysicalDeviceVulkan13Features f13;
    f13.setDynamicRendering(vk::True);
    f13.setSynchronization2(vk::True);
    orphee::FC f{{}, {}, {}, f13};

    orphee::DeviceRequirements r{dExtensions,
                                 f,
                                 {{"G", vk::QueueFlagBits::eGraphics},
                                  {"C", vk::QueueFlagBits::eCompute},
                                  {"T", vk::QueueFlagBits::eTransfer}}};

    auto dR = mVK->createDevice(r);
    if (!dR) {
      throw std::runtime_error("Failed to create device");
    }
    mDevice = std::make_unique<orphee::Device>(std::move(*dR));
    mG = &mDevice->queues.at("G");

    // create swapchain
    uint32_t minInageCount = 2;
    int dw{};
    int dh{};
    SDL_Vulkan_GetDrawableSize(mWindow, &dw, &dh);
    vk::Extent2D swapchainExtent{static_cast<uint32_t>(dw),
                                 static_cast<uint32_t>(dh)};
    vk::Format swapchainFormat{vk::Format::eB8G8R8A8Unorm};
    vk::SwapchainCreateInfoKHR swapchainInfo{
        {},
        *mSurface,
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
        VK_TRUE,
        nullptr};
    auto swapchain = mDevice->device.createSwapchainKHR(swapchainInfo);

    auto swapchainImages = swapchain.getImages();
    std::vector<vk::raii::ImageView> swapchainImageViews;
    swapchainImageViews.reserve(swapchainImages.size());

    for (const auto &image : swapchainImages) {
      swapchainImageViews.push_back(mDevice->device.createImageView(
          {{},
           image,
           vk::ImageViewType::e2D,
           swapchainFormat,
           {},
           {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    }

    mSwapchain = std::make_unique<orphee::Swapchain>(
        std::move(swapchain), swapchainImages, std::move(swapchainImageViews),
        swapchainFormat, swapchainExtent);

    // draw setup
    mCP = mDevice->device.createCommandPool(
        {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, mG->fIdx});
    mCMD = std::move(
        mDevice->device
            .allocateCommandBuffers({mCP, vk::CommandBufferLevel::ePrimary, 1})
            .front());

    mDrawFence =
        mDevice->device.createFence({vk::FenceCreateFlagBits::eSignaled});
    mImageAvailable = mDevice->device.createSemaphore({});
    mRenderFinished = mDevice->device.createSemaphore({});
  }

  ~Sandbox() {
    mDevice->device.waitIdle();
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

      const auto wfR =
          mDevice->device.waitForFences(*mDrawFence, vk::True, UINT64_MAX);
      if (wfR != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for fence");
      }

      mDevice->device.resetFences(*mDrawFence);

      const auto aiR =
          mSwapchain->swapchain.acquireNextImage(UINT64_MAX, mImageAvailable);
      if (aiR.first != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to acquire next image");
      }
      const auto imageIndex = aiR.second;

      vk::CommandBufferBeginInfo beginInfo{
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
      mCMD.begin(beginInfo);

      vk::ImageMemoryBarrier2 toWriteBarrier{
          vk::PipelineStageFlagBits2::eAllCommands,
          vk::AccessFlagBits2::eMemoryWrite,
          vk::PipelineStageFlagBits2::eAllCommands,
          vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
          vk::ImageLayout::eUndefined,
          vk::ImageLayout::eGeneral,
          mG->fIdx,
          mG->fIdx,
          mSwapchain->images[imageIndex],
          vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                    vk::RemainingMipLevels, 0,
                                    vk::RemainingArrayLayers}};
      vk::DependencyInfo toWriteInfo{{}, {}, {}, toWriteBarrier};
      mCMD.pipelineBarrier2(toWriteInfo);

      mCMD.clearColorImage(
          mSwapchain->images[imageIndex],
          vk::ImageLayout::eGeneral,
          vk::ClearColorValue{std::array<float, 4>{1.0F, 0.0F, 0.0F, 1.0F}},
          vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                    vk::RemainingMipLevels, 0,
                                    vk::RemainingArrayLayers});

      vk::ImageMemoryBarrier2 toPresentBarrier{
          vk::PipelineStageFlagBits2::eAllCommands,
          vk::AccessFlagBits2::eMemoryWrite,
          vk::PipelineStageFlagBits2::eAllCommands,
          vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
          vk::ImageLayout::eGeneral,
          vk::ImageLayout::ePresentSrcKHR,
          mG->fIdx,
          mG->fIdx,
          mSwapchain->images[imageIndex],
          vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0,
                                    vk::RemainingMipLevels, 0,
                                    vk::RemainingArrayLayers}};
      vk::DependencyInfo toPresentInfo{{}, {}, {}, toPresentBarrier};
      mCMD.pipelineBarrier2(toPresentInfo);

      mCMD.end();

      vk::CommandBufferSubmitInfo cmdSubmitInfo{*mCMD};
      vk::SemaphoreSubmitInfo swapchainWait{
          *mImageAvailable, {}, vk::PipelineStageFlagBits2::eAllCommands};
      vk::SemaphoreSubmitInfo renderSignal{
          *mRenderFinished, {}, vk::PipelineStageFlagBits2::eAllCommands};
      vk::SubmitInfo2 submitInfo{
          {}, swapchainWait, cmdSubmitInfo, renderSignal};

      mG->queue.submit2(submitInfo, *mDrawFence);

      const auto pR = mG->queue.presentKHR(
          {*mRenderFinished, *mSwapchain->swapchain, imageIndex});
      if (pR != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present image");
      }
    }
  }

private:
  // App
  std::string mName;
  // SDL
  SDL_Window *mWindow;
  // Vulkan
  std::unique_ptr<orphee::vkManager> mVK;
  vk::raii::SurfaceKHR mSurface{nullptr};
  std::unique_ptr<orphee::Device> mDevice;
  orphee::Queue *mG;
  std::unique_ptr<orphee::Swapchain> mSwapchain;

  // ImGui
  vk::raii::DescriptorPool imguiDescriptorPool{nullptr};

  // render
  vk::raii::CommandPool mCP{nullptr};
  vk::raii::CommandBuffer mCMD{nullptr};
  vk::raii::Fence mDrawFence{nullptr};
  vk::raii::Semaphore mImageAvailable{nullptr};
  vk::raii::Semaphore mRenderFinished{nullptr};
};

int main(int argc, char **argv) {
  try {
    Sandbox sandbox{"Sandbox", 1080U, 720U};
    sandbox.run();
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
  }
  return 0;
}
