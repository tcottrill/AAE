// sys_vk.h
// Vulkan backend helpers.
// ASCII-only comments.

#pragma once

#include <vulkan/vulkan.h>

#include <stdint.h>
#include <vector>
#include <string>

// Platform-neutral: surface creation, required instance extensions, and
// drawable size all come from the window layer's IPresentSurface contract
// (aae/system/window/sys_window.h). No windows.h, no vulkan_win32.h here.
class IPresentSurface;

//Forward Declare
class ScreenQuadVK;

// -----------------------------------------------------------------------------
// VkTexture
// Small texture POD owned by caller; created/destroyed by sys_vk helpers.
// -----------------------------------------------------------------------------
struct VkTexture
{
    VkImage        image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;

    VkFormat       format = VK_FORMAT_UNDEFINED;
    uint32_t       width = 0;
    uint32_t       height = 0;
    uint32_t       mipLevels = 1;  // Number of mip levels in the image view
};

// -----------------------------------------------------------------------------
// VkContext
// Owns Vulkan instance/device/swapchain + fullscreen pipeline + per-frame sync.
// -----------------------------------------------------------------------------
struct VkContext
{
    static const uint32_t kFramesInFlight = 2;

    // The window-layer presentation contract (never null while initialized).
    IPresentSurface* present = nullptr;
    // Runtime handle for vulkan-1.dll / libvulkan.so.1 (spec sec. 6: no
    // import-lib link; the loader is bound with LoadLibrary/dlopen so builds
    // need no Vulkan SDK or package installed).
    void* loaderModule = nullptr;

    // Swapchain sizing
    VkExtent2D swapchainExtent{ 0, 0 };
    VkFormat   swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;

    // True when CreateSwapchain was able to add TRANSFER_SRC to the swapchain
    // images' usage (i.e. the surface reported it in supportedUsageFlags).
    // Required to vkCmdCopyImageToBuffer out of a presentable image, which is
    // what the F12 screenshot readback does. Every desktop driver supports it,
    // but it is optional per spec, so the flag is queried rather than assumed
    // and consumers degrade (snapshot reports failure) instead of the
    // swapchain failing to create.
    bool swapchainTransferSrc = false;

    // Color format of the currently open dynamic-rendering pass. Set by
    // whoever opens a pass (RenderTarget::VK_Begin_, the compositor screen
    // pass, legacy VK_BeginFrame); reset to UNDEFINED when it closes.
    // Dynamic rendering requires a pipeline's declared attachment format to
    // match the active pass's attachment. Subsystems that record draws must
    // therefore select/build their pipeline against VK_ActiveColorFormat()
    // below, NOT against swapchainFormat -- the platformer renders into an
    // R8G8B8A8_UNORM RenderTarget while the swapchain is *_SRGB.
    VkFormat activeColorFormat = VK_FORMAT_UNDEFINED;

    // Present configuration. vsync=true selects FIFO; vsync=false prefers
    // MAILBOX, then IMMEDIATE, falling back to FIFO when unsupported.
    // presentMode records what CreateSwapchain actually chose.
    bool             vsync = true;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    // Optional validation instrumentation (opt-in via VK_Init). When the
    // Khronos validation layer and the debug-utils extension are available,
    // messages are routed into the engine log.
    bool                     validationEnabled = false;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // Core handles
    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice         device = VK_NULL_HANDLE;
    VkSurfaceKHR     surface = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain = VK_NULL_HANDLE;

    // Queues
    uint32_t gfxQueueFamily = 0;
    VkQueue  gfxQueue = VK_NULL_HANDLE;
    VkQueue  presentQueue = VK_NULL_HANDLE;

    // Swapchain images/views
    std::vector<VkImage>     swapchainImages;
    std::vector<VkImageView> swapchainViews;

    // Per-frame
    uint32_t frameIndex = 0;
    VkCommandPool               cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBuffers;

    VkSemaphore imageAvailable[kFramesInFlight]{};
    VkFence     inFlight[kFramesInFlight]{};

    // renderFinished is indexed by swapchain IMAGE index, not frame slot.
    // The inFlight fence proves command-buffer execution finished, but NOT
    // that the presentation engine consumed the semaphore wait; re-signaling
    // a per-slot semaphore two frames later can race a still-pending present.
    // Per-image semaphores (the current Khronos-sample pattern) make the
    // signal target unique to the presented image. Owned by CreateSwapchain
    // and DestroySwapchain, so a swapchain recreate always yields fresh ones.
    std::vector<VkSemaphore> renderFinished;

