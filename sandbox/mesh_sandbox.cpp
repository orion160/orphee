#include <filesystem>
#include <iostream>

#include <SDL.h>
#include <SDL_vulkan.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <orphee/orphee.hpp>

#include "mesh/util.hpp"
#include "shader/util.hpp"

struct MeshUniform {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
};

class MeshSandbox {
public:
  MeshSandbox(std::string modelPath, uint32_t iWidth, uint32_t iHeight)
      : mPath{std::move(modelPath)} {
    /** SDL **/
    SDL_Init(SDL_INIT_VIDEO);
    mWindow = SDL_CreateWindow(mName.c_str(), SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, iWidth, iHeight,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI |
                                   SDL_WINDOW_SHOWN);
    /** Vulkan **/
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
        vk::ImageUsageFlagBits::eColorAttachment,
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
    vk::DescriptorSetLayoutBinding uboLayoutBinding{
        0,
        vk::DescriptorType::eUniformBuffer,
        1,
        vk::ShaderStageFlagBits::eVertex,
        {}};
    uboLayout = D.h.createDescriptorSetLayout({{}, uboLayoutBinding});

    /* descriptors */
    vk::DescriptorPoolSize uboDescriptorPool{vk::DescriptorType::eUniformBuffer,
                                             1};

    descriptorPool = D.h.createDescriptorPool(
        {vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1,
         uboDescriptorPool});

    meshDescriptorSet = std::move(
        D.h.allocateDescriptorSets({descriptorPool, *uboLayout}).front());

    /* vertex shader */
    const auto vertexCode =
        shader::load(std::filesystem::path{"meshVertex.spv"});
    const auto vertexModule = D.h.createShaderModule({{}, vertexCode});
    /* fragment shader */
    const auto fragmentCode =
        shader::load(std::filesystem::path{"meshFragment.spv"});
    const auto fragmentModule = D.h.createShaderModule({{}, fragmentCode});
    /* shader modules */
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages{
        vk::PipelineShaderStageCreateInfo{
            {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main", {}},
        vk::PipelineShaderStageCreateInfo{
            {}, vk::ShaderStageFlagBits::eFragment, fragmentModule, "main", {}},
    };
    /* vertex input */
    vk::VertexInputBindingDescription bindingDescription{
        0, sizeof(glm::vec3), vk::VertexInputRate::eVertex};
    vk::VertexInputAttributeDescription attributeDescription{
        0, 0, vk::Format::eR32G32B32Sfloat, 0};
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        {}, bindingDescription, attributeDescription};
    /* input assembly */
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        {}, vk::PrimitiveTopology::eTriangleList, vk::False};
    /* viewport */
    vk::PipelineViewportStateCreateInfo viewportInfo{{}, 1, {}, 1, {}};
    /* rasterization  */
    vk::PipelineRasterizationStateCreateInfo rasterizationInfo{
        {},
        vk::False,
        vk::False,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack,
        vk::FrontFace::eCounterClockwise,
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
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{{}, *uboLayout};
    graphicsLayout = D.h.createPipelineLayout(pipelineLayoutInfo);
    /* dynamic rendering */
    vk::PipelineRenderingCreateInfo renderingInfo{{}, SC.format, {}, {}};
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
    /* CMD */
    CP = D.h.createCommandPool(
        {vk::CommandPoolCreateFlagBits::eResetCommandBuffer, Q->fIdx});

    CMD = std::move(
        D.h.allocateCommandBuffers({CP, vk::CommandBufferLevel::ePrimary, 1})
            .front());
    /* sync */
    DrawFence = D.h.createFence({vk::FenceCreateFlagBits::eSignaled});
    ImageAvailable = D.h.createSemaphore({});
    RenderFinished = D.h.createSemaphore({});
    /** mesh **/
    auto mO = mesh::load(std::filesystem::path{mPath});
    if (!mO) {
      throw std::runtime_error("Failed to load mesh");
    }
    mesh = std::move(*mO);
    /* RT */
    /* ubo*/
    vk::BufferCreateInfo uboInfo{{},
                                 sizeof(MeshUniform),
                                 vk::BufferUsageFlagBits::eUniformBuffer,
                                 vk::SharingMode::eExclusive,
                                 Q->fIdx};
    VmaAllocationCreateInfo uboAllocateInfo{};
    uboAllocateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    ubo = D.createBuffer(uboInfo, uboAllocateInfo);
    /* vertex buffer */
    vk::BufferCreateInfo vbStagingInfo{
        {},
        mesh.vertices.size() * sizeof(glm::vec3),
        vk::BufferUsageFlagBits::eVertexBuffer |
            vk::BufferUsageFlagBits::eTransferSrc,
        vk::SharingMode::eExclusive,
        {}};

    VmaAllocationCreateInfo vbStagingAllocateInfo{};
    vbStagingAllocateInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vbStagingAllocateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vbStaging = D.createBuffer(vbStagingInfo, vbStagingAllocateInfo);

    vk::BufferCreateInfo vbInfo{{},
                                mesh.vertices.size() * sizeof(glm::vec3),
                                vk::BufferUsageFlagBits::eVertexBuffer |
                                    vk::BufferUsageFlagBits::eTransferDst,
                                vk::SharingMode::eExclusive,
                                {}};
    VmaAllocationCreateInfo vbAllocateInfo{};
    vbAllocateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vb = D.createBuffer(vbInfo, vbAllocateInfo);

    vmaCopyMemoryToAllocation(D.allocator, mesh.vertices.data(),
                              vbStaging.allocation, 0,
                              mesh.vertices.size() * sizeof(glm::vec3));
    /* index buffer */
    vk::BufferCreateInfo ibStagingInfo{
        {},
        mesh.indices.size() * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eIndexBuffer |
            vk::BufferUsageFlagBits::eTransferSrc,
        vk::SharingMode::eExclusive,
        {}};

    VmaAllocationCreateInfo ibStagingAllocateInfo{};
    ibStagingAllocateInfo.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    ibStagingAllocateInfo.usage = VMA_MEMORY_USAGE_AUTO;

    ibStaging = D.createBuffer(ibStagingInfo, ibStagingAllocateInfo);

    vk::BufferCreateInfo ibInfo{{},
                                mesh.indices.size() * sizeof(uint32_t),
                                vk::BufferUsageFlagBits::eIndexBuffer |
                                    vk::BufferUsageFlagBits::eTransferDst,
                                vk::SharingMode::eExclusive,
                                {}};

    VmaAllocationCreateInfo ibAllocateInfo{};
    ibAllocateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    ib = D.createBuffer(ibInfo, ibAllocateInfo);

    vmaCopyMemoryToAllocation(D.allocator, mesh.indices.data(),
                              ibStaging.allocation, 0,
                              mesh.indices.size() * sizeof(uint32_t));
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
      draw();
    }
  }

