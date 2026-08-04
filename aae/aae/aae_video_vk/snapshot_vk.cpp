//==========================================================================
// AAE - Another Arcade Emulator
// Copyright (C) 2026 Tim Cottrill - GNU GPL v3 or later.
//==========================================================================
// ===========================================================================
// snapshot_vk.cpp - F12 screenshot readback for the Vulkan chain.
//
// See snapshot_vk.h for the request/record/resolve contract. Three details
// are worth stating up front because they are the easy things to get wrong:
//
// ROW ORDER - no flip. glReadPixels' window origin is BOTTOM-left, which is
//   why the GL path flips its buffer before writing the PNG. Vulkan is the
//   other way round: framebuffer coordinates start at the TOP-left (the
//   engine's y-down viewports and ortho matrices in VK_BeginFrame assume
//   exactly that), so image row 0 is the top row of what was presented.
//   vkCmdCopyImageToBuffer with bufferRowLength = 0 packs row y at byte
//   offset y * width * 4, so buffer scanline 0 == image row 0 == TOP row,
//   which is precisely what PNG scanline 0 must be. Flipping here would
//   produce upside-down screenshots.
//
// SWIZZLE - the swapchain is B8G8R8A8_UNORM on essentially every desktop
//   driver (sys_vk's CreateSwapchain prefers it), while PNG wants R,G,B,A
//   byte order, so R and B are swapped on the CPU. The branch is on
//   ctx.swapchainFormat, never hardcoded; the *_SRGB formats are accepted
//   too (only sys_vk's never-seen-on-desktop fallback produces them) because
//   the BYTE LAYOUT is what matters here - no color conversion is applied in
//   either case, so the PNG holds the presented bytes verbatim.
//
// ALPHA - forced to 255. The composited swapchain alpha is not meaningful:
//   several passes (beam glow, trails, UI dim) blend additively and leave
//   whatever alpha they happened to write, so honoring it would make the PNG
//   randomly transparent in viewers. The RGB is untouched.
// ===========================================================================
#include "snapshot_vk.h"

#include "sys_vk.h"
#include "sys_log.h"

#include <vector>

// Shared PNG writer (fileio/texture_handler.cpp): snap/ creation, the
// timestamped filename, stbi_write_png. Forward-declared rather than pulling
// texture_handler.h (and the driver headers behind it) into a Vulkan TU -
// same style as renderer_dispatch.cpp's allegro_message declaration.
bool snapshot_write_rgba8_png(const unsigned char* rgba, int width, int height);

namespace
{
	// A single latched F12 press. Cleared as soon as a capture is recorded or
	// dropped, so a press never fires twice.
	bool s_requested = false;

	uint32_t FindMemoryTypeIdx_(VkContext& ctx, uint32_t typeBits, VkMemoryPropertyFlags want)
	{
		VkPhysicalDeviceMemoryProperties mp{};
		ctx.vkGetPhysicalDeviceMemoryProperties_(ctx.phys, &mp);
		for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
			if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
				return i;
		return 0xFFFFFFFFu;
	}

	void DestroyCapture_(VkContext& ctx, VkSnapshot::Capture& cap)
	{
		if (cap.buffer) ctx.vkDestroyBuffer_(ctx.device, cap.buffer, nullptr);
		if (cap.memory) ctx.vkFreeMemory_(ctx.device, cap.memory, nullptr);
		cap = VkSnapshot::Capture{};
	}
}

