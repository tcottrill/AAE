#pragma once
// ===========================================================================
// snapshot_vk.h - F12 screenshot readback for the Vulkan chain.
//
// snapshot() (renderer_dispatch.cpp) routes to VkSnapshot::Request() under
// RENDERER_VULKAN. That call happens from the emulator's input handling
// MID-TICK - the frame's dynamic-rendering pass is open, or no frame is open
// at all - so nothing is read there; the request is only latched.
//
// vkchain_swap_buffers() services it at the frame boundary:
//
//   BeginCapture(ctx, cmd, imageIndex, cap)   // records the readback into
//                                             // THIS frame's command buffer,
//                                             // just before VK_EndFrame
//   VK_EndFrame(...)                          // submit + present
//   FinishCapture(ctx, cap, submitted)        // wait, map, convert, write PNG
//
// Recording into the frame's own command buffer (rather than a separate
// one-shot submit after present) is what makes the captured pixels provably
// the frame the user saw: the copy sits between the last draw and the
// COLOR->PRESENT barrier in one ordered stream, so no second acquire, no
// "which image is on screen now" guesswork, and no dependence on the
// presentation engine leaving the image readable after present.
//
// The pixels land in a host-visible staging buffer that lives on the caller's
// stack (Capture) for exactly one call pair - no cross-frame ownership, and
// nothing for vkchain_shutdown to clean up.
// ===========================================================================

#include <vulkan/vulkan.h>
#include <stdint.h>

struct VkContext;

namespace VkSnapshot
{
	// One in-flight readback. Owned by the caller's stack frame; BeginCapture
	// fills it, FinishCapture consumes and frees it.
	struct Capture
	{
		VkBuffer       buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		uint32_t       width = 0;
		uint32_t       height = 0;
		bool           bgraSwizzle = false;  // swapchain is B8G8R8A8_*
		bool           active = false;
	};

	// Latch an F12 request (one-shot; repeated presses before the next frame
	// boundary collapse into a single screenshot). Logs and drops the request
	// when the chain cannot service it at all.
	void Request(void);

	// Discard a latched request that can no longer be serviced (swapchain
	// deferred / minimized). Silent when nothing is pending.
	void DropPending(const char* why);

	// Records the swapchain->buffer readback into cmd. Returns false (and
	// leaves cap inactive) when no request is pending or the capture cannot
	// be done; in that case the caller does nothing else.
	// MUST be called with the frame pass OPEN and before VK_EndFrame.
	bool BeginCapture(VkContext& ctx, VkCommandBuffer cmd, uint32_t imageIndex, Capture& cap);

	// Waits for the frame to finish, converts to top-row-first RGBA8 and hands
	// it to the shared writer. frameSubmitted must be VK_EndFrame's return
	// value: false means the command buffer never reached the GPU, so the
	// staging buffer holds garbage and the shot is dropped. Always frees cap.
	void FinishCapture(VkContext& ctx, Capture& cap, bool frameSubmitted);
}
