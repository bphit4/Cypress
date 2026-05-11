#pragma once
#include <fb/Engine/ClientGameContext.h>
#include <fb/Engine/GameView.h>

#define OFFSET_SETACTIVECAMERA CYPRESS_GW_SELECT(0x14059E940, 0x1404DCD80, 0x140F97310)
#define OFFSET_ADDINPUT CYPRESS_GW_SELECT(0x140561E10, 0x140412390, 0x1411F8140)
#define OFFSET_REMOVEINPUT CYPRESS_GW_SELECT(0x140566680, 0x1404124C0, 0x1411FF4A0)

namespace Cypress
{
	inline bool ToggleFreeCam()
	{
		fb::ClientGameContext* ctx = fb::ClientGameContext::GetInstance();
		if (!ctx || !ctx->m_clientGameView || !ctx->m_clientGameView->m_freeCamera)
			return false;

		fb::ClientGameView* view = ctx->m_clientGameView;
		fb::FreeCamera* cam = view->m_freeCamera;
		if (!cam->m_input)
			return false;

		bool isActive = (view->m_activeCameraId == fb::FreeCameraId);

		using tSetActiveCamera = void (*)(fb::ClientGameView*, fb::CameraIds);
		auto setActiveCamera = reinterpret_cast<tSetActiveCamera>(OFFSET_SETACTIVECAMERA);

		if (isActive)
		{
			// Deactivate
			setActiveCamera(view, fb::EntryCameraId);

			using tRemoveInput = void (*)(void*, unsigned int localPlayerId);
			auto removeInput = reinterpret_cast<tRemoveInput>(OFFSET_REMOVEINPUT);
			removeInput(cam->m_input, 0);
		}
		else
		{
			// Activate
			setActiveCamera(view, fb::FreeCameraId);

			using tAddInput = void (*)(void*, int priority, unsigned int localPlayerId);
			auto addInput = reinterpret_cast<tAddInput>(OFFSET_ADDINPUT);
			addInput(cam->m_input, 32, 0);
		}

		return !isActive; // returns new state: true = now in freecam
	}

	inline bool IsFreeCamActive()
	{
		fb::ClientGameContext* ctx = fb::ClientGameContext::GetInstance();
		if (!ctx || !ctx->m_clientGameView)
			return false;
		return ctx->m_clientGameView->m_activeCameraId == fb::FreeCameraId;
	}
} // namespace Cypress