namespace VkSnapshot
{

void Request(void)
{
	s_requested = true;
}

void DropPending(const char* why)
{
	if (!s_requested)
		return;
	s_requested = false;
	LOG_ERROR("snapshot (vulkan): dropped - %s", why ? why : "unavailable");
}

bool BeginCapture(VkContext& ctx, VkCommandBuffer cmd, uint32_t imageIndex, Capture& cap)
{
	cap = Capture{};

	if (!s_requested)
		return false;
	s_requested = false;          // one-shot, whatever happens below

	if (!ctx.swapchainTransferSrc)
	{
		LOG_ERROR("snapshot (vulkan): swapchain images lack TRANSFER_SRC usage "
			"(surface does not support it); cannot read back");
		return false;
	}
	if (!ctx.vkCmdCopyImageToBuffer_)
	{
		LOG_ERROR("snapshot (vulkan): vkCmdCopyImageToBuffer not loaded; cannot read back");
		return false;
	}
	if (cmd == VK_NULL_HANDLE)
	{
		LOG_ERROR("snapshot (vulkan): no command buffer");
		return false;
	}

	// The swapchain extent (not the window client size) is what was actually
	// rendered and presented. The two can disagree transiently during a
	// resize, and a mismatch here would mean copying a region the image does
	// not have.
	const uint32_t w = ctx.swapchainExtent.width;
	const uint32_t h = ctx.swapchainExtent.height;
	if (w == 0 || h == 0)
	{
		LOG_ERROR("snapshot (vulkan): swapchain extent is zero");
		return false;
	}
	if (imageIndex >= ctx.swapchainImages.size())
	{
		LOG_ERROR("snapshot (vulkan): imageIndex %u out of range (size=%zu)",
			imageIndex, ctx.swapchainImages.size());
		return false;
	}

	// Byte layout only - see the ALPHA/SWIZZLE note at the top of the file.
	bool bgra;
	switch (ctx.swapchainFormat)
	{
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB: bgra = true;  break;
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB: bgra = false; break;
	default:
		LOG_ERROR("snapshot (vulkan): unsupported swapchain format %d",
			(int)ctx.swapchainFormat);
		return false;
	}

	const VkDeviceSize bytes = (VkDeviceSize)w * (VkDeviceSize)h * 4u;

	VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bci.size = bytes;
	bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (ctx.vkCreateBuffer_(ctx.device, &bci, nullptr, &cap.buffer) != VK_SUCCESS)
	{
		LOG_ERROR("snapshot (vulkan): vkCreateBuffer failed (%llu bytes)",
			(unsigned long long)bytes);
		DestroyCapture_(ctx, cap);
		return false;
	}

	VkMemoryRequirements mr{};
	ctx.vkGetBufferMemoryRequirements_(ctx.device, cap.buffer, &mr);

	const uint32_t mt = FindMemoryTypeIdx_(ctx, mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (mt == 0xFFFFFFFFu)
	{
		LOG_ERROR("snapshot (vulkan): no host-visible coherent memory type");
		DestroyCapture_(ctx, cap);
		return false;
	}

	VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	ai.allocationSize = mr.size;
	ai.memoryTypeIndex = mt;
	if (ctx.vkAllocateMemory_(ctx.device, &ai, nullptr, &cap.memory) != VK_SUCCESS ||
		ctx.vkBindBufferMemory_(ctx.device, cap.buffer, cap.memory, 0) != VK_SUCCESS)
	{
		LOG_ERROR("snapshot (vulkan): staging allocation/bind failed");
		DestroyCapture_(ctx, cap);
		return false;
	}

	VkImage scImg = ctx.swapchainImages[imageIndex];

	// -----------------------------------------------------------------------
	// Record: suspend the frame pass, round-trip the swapchain image through
	// TRANSFER_SRC_OPTIMAL for the copy, put it back, resume.
	//
	// The suspend/resume pair is mandatory - vkCmdCopyImageToBuffer is a
	// transfer command and is illegal inside a dynamic-rendering pass
	// instance. It is also exactly the escape hatch sys_vk documents for
	// offscreen work (RenderTargetVK, GenerateMips) and costs nothing here:
	// the resumed pass uses LOAD_OP_LOAD, so the frame's pixels survive, and
	// no draws follow before VK_EndFrame closes it.
	//
	// The image is returned to COLOR_ATTACHMENT_OPTIMAL before resuming, which
	// is the layout VK_BeginFrame left it in and the one VK_ResumeFramePass'
	// attachment info and VK_EndFrame's COLOR->PRESENT barrier both require.
	// COLOR -> TRANSFER_SRC -> COLOR preserves contents (only transitions FROM
	// UNDEFINED may discard), so the presented frame is unaffected: this is
	// invisible to the user apart from the one-frame hitch below.
	// -----------------------------------------------------------------------
	VK_SuspendFramePass(ctx, cmd);

	{
		VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		b.image = scImg;
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

	// bufferRowLength / bufferImageHeight = 0 means "tightly packed to
	// imageExtent", i.e. row stride == w * 4 with no padding, which is what
	// the shared writer expects.
	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { w, h, 1 };

	ctx.vkCmdCopyImageToBuffer_(cmd, scImg,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cap.buffer, 1, &region);

	{
		// Image back to COLOR_ATTACHMENT_OPTIMAL, and - in the same dependency
		// - make the copy's buffer writes visible to the host. The HOST_READ
		// barrier is required even with HOST_COHERENT memory: coherency covers
		// cache flushing, not the availability/visibility dependency, and a
		// fence/idle wait alone does not supply it.
		VkImageMemoryBarrier2 bi{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		bi.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		bi.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		bi.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		bi.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		bi.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		bi.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		bi.image = scImg;
		bi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bi.subresourceRange.baseMipLevel = 0;
		bi.subresourceRange.levelCount = 1;
		bi.subresourceRange.baseArrayLayer = 0;
		bi.subresourceRange.layerCount = 1;

		VkBufferMemoryBarrier2 bb{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
		bb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		bb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		bb.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
		bb.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
		bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bb.buffer = cap.buffer;
		bb.offset = 0;
		bb.size = VK_WHOLE_SIZE;

		VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &bi;
		dep.bufferMemoryBarrierCount = 1;
		dep.pBufferMemoryBarriers = &bb;
		ctx.vkCmdPipelineBarrier2_(cmd, &dep);
	}

	VK_ResumeFramePass(ctx, cmd, imageIndex);

	cap.width = w;
	cap.height = h;
	cap.bgraSwizzle = bgra;
	cap.active = true;
	return true;
}

void FinishCapture(VkContext& ctx, Capture& cap, bool frameSubmitted)
{
	if (!cap.active)
	{
		DestroyCapture_(ctx, cap);
		return;
	}

	// One-shot user keypress: a full device drain is the obviously-correct
	// wait. It costs a single frame of hitch on F12 and nothing at all on
	// every other frame, and it removes any question about which fence slot
	// owns the submit VK_EndFrame just made (it has already advanced
	// ctx.frameIndex by the time we get here).
	if (ctx.vkDeviceWaitIdle_)
		ctx.vkDeviceWaitIdle_(ctx.device);

	if (!frameSubmitted)
	{
		// VK_EndFrame bailed before/at submit (swapchain out of date, command
		// buffer error). The staging buffer was never written, so there is
		// nothing to save.
		LOG_ERROR("snapshot (vulkan): dropped - frame submit/present failed");
		DestroyCapture_(ctx, cap);
		return;
	}

	void* mapped = nullptr;
	if (ctx.vkMapMemory_(ctx.device, cap.memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS || !mapped)
	{
		LOG_ERROR("snapshot (vulkan): vkMapMemory failed");
		DestroyCapture_(ctx, cap);
		return;
	}

	const size_t texels = (size_t)cap.width * (size_t)cap.height;
	std::vector<unsigned char> rgba(texels * 4);
	const unsigned char* src = static_cast<const unsigned char*>(mapped);
	unsigned char* dst = rgba.data();

	// Straight linear walk: the copy is tightly packed and top-row-first, so
	// scanline order needs no reordering at all (see the ROW ORDER note).
	if (cap.bgraSwizzle)
	{
		for (size_t i = 0; i < texels; ++i)
		{
			dst[i * 4 + 0] = src[i * 4 + 2];
			dst[i * 4 + 1] = src[i * 4 + 1];
			dst[i * 4 + 2] = src[i * 4 + 0];
			dst[i * 4 + 3] = 255;
		}
	}
	else
	{
		for (size_t i = 0; i < texels; ++i)
		{
			dst[i * 4 + 0] = src[i * 4 + 0];
			dst[i * 4 + 1] = src[i * 4 + 1];
			dst[i * 4 + 2] = src[i * 4 + 2];
			dst[i * 4 + 3] = 255;
		}
	}

	const int outW = (int)cap.width;
	const int outH = (int)cap.height;

	ctx.vkUnmapMemory_(ctx.device, cap.memory);
	DestroyCapture_(ctx, cap);   // zeroes cap, hence the locals above

	snapshot_write_rgba8_png(rgba.data(), outW, outH);
}

}   // namespace VkSnapshot