    // Upload (staging) objects used by sys_vk texture creation.
    VkCommandPool   uploadPool = VK_NULL_HANDLE;
    VkCommandBuffer uploadCmd = VK_NULL_HANDLE;
    VkFence         uploadFence = VK_NULL_HANDLE;

    // Fullscreen pipeline resources (owned by sys_vk)
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool      descPool = VK_NULL_HANDLE;
    VkDescriptorSet       descSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline = VK_NULL_HANDLE;

    VkPipelineCache  pipelineCache = VK_NULL_HANDLE;

    // -------------------------------------------------------------------------
    // Function pointers (loaded once)
    // -------------------------------------------------------------------------

    // Instance-level

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_ = nullptr;
    PFN_vkCreateInstance vkCreateInstance_ = nullptr;
    PFN_vkDestroyInstance vkDestroyInstance_ = nullptr;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices_ = nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties_ = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties_ = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR_ = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR_ = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties_ = nullptr;

    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR_ = nullptr;

    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR_ = nullptr;

    // Debug utils (only loaded when validation is enabled and available).
    PFN_vkCreateDebugUtilsMessengerEXT  vkCreateDebugUtilsMessengerEXT_ = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT_ = nullptr;

    // Device-level
    PFN_vkCreateDevice vkCreateDevice_ = nullptr;
    PFN_vkDestroyDevice vkDestroyDevice_ = nullptr;
    PFN_vkGetDeviceQueue vkGetDeviceQueue_ = nullptr;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle_ = nullptr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr_ = nullptr;

    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR_ = nullptr;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR_ = nullptr;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR_ = nullptr;
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR_ = nullptr;
    PFN_vkQueuePresentKHR vkQueuePresentKHR_ = nullptr;

    PFN_vkCreateImageView vkCreateImageView_ = nullptr;
    PFN_vkDestroyImageView vkDestroyImageView_ = nullptr;

    PFN_vkCreateCommandPool vkCreateCommandPool_ = nullptr;
    PFN_vkDestroyCommandPool vkDestroyCommandPool_ = nullptr;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers_ = nullptr;
    PFN_vkResetCommandPool vkResetCommandPool_ = nullptr;
    PFN_vkResetCommandBuffer vkResetCommandBuffer_ = nullptr;

    PFN_vkBeginCommandBuffer vkBeginCommandBuffer_ = nullptr;
    PFN_vkEndCommandBuffer vkEndCommandBuffer_ = nullptr;

    PFN_vkCreateSemaphore vkCreateSemaphore_ = nullptr;
    PFN_vkDestroySemaphore vkDestroySemaphore_ = nullptr;

    PFN_vkCreateFence vkCreateFence_ = nullptr;
    PFN_vkDestroyFence vkDestroyFence_ = nullptr;
    PFN_vkWaitForFences vkWaitForFences_ = nullptr;
    PFN_vkResetFences vkResetFences_ = nullptr;

    PFN_vkQueueSubmit2 vkQueueSubmit2_ = nullptr;

    PFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2_ = nullptr;
    PFN_vkCmdBeginRendering vkCmdBeginRendering_ = nullptr;
    PFN_vkCmdEndRendering vkCmdEndRendering_ = nullptr;
    PFN_vkCmdSetViewport vkCmdSetViewport_ = nullptr;
    PFN_vkCmdSetScissor vkCmdSetScissor_ = nullptr;
    PFN_vkCmdBindPipeline vkCmdBindPipeline_ = nullptr;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets_ = nullptr;
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers_ = nullptr;
    PFN_vkCmdPushConstants vkCmdPushConstants_ = nullptr;
    PFN_vkCmdDraw vkCmdDraw_ = nullptr;

    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout_ = nullptr;
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout_ = nullptr;
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool_ = nullptr;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool_ = nullptr;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets_ = nullptr;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets_ = nullptr;

    PFN_vkCreatePipelineLayout vkCreatePipelineLayout_ = nullptr;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout_ = nullptr;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines_ = nullptr;
    PFN_vkDestroyPipeline vkDestroyPipeline_ = nullptr;

    PFN_vkCreateShaderModule vkCreateShaderModule_ = nullptr;
    PFN_vkDestroyShaderModule vkDestroyShaderModule_ = nullptr;

