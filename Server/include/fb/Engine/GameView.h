#pragma once
#include <Cypress/Core/Config.h>

namespace fb
{
    struct FreeCamera
    {
        void** vftptr;
        void* unk1;
        char m_cTransform[0x40]; // LinearTransform
        char m_transform[0x40];	 // LinearTransform
        char pad[CYPRESS_GW_SELECT(0x80, 0xC0, 0x128)];
        void* m_input;
    };

    enum CameraIds : int
    {
        NoCameraId,
        FreeCameraId,
        EntryCameraId,
        CameraIdCount
    };

    struct ClientGameView
    {
        void* vftptr;
        CameraIds m_activeCameraId;
        char pad[CYPRESS_GW_SELECT(0xBC, 0xB8, 0xAC)];
        FreeCamera* m_freeCamera;
    };
}