  ~MeshSandbox() {
    D.h.waitIdle();

    SDL_DestroyWindow(mWindow);
    SDL_Quit();
  }

private:
  void draw() {
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

    MeshUniform meshUniform{};
    meshUniform.model = glm::rotate(glm::mat4(1.0F), glm::radians(90.0F) * dt,
                                    glm::vec3(0.0F, 0.0F, 1.0F));
    meshUniform.view =
        glm::lookAt(glm::vec3(2.0F, 2.0F, 2.0F), glm::vec3(0.0F, 0.0F, 0.0F),
                    glm::vec3(0.0F, 0.0F, 1.0F));
    meshUniform.proj = glm::perspective(
        glm::radians(45.0f),
        SC.extent.width / static_cast<float>(SC.extent.width), 0.1F, 10.0F);
    meshUniform.proj[1][1] *= -1.0F;
    vmaCopyMemoryToAllocation(D.allocator, &meshUniform, ubo.allocation, 0,
                              sizeof(MeshUniform));

    vk::DescriptorBufferInfo meshUniformInfo{ubo.h, 0, sizeof(MeshUniform)};
    vk::WriteDescriptorSet writeDescriptor{
        meshDescriptorSet, 0, {}, vk::DescriptorType::eUniformBuffer, {},
        meshUniformInfo,   {}};
    D.h.updateDescriptorSets(writeDescriptor, {});

    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    CMD.begin(beginInfo);

    vk::BufferCopy2 vbCopyRegion{0, 0,
                                 mesh.vertices.size() * sizeof(glm::vec3)};
    CMD.copyBuffer2({vbStaging.h, vb.h, vbCopyRegion});

    vk::BufferCopy2 ibCopyRegion{0, 0, mesh.indices.size() * sizeof(uint32_t)};
    CMD.copyBuffer2({ibStaging.h, ib.h, ibCopyRegion});

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
    vk::RenderingInfo renderInfo{{},
                                 {{0, 0}, {SC.extent.width, SC.extent.height}},
                                 1,
                                 {},
                                 colorAttachmentInfo,
                                 {},
                                 {}};

    CMD.beginRendering(renderInfo);
    CMD.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    CMD.bindVertexBuffers(0, vb.h, {0});
    CMD.bindIndexBuffer(ib.h, 0, vk::IndexType::eUint32);

    vk::Viewport viewport{0.0F,
                          0.0F,
                          static_cast<float>(SC.extent.width),
                          static_cast<float>(SC.extent.height),
                          0.0F,
                          1.0F};
    CMD.setViewport(0, viewport);

    vk::Rect2D scissor{{0, 0}, SC.extent};
    CMD.setScissor(0, scissor);

    CMD.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphicsLayout, 0,
                           *meshDescriptorSet, {});