    PFN_vkCreateBuffer vkCreateBuffer_ = nullptr;
    PFN_vkDestroyBuffer vkDestroyBuffer_ = nullptr;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements_ = nullptr;
    PFN_vkAllocateMemory vkAllocateMemory_ = nullptr;
    PFN_vkFreeMemory vkFreeMemory_ = nullptr;
    PFN_vkBindBufferMemory vkBindBufferMemory_ = nullptr;
    PFN_vkMapMemory vkMapMemory_ = nullptr;
    PFN_vkUnmapMemory vkUnmapMemory_ = nullptr;

    PFN_vkCreateImage vkCreateImage_ = nullptr;
    PFN_vkDestroyImage vkDestroyImage_ = nullptr;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements_ = nullptr;
    PFN_vkBindImageMemory vkBindImageMemory_ = nullptr;

    PFN_vkCreateSampler vkCreateSampler_ = nullptr;
    PFN_vkDestroySampler vkDestroySampler_ = nullptr;

    PFN_vkCmdCopyBuffer vkCmdCopyBuffer_ = nullptr;
    PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage_ = nullptr;

    // Image -> host-visible buffer readback (F12 screenshot). Optional, like
    // vkCmdBlitImage: it is deliberately NOT in VK_Init's required[] table, so
    // a driver that somehow lacks it costs the screenshot feature, not the
    // whole Vulkan chain. Consumers must null-check.
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer_ = nullptr;

    // Mip generation support. vkCmdBlitImage downsamples level N into level N+1
    // with linear filtering. Format feature query is used to verify the target
    // format supports blit src/dst at runtime.
    PFN_vkCmdBlitImage vkCmdBlitImage_ = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties_ = nullptr;

    PFN_vkCreatePipelineCache   vkCreatePipelineCache_ = nullptr;
    PFN_vkDestroyPipelineCache  vkDestroyPipelineCache_ = nullptr;
    PFN_vkGetPipelineCacheData  vkGetPipelineCacheData_ = nullptr;

    // Timestamp query pool. Serves the GPU profiler ([main] vk_profile) and
    // nothing else, so - like vkCmdBlitImage and vkCmdCopyImageToBuffer -
    // these are deliberately NOT in VK_Init's required[] table: a driver that
    // somehow lacks them costs the profiler, not the whole Vulkan chain.
    // GpuProfilerVK::Init null-checks all five and disables itself.
    PFN_vkCreateQueryPool     vkCreateQueryPool_ = nullptr;
    PFN_vkDestroyQueryPool    vkDestroyQueryPool_ = nullptr;
    PFN_vkCmdWriteTimestamp   vkCmdWriteTimestamp_ = nullptr;
    PFN_vkCmdResetQueryPool   vkCmdResetQueryPool_ = nullptr;
    PFN_vkGetQueryPoolResults vkGetQueryPoolResults_ = nullptr;

};

// Resolves the color format of the pass a subsystem is recording into.
// Falls back to the swapchain format when no pass has published a format
// (defensive; every pass owner sets activeColorFormat).
inline VkFormat VK_ActiveColorFormat(const VkContext& ctx)
{
    return (ctx.activeColorFormat != VK_FORMAT_UNDEFINED)
        ? ctx.activeColorFormat
        : ctx.swapchainFormat;
}

// -----------------------------------------------------------------------------
// Public API
//
// enableValidation: opt-in. When true and VK_LAYER_KHRONOS_validation is
// installed, the layer is enabled and debug-utils messages are routed to the
// engine log. Default off -- release behavior is unchanged.
// vsync: true selects FIFO; false prefers MAILBOX then IMMEDIATE.
// -----------------------------------------------------------------------------
bool VK_Init(VkContext& ctx, IPresentSurface& present, bool enableValidation = false, bool vsync = true);
void VK_Shutdown(VkContext& ctx);

bool VK_BeginFrame(VkContext& ctx, uint32_t& outImageIndex);
bool VK_EndFrame(VkContext& ctx, uint32_t imageIndex);

