// sys_vk.cpp
// Vulkan backend helpers.
// ASCII-only comments.

#include "sys_vk.h"
#include "sys_window.h"
#include "sys_log.h"

#include <stdio.h>
#include <string.h>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// ---------------------------------------------------------------------------
// Runtime loader bootstrap (spec sec. 6). Only these two touch the platform;
// every other Vulkan function is fetched through vkGetInstanceProcAddr_/
// vkGetDeviceProcAddr_ exactly as the donor engine always did.
// ---------------------------------------------------------------------------
static void* LoaderOpen(void)
{
#ifdef _WIN32
	return (void*)LoadLibraryA("vulkan-1.dll");
#else
	return dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* LoaderSym(void* module, const char* name)
{
#ifdef _WIN32
	return (void*)GetProcAddress((HMODULE)module, name);
#else
	return dlsym(module, name);
#endif
}

static void LoaderClose(void* module)
{
	if (!module) return;
#ifdef _WIN32
	FreeLibrary((HMODULE)module);
#else
	dlclose(module);
#endif
}

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------
static bool HasExtension(const std::vector<VkExtensionProperties>& exts, const char* name)
{
	for (auto& e : exts)
	{
		if (strcmp(e.extensionName, name) == 0)
			return true;
	}
	return false;
}

static PFN_vkVoidFunction GI(VkContext& ctx, const char* name)
{
	if (!ctx.vkGetInstanceProcAddr_ || !ctx.instance)
		return nullptr;
	return ctx.vkGetInstanceProcAddr_(ctx.instance, name);
}

static PFN_vkVoidFunction GD(VkContext& ctx, const char* name)
{
	if (!ctx.vkGetDeviceProcAddr_ || !ctx.device)
		return nullptr;
	return ctx.vkGetDeviceProcAddr_(ctx.device, name);
}

// NOTE: Some toolchains have vkGetDeviceProcAddr as a global symbol even when
// storing function pointers. If your header does not declare it, you can also
// load it from vkGetInstanceProcAddr("vkGetDeviceProcAddr") and store it.
// This file expects Vulkan prototypes are available via <vulkan/vulkan.h>.

// -----------------------------------------------------------------------------
// Pick physical device and queues
//
// Scores every device that meets the hard requirements (Vulkan 1.3, a queue
// family with graphics + present) and keeps the best one. Discrete GPUs win
// over integrated so laptops that enumerate the iGPU first do not silently
// run on the wrong adapter. Devices below 1.3 are skipped outright -- the
// engine records with vkCmdPipelineBarrier2 / vkQueueSubmit2 / dynamic
// rendering, and limping onto an older device just crashes later.
// -----------------------------------------------------------------------------
static bool PickPhysicalDevice(VkContext& ctx)
{
	uint32_t count = 0;
	if (ctx.vkEnumeratePhysicalDevices_(ctx.instance, &count, nullptr) != VK_SUCCESS || count == 0)
		return false;

	std::vector<VkPhysicalDevice> devs(count);
	if (ctx.vkEnumeratePhysicalDevices_(ctx.instance, &count, devs.data()) != VK_SUCCESS)
		return false;

	VkPhysicalDevice bestDev = VK_NULL_HANDLE;
	uint32_t         bestQueue = 0;
	int              bestScore = -1;
	char             bestName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {};

	for (uint32_t i = 0; i < count; ++i)
	{
		VkPhysicalDevice pd = devs[i];

		VkPhysicalDeviceProperties props{};
		ctx.vkGetPhysicalDeviceProperties_(pd, &props);

		if (props.apiVersion < VK_API_VERSION_1_3)
		{
			LOG_INFO("PickPhysicalDevice: skipping '%s' (below Vulkan 1.3)", props.deviceName);
			continue;
		}

		uint32_t qCount = 0;
		ctx.vkGetPhysicalDeviceQueueFamilyProperties_(pd, &qCount, nullptr);
		if (qCount == 0)
			continue;

		std::vector<VkQueueFamilyProperties> qProps(qCount);
		ctx.vkGetPhysicalDeviceQueueFamilyProperties_(pd, &qCount, qProps.data());

		for (uint32_t q = 0; q < qCount; ++q)
		{
			if ((qProps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				continue;

			VkBool32 presentOK = VK_FALSE;
			if (ctx.vkGetPhysicalDeviceSurfaceSupportKHR_(pd, q, ctx.surface, &presentOK) != VK_SUCCESS)
				continue;

			if (!presentOK)
				continue;

			int score = 10;
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				score = 100;
			else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
				score = 50;

			if (score > bestScore)
			{
				bestScore = score;
				bestDev = pd;
				bestQueue = q;
				memcpy(bestName, props.deviceName, sizeof(bestName));
			}
			break; // first suitable queue family on this device is enough
		}
	}

	if (bestDev == VK_NULL_HANDLE)
	{
		LOG_ERROR("PickPhysicalDevice: no Vulkan 1.3 device with graphics+present found");
		return false;
	}

	ctx.phys = bestDev;
	ctx.gfxQueueFamily = bestQueue;
	LOG_INFO("PickPhysicalDevice: using '%s'", bestName);
	return true;
}

// -----------------------------------------------------------------------------
// Swapchain creation
// -----------------------------------------------------------------------------
static bool CreateSwapchain(VkContext& ctx)
{
	VkSurfaceCapabilitiesKHR caps{};
	if (ctx.vkGetPhysicalDeviceSurfaceCapabilitiesKHR_(ctx.phys, ctx.surface, &caps) != VK_SUCCESS)
		return false;

	uint32_t fmtCount = 0;
	if (ctx.vkGetPhysicalDeviceSurfaceFormatsKHR_(ctx.phys, ctx.surface, &fmtCount, nullptr) != VK_SUCCESS || fmtCount == 0)
		return false;

	std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
	if (ctx.vkGetPhysicalDeviceSurfaceFormatsKHR_(ctx.phys, ctx.surface, &fmtCount, fmts.data()) != VK_SUCCESS)
		return false;

	VkSurfaceFormatKHR chosen = fmts[0];

	// Prefer SRGB swapchain formats.
	for (auto& f : fmts)
	{
		if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
			f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			chosen = f;
			break;
		}
	}

	// HARD-FAIL if no SRGB swapchain exists.
	if (chosen.format != VK_FORMAT_B8G8R8A8_SRGB && chosen.format != VK_FORMAT_R8G8B8A8_SRGB)
	{
		LOG_ERROR("CreateSwapchain: no SRGB swapchain format available");
		return false;
	}

	ctx.swapchainFormat = chosen.format;

	LOG_INFO("CreateSwapchain: using SRGB swapchain format");

	// Resolve the extent. currentExtent == 0xFFFFFFFF means the surface lets
	// the swapchain decide; take the window drawable size (via the window
	// layer's IPresentSurface contract) and clamp it to the surface's
	// allowed range.
	VkExtent2D extent = caps.currentExtent;
	if (extent.width == 0xFFFFFFFFu || extent.height == 0xFFFFFFFFu)
	{
		int dw = 0, dh = 0;
		if (ctx.present)
			ctx.present->GetDrawableSize(&dw, &dh);
		if (dw > 0 && dh > 0)
		{
			extent.width = (uint32_t)dw;
			extent.height = (uint32_t)dh;
		}
		else
		{
			extent.width = 640;
			extent.height = 480;
		}

		if (extent.width < caps.minImageExtent.width)   extent.width = caps.minImageExtent.width;
		if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
		if (caps.maxImageExtent.width > 0 && extent.width > caps.maxImageExtent.width)
			extent.width = caps.maxImageExtent.width;
		if (caps.maxImageExtent.height > 0 && extent.height > caps.maxImageExtent.height)
			extent.height = caps.maxImageExtent.height;
	}

	// A zero-area surface (minimized window) cannot back a swapchain --
	// creating one with extent 0 violates the spec. Bail quietly; the caller
	// retries once the window has area again.
	if (extent.width == 0 || extent.height == 0)
	{
		LOG_INFO("CreateSwapchain: surface extent is zero (window minimized?); deferring");
		return false;
	}

	ctx.swapchainExtent = extent;

	LOG_INFO("CreateSwapchain: extent=%ux%u (caps.currentExtent=%ux%u)",
		extent.width, extent.height, caps.currentExtent.width, caps.currentExtent.height);

	// Present mode. FIFO is always available and is the vsync choice. With
	// vsync off, prefer MAILBOX (no tearing, low latency), then IMMEDIATE.
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	if (!ctx.vsync && ctx.vkGetPhysicalDeviceSurfacePresentModesKHR_)
	{
		uint32_t pmCount = 0;
		if (ctx.vkGetPhysicalDeviceSurfacePresentModesKHR_(ctx.phys, ctx.surface, &pmCount, nullptr) == VK_SUCCESS && pmCount > 0)
		{
			std::vector<VkPresentModeKHR> pms(pmCount);
			if (ctx.vkGetPhysicalDeviceSurfacePresentModesKHR_(ctx.phys, ctx.surface, &pmCount, pms.data()) == VK_SUCCESS)
			{
				for (auto pm : pms)
				{
					if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = pm; break; }
				}
				if (presentMode == VK_PRESENT_MODE_FIFO_KHR)
				{
					for (auto pm : pms)
					{
						if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR) { presentMode = pm; break; }
					}
				}
			}
		}
	}
	ctx.presentMode = presentMode;
	LOG_INFO("CreateSwapchain: presentMode=%d (vsync=%d)", (int)presentMode, (int)ctx.vsync);

	uint32_t imageCount = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
		imageCount = caps.maxImageCount;

	VkSwapchainCreateInfoKHR sci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	sci.surface = ctx.surface;
	sci.minImageCount = imageCount;
	sci.imageFormat = chosen.format;
	sci.imageColorSpace = chosen.colorSpace;
	sci.imageExtent = extent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sci.preTransform = caps.currentTransform;
	sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	sci.presentMode = presentMode;
	sci.clipped = VK_TRUE;
	sci.oldSwapchain = VK_NULL_HANDLE;

	if (ctx.vkCreateSwapchainKHR_(ctx.device, &sci, nullptr, &ctx.swapchain) != VK_SUCCESS)
		return false;

	// Images
	uint32_t scCount = 0;
	if (ctx.vkGetSwapchainImagesKHR_(ctx.device, ctx.swapchain, &scCount, nullptr) != VK_SUCCESS || scCount == 0)
		return false;

	ctx.swapchainImages.resize(scCount);
	if (ctx.vkGetSwapchainImagesKHR_(ctx.device, ctx.swapchain, &scCount, ctx.swapchainImages.data()) != VK_SUCCESS)
		return false;

	// Views
	ctx.swapchainViews.resize(scCount);
	for (uint32_t i = 0; i < scCount; ++i)
	{
		VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		iv.image = ctx.swapchainImages[i];
		iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
		iv.format = ctx.swapchainFormat;
		iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		iv.subresourceRange.baseMipLevel = 0;
		iv.subresourceRange.levelCount = 1;
		iv.subresourceRange.baseArrayLayer = 0;
		iv.subresourceRange.layerCount = 1;

		if (ctx.vkCreateImageView_(ctx.device, &iv, nullptr, &ctx.swapchainViews[i]) != VK_SUCCESS)
			return false;
	}

	// Per-image renderFinished semaphores (see sys_vk.h for why these are
	// per image rather than per frame slot). Created with the swapchain so
	// a recreate always yields fresh, unsignaled semaphores.
	ctx.renderFinished.resize(scCount, VK_NULL_HANDLE);
	VkSemaphoreCreateInfo rsi{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	for (uint32_t i = 0; i < scCount; ++i)
	{
		if (ctx.vkCreateSemaphore_(ctx.device, &rsi, nullptr, &ctx.renderFinished[i]) != VK_SUCCESS)
		{
			LOG_ERROR("CreateSwapchain: vkCreateSemaphore (renderFinished) failed");
			return false;
		}
	}

	return true;
}

static void DestroySwapchain(VkContext& ctx)
{
	for (auto s : ctx.renderFinished)
	{
		if (s)
			ctx.vkDestroySemaphore_(ctx.device, s, nullptr);
	}
	ctx.renderFinished.clear();

	for (auto v : ctx.swapchainViews)
	{
		if (v)
			ctx.vkDestroyImageView_(ctx.device, v, nullptr);
	}
	ctx.swapchainViews.clear();
	ctx.swapchainImages.clear();

	if (ctx.swapchain)
	{
		ctx.vkDestroySwapchainKHR_(ctx.device, ctx.swapchain, nullptr);
		ctx.swapchain = VK_NULL_HANDLE;
	}
}

// -----------------------------------------------------------------------------
// Fullscreen (screenquad) pipeline -- EXCLUDED from this port (Phase 4a Plan 2).
// The donor's CreateFullscreenPipeline/DestroyFullscreenPipeline and the
// VK_BindFullscreenTexture descriptor cache depend on ScreenQuadVK and the
// screenquad SPIR-V shaders, which AAE does not carry yet. Plans 3-6
// re-import them with their subsystems.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Validation-layer message routing. Only active when VK_Init was asked to
// enable validation AND the layer + debug-utils extension are present.
// Warnings and errors go to LOG_ERROR so they stand out in the engine log.
// -----------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT /*types*/,
	const VkDebugUtilsMessengerCallbackDataEXT* data,
	void* /*user*/)
{
	if (!data || !data->pMessage)
		return VK_FALSE;

	if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
	                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT))
	{
		LOG_ERROR("VK-validation: %s", data->pMessage);
	}
	else
	{
		LOG_INFO("VK-validation: %s", data->pMessage);
	}

	return VK_FALSE;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool VK_Init(VkContext& ctx, IPresentSurface& present, bool enableValidation, bool vsync)
{
	LOG_INFO("VK_Init: begin");

	// Reset by value assignment, NOT memset: VkContext contains std::vector
	// members (swapchainImages, swapchainViews, cmdBuffers, renderFinished),
	// and memset over a live vector is undefined behavior. Assignment runs
	// the proper destructors/constructors and works on a reused context too.
	ctx = VkContext{};
	ctx.present = &present;
	ctx.vsync = vsync;

	LOG_INFO("VK_Init: ctx cleared and present surface stored");

	// Load global entry points through the runtime loader (spec sec. 6:
	// nothing links vulkan-1.lib; the loader is bound at runtime).
	ctx.loaderModule = LoaderOpen();
	if (!ctx.loaderModule)
	{
		LOG_ERROR("VK_Init: Vulkan runtime not found (vulkan-1.dll / libvulkan.so.1)");
		return false;
	}
	ctx.vkGetInstanceProcAddr_ =
		(PFN_vkGetInstanceProcAddr)LoaderSym(ctx.loaderModule, "vkGetInstanceProcAddr");
	if (!ctx.vkGetInstanceProcAddr_)
	{
		LOG_ERROR("VK_Init: vkGetInstanceProcAddr not found in loader");
		return false;
	}
	LOG_INFO("VK_Init: vkGetInstanceProcAddr_ assigned");

	ctx.vkCreateInstance_ = (PFN_vkCreateInstance)ctx.vkGetInstanceProcAddr_(nullptr, "vkCreateInstance");
	if (!ctx.vkCreateInstance_)
		LOG_ERROR("VK_Init: vkCreateInstance_ is NULL");
	else
		LOG_INFO("VK_Init: vkCreateInstance_ loaded");

	ctx.vkDestroyInstance_ = (PFN_vkDestroyInstance)ctx.vkGetInstanceProcAddr_(nullptr, "vkDestroyInstance");
	if (!ctx.vkDestroyInstance_)
		LOG_INFO("VK_Init: vkDestroyInstance not available pre-instance (normal on some loaders); re-fetched after instance creation");
	else
		LOG_INFO("VK_Init: vkDestroyInstance_ loaded");

	// FIX: Do NOT load vkGetDeviceProcAddr_ from ctx.instance yet, because ctx.instance
	// has not been created at this point. We will load it right after vkCreateInstance succeeds.
	ctx.vkGetDeviceProcAddr_ = nullptr;
	LOG_INFO("VK_Init: vkGetDeviceProcAddr_ deferred until after instance creation");

	if (!ctx.vkCreateInstance_)
	{
		LOG_ERROR("VK_Init: aborting because vkCreateInstance_ was NULL");
		return false;
	}

	LOG_INFO("VK_Init: building instance extension list");

	// Instance extensions and layers. Validation is opt-in: it is only
	// enabled when requested AND the Khronos layer is actually installed,
	// so machines without the Vulkan SDK behave exactly as before.
	// The platform surface extension pair comes from the window layer's
	// IPresentSurface contract instead of being hardcoded here.
	uint32_t platformExtCount = 0;
	const char* const* platformExts = present.RequiredVkInstanceExtensions(&platformExtCount);
	std::vector<const char*> instExts(platformExts, platformExts + platformExtCount);

	std::vector<const char*> instLayers;
	bool wantDebugUtils = false;
	if (enableValidation)
	{
		// Pre-instance enumeration entry points, fetched through the runtime
		// loader (no vulkan-1.lib link; spec sec. 6).
		PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties_ =
			(PFN_vkEnumerateInstanceLayerProperties)ctx.vkGetInstanceProcAddr_(nullptr, "vkEnumerateInstanceLayerProperties");
		PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties_ =
			(PFN_vkEnumerateInstanceExtensionProperties)ctx.vkGetInstanceProcAddr_(nullptr, "vkEnumerateInstanceExtensionProperties");

		uint32_t layerCount = 0;
		if (vkEnumerateInstanceLayerProperties_)
			vkEnumerateInstanceLayerProperties_(&layerCount, nullptr);
		std::vector<VkLayerProperties> layers(layerCount);
		if (layerCount > 0)
			vkEnumerateInstanceLayerProperties_(&layerCount, layers.data());

		bool haveLayer = false;
		for (auto& l : layers)
		{
			if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
			{
				haveLayer = true;
				break;
			}
		}

		if (haveLayer)
		{
			instLayers.push_back("VK_LAYER_KHRONOS_validation");

			uint32_t extCount = 0;
			if (vkEnumerateInstanceExtensionProperties_)
				vkEnumerateInstanceExtensionProperties_(nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> exts(extCount);
			if (extCount > 0)
				vkEnumerateInstanceExtensionProperties_(nullptr, &extCount, exts.data());

			if (HasExtension(exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				wantDebugUtils = true;
			}

			ctx.validationEnabled = true;
			LOG_INFO("VK_Init: validation layer enabled (debugUtils=%d)", (int)wantDebugUtils);
		}
		else
		{
			LOG_INFO("VK_Init: validation requested but VK_LAYER_KHRONOS_validation is not installed");
		}
	}

	VkApplicationInfo ai{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	ai.pApplicationName = "VulkanApp";
	ai.applicationVersion = 1;
	ai.pEngineName = "sys_vk";
	ai.engineVersion = 1;
	ai.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &ai;
	ici.enabledExtensionCount = (uint32_t)instExts.size();
	ici.ppEnabledExtensionNames = instExts.data();
	ici.enabledLayerCount = (uint32_t)instLayers.size();
	ici.ppEnabledLayerNames = instLayers.empty() ? nullptr : instLayers.data();

	LOG_INFO("VK_Init: calling vkCreateInstance");

	VkResult cir = ctx.vkCreateInstance_(&ici, nullptr, &ctx.instance);
	if (cir != VK_SUCCESS)
	{
		LOG_ERROR("VK_Init: vkCreateInstance failed (VkResult=%d)", (int)cir);
		return false;
	}

	LOG_INFO("VK_Init: instance created successfully");

	// Reload vkDestroyInstance_ from the created instance.
// Some loaders do not return it from vkGetInstanceProcAddr(nullptr, ...).
	ctx.vkDestroyInstance_ =
		(PFN_vkDestroyInstance)ctx.vkGetInstanceProcAddr_(ctx.instance, "vkDestroyInstance");

	if (!ctx.vkDestroyInstance_)
	{
		LOG_ERROR("VK_Init: vkDestroyInstance_ is NULL after instance creation");
		return false;
	}

	// FIX: Now that ctx.instance exists, load vkGetDeviceProcAddr_ the correct way.
	ctx.vkGetDeviceProcAddr_ =
		(PFN_vkGetDeviceProcAddr)ctx.vkGetInstanceProcAddr_(ctx.instance, "vkGetDeviceProcAddr");

	if (!ctx.vkGetDeviceProcAddr_)
	{
		LOG_ERROR("VK_Init: vkGetDeviceProcAddr_ is NULL after instance creation");
		// Not strictly fatal for code paths that call global vkGetDeviceProcAddr,
		// but we keep this as a failure because you are explicitly storing it.
		return false;
	}
	LOG_INFO("VK_Init: vkGetDeviceProcAddr_ loaded (post-instance)");

	// Debug messenger (validation opt-in only). Failure here is non-fatal:
	// the layer still validates, its default sink just is not our log.
	if (wantDebugUtils)
	{
		ctx.vkCreateDebugUtilsMessengerEXT_ = (PFN_vkCreateDebugUtilsMessengerEXT)GI(ctx, "vkCreateDebugUtilsMessengerEXT");
		ctx.vkDestroyDebugUtilsMessengerEXT_ = (PFN_vkDestroyDebugUtilsMessengerEXT)GI(ctx, "vkDestroyDebugUtilsMessengerEXT");
		if (ctx.vkCreateDebugUtilsMessengerEXT_ && ctx.vkDestroyDebugUtilsMessengerEXT_)
		{
			VkDebugUtilsMessengerCreateInfoEXT mi{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
			mi.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			mi.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			mi.pfnUserCallback = DebugUtilsCallback;

			if (ctx.vkCreateDebugUtilsMessengerEXT_(ctx.instance, &mi, nullptr, &ctx.debugMessenger) != VK_SUCCESS)
			{
				LOG_ERROR("VK_Init: vkCreateDebugUtilsMessengerEXT failed (continuing without)");
				ctx.debugMessenger = VK_NULL_HANDLE;
			}
		}
	}

	// Load instance procs
	LOG_INFO("VK_Init: loading instance-level function pointers");

	ctx.vkEnumeratePhysicalDevices_ = (PFN_vkEnumeratePhysicalDevices)GI(ctx, "vkEnumeratePhysicalDevices");
	if (!ctx.vkEnumeratePhysicalDevices_) LOG_ERROR("VK_Init: vkEnumeratePhysicalDevices_ is NULL");

	ctx.vkGetPhysicalDeviceProperties_ = (PFN_vkGetPhysicalDeviceProperties)GI(ctx, "vkGetPhysicalDeviceProperties");
	if (!ctx.vkGetPhysicalDeviceProperties_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceProperties_ is NULL");

	ctx.vkGetPhysicalDeviceQueueFamilyProperties_ = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)GI(ctx, "vkGetPhysicalDeviceQueueFamilyProperties");
	if (!ctx.vkGetPhysicalDeviceQueueFamilyProperties_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceQueueFamilyProperties_ is NULL");

	ctx.vkGetPhysicalDeviceSurfaceSupportKHR_ = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)GI(ctx, "vkGetPhysicalDeviceSurfaceSupportKHR");
	if (!ctx.vkGetPhysicalDeviceSurfaceSupportKHR_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceSurfaceSupportKHR_ is NULL");

	ctx.vkGetPhysicalDeviceSurfaceFormatsKHR_ = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)GI(ctx, "vkGetPhysicalDeviceSurfaceFormatsKHR");
	if (!ctx.vkGetPhysicalDeviceSurfaceFormatsKHR_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceSurfaceFormatsKHR_ is NULL");

	ctx.vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)GI(ctx, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
	if (!ctx.vkGetPhysicalDeviceSurfaceCapabilitiesKHR_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceSurfaceCapabilitiesKHR_ is NULL");

	ctx.vkGetPhysicalDeviceSurfacePresentModesKHR_ = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)GI(ctx, "vkGetPhysicalDeviceSurfacePresentModesKHR");
	if (!ctx.vkGetPhysicalDeviceSurfacePresentModesKHR_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceSurfacePresentModesKHR_ is NULL");

	ctx.vkGetPhysicalDeviceMemoryProperties_ = (PFN_vkGetPhysicalDeviceMemoryProperties)GI(ctx, "vkGetPhysicalDeviceMemoryProperties");
	if (!ctx.vkGetPhysicalDeviceMemoryProperties_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceMemoryProperties_ is NULL");

	ctx.vkGetPhysicalDeviceFormatProperties_ = (PFN_vkGetPhysicalDeviceFormatProperties)GI(ctx, "vkGetPhysicalDeviceFormatProperties");
	if (!ctx.vkGetPhysicalDeviceFormatProperties_) LOG_ERROR("VK_Init: vkGetPhysicalDeviceFormatProperties_ is NULL");

	ctx.vkDestroySurfaceKHR_ = (PFN_vkDestroySurfaceKHR)GI(ctx, "vkDestroySurfaceKHR");
	if (!ctx.vkDestroySurfaceKHR_) LOG_ERROR("VK_Init: vkDestroySurfaceKHR_ is NULL");

	LOG_INFO("VK_Init: validating required instance-level function pointers");

	if (!ctx.vkEnumeratePhysicalDevices_ || !ctx.vkDestroySurfaceKHR_)
	{
		LOG_ERROR("VK_Init: missing required instance-level entry points");
		return false;
	}

	// Surface -- created by the window layer through the IPresentSurface
	// contract, so sys_vk stays platform-neutral (spec sec. 3.5).
	LOG_INFO("VK_Init: creating presentation surface via IPresentSurface");

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (!present.CreateVkSurface((void*)ctx.instance, (void*)&surface) || surface == VK_NULL_HANDLE)
	{
		LOG_ERROR("VK_Init: IPresentSurface::CreateVkSurface failed");
		return false;
	}
	ctx.surface = surface;

	LOG_INFO("VK_Init: presentation surface created");

	// Choose phys device
	LOG_INFO("VK_Init: picking physical device");

	if (!PickPhysicalDevice(ctx))
	{
		LOG_ERROR("VK_Init: PickPhysicalDevice failed");
		return false;
	}

	LOG_INFO("VK_Init: physical device selected and queue family chosen");

	// Device creation
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qci.queueFamilyIndex = ctx.gfxQueueFamily;
	qci.queueCount = 1;
	qci.pQueuePriorities = &prio;

	const char* devExts[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	// Vulkan 1.3 features the engine records with. BOTH must be explicitly
	// enabled: using vkCmdPipelineBarrier2 / vkQueueSubmit2 without the
	// synchronization2 feature is invalid per spec even on drivers that
	// happen to accept it (VUID-vkCmdPipelineBarrier2-synchronization2-03848).
	VkPhysicalDeviceSynchronization2Features s2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
	s2.synchronization2 = VK_TRUE;

	VkPhysicalDeviceDynamicRenderingFeatures dr{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
	dr.dynamicRendering = VK_TRUE;
	dr.pNext = &s2;

	VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.pNext = &dr;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = (uint32_t)(sizeof(devExts) / sizeof(devExts[0]));
	dci.ppEnabledExtensionNames = devExts;

	LOG_INFO("VK_Init: loading vkCreateDevice");

	ctx.vkCreateDevice_ = (PFN_vkCreateDevice)GI(ctx, "vkCreateDevice");
	if (!ctx.vkCreateDevice_)
	{
		LOG_ERROR("VK_Init: vkCreateDevice_ is NULL");
		return false;
	}

	LOG_INFO("VK_Init: calling vkCreateDevice");

	if (ctx.vkCreateDevice_(ctx.phys, &dci, nullptr, &ctx.device) != VK_SUCCESS)
	{
		LOG_ERROR("VK_Init: vkCreateDevice failed");
		return false;
	}

	LOG_INFO("VK_Init: device created");

	// NOTE: From here on, your original code continues unchanged,
	// except you may optionally log that vkGetDeviceProcAddr is being used.
	// I am leaving all your vkGetDeviceProcAddr(...) loads intact.

	// Load device procs
	LOG_INFO("VK_Init: loading device-level function pointers");

	ctx.vkDestroyDevice_ = (PFN_vkDestroyDevice)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyDevice");
	if (!ctx.vkDestroyDevice_) LOG_ERROR("VK_Init: vkDestroyDevice_ is NULL");

	ctx.vkGetDeviceQueue_ = (PFN_vkGetDeviceQueue)ctx.vkGetDeviceProcAddr_(ctx.device, "vkGetDeviceQueue");
	if (!ctx.vkGetDeviceQueue_) LOG_ERROR("VK_Init: vkGetDeviceQueue_ is NULL");

	ctx.vkDeviceWaitIdle_ = (PFN_vkDeviceWaitIdle)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDeviceWaitIdle");
	if (!ctx.vkDeviceWaitIdle_) LOG_ERROR("VK_Init: vkDeviceWaitIdle_ is NULL");

	ctx.vkCreateSwapchainKHR_ = (PFN_vkCreateSwapchainKHR)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateSwapchainKHR");
	if (!ctx.vkCreateSwapchainKHR_) LOG_ERROR("VK_Init: vkCreateSwapchainKHR_ is NULL");

	ctx.vkDestroySwapchainKHR_ = (PFN_vkDestroySwapchainKHR)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroySwapchainKHR");
	if (!ctx.vkDestroySwapchainKHR_) LOG_ERROR("VK_Init: vkDestroySwapchainKHR_ is NULL");

	ctx.vkGetSwapchainImagesKHR_ = (PFN_vkGetSwapchainImagesKHR)ctx.vkGetDeviceProcAddr_(ctx.device, "vkGetSwapchainImagesKHR");
	if (!ctx.vkGetSwapchainImagesKHR_) LOG_ERROR("VK_Init: vkGetSwapchainImagesKHR_ is NULL");

	ctx.vkAcquireNextImageKHR_ = (PFN_vkAcquireNextImageKHR)ctx.vkGetDeviceProcAddr_(ctx.device, "vkAcquireNextImageKHR");
	if (!ctx.vkAcquireNextImageKHR_) LOG_ERROR("VK_Init: vkAcquireNextImageKHR_ is NULL");

	ctx.vkQueuePresentKHR_ = (PFN_vkQueuePresentKHR)ctx.vkGetDeviceProcAddr_(ctx.device, "vkQueuePresentKHR");
	if (!ctx.vkQueuePresentKHR_) LOG_ERROR("VK_Init: vkQueuePresentKHR_ is NULL");

	ctx.vkCreateImageView_ = (PFN_vkCreateImageView)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateImageView");
	if (!ctx.vkCreateImageView_) LOG_ERROR("VK_Init: vkCreateImageView_ is NULL");

	ctx.vkDestroyImageView_ = (PFN_vkDestroyImageView)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyImageView");
	if (!ctx.vkDestroyImageView_) LOG_ERROR("VK_Init: vkDestroyImageView_ is NULL");

	ctx.vkCreateCommandPool_ = (PFN_vkCreateCommandPool)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateCommandPool");
	if (!ctx.vkCreateCommandPool_) LOG_ERROR("VK_Init: vkCreateCommandPool_ is NULL");

	ctx.vkDestroyCommandPool_ = (PFN_vkDestroyCommandPool)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyCommandPool");
	if (!ctx.vkDestroyCommandPool_) LOG_ERROR("VK_Init: vkDestroyCommandPool_ is NULL");

	ctx.vkAllocateCommandBuffers_ = (PFN_vkAllocateCommandBuffers)ctx.vkGetDeviceProcAddr_(ctx.device, "vkAllocateCommandBuffers");
	if (!ctx.vkAllocateCommandBuffers_) LOG_ERROR("VK_Init: vkAllocateCommandBuffers_ is NULL");

	ctx.vkResetCommandPool_ = (PFN_vkResetCommandPool)ctx.vkGetDeviceProcAddr_(ctx.device, "vkResetCommandPool");
	if (!ctx.vkResetCommandPool_) LOG_ERROR("VK_Init: vkResetCommandPool_ is NULL");

	ctx.vkResetCommandBuffer_ = (PFN_vkResetCommandBuffer)ctx.vkGetDeviceProcAddr_(ctx.device, "vkResetCommandBuffer");
	if (!ctx.vkResetCommandBuffer_) LOG_ERROR("VK_Init: vkResetCommandBuffer_ is NULL");

	ctx.vkBeginCommandBuffer_ = (PFN_vkBeginCommandBuffer)ctx.vkGetDeviceProcAddr_(ctx.device, "vkBeginCommandBuffer");
	if (!ctx.vkBeginCommandBuffer_) LOG_ERROR("VK_Init: vkBeginCommandBuffer_ is NULL");

	ctx.vkEndCommandBuffer_ = (PFN_vkEndCommandBuffer)ctx.vkGetDeviceProcAddr_(ctx.device, "vkEndCommandBuffer");
	if (!ctx.vkEndCommandBuffer_) LOG_ERROR("VK_Init: vkEndCommandBuffer_ is NULL");

	ctx.vkCreateSemaphore_ = (PFN_vkCreateSemaphore)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateSemaphore");
	if (!ctx.vkCreateSemaphore_) LOG_ERROR("VK_Init: vkCreateSemaphore_ is NULL");

	ctx.vkDestroySemaphore_ = (PFN_vkDestroySemaphore)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroySemaphore");
	if (!ctx.vkDestroySemaphore_) LOG_ERROR("VK_Init: vkDestroySemaphore_ is NULL");

	ctx.vkCreateFence_ = (PFN_vkCreateFence)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateFence");
	if (!ctx.vkCreateFence_) LOG_ERROR("VK_Init: vkCreateFence_ is NULL");

	ctx.vkDestroyFence_ = (PFN_vkDestroyFence)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyFence");
	if (!ctx.vkDestroyFence_) LOG_ERROR("VK_Init: vkDestroyFence_ is NULL");

	ctx.vkWaitForFences_ = (PFN_vkWaitForFences)ctx.vkGetDeviceProcAddr_(ctx.device, "vkWaitForFences");
	if (!ctx.vkWaitForFences_) LOG_ERROR("VK_Init: vkWaitForFences_ is NULL");

	ctx.vkResetFences_ = (PFN_vkResetFences)ctx.vkGetDeviceProcAddr_(ctx.device, "vkResetFences");
	if (!ctx.vkResetFences_) LOG_ERROR("VK_Init: vkResetFences_ is NULL");

	ctx.vkQueueSubmit2_ = (PFN_vkQueueSubmit2)ctx.vkGetDeviceProcAddr_(ctx.device, "vkQueueSubmit2");
	if (!ctx.vkQueueSubmit2_) LOG_ERROR("VK_Init: vkQueueSubmit2_ is NULL");

	ctx.vkCmdPipelineBarrier2_ = (PFN_vkCmdPipelineBarrier2)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdPipelineBarrier2");
	if (!ctx.vkCmdPipelineBarrier2_) LOG_ERROR("VK_Init: vkCmdPipelineBarrier2_ is NULL");

	ctx.vkCmdBeginRendering_ = (PFN_vkCmdBeginRendering)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdBeginRendering");
	if (!ctx.vkCmdBeginRendering_) LOG_ERROR("VK_Init: vkCmdBeginRendering_ is NULL");

	ctx.vkCmdEndRendering_ = (PFN_vkCmdEndRendering)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdEndRendering");
	if (!ctx.vkCmdEndRendering_) LOG_ERROR("VK_Init: vkCmdEndRendering_ is NULL");

	ctx.vkCmdSetViewport_ = (PFN_vkCmdSetViewport)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdSetViewport");
	if (!ctx.vkCmdSetViewport_) LOG_ERROR("VK_Init: vkCmdSetViewport_ is NULL");

	ctx.vkCmdSetScissor_ = (PFN_vkCmdSetScissor)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdSetScissor");
	if (!ctx.vkCmdSetScissor_) LOG_ERROR("VK_Init: vkCmdSetScissor_ is NULL");

	ctx.vkCmdBindPipeline_ = (PFN_vkCmdBindPipeline)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdBindPipeline");
	if (!ctx.vkCmdBindPipeline_) LOG_ERROR("VK_Init: vkCmdBindPipeline_ is NULL");

	ctx.vkCmdBindDescriptorSets_ = (PFN_vkCmdBindDescriptorSets)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdBindDescriptorSets");
	if (!ctx.vkCmdBindDescriptorSets_) LOG_ERROR("VK_Init: vkCmdBindDescriptorSets_ is NULL");

	ctx.vkCmdBindVertexBuffers_ = (PFN_vkCmdBindVertexBuffers)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdBindVertexBuffers");
	if (!ctx.vkCmdBindVertexBuffers_) LOG_ERROR("VK_Init: vkCmdBindVertexBuffers_ is NULL");

	ctx.vkCmdPushConstants_ = (PFN_vkCmdPushConstants)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdPushConstants");
	if (!ctx.vkCmdPushConstants_) LOG_ERROR("VK_Init: vkCmdPushConstants_ is NULL");

	ctx.vkCmdDraw_ = (PFN_vkCmdDraw)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdDraw");
	if (!ctx.vkCmdDraw_) LOG_ERROR("VK_Init: vkCmdDraw_ is NULL");

	ctx.vkCreateDescriptorSetLayout_ = (PFN_vkCreateDescriptorSetLayout)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateDescriptorSetLayout");
	if (!ctx.vkCreateDescriptorSetLayout_) LOG_ERROR("VK_Init: vkCreateDescriptorSetLayout_ is NULL");

	ctx.vkDestroyDescriptorSetLayout_ = (PFN_vkDestroyDescriptorSetLayout)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyDescriptorSetLayout");
	if (!ctx.vkDestroyDescriptorSetLayout_) LOG_ERROR("VK_Init: vkDestroyDescriptorSetLayout_ is NULL");

	ctx.vkCreateDescriptorPool_ = (PFN_vkCreateDescriptorPool)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateDescriptorPool");
	if (!ctx.vkCreateDescriptorPool_) LOG_ERROR("VK_Init: vkCreateDescriptorPool_ is NULL");

	ctx.vkDestroyDescriptorPool_ = (PFN_vkDestroyDescriptorPool)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyDescriptorPool");
	if (!ctx.vkDestroyDescriptorPool_) LOG_ERROR("VK_Init: vkDestroyDescriptorPool_ is NULL");

	ctx.vkAllocateDescriptorSets_ = (PFN_vkAllocateDescriptorSets)ctx.vkGetDeviceProcAddr_(ctx.device, "vkAllocateDescriptorSets");
	if (!ctx.vkAllocateDescriptorSets_) LOG_ERROR("VK_Init: vkAllocateDescriptorSets_ is NULL");

	ctx.vkUpdateDescriptorSets_ = (PFN_vkUpdateDescriptorSets)ctx.vkGetDeviceProcAddr_(ctx.device, "vkUpdateDescriptorSets");
	if (!ctx.vkUpdateDescriptorSets_) LOG_ERROR("VK_Init: vkUpdateDescriptorSets_ is NULL");

	ctx.vkCreatePipelineLayout_ = (PFN_vkCreatePipelineLayout)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreatePipelineLayout");
	if (!ctx.vkCreatePipelineLayout_) LOG_ERROR("VK_Init: vkCreatePipelineLayout_ is NULL");

	ctx.vkDestroyPipelineLayout_ = (PFN_vkDestroyPipelineLayout)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyPipelineLayout");
	if (!ctx.vkDestroyPipelineLayout_) LOG_ERROR("VK_Init: vkDestroyPipelineLayout_ is NULL");

	ctx.vkCreateGraphicsPipelines_ = (PFN_vkCreateGraphicsPipelines)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateGraphicsPipelines");
	if (!ctx.vkCreateGraphicsPipelines_) LOG_ERROR("VK_Init: vkCreateGraphicsPipelines_ is NULL");

	ctx.vkDestroyPipeline_ = (PFN_vkDestroyPipeline)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyPipeline");
	if (!ctx.vkDestroyPipeline_) LOG_ERROR("VK_Init: vkDestroyPipeline_ is NULL");

	ctx.vkCreateShaderModule_ = (PFN_vkCreateShaderModule)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateShaderModule");
	if (!ctx.vkCreateShaderModule_) LOG_ERROR("VK_Init: vkCreateShaderModule_ is NULL");

	ctx.vkDestroyShaderModule_ = (PFN_vkDestroyShaderModule)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyShaderModule");
	if (!ctx.vkDestroyShaderModule_) LOG_ERROR("VK_Init: vkDestroyShaderModule_ is NULL");

	ctx.vkCreateBuffer_ = (PFN_vkCreateBuffer)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateBuffer");
	if (!ctx.vkCreateBuffer_) LOG_ERROR("VK_Init: vkCreateBuffer_ is NULL");

	ctx.vkDestroyBuffer_ = (PFN_vkDestroyBuffer)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyBuffer");
	if (!ctx.vkDestroyBuffer_) LOG_ERROR("VK_Init: vkDestroyBuffer_ is NULL");

	ctx.vkGetBufferMemoryRequirements_ = (PFN_vkGetBufferMemoryRequirements)ctx.vkGetDeviceProcAddr_(ctx.device, "vkGetBufferMemoryRequirements");
	if (!ctx.vkGetBufferMemoryRequirements_) LOG_ERROR("VK_Init: vkGetBufferMemoryRequirements_ is NULL");

	ctx.vkAllocateMemory_ = (PFN_vkAllocateMemory)ctx.vkGetDeviceProcAddr_(ctx.device, "vkAllocateMemory");
	if (!ctx.vkAllocateMemory_) LOG_ERROR("VK_Init: vkAllocateMemory_ is NULL");

	ctx.vkFreeMemory_ = (PFN_vkFreeMemory)ctx.vkGetDeviceProcAddr_(ctx.device, "vkFreeMemory");
	if (!ctx.vkFreeMemory_) LOG_ERROR("VK_Init: vkFreeMemory_ is NULL");

	ctx.vkBindBufferMemory_ = (PFN_vkBindBufferMemory)ctx.vkGetDeviceProcAddr_(ctx.device, "vkBindBufferMemory");
	if (!ctx.vkBindBufferMemory_) LOG_ERROR("VK_Init: vkBindBufferMemory_ is NULL");

	ctx.vkMapMemory_ = (PFN_vkMapMemory)ctx.vkGetDeviceProcAddr_(ctx.device, "vkMapMemory");
	if (!ctx.vkMapMemory_) LOG_ERROR("VK_Init: vkMapMemory_ is NULL");

	ctx.vkUnmapMemory_ = (PFN_vkUnmapMemory)ctx.vkGetDeviceProcAddr_(ctx.device, "vkUnmapMemory");
	if (!ctx.vkUnmapMemory_) LOG_ERROR("VK_Init: vkUnmapMemory_ is NULL");

	ctx.vkCreateImage_ = (PFN_vkCreateImage)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateImage");
	if (!ctx.vkCreateImage_) LOG_ERROR("VK_Init: vkCreateImage_ is NULL");

	ctx.vkDestroyImage_ = (PFN_vkDestroyImage)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyImage");
	if (!ctx.vkDestroyImage_) LOG_ERROR("VK_Init: vkDestroyImage_ is NULL");

	ctx.vkGetImageMemoryRequirements_ = (PFN_vkGetImageMemoryRequirements)ctx.vkGetDeviceProcAddr_(ctx.device, "vkGetImageMemoryRequirements");
	if (!ctx.vkGetImageMemoryRequirements_) LOG_ERROR("VK_Init: vkGetImageMemoryRequirements_ is NULL");

	ctx.vkBindImageMemory_ = (PFN_vkBindImageMemory)ctx.vkGetDeviceProcAddr_(ctx.device, "vkBindImageMemory");
	if (!ctx.vkBindImageMemory_) LOG_ERROR("VK_Init: vkBindImageMemory_ is NULL");

	ctx.vkCreateSampler_ = (PFN_vkCreateSampler)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreateSampler");
	if (!ctx.vkCreateSampler_) LOG_ERROR("VK_Init: vkCreateSampler_ is NULL");

	ctx.vkDestroySampler_ = (PFN_vkDestroySampler)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroySampler");
	if (!ctx.vkDestroySampler_) LOG_ERROR("VK_Init: vkDestroySampler_ is NULL");

	ctx.vkCmdCopyBuffer_ = (PFN_vkCmdCopyBuffer)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdCopyBuffer");
	if (!ctx.vkCmdCopyBuffer_) LOG_ERROR("VK_Init: vkCmdCopyBuffer_ is NULL");

	ctx.vkCmdCopyBufferToImage_ = (PFN_vkCmdCopyBufferToImage)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdCopyBufferToImage");
	if (!ctx.vkCmdCopyBufferToImage_) LOG_ERROR("VK_Init: vkCmdCopyBufferToImage_ is NULL");

	ctx.vkCmdBlitImage_ = (PFN_vkCmdBlitImage)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCmdBlitImage");
	if (!ctx.vkCmdBlitImage_) LOG_ERROR("VK_Init: vkCmdBlitImage_ is NULL");

	ctx.vkCreatePipelineCache_ = (PFN_vkCreatePipelineCache)ctx.vkGetDeviceProcAddr_(ctx.device, "vkCreatePipelineCache");
	if (!ctx.vkCreatePipelineCache_) LOG_ERROR("VK_Init: vkCreatePipelineCache_ is NULL");
	ctx.vkDestroyPipelineCache_ = (PFN_vkDestroyPipelineCache)ctx.vkGetDeviceProcAddr_(ctx.device, "vkDestroyPipelineCache");
	if (!ctx.vkDestroyPipelineCache_) LOG_ERROR("VK_Init: vkDestroyPipelineCache_ is NULL");
	ctx.vkGetPipelineCacheData_ = (PFN_vkGetPipelineCacheData)ctx.vkGetDeviceProcAddr_(ctx.device, "vkGetPipelineCacheData");
	if (!ctx.vkGetPipelineCacheData_) LOG_ERROR("VK_Init: vkGetPipelineCacheData_ is NULL");

	// Hard-fail if any required device entry point is missing. Logging and
	// limping on turns "driver too old" into a null-call crash mid-frame;
	// failing here lets the caller fall back to the GL backend cleanly.
	// vkCmdBlitImage / vkGetPhysicalDeviceFormatProperties are deliberately
	// absent from this list -- the texture builder has a 1-mip fallback.
	{
		const struct { const void* fn; const char* name; } required[] =
		{
			{ (const void*)ctx.vkDestroyDevice_,              "vkDestroyDevice" },
			{ (const void*)ctx.vkGetDeviceQueue_,             "vkGetDeviceQueue" },
			{ (const void*)ctx.vkDeviceWaitIdle_,             "vkDeviceWaitIdle" },
			{ (const void*)ctx.vkCreateSwapchainKHR_,         "vkCreateSwapchainKHR" },
			{ (const void*)ctx.vkDestroySwapchainKHR_,        "vkDestroySwapchainKHR" },
			{ (const void*)ctx.vkGetSwapchainImagesKHR_,      "vkGetSwapchainImagesKHR" },
			{ (const void*)ctx.vkAcquireNextImageKHR_,        "vkAcquireNextImageKHR" },
			{ (const void*)ctx.vkQueuePresentKHR_,            "vkQueuePresentKHR" },
			{ (const void*)ctx.vkCreateImageView_,            "vkCreateImageView" },
			{ (const void*)ctx.vkDestroyImageView_,           "vkDestroyImageView" },
			{ (const void*)ctx.vkCreateCommandPool_,          "vkCreateCommandPool" },
			{ (const void*)ctx.vkDestroyCommandPool_,         "vkDestroyCommandPool" },
			{ (const void*)ctx.vkAllocateCommandBuffers_,     "vkAllocateCommandBuffers" },
			{ (const void*)ctx.vkResetCommandPool_,           "vkResetCommandPool" },
			{ (const void*)ctx.vkResetCommandBuffer_,         "vkResetCommandBuffer" },
			{ (const void*)ctx.vkBeginCommandBuffer_,         "vkBeginCommandBuffer" },
			{ (const void*)ctx.vkEndCommandBuffer_,           "vkEndCommandBuffer" },
			{ (const void*)ctx.vkCreateSemaphore_,            "vkCreateSemaphore" },
			{ (const void*)ctx.vkDestroySemaphore_,           "vkDestroySemaphore" },
			{ (const void*)ctx.vkCreateFence_,                "vkCreateFence" },
			{ (const void*)ctx.vkDestroyFence_,               "vkDestroyFence" },
			{ (const void*)ctx.vkWaitForFences_,              "vkWaitForFences" },
			{ (const void*)ctx.vkResetFences_,                "vkResetFences" },
			{ (const void*)ctx.vkQueueSubmit2_,               "vkQueueSubmit2" },
			{ (const void*)ctx.vkCmdPipelineBarrier2_,        "vkCmdPipelineBarrier2" },
			{ (const void*)ctx.vkCmdBeginRendering_,          "vkCmdBeginRendering" },
			{ (const void*)ctx.vkCmdEndRendering_,            "vkCmdEndRendering" },
			{ (const void*)ctx.vkCmdSetViewport_,             "vkCmdSetViewport" },
			{ (const void*)ctx.vkCmdSetScissor_,              "vkCmdSetScissor" },
			{ (const void*)ctx.vkCmdBindPipeline_,            "vkCmdBindPipeline" },
			{ (const void*)ctx.vkCmdBindDescriptorSets_,      "vkCmdBindDescriptorSets" },
			{ (const void*)ctx.vkCmdBindVertexBuffers_,       "vkCmdBindVertexBuffers" },
			{ (const void*)ctx.vkCmdPushConstants_,           "vkCmdPushConstants" },
			{ (const void*)ctx.vkCmdDraw_,                    "vkCmdDraw" },
			{ (const void*)ctx.vkCreateDescriptorSetLayout_,  "vkCreateDescriptorSetLayout" },
			{ (const void*)ctx.vkDestroyDescriptorSetLayout_, "vkDestroyDescriptorSetLayout" },
			{ (const void*)ctx.vkCreateDescriptorPool_,       "vkCreateDescriptorPool" },
			{ (const void*)ctx.vkDestroyDescriptorPool_,      "vkDestroyDescriptorPool" },
			{ (const void*)ctx.vkAllocateDescriptorSets_,     "vkAllocateDescriptorSets" },
			{ (const void*)ctx.vkUpdateDescriptorSets_,       "vkUpdateDescriptorSets" },
			{ (const void*)ctx.vkCreatePipelineLayout_,       "vkCreatePipelineLayout" },
			{ (const void*)ctx.vkDestroyPipelineLayout_,      "vkDestroyPipelineLayout" },
			{ (const void*)ctx.vkCreateGraphicsPipelines_,    "vkCreateGraphicsPipelines" },
			{ (const void*)ctx.vkDestroyPipeline_,            "vkDestroyPipeline" },
			{ (const void*)ctx.vkCreateShaderModule_,         "vkCreateShaderModule" },
			{ (const void*)ctx.vkDestroyShaderModule_,        "vkDestroyShaderModule" },
			{ (const void*)ctx.vkCreateBuffer_,               "vkCreateBuffer" },
			{ (const void*)ctx.vkDestroyBuffer_,              "vkDestroyBuffer" },
			{ (const void*)ctx.vkGetBufferMemoryRequirements_,"vkGetBufferMemoryRequirements" },
			{ (const void*)ctx.vkAllocateMemory_,             "vkAllocateMemory" },
			{ (const void*)ctx.vkFreeMemory_,                 "vkFreeMemory" },
			{ (const void*)ctx.vkBindBufferMemory_,           "vkBindBufferMemory" },
			{ (const void*)ctx.vkMapMemory_,                  "vkMapMemory" },
			{ (const void*)ctx.vkUnmapMemory_,                "vkUnmapMemory" },
			{ (const void*)ctx.vkCreateImage_,                "vkCreateImage" },
			{ (const void*)ctx.vkDestroyImage_,               "vkDestroyImage" },
			{ (const void*)ctx.vkGetImageMemoryRequirements_, "vkGetImageMemoryRequirements" },
			{ (const void*)ctx.vkBindImageMemory_,            "vkBindImageMemory" },
			{ (const void*)ctx.vkCreateSampler_,              "vkCreateSampler" },
			{ (const void*)ctx.vkDestroySampler_,             "vkDestroySampler" },
			{ (const void*)ctx.vkCmdCopyBuffer_,              "vkCmdCopyBuffer" },
			{ (const void*)ctx.vkCmdCopyBufferToImage_,       "vkCmdCopyBufferToImage" },
			{ (const void*)ctx.vkCreatePipelineCache_,        "vkCreatePipelineCache" },
			{ (const void*)ctx.vkDestroyPipelineCache_,       "vkDestroyPipelineCache" },
			{ (const void*)ctx.vkGetPipelineCacheData_,       "vkGetPipelineCacheData" },
		};

		bool allLoaded = true;
		for (const auto& r : required)
		{
			if (!r.fn)
			{
				LOG_ERROR("VK_Init: required device entry point %s is missing", r.name);
				allLoaded = false;
			}
		}
		if (!allLoaded)
		{
			LOG_ERROR("VK_Init: aborting -- device/driver does not expose the required Vulkan 1.3 entry points");
			return false;
		}
	}

	// Get queues
	LOG_INFO("VK_Init: getting device queue(s)");
	ctx.vkGetDeviceQueue_(ctx.device, ctx.gfxQueueFamily, 0, &ctx.gfxQueue);
	ctx.presentQueue = ctx.gfxQueue;

	// Command pool + buffers
	LOG_INFO("VK_Init: creating command pool");

	VkCommandPoolCreateInfo cp{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cp.queueFamilyIndex = ctx.gfxQueueFamily;
	cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	if (ctx.vkCreateCommandPool_(ctx.device, &cp, nullptr, &ctx.cmdPool) != VK_SUCCESS)
	{
		LOG_ERROR("VK_Init: vkCreateCommandPool failed");
		return false;
	}

	ctx.cmdBuffers.resize(VkContext::kFramesInFlight);

	VkCommandBufferAllocateInfo cba{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cba.commandPool = ctx.cmdPool;
	cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cba.commandBufferCount = (uint32_t)ctx.cmdBuffers.size();

	LOG_INFO("VK_Init: allocating per-frame command buffers");

	if (ctx.vkAllocateCommandBuffers_(ctx.device, &cba, ctx.cmdBuffers.data()) != VK_SUCCESS)
	{
		LOG_ERROR("VK_Init: vkAllocateCommandBuffers failed for main cmd buffers");
		return false;
	}

	// Sync
	LOG_INFO("VK_Init: creating sync objects");

	VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	// renderFinished semaphores are NOT created here -- they are per swapchain
	// image and are owned by CreateSwapchain/DestroySwapchain.
	for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
	{
		if (ctx.vkCreateSemaphore_(ctx.device, &si, nullptr, &ctx.imageAvailable[i]) != VK_SUCCESS)
		{
			LOG_ERROR("VK_Init: vkCreateSemaphore (imageAvailable) failed");
			return false;
		}
		if (ctx.vkCreateFence_(ctx.device, &fi, nullptr, &ctx.inFlight[i]) != VK_SUCCESS)
		{
			LOG_ERROR("VK_Init: vkCreateFence failed");
			return false;
		}
	}

	// Swapchain
	LOG_INFO("VK_Init: creating swapchain");
	if (!CreateSwapchain(ctx))
	{
		LOG_ERROR("VK_Init: CreateSwapchain failed");
		return false;
	}

	// After swapchain, before any pipeline creation
	LOG_INFO("VK_Init: creating pipeline cache");
	VK_CreatePipelineCache(ctx, "pipeline_cache.bin");

	// Fullscreen (screenquad) pipeline creation is excluded from this port --
	// it depends on ScreenQuadVK and the screenquad shaders (Plans 3-6).

	// Upload pool/cmd/fence for texture creation
	LOG_INFO("VK_Init: creating upload command pool");

	VkCommandPoolCreateInfo ucp{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	ucp.queueFamilyIndex = ctx.gfxQueueFamily;
	ucp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	if (ctx.vkCreateCommandPool_(ctx.device, &ucp, nullptr, &ctx.uploadPool) != VK_SUCCESS)
	{
		LOG_ERROR("Vulkan: failed to create upload command pool");
		return false;
	}

	VkCommandBufferAllocateInfo ucba{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	ucba.commandPool = ctx.uploadPool;
	ucba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ucba.commandBufferCount = 1;

	LOG_INFO("VK_Init: allocating upload command buffer");

	if (ctx.vkAllocateCommandBuffers_(ctx.device, &ucba, &ctx.uploadCmd) != VK_SUCCESS)
	{
		LOG_ERROR("VK_Init: vkAllocateCommandBuffers failed for upload cmd");
		return false;
	}

	VkFenceCreateInfo ufi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	ufi.flags = 0;

	LOG_INFO("VK_Init: creating upload fence");

	if (ctx.vkCreateFence_(ctx.device, &ufi, nullptr, &ctx.uploadFence) != VK_SUCCESS)
	{
		LOG_ERROR("VK_Init: vkCreateFence failed for upload fence");
		return false;
	}

	LOG_INFO("VK_Init: success");
	return true;
}

void VK_Shutdown(VkContext& ctx)
{
	if (!ctx.device)
	{
		if (ctx.surface && ctx.instance && ctx.vkDestroySurfaceKHR_)
		{
			ctx.vkDestroySurfaceKHR_(ctx.instance, ctx.surface, nullptr);
			ctx.surface = VK_NULL_HANDLE;
		}

		if (ctx.debugMessenger && ctx.instance && ctx.vkDestroyDebugUtilsMessengerEXT_)
		{
			ctx.vkDestroyDebugUtilsMessengerEXT_(ctx.instance, ctx.debugMessenger, nullptr);
			ctx.debugMessenger = VK_NULL_HANDLE;
		}

		if (ctx.instance && ctx.vkDestroyInstance_)
		{
			ctx.vkDestroyInstance_(ctx.instance, nullptr);
			ctx.instance = VK_NULL_HANDLE;
		}

		LoaderClose(ctx.loaderModule);
		ctx.loaderModule = nullptr;
		return;
	}

	ctx.vkDeviceWaitIdle_(ctx.device);
	// Save and destroy pipeline cache before tearing down pipelines
	VK_SavePipelineCache(ctx, "pipeline_cache.bin");
	VK_DestroyPipelineCache(ctx);
	DestroySwapchain(ctx);

	// renderFinished semaphores were already destroyed by DestroySwapchain.
	for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
	{
		if (ctx.imageAvailable[i]) { ctx.vkDestroySemaphore_(ctx.device, ctx.imageAvailable[i], nullptr); ctx.imageAvailable[i] = VK_NULL_HANDLE; }
		if (ctx.inFlight[i]) { ctx.vkDestroyFence_(ctx.device, ctx.inFlight[i], nullptr); ctx.inFlight[i] = VK_NULL_HANDLE; }
	}

	if (ctx.uploadFence) { ctx.vkDestroyFence_(ctx.device, ctx.uploadFence, nullptr); ctx.uploadFence = VK_NULL_HANDLE; }
	if (ctx.uploadPool) { ctx.vkDestroyCommandPool_(ctx.device, ctx.uploadPool, nullptr); ctx.uploadPool = VK_NULL_HANDLE; }
	ctx.uploadCmd = VK_NULL_HANDLE;

	if (ctx.cmdPool) { ctx.vkDestroyCommandPool_(ctx.device, ctx.cmdPool, nullptr); ctx.cmdPool = VK_NULL_HANDLE; }
	ctx.cmdBuffers.clear();

	ctx.vkDestroyDevice_(ctx.device, nullptr);
	ctx.device = VK_NULL_HANDLE;

	if (ctx.surface)
	{
		ctx.vkDestroySurfaceKHR_(ctx.instance, ctx.surface, nullptr);
		ctx.surface = VK_NULL_HANDLE;
	}

	if (ctx.debugMessenger && ctx.vkDestroyDebugUtilsMessengerEXT_)
	{
		ctx.vkDestroyDebugUtilsMessengerEXT_(ctx.instance, ctx.debugMessenger, nullptr);
		ctx.debugMessenger = VK_NULL_HANDLE;
	}

	if (ctx.instance)
	{
		ctx.vkDestroyInstance_(ctx.instance, nullptr);
		ctx.instance = VK_NULL_HANDLE;
	}

	LoaderClose(ctx.loaderModule);
	ctx.loaderModule = nullptr;
}

bool VK_RecreateSwapchain(VkContext& ctx)
{
	if (!ctx.device)
		return false;

	ctx.vkDeviceWaitIdle_(ctx.device);

	// Fullscreen (screenquad) pipeline destroy/recreate is excluded from this
	// port -- nothing creates it yet (Plans 3-6).
	DestroySwapchain(ctx);

	if (!CreateSwapchain(ctx))
		return false;

	return true;
}

// -----------------------------------------------------------------------------
// Frame-pass state tracker.
//
// Tracks whether the frame's render pass is currently open. Used to detect
// misuse -- specifically, if a subsystem still has its own vkCmdBeginRendering
// / vkCmdEndRendering left in (i.e. the user didn't install a pass-less
// version of that subsystem), it'll close our pass mid-frame and we'll hit
// Undefined Behavior in VK_EndFrame. Logging a clear error here beats a
// driver-side crash with no context.
// -----------------------------------------------------------------------------
static bool s_framePassOpen = false;

// Per-frame cache. Populated in VK_BeginFrame, consumed in VK_EndFrame.
// This lets VK_EndFrame avoid touching ctx.swapchainImages/.swapchainViews
// altogether -- useful as a diagnostic / workaround if something about the
// vector access is unreliable (e.g. mismatched struct layout across TUs).
static VkImage       s_frameSwapImage = VK_NULL_HANDLE;
static uint32_t      s_frameImageIndex = UINT32_MAX;

// -----------------------------------------------------------------------------
// VK_BeginFrame / VK_EndFrame
//
// The engine frame owns exactly ONE Vulkan render pass. VK_BeginFrame acquires
// a swapchain image, transitions it to COLOR_ATTACHMENT_OPTIMAL, and opens a
// dynamic rendering pass with LOAD_OP_CLEAR. The command buffer is returned
// to the caller already inside that pass, with a default full-surface viewport
// and scissor set.
//
// VK_EndFrame closes the pass, transitions the image to PRESENT_SRC_KHR, ends
// the command buffer, submits, and presents.
//
// Subsystems (BMFont, winfont, sprite, UI, debug draw, fpoly, bg) do NOT open
// their own render passes. They just record draws -- bind pipeline, set
// viewport/scissor if they need something other than full-surface, bind
// descriptors and vertex buffers, draw. This is the "one big pass per frame"
// architecture that matches canonical Vulkan usage and avoids the cascading
// barrier/layout issues that come from seven subsystems each opening and
// closing their own pass.
//
// The clear color is a fixed dark navy; the background renderer draws its
// fullscreen texture over the cleared image as the first draw of the frame,
// so the clear color is only visible for frames where the BG isn't active
// (e.g. init errors, missing texture). If you need a different clear, edit
// the clearValue below.
// -----------------------------------------------------------------------------

// Forward declaration -- full definition lives near the bottom of this file
// alongside VK_RecordFullscreenPass (which is the other caller).
static void CmdSwapchainBarrier(VkContext& ctx,
	VkCommandBuffer cmd,
	VkImage image,
	VkImageLayout oldLayout,
	VkImageLayout newLayout);

bool VK_BeginFrame(VkContext& ctx, uint32_t& outImageIndex)
{
	// First-few-frames diagnostic. When isolating crashes that happen right
	// at the start, the log tells us the flow got this far. Stops logging
	// after a handful of frames to avoid spamming the log file.
	static int s_traceCount = 0;
	if (s_traceCount < 5)
	{
		LOG_INFO("VK_BeginFrame: enter (fi=%u, trace=%d)", ctx.frameIndex, s_traceCount);
		++s_traceCount;
	}

	if (!ctx.swapchain)
	{
		LOG_ERROR("VK_BeginFrame: ctx.swapchain is NULL");
		return false;
	}

	uint32_t fi = ctx.frameIndex;

	// Wait for the previous use of this frame-in-flight slot to finish on the
	// GPU. After this returns, everything associated with slot fi (cmd buffer,
	// per-frame VBOs/UBOs in subsystems) is safe to overwrite.
	ctx.vkWaitForFences_(ctx.device, 1, &ctx.inFlight[fi], VK_TRUE, UINT64_MAX);

	VkResult r = ctx.vkAcquireNextImageKHR_(ctx.device, ctx.swapchain,
		UINT64_MAX, ctx.imageAvailable[fi], VK_NULL_HANDLE, &outImageIndex);

	// VK_SUBOPTIMAL_KHR is not a failure. The image is usable and present
	// will still work; SUBOPTIMAL just means the swapchain doesn't perfectly
	// match the surface (commonly seen for a frame or two after resize).
	// Only VK_ERROR_OUT_OF_DATE_KHR and genuine errors warrant a bail.
	if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
	{
		LOG_ERROR("VK_BeginFrame: vkAcquireNextImageKHR returned VkResult=%d", (int)r);
		return false;
	}

	// Reset the fence only AFTER the acquire succeeded. Resetting before a
	// failed acquire leaves the slot's fence unsignaled with no submit to
	// re-signal it -- the next wait on this slot would then deadlock (only
	// the caller's RecreateSyncObjects recovery would save it).
	ctx.vkResetFences_(ctx.device, 1, &ctx.inFlight[fi]);

	// Per-frame debug logging from the donor bring-up; re-enable locally when
	// debugging frame issues. (s_traceCount stops incrementing at 5, so this
	// <= 5 check was true every frame and flooded systemlog.txt.)
	//if (s_traceCount <= 5)
	//{
	//	LOG_INFO("VK_BeginFrame: acquired imageIndex=%u (fi=%u, acquireResult=%d, swapchainImages.size=%zu)",
	//		outImageIndex, fi, (int)r, ctx.swapchainImages.size());
	//}

	// Reset only THIS slot's command buffer. The previous code called
	// vkResetCommandPool on the entire pool, which is a Vulkan spec
	// violation: it frees ALL command buffer allocations in the pool,
	// including the OTHER slot's cmd buffer that may still be executing
	// on the GPU. With kFramesInFlight=2 and a triple-buffered swapchain,
	// the engine got away with this for the first few frames because the
	// GPU happened to finish ahead of the next reset; once frame timing
	// crossed a threshold (e.g., when UI work or a busier scene was added)
	// the pool reset started corrupting the still-executing cmd buffer
	// for the other slot, surfacing as VK_ERROR_DEVICE_LOST on the next
	// submit. Per-buffer reset is safe because VK_BeginFrame already
	// waited on inFlight[fi], so cmdBuffers[fi] is guaranteed idle.
	ctx.vkResetCommandBuffer_(ctx.cmdBuffers[fi], 0);

	VkCommandBuffer cmd = ctx.cmdBuffers[fi];

	VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkResult br = ctx.vkBeginCommandBuffer_(cmd, &bi);
	if (br != VK_SUCCESS)
	{
		LOG_ERROR("VK_BeginFrame: vkBeginCommandBuffer returned VkResult=%d (fi=%u)",
			(int)br, fi);
		return false;
	}

	// Transition the acquired swapchain image to COLOR_ATTACHMENT_OPTIMAL.
	// This is the ONE barrier at the start of the frame. Subsystems do not
	// transition the swapchain image themselves.
	//
	// oldLayout is UNDEFINED, not PRESENT_SRC: a freshly created swapchain
	// image has never been presented, so claiming PRESENT_SRC on first use
	// (and after every swapchain recreate) is a spec violation. The pass
	// below clears, so discarding previous contents is always correct.
	VkImage scImg = ctx.swapchainImages[outImageIndex];

	// Cache for VK_EndFrame. This saves VK_EndFrame from touching the vector
	// again at end-of-frame (which was crashing in some configurations).
	s_frameSwapImage = scImg;
	s_frameImageIndex = outImageIndex;

	CmdSwapchainBarrier(ctx, cmd, scImg,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Open the frame's render pass. LOAD_OP_CLEAR so a missing BG draw still
	// gives us a defined starting color and no sampling-from-uninitialized.
	// Black from Plan 3 on: game pixels are the proof of rendering now, and
	// the letterbox borders must be black like the GL chain. (Plan 2 used a
	// gate blue here to prove presentation before anything drew.)
	VkClearValue clearVal{};
	clearVal.color.float32[0] = 0.0f;
	clearVal.color.float32[1] = 0.0f;
	clearVal.color.float32[2] = 0.0f;
	clearVal.color.float32[3] = 1.0f;

	VkRenderingAttachmentInfo colorAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	colorAtt.imageView = ctx.swapchainViews[outImageIndex];
	colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAtt.clearValue = clearVal;

	VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	ri.renderArea.offset = { 0, 0 };
	ri.renderArea.extent = ctx.swapchainExtent;
	ri.layerCount = 1;
	ri.colorAttachmentCount = 1;
	ri.pColorAttachments = &colorAtt;

	// Per-frame debug logging from the donor bring-up; re-enable locally when
	// debugging frame issues.
	//if (s_traceCount <= 5)
	//{
	//	LOG_INFO("VK_BeginFrame: about to vkCmdBeginRendering (cmd=%p, &ctx=%p, fi=%u, imgIdx=%u, ext=%ux%u, view=%p)",
	//		(void*)cmd, (void*)&ctx, fi, outImageIndex, ctx.swapchainExtent.width, ctx.swapchainExtent.height,
	//		(void*)ctx.swapchainViews[outImageIndex]);
	//}

	ctx.vkCmdBeginRendering_(cmd, &ri);
	s_framePassOpen = true;
	ctx.activeColorFormat = ctx.swapchainFormat;

	// Per-frame debug logging from the donor bring-up; re-enable locally when
	// debugging frame issues.
	//if (s_traceCount <= 5)
	//{
	//	LOG_INFO("VK_BeginFrame: vkCmdBeginRendering OK, setting default viewport/scissor");
	//}

	// Default full-surface viewport. Subsystems can override per-draw, but
	// this is what they'll inherit unless they say otherwise. y-down, matching
	// the engine's ortho projections for UI / BMFont / winfont text. For
	// subsystems that need a flipped viewport (winfont with y-down ortho
	// texture-space expectations), they set their own viewport in their
	// record function.
	VkViewport vp{};
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = (float)ctx.swapchainExtent.width;
	vp.height = (float)ctx.swapchainExtent.height;
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;
	ctx.vkCmdSetViewport_(cmd, 0, 1, &vp);

	VkRect2D sc{};
	sc.offset = { 0, 0 };
	sc.extent = ctx.swapchainExtent;
	ctx.vkCmdSetScissor_(cmd, 0, 1, &sc);

	return true;
}

bool VK_EndFrame(VkContext& ctx, uint32_t imageIndex)
{
	uint32_t fi = ctx.frameIndex;
	VkCommandBuffer cmd = ctx.cmdBuffers[fi];

	// renderFinished is per swapchain image (see sys_vk.h). Look it up once
	// for both the normal and the recovery submit paths below.
	if (imageIndex >= ctx.renderFinished.size())
	{
		LOG_ERROR("VK_EndFrame: imageIndex %u out of range for renderFinished (size=%zu)",
			imageIndex, ctx.renderFinished.size());
		return false;
	}
	VkSemaphore renderDone = ctx.renderFinished[imageIndex];

	// Sanity check: s_framePassOpen should be true here. If it's false it
	// means something already called vkCmdEndRendering during the frame --
	// almost certainly a subsystem (sprite / bmfont / debug_draw / fast_poly
	// / winfont / ui) whose pass-less .cpp was NOT installed from the refactor
	// bundle. Its RecordSwapchainPass or equivalent still opens+closes a
	// render pass, and that closes ours prematurely. Calling vkCmdEndRendering
	// again here is undefined behavior (crashes on NVIDIA).
	//
	// Log a clear message and skip the bad calls so the user sees which pass
	// management got duplicated.
	if (!s_framePassOpen)
	{
		LOG_ERROR("VK_EndFrame: pass already closed by a subsystem. "
			"Check that sprite_vk_renderer.cpp, bmfont_vk_renderer.cpp, "
			"debug_draw_vk.cpp, fast_poly.cpp, winfont_backend_vk.cpp, "
			"and ui_renderer_vk.cpp are all from the pass-less refactor "
			"bundle (they must NOT contain vkCmdBeginRendering or "
			"vkCmdEndRendering).");
		// Try to recover: just end the cmd buffer and submit. The swapchain
		// is probably already in PRESENT layout because whoever closed the
		// pass probably also did the COLOR->PRESENT transition. Don't add
		// another barrier -- that would transition from the WRONG layout.
		VkResult ec = ctx.vkEndCommandBuffer_(cmd);
		if (ec != VK_SUCCESS)
		{
			LOG_ERROR("VK_EndFrame (recovery): vkEndCommandBuffer VkResult=%d", (int)ec);
			return false;
		}

		// Submit + present so at least we don't hang.
		VkCommandBufferSubmitInfo csi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
		csi.commandBuffer = ctx.cmdBuffers[fi];

		VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		wait.semaphore = ctx.imageAvailable[fi];
		wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		signal.semaphore = renderDone;
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

		VkSubmitInfo2 si{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		si.waitSemaphoreInfoCount = 1;
		si.pWaitSemaphoreInfos = &wait;
		si.commandBufferInfoCount = 1;
		si.pCommandBufferInfos = &csi;
		si.signalSemaphoreInfoCount = 1;
		si.pSignalSemaphoreInfos = &signal;

		ctx.vkQueueSubmit2_(ctx.gfxQueue, 1, &si, ctx.inFlight[fi]);

		VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		pi.waitSemaphoreCount = 1;
		pi.pWaitSemaphores = &renderDone;
		pi.swapchainCount = 1;
		pi.pSwapchains = &ctx.swapchain;
		pi.pImageIndices = &imageIndex;
		ctx.vkQueuePresentKHR_(ctx.presentQueue, &pi);

		ctx.frameIndex = (ctx.frameIndex + 1) % VkContext::kFramesInFlight;
		return true;
	}

	// Close the frame's render pass. Single vkCmdEndRendering per frame.
	static int s_endTraceCount = 0;
	const bool trace = (s_endTraceCount < 5);
	if (trace) LOG_INFO("VK_EndFrame: step 1 - about to vkCmdEndRendering (fi=%u, cmd=%p, fptr=%p, &ctx=%p, ctx.frameIndex=%u, s_framePassOpen=%d)",
		fi, (void*)cmd, (void*)ctx.vkCmdEndRendering_, (void*)&ctx, ctx.frameIndex, (int)s_framePassOpen);

	if (cmd == VK_NULL_HANDLE)
	{
		LOG_ERROR("VK_EndFrame: cmd is NULL, aborting");
		return false;
	}
	if (!ctx.vkCmdEndRendering_)
	{
		LOG_ERROR("VK_EndFrame: ctx.vkCmdEndRendering_ is NULL, aborting");
		return false;
	}

	ctx.vkCmdEndRendering_(cmd);
	s_framePassOpen = false;
	ctx.activeColorFormat = VK_FORMAT_UNDEFINED;

	if (trace) LOG_INFO("VK_EndFrame: step 2 - vkCmdEndRendering OK; cached imageIndex=%u (passed=%u), cached scImg=%p",
		s_frameImageIndex, imageIndex, (void*)s_frameSwapImage);

	if (s_frameSwapImage == VK_NULL_HANDLE)
	{
		LOG_ERROR("VK_EndFrame: cached swapchain image is NULL -- was VK_BeginFrame actually called this frame?");
		ctx.vkEndCommandBuffer_(cmd);
		ctx.frameIndex = (ctx.frameIndex + 1) % VkContext::kFramesInFlight;
		return false;
	}

	// Transition the swapchain image COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR.
	// The ONE barrier at the end of the frame.
	//
	// We use the cached VkImage from VK_BeginFrame (s_frameSwapImage) rather
	// than indexing ctx.swapchainImages[imageIndex] here. Repeated std::vector
	// indexing across a long frame's command buffer recording has been
	// triggering an access violation in some configurations on NVIDIA; the
	// cached handle avoids the access entirely. The handle itself is stable
	// across the frame -- the swapchain isn't being recreated mid-frame.
	VkImage scImg = s_frameSwapImage;

	if (trace) LOG_INFO("VK_EndFrame: step 3 - about to barrier COLOR->PRESENT on scImg=%p", (void*)scImg);

	CmdSwapchainBarrier(ctx, cmd, scImg,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	if (trace) LOG_INFO("VK_EndFrame: step 4 - barrier OK, about to vkEndCommandBuffer");
	++s_endTraceCount;

	// Clear cache so a stray EndFrame without a matching BeginFrame fails
	// safely rather than reusing stale state.
	s_frameSwapImage = VK_NULL_HANDLE;
	s_frameImageIndex = UINT32_MAX;

	VkResult ec = ctx.vkEndCommandBuffer_(cmd);
	if (ec != VK_SUCCESS)
	{
		LOG_ERROR("VK_EndFrame: vkEndCommandBuffer returned VkResult=%d (fi=%u)",
			(int)ec, fi);
		return false;
	}

	VkCommandBufferSubmitInfo csi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	csi.commandBuffer = ctx.cmdBuffers[fi];

	VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	wait.semaphore = ctx.imageAvailable[fi];
	wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	signal.semaphore = renderDone;
	signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkSubmitInfo2 si{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	si.waitSemaphoreInfoCount = 1;
	si.pWaitSemaphoreInfos = &wait;
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &csi;
	si.signalSemaphoreInfoCount = 1;
	si.pSignalSemaphoreInfos = &signal;

	VkResult sr = ctx.vkQueueSubmit2_(ctx.gfxQueue, 1, &si, ctx.inFlight[fi]);
	if (sr != VK_SUCCESS)
	{
		LOG_ERROR("VK_EndFrame: vkQueueSubmit2 returned VkResult=%d (fi=%u)",
			(int)sr, fi);
		return false;
	}

	VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &renderDone;
	pi.swapchainCount = 1;
	pi.pSwapchains = &ctx.swapchain;
	pi.pImageIndices = &imageIndex;

	VkResult pr = ctx.vkQueuePresentKHR_(ctx.presentQueue, &pi);

	// VK_SUBOPTIMAL_KHR means "image presented, but swapchain is stale" --
	// still a success for the current frame. VK_ERROR_OUT_OF_DATE_KHR is
	// the one that genuinely requires a recreate. Caller can recreate at
	// leisure; we still report success here.
	if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR)
	{
		LOG_ERROR("VK_EndFrame: vkQueuePresentKHR returned VkResult=%d", (int)pr);
		return false;
	}

	ctx.frameIndex = (ctx.frameIndex + 1) % VkContext::kFramesInFlight;
	return true;
}

// -----------------------------------------------------------------------------
// Upload helpers
// -----------------------------------------------------------------------------
bool VK_BeginUpload(VkContext& ctx)
{
	ctx.vkResetCommandPool_(ctx.device, ctx.uploadPool, 0);

	VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	if (ctx.vkBeginCommandBuffer_(ctx.uploadCmd, &bi) != VK_SUCCESS)
		return false;

	return true;
}

bool VK_EndUpload(VkContext& ctx)
{
	if (ctx.vkEndCommandBuffer_(ctx.uploadCmd) != VK_SUCCESS)
		return false;

	ctx.vkResetFences_(ctx.device, 1, &ctx.uploadFence);

	VkCommandBufferSubmitInfo csi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	csi.commandBuffer = ctx.uploadCmd;

	VkSubmitInfo2 si{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	si.commandBufferInfoCount = 1;
	si.pCommandBufferInfos = &csi;

	if (ctx.vkQueueSubmit2_(ctx.gfxQueue, 1, &si, ctx.uploadFence) != VK_SUCCESS)
		return false;

	if (ctx.vkWaitForFences_(ctx.device, 1, &ctx.uploadFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		return false;

	return true;
}

// -----------------------------------------------------------------------------
// Texture helpers
// -----------------------------------------------------------------------------
static uint32_t FindMemoryTypeIdx(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags props)
{
	VkPhysicalDeviceMemoryProperties mp{};
	ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);

	for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
	{
		if ((typeBits & (1u << i)) == 0)
			continue;

		if ((mp.memoryTypes[i].propertyFlags & props) == props)
			return i;
	}
	return 0xFFFFFFFFu;
}

// -----------------------------------------------------------------------------
// VK_BuildRGBA8Texture
//
// Internal helper that implements the SRGB and UNORM texture builders in a
// single code path. The only difference between the two public entry points
// is the image format, so sharing the implementation keeps their behavior
// identical (mip count, layout transitions, sampler settings).
//
// Generates a full mip chain via vkCmdBlitImage. Each level N+1 is produced
// by linear-downsampling level N. Requires the target format to support
// BLIT_SRC and BLIT_DST in optimal tiling, which every desktop driver reports
// for R8G8B8A8_SRGB and R8G8B8A8_UNORM. If the runtime format query reports
// the feature is missing, falls back to a single-level upload.
//
// Final image layout is SHADER_READ_ONLY_OPTIMAL across every mip level.
// -----------------------------------------------------------------------------
static bool VK_BuildRGBA8Texture(VkContext& ctx,
	const void* rgba8Pixels,
	uint32_t width, uint32_t height,
	VkFormat format,
	bool generateMips,
	bool nearestFilter,
	VkTexture& outTex)
{
	VK_DestroyTexture(ctx, outTex);

	if (!rgba8Pixels || width == 0 || height == 0)
		return false;

	// Staging handles are declared up front so the shared failure path can
	// clean them. The image/memory/view/sampler side lives in outTex and is
	// cleaned by VK_DestroyTexture. Without this, an allocation failure
	// mid-build leaked the image and/or staging buffer.
	VkBuffer sbuf = VK_NULL_HANDLE;
	VkDeviceMemory smem = VK_NULL_HANDLE;

	auto fail = [&]() -> bool
	{
		if (sbuf) { ctx.vkDestroyBuffer_(ctx.device, sbuf, nullptr); sbuf = VK_NULL_HANDLE; }
		if (smem) { ctx.vkFreeMemory_(ctx.device, smem, nullptr); smem = VK_NULL_HANDLE; }
		VK_DestroyTexture(ctx, outTex);
		return false;
	};

	// Decide whether we can generate mips. Requires the caller to have opted
	// in AND the format to advertise BLIT_SRC_BIT + BLIT_DST_BIT +
	// SAMPLED_IMAGE_FILTER_LINEAR_BIT. If either condition fails we fall back
	// to a 1-mip upload.
	bool canBlit = false;
	if (generateMips && ctx.vkGetPhysicalDeviceFormatProperties_ && ctx.vkCmdBlitImage_)
	{
		VkFormatProperties fp{};
		ctx.vkGetPhysicalDeviceFormatProperties_(ctx.phys, format, &fp);
		const VkFormatFeatureFlags need =
			VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
			VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		canBlit = ((fp.optimalTilingFeatures & need) == need);
	}

	// log2 chain size. Single-pixel dimensions clamp to 1 level.
	uint32_t mipLevels = 1;
	if (canBlit)
	{
		uint32_t dim = (width > height) ? width : height;
		while (dim > 1u) { ++mipLevels; dim >>= 1; }
	}

	outTex.width = width;
	outTex.height = height;
	outTex.format = format;
	outTex.mipLevels = mipLevels;

	// Create image. TRANSFER_SRC is required so we can blit OUT of level N
	// when producing level N+1.
	VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ii.imageType = VK_IMAGE_TYPE_2D;
	ii.format = outTex.format;
	ii.extent.width = width;
	ii.extent.height = height;
	ii.extent.depth = 1;
	ii.mipLevels = mipLevels;
	ii.arrayLayers = 1;
	ii.samples = VK_SAMPLE_COUNT_1_BIT;
	ii.tiling = VK_IMAGE_TILING_OPTIMAL;
	ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (mipLevels > 1)
		ii.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (ctx.vkCreateImage_(ctx.device, &ii, nullptr, &outTex.image) != VK_SUCCESS)
		return false;

	VkMemoryRequirements mr{};
	ctx.vkGetImageMemoryRequirements_(ctx.device, outTex.image, &mr);

	uint32_t memIdx = FindMemoryTypeIdx(ctx, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (memIdx == 0xFFFFFFFFu)
		return fail();

	VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = memIdx;

	if (ctx.vkAllocateMemory_(ctx.device, &mai, nullptr, &outTex.memory) != VK_SUCCESS)
		return fail();

	if (ctx.vkBindImageMemory_(ctx.device, outTex.image, outTex.memory, 0) != VK_SUCCESS)
		return fail();

	// Staging buffer, host-visible, coherent.
	VkDeviceSize sizeBytes = (VkDeviceSize)width * (VkDeviceSize)height * 4;

	VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bc.size = sizeBytes;
	bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	if (ctx.vkCreateBuffer_(ctx.device, &bc, nullptr, &sbuf) != VK_SUCCESS)
		return fail();

	VkMemoryRequirements bmr{};
	ctx.vkGetBufferMemoryRequirements_(ctx.device, sbuf, &bmr);

	uint32_t smIdx = FindMemoryTypeIdx(ctx, bmr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (smIdx == 0xFFFFFFFFu)
		return fail();

	VkMemoryAllocateInfo bmai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	bmai.allocationSize = bmr.size;
	bmai.memoryTypeIndex = smIdx;

	if (ctx.vkAllocateMemory_(ctx.device, &bmai, nullptr, &smem) != VK_SUCCESS)
		return fail();

	if (ctx.vkBindBufferMemory_(ctx.device, sbuf, smem, 0) != VK_SUCCESS)
		return fail();

	void* dst = nullptr;
	if (ctx.vkMapMemory_(ctx.device, smem, 0, sizeBytes, 0, &dst) != VK_SUCCESS)
		return fail();

	memcpy(dst, rgba8Pixels, (size_t)sizeBytes);
	ctx.vkUnmapMemory_(ctx.device, smem);

	// Upload and mip-generation in a single command buffer submission.
	if (!VK_BeginUpload(ctx))
		return fail();

	// Transition every mip level UNDEFINED -> TRANSFER_DST_OPTIMAL. We copy
	// into level 0 from the staging buffer, then blit cascade into levels
	// 1..N-1. Levels 1..N-1 then get transitioned from TRANSFER_DST -> SRC
	// inside the blit loop below, except the last one, which is the blit
	// destination of the final iteration. We handle that at the end.
	{
		VkImageMemoryBarrier2 b1{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		b1.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		b1.srcAccessMask = 0;
		b1.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		b1.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		b1.image = outTex.image;
		b1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		b1.subresourceRange.baseMipLevel = 0;
		b1.subresourceRange.levelCount = mipLevels;
		b1.subresourceRange.baseArrayLayer = 0;
		b1.subresourceRange.layerCount = 1;

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &b1;
		ctx.vkCmdPipelineBarrier2_(ctx.uploadCmd, &dep);
	}

	// Copy staging into mip 0.
	{
		VkBufferImageCopy bic{};
		bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bic.imageSubresource.mipLevel = 0;
		bic.imageSubresource.baseArrayLayer = 0;
		bic.imageSubresource.layerCount = 1;
		bic.imageExtent.width = width;
		bic.imageExtent.height = height;
		bic.imageExtent.depth = 1;

		ctx.vkCmdCopyBufferToImage_(ctx.uploadCmd, sbuf, outTex.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
	}

	// Blit cascade: for each i in [1..mipLevels-1], blit level (i-1) -> i.
	// Before each blit, transition source level (i-1) from TRANSFER_DST ->
	// TRANSFER_SRC. After the whole loop, we transition level mipLevels-1
	// from TRANSFER_DST -> TRANSFER_SRC as part of the final barrier.
	int32_t mipW = (int32_t)width;
	int32_t mipH = (int32_t)height;

	for (uint32_t i = 1; i < mipLevels; ++i)
	{
		// Transition source level (i-1) to TRANSFER_SRC_OPTIMAL.
		{
			VkImageMemoryBarrier2 bs{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			bs.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			bs.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			bs.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			bs.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			bs.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			bs.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			bs.image = outTex.image;
			bs.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bs.subresourceRange.baseMipLevel = i - 1;
			bs.subresourceRange.levelCount = 1;
			bs.subresourceRange.baseArrayLayer = 0;
			bs.subresourceRange.layerCount = 1;

			VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dep.imageMemoryBarrierCount = 1;
			dep.pImageMemoryBarriers = &bs;
			ctx.vkCmdPipelineBarrier2_(ctx.uploadCmd, &dep);
		}

		int32_t nextW = (mipW > 1) ? (mipW >> 1) : 1;
		int32_t nextH = (mipH > 1) ? (mipH >> 1) : 1;

		VkImageBlit blit{};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipW, mipH, 1 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;

		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { nextW, nextH, 1 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		ctx.vkCmdBlitImage_(ctx.uploadCmd,
			outTex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			outTex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit,
			VK_FILTER_LINEAR);

		mipW = nextW;
		mipH = nextH;
	}

	// Final transitions to SHADER_READ_ONLY_OPTIMAL.
	// Levels [0..mipLevels-2] are currently in TRANSFER_SRC_OPTIMAL.
	// Level [mipLevels-1] is in TRANSFER_DST_OPTIMAL (or still in TRANSFER_DST
	// if mipLevels == 1, since we skipped the blit loop entirely).
	{
		VkImageMemoryBarrier2 bfinal[2]{};
		uint32_t barrierCount = 0;

		if (mipLevels > 1)
		{
			// Levels [0..mipLevels-2]: SRC -> SHADER_READ
			VkImageMemoryBarrier2& bA = bfinal[barrierCount++];
			bA.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			bA.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			bA.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			bA.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			bA.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			bA.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			bA.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			bA.image = outTex.image;
			bA.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bA.subresourceRange.baseMipLevel = 0;
			bA.subresourceRange.levelCount = mipLevels - 1;
			bA.subresourceRange.baseArrayLayer = 0;
			bA.subresourceRange.layerCount = 1;
		}

		// Last mip: TRANSFER_DST -> SHADER_READ
		VkImageMemoryBarrier2& bB = bfinal[barrierCount++];
		bB.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		bB.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		bB.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		bB.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		bB.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		bB.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		bB.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		bB.image = outTex.image;
		bB.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bB.subresourceRange.baseMipLevel = mipLevels - 1;
		bB.subresourceRange.levelCount = 1;
		bB.subresourceRange.baseArrayLayer = 0;
		bB.subresourceRange.layerCount = 1;

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = barrierCount;
		dep.pImageMemoryBarriers = bfinal;
		ctx.vkCmdPipelineBarrier2_(ctx.uploadCmd, &dep);
	}

	if (!VK_EndUpload(ctx))
		return fail();

	// Destroy staging (and null the handles so a later failure does not
	// double-destroy them in fail()).
	ctx.vkDestroyBuffer_(ctx.device, sbuf, nullptr);
	ctx.vkFreeMemory_(ctx.device, smem, nullptr);
	sbuf = VK_NULL_HANDLE;
	smem = VK_NULL_HANDLE;

	// Image view covers the whole mip chain.
	VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	iv.image = outTex.image;
	iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
	iv.format = outTex.format;
	iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	iv.subresourceRange.baseMipLevel = 0;
	iv.subresourceRange.levelCount = mipLevels;
	iv.subresourceRange.baseArrayLayer = 0;
	iv.subresourceRange.layerCount = 1;

	if (ctx.vkCreateImageView_(ctx.device, &iv, nullptr, &outTex.view) != VK_SUCCESS)
		return fail();

	// Sampler. Caller chooses NEAREST vs LINEAR via nearestFilter. The
	// mipmap mode follows the same choice for consistency at minified
	// levels (only meaningful when mipLevels > 1).
	const VkFilter            kFilter  = nearestFilter ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	const VkSamplerMipmapMode kMipMode = nearestFilter ? VK_SAMPLER_MIPMAP_MODE_NEAREST
	                                                   : VK_SAMPLER_MIPMAP_MODE_LINEAR;
	VkSamplerCreateInfo sp{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sp.magFilter = kFilter;
	sp.minFilter = kFilter;
	sp.mipmapMode = kMipMode;
	sp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sp.maxAnisotropy = 1.0f;
	sp.minLod = 0.0f;
	sp.maxLod = (float)mipLevels;

	if (ctx.vkCreateSampler_(ctx.device, &sp, nullptr, &outTex.sampler) != VK_SUCCESS)
		return fail();

	return true;
}


bool VK_CreateTextureRGBA8_SRGB_FromPixels(VkContext& ctx,
	const void* rgba8Pixels, uint32_t width, uint32_t height,
	VkTexture& outTex, bool generateMips, bool nearestFilter)
{
	return VK_BuildRGBA8Texture(ctx, rgba8Pixels, width, height,
		VK_FORMAT_R8G8B8A8_SRGB, generateMips, nearestFilter, outTex);
}

bool VK_CreateTextureRGBA8_UNORM_FromPixels(VkContext& ctx,
	const void* rgba8Pixels, uint32_t width, uint32_t height,
	VkTexture& outTex, bool generateMips, bool nearestFilter)
{
	return VK_BuildRGBA8Texture(ctx, rgba8Pixels, width, height,
		VK_FORMAT_R8G8B8A8_UNORM, generateMips, nearestFilter, outTex);
}

void VK_DestroyTexture(VkContext& ctx, VkTexture& tex)
{
	if (!ctx.device)
	{
		tex = VkTexture{};
		return;
	}

	// A texture may still be referenced by command buffers from in-flight
	// frames. Draining the device before destroying makes replace-at-runtime
	// safe; at load/shutdown time the device is already idle so the wait is
	// effectively free. NOTE: this does not protect a texture that was
	// already recorded into the CURRENT frame's still-open command buffer --
	// do not destroy those mid-frame.
	if (tex.sampler || tex.view || tex.image || tex.memory)
		ctx.vkDeviceWaitIdle_(ctx.device);

	if (tex.sampler) { ctx.vkDestroySampler_(ctx.device, tex.sampler, nullptr); tex.sampler = VK_NULL_HANDLE; }
	if (tex.view) { ctx.vkDestroyImageView_(ctx.device, tex.view, nullptr); tex.view = VK_NULL_HANDLE; }
	if (tex.image) { ctx.vkDestroyImage_(ctx.device, tex.image, nullptr); tex.image = VK_NULL_HANDLE; }
	if (tex.memory) { ctx.vkFreeMemory_(ctx.device, tex.memory, nullptr); tex.memory = VK_NULL_HANDLE; }

	tex.format = VK_FORMAT_UNDEFINED;
	tex.width = 0;
	tex.height = 0;
	tex.mipLevels = 1;
}

// VK_BindFullscreenTexture -- EXCLUDED from this port (ScreenQuadVK-coupled;
// the fullscreen descriptor set it writes is never created here). Plans 3-6
// re-import it with the compositor subsystem.

//I think this should be moved to sys_vk_helpers.cpp but leaving it here for now to match the request.
// -----------------------------------------------------------------------------
// CmdSwapchainBarrier
// Transitions the swapchain image between PRESENT and COLOR_ATTACHMENT.
// -----------------------------------------------------------------------------
static void CmdSwapchainBarrier(VkContext& ctx,
	VkCommandBuffer cmd,
	VkImage image,
	VkImageLayout oldLayout,
	VkImageLayout newLayout)
{
	VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };

	// Pick stage/access masks based on transition direction.
	//
	// Acquire (PRESENT_SRC -> COLOR_ATTACHMENT_OPTIMAL):
	//   - Wait for the previous use to be done. After acquire, the swapchain
	//     image was last touched by the present engine. Sync2 says the
	//     correct src is the same imageAvailable semaphore wait we already
	//     do at submit time, so the barrier itself only needs to make the
	//     subsequent color-attachment write visible.
	//   - dst: COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE.
	//
	// Release (COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC):
	//   - Make our color writes visible to the present engine.
	//   - src: COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE.
	//   - dst: NONE -- the present queue's read is synchronized via the
	//     renderFinished semaphore, not via this barrier. Using
	//     ALL_COMMANDS + MEMORY_READ here is a heavyweight catch-all that
	//     happens to work most of the time but on some drivers (especially
	//     under tight sync with kFramesInFlight=2) ends up corrupting GPU
	//     state.
	//
	// The previous code used the same ALL_COMMANDS / COLOR_ATTACHMENT_OUTPUT
	// pair for both directions, which is wrong for the release direction
	// and is the most plausible fit for the validation-disappears,
	// release-only DEVICE_LOST symptom.

	const bool acquire =
		(oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ||
			oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) &&
		(newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	const bool release =
		(oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) &&
		(newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	if (acquire)
	{
		b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		b.srcAccessMask = 0;
		b.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	}
	else if (release)
	{
		b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		b.dstAccessMask = 0;
	}
	else
	{
		// Catch-all path for any other transition (e.g. one-off layout
		// initialization). Heavier than necessary but correct.
		b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		b.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	}

	b.oldLayout = oldLayout;
	b.newLayout = newLayout;

	b.image = image;
	b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	b.subresourceRange.baseMipLevel = 0;
	b.subresourceRange.levelCount = 1;
	b.subresourceRange.baseArrayLayer = 0;
	b.subresourceRange.layerCount = 1;

	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &b;

	ctx.vkCmdPipelineBarrier2_(cmd, &dep);
}

// -----------------------------------------------------------------------------
// VK_RecordFullscreenPass / VK_RecordRectPass / the legacy self-contained
// fullscreen record -- EXCLUDED from this port. All three take a ScreenQuadVK&,
// which AAE does not carry yet. Plans 3-6 re-import them with their
// subsystems.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// VK_CreatePipelineCache
// Loads a previously saved pipeline cache from disk, or creates an empty one.
// Call once after device creation in VK_Init.
// -----------------------------------------------------------------------------
bool VK_CreatePipelineCache(VkContext& ctx, const char* cacheFilePath)
{
	std::vector<uint8_t> cacheData;

	// Try to load existing cache from disk
	FILE* f = nullptr;
#ifdef _WIN32
	fopen_s(&f, cacheFilePath, "rb");
#else
	f = fopen(cacheFilePath, "rb");
#endif
	if (f)
	{
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		fseek(f, 0, SEEK_SET);

		if (size > 0)
		{
			cacheData.resize((size_t)size);
			size_t bytesRead = fread(cacheData.data(), 1, (size_t)size, f);
			if (bytesRead != (size_t)size)
			{
				LOG_INFO("VK_CreatePipelineCache: partial read, starting fresh");
				cacheData.clear();
			}
			else
			{
				LOG_INFO("VK_CreatePipelineCache: loaded %ld bytes from '%s'",
					size, cacheFilePath);
			}
		}
		fclose(f);
	}
	else
	{
		LOG_INFO("VK_CreatePipelineCache: no existing cache at '%s', starting fresh",
			cacheFilePath);
	}

	VkPipelineCacheCreateInfo ci{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	ci.initialDataSize = cacheData.size();
	ci.pInitialData = cacheData.empty() ? nullptr : cacheData.data();

	VkResult r = ctx.vkCreatePipelineCache_(ctx.device, &ci, nullptr, &ctx.pipelineCache);
	if (r != VK_SUCCESS)
	{
		LOG_ERROR("VK_CreatePipelineCache: vkCreatePipelineCache failed (%d)", (int)r);
		ctx.pipelineCache = VK_NULL_HANDLE;
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// VK_SavePipelineCache
// Writes the current pipeline cache blob to disk.
// Call once during shutdown before destroying the device.
// -----------------------------------------------------------------------------
bool VK_SavePipelineCache(VkContext& ctx, const char* cacheFilePath)
{
	if (!ctx.pipelineCache)
		return false;

	size_t dataSize = 0;
	VkResult r = ctx.vkGetPipelineCacheData_(ctx.device, ctx.pipelineCache,
		&dataSize, nullptr);
	if (r != VK_SUCCESS || dataSize == 0)
	{
		LOG_ERROR("VK_SavePipelineCache: failed to query cache size (%d)", (int)r);
		return false;
	}

	std::vector<uint8_t> data(dataSize);
	r = ctx.vkGetPipelineCacheData_(ctx.device, ctx.pipelineCache,
		&dataSize, data.data());
	if (r != VK_SUCCESS)
	{
		LOG_ERROR("VK_SavePipelineCache: failed to retrieve cache data (%d)", (int)r);
		return false;
	}

	FILE* f = nullptr;
#ifdef _WIN32
	fopen_s(&f, cacheFilePath, "wb");
#else
	f = fopen(cacheFilePath, "wb");
#endif
	if (!f)
	{
		LOG_ERROR("VK_SavePipelineCache: cannot open '%s' for writing", cacheFilePath);
		return false;
	}

	fwrite(data.data(), 1, dataSize, f);
	fclose(f);

	LOG_INFO("VK_SavePipelineCache: saved %zu bytes to '%s'", dataSize, cacheFilePath);
	return true;
}

// -----------------------------------------------------------------------------
// VK_DestroyPipelineCache
// Cleans up the cache object. Call after saving but before device destruction.
// -----------------------------------------------------------------------------
void VK_DestroyPipelineCache(VkContext& ctx)
{
	if (ctx.pipelineCache)
	{
		ctx.vkDestroyPipelineCache_(ctx.device, ctx.pipelineCache, nullptr);
		ctx.pipelineCache = VK_NULL_HANDLE;
	}
}

// -----------------------------------------------------------------------------
// VK_RecreateSyncObjects
//
// Destroys and recreates the per-frame imageAvailable semaphores and the
// inFlight fences. Required after vkQueuePresentKHR returns
// VK_ERROR_OUT_OF_DATE_KHR -- the wait semaphore for that present is in an
// undefined state, and reusing it on the next vkQueueSubmit2 (signaling an
// already-signaled binary semaphore) is UB. On NVIDIA this surfaces as
// VK_ERROR_DEVICE_LOST on the very next submit.
//
// renderFinished semaphores are NOT handled here: they are per swapchain
// image and owned by CreateSwapchain/DestroySwapchain, so the swapchain
// recreate that pairs with this call already replaced them.
//
// Drains the device first so the recreate is safe regardless of in-flight
// work. After this call, all per-frame sync state is fresh:
//   - imageAvailable[i] : unsignaled (default for new binary semaphore)
//   - inFlight[i]       : signaled (as if a previous frame had completed)
//
// Pair with VK_RecreateSwapchain in OUT_OF_DATE recovery flows.
// -----------------------------------------------------------------------------
bool VK_RecreateSyncObjects(VkContext& ctx)
{
	if (!ctx.device)
		return false;

	ctx.vkDeviceWaitIdle_(ctx.device);

	for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
	{
		if (ctx.imageAvailable[i])
		{
			ctx.vkDestroySemaphore_(ctx.device, ctx.imageAvailable[i], nullptr);
			ctx.imageAvailable[i] = VK_NULL_HANDLE;
		}
		if (ctx.inFlight[i])
		{
			ctx.vkDestroyFence_(ctx.device, ctx.inFlight[i], nullptr);
			ctx.inFlight[i] = VK_NULL_HANDLE;
		}
	}

	VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkFenceCreateInfo     fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (uint32_t i = 0; i < VkContext::kFramesInFlight; ++i)
	{
		if (ctx.vkCreateSemaphore_(ctx.device, &si, nullptr, &ctx.imageAvailable[i]) != VK_SUCCESS)
		{
			LOG_ERROR("VK_RecreateSyncObjects: vkCreateSemaphore (imageAvailable) failed");
			return false;
		}
		if (ctx.vkCreateFence_(ctx.device, &fi, nullptr, &ctx.inFlight[i]) != VK_SUCCESS)
		{
			LOG_ERROR("VK_RecreateSyncObjects: vkCreateFence failed");
			return false;
		}
	}

	// Reset the frame index -- the slot we were on may be ahead of where the
	// fresh fences expect us to be. Starting from slot 0 with all fences
	// signaled is the same state as right after VK_Init.
	ctx.frameIndex = 0;

	LOG_INFO("VK_RecreateSyncObjects: recreated %u frames-in-flight",
		VkContext::kFramesInFlight);
	return true;
}