    CMD.drawIndexed(mesh.indices.size(), 1, 0, 0, 0);

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
    vk::SubmitInfo2 submitInfo{{}, swapchainWait, cmdSubmitInfo, renderSignal};

    Q->h.submit2(submitInfo, *DrawFence);

    const auto pR = Q->h.presentKHR({*RenderFinished, *SC.h, imageIndex});
    if (pR != vk::Result::eSuccess) {
      throw std::runtime_error("Failed to present image");
    }

    dt += 0.01F;
  }

  /** App **/
  std::string mName{"Mesh sandbox"};
  /** SDL **/
  SDL_Window *mWindow;
  /** Vulkan **/
  orphee::vkManager VK;
  vk::raii::SurfaceKHR S{nullptr};
  orphee::Device D;
  orphee::Swapchain SC;
  orphee::Queue *Q;
  /* CMD STATE */
  vk::raii::DescriptorPool descriptorPool{nullptr};
  vk::raii::DescriptorSet meshDescriptorSet{nullptr};
  /* Graphics */
  vk::raii::DescriptorSetLayout uboLayout{nullptr};
  vk::raii::PipelineLayout graphicsLayout{nullptr};
  vk::raii::Pipeline graphicsPipeline{nullptr};
  /* CMD */
  vk::raii::CommandPool CP{nullptr};
  vk::raii::CommandBuffer CMD{nullptr};
  /* sync */
  vk::raii::Fence DrawFence{nullptr};
  vk::raii::Semaphore ImageAvailable{nullptr};
  vk::raii::Semaphore RenderFinished{nullptr};
  /** model **/
  std::string mPath;
  mesh::Mesh mesh;
  /* RT */
  orphee::vmaBuffer ubo{nullptr};
  orphee::vmaBuffer vbStaging{nullptr};
  orphee::vmaBuffer vb{nullptr};
  orphee::vmaBuffer ibStaging{nullptr};
  orphee::vmaBuffer ib{nullptr};
  //
  float dt = 0.0F;
};

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <usd file>\n";
    return 1;
  }

  try {
    MeshSandbox sandbox{std::string(argv[1]), 1080U, 720U};
    sandbox.run();
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
  }
  return 0;
}