// -----------------------------------------------------------------------------
// Mid-frame pass suspension (Phase 4a Plan 4, Task 3).
//
// Ends the swapchain dynamic-rendering pass so offscreen passes (render
// targets, mip generation) can record mid-frame, and re-opens it with
// LOAD_OP_LOAD (the attachment was cleared when VK_BeginFrame opened it).
//
// Contract:
//   - Only legal between VK_BeginFrame and VK_EndFrame.
//   - Suspend and Resume must be paired; imageIndex passed to Resume must be
//     the one VK_BeginFrame returned this frame.
//   - No swapchain image barriers happen here: the image stays
//     COLOR_ATTACHMENT_OPTIMAL throughout, so VK_EndFrame's single
//     COLOR->PRESENT barrier remains the one end-of-frame transition.
//   - Suspend resets ctx.activeColorFormat to UNDEFINED (no pass open);
//     Resume restores it to the swapchain format and re-establishes
//     VK_BeginFrame's default full-surface viewport/scissor.
//   - A mismatched pair is detected and logged; VK_EndFrame still works
//     either way (its s_framePassOpen recovery path handles an unmatched
//     Suspend, and an unmatched Resume is a logged no-op).
// -----------------------------------------------------------------------------
void VK_SuspendFramePass(VkContext& ctx, VkCommandBuffer cmd);
void VK_ResumeFramePass(VkContext& ctx, VkCommandBuffer cmd, uint32_t imageIndex);

// Opens the swapchain render pass if it is not already open; no-op if it is.
// VK_BeginFrame deliberately does NOT open it (that cost a tile store+reload
// per frame on tile-based GPUs), so every entry point that draws to the
// SWAPCHAIN must call this first. Safe to call unconditionally and repeatedly.
// The first open of a frame clears; later ones load. See the definition.
void VK_EnsureFramePass(VkContext& ctx, VkCommandBuffer cmd);

bool VK_RecreateSwapchain(VkContext& ctx);

// -----------------------------------------------------------------------------
// Texture helpers
//
// generateMips == true: full mip chain built via blit cascade. Use for
// photographic content and any texture that will be minified on screen
// with linear or trilinear sampling.
//
// generateMips == false: single-level image. Use for pixel art, UI atlases
// with crisp 1:1 sampling, or any texture that will be sampled with
// VK_FILTER_NEAREST and must preserve its authored pixel boundaries.
// -----------------------------------------------------------------------------
bool VK_CreateTextureRGBA8_SRGB_FromPixels(VkContext& ctx,
    const void* rgba8Pixels, uint32_t width, uint32_t height,
    VkTexture& outTex, bool generateMips = true, bool nearestFilter = false);

bool VK_CreateTextureRGBA8_UNORM_FromPixels(VkContext& ctx,
    const void* rgba8Pixels, uint32_t width, uint32_t height,
    VkTexture& outTex, bool generateMips = true, bool nearestFilter = false);

void VK_DestroyTexture(VkContext& ctx, VkTexture& tex);

// -----------------------------------------------------------------------------
// Upload helpers (shared staging command buffer)
// -----------------------------------------------------------------------------
bool VK_BeginUpload(VkContext& ctx);
bool VK_EndUpload(VkContext& ctx);

// NOTE (Phase 4a Plan 2): the ScreenQuadVK-coupled entry points from the
// donor (VK_BindFullscreenTexture, VK_RecordFullscreenPass, VK_RecordRectPass)
// are excluded from this port. Plans 3-6 re-import them with their subsystems.

// Recreate per-frame sync objects (semaphores + fences). Must be called
// after a swapchain recreate when a previous frame's vkQueuePresentKHR
// returned VK_ERROR_OUT_OF_DATE_KHR -- the wait-semaphore state for that
// frame is undefined per spec, and signaling an already-signaled binary
// semaphore on the next submit is undefined behavior (manifests as
// VK_ERROR_DEVICE_LOST on NVIDIA). Fully drains the device before
// recreating, so it is also safe to call from any non-recording frame.
bool VK_RecreateSyncObjects(VkContext& ctx);

// -----------------------------------------------------------------------------
// Sprite descriptor writers
// -----------------------------------------------------------------------------
// Writes set=0 (per-frame UBO) for a given frame index.
// You own the UBO buffer; sys_vk only stores the descriptor set handles.
bool VK_SpriteWriteFrameUBO(VkContext& ctx, uint32_t frameIndex, VkBuffer ubo, VkDeviceSize range);

// Writes set=1 (atlas sampler) once.
bool VK_SpriteWriteAtlas(VkContext& ctx, VkSampler sampler, VkImageView view, VkImageLayout layout);

// Cache:
bool VK_CreatePipelineCache(VkContext& ctx, const char* cacheFilePath);
bool VK_SavePipelineCache(VkContext& ctx, const char* cacheFilePath);
void VK_DestroyPipelineCache(VkContext& ctx);