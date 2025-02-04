/************************************************************************************
Filename    :   main.cpp
Content     :   First-person view test application for custom VR project
Created     :   10/28/2015

Copyright   :   Copyright (c) Facebook Technologies, LLC and its affiliates. All rights reserved.

Copyright   :   Copyright (c) Xavier Knoll 2024 All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*************************************************************************************/
/// This is a customized VR application sample for demonstration purposes.
/// Use WASD keys to move around, and cursor keys for interaction.
/// It utilizes DirectX12 Raytracing for rendering.


#define win32_lean_and_mean
#include <Windows.h>
// Include the Oculus SDK
#include "OVR_CAPI_D3D.h"
#include "Win32_d3dx12.h"
#include "Win32_DirectX12AppUtil.h"


//------------------------------------------------------------
// ovrSwapTextureSet wrapper class that also maintains the render target views
// needed for D3D12 rendering.
struct OculusEyeTexture
{
    ovrSession               Session;
    ovrTextureSwapChain      TextureChain;
    ovrTextureSwapChain      DepthTextureChain;

    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> TexRtv;
    std::vector<ID3D12Resource*> TexResource;

    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> DepthTexDsv;
    std::vector<ID3D12Resource*>             DepthTex;

    OculusEyeTexture() :
        Session(nullptr),
        TextureChain(nullptr),
        DepthTextureChain(nullptr)
    {
    }

    bool Init(ovrSession session, int sizeW, int sizeH, bool createDepth)
    {
        Session = session;

        ovrTextureSwapChainDesc desc{};
        desc.Type = ovrTexture_2D;
        desc.ArraySize = 1;
        desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.Width = sizeW;
        desc.Height = sizeH;
        desc.MipLevels = 1;
        desc.SampleCount = DIRECTX.EyeMsaaRate;
        desc.MiscFlags = ovrTextureMisc_DX_Typeless | ovrTextureMisc_AutoGenerateMips;
        desc.StaticImage = ovrFalse;
        desc.BindFlags = ovrTextureBind_DX_RenderTarget;

        ovrResult result = ovr_CreateTextureSwapChainDX(session, DIRECTX.CommandQueue, &desc, &TextureChain);
        if (!OVR_SUCCESS(result))
            return false;

        int textureCount = 0;
        ovr_GetTextureSwapChainLength(Session, TextureChain, &textureCount);
        TexRtv.resize(textureCount);
        TexResource.resize(textureCount);
        for (int i = 0; i < textureCount; ++i)
        {
            result = ovr_GetTextureSwapChainBufferDX(Session, TextureChain, i, IID_PPV_ARGS(&TexResource[i]));
            if (!OVR_SUCCESS(result))
                return false;
            TexResource[i]->SetName(L"EyeColorRes");

            D3D12_RENDER_TARGET_VIEW_DESC rtvd{};
            rtvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvd.ViewDimension = (DIRECTX.EyeMsaaRate > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS
                : D3D12_RTV_DIMENSION_TEXTURE2D;
            TexRtv[i] = DIRECTX.RtvHandleProvider.AllocCpuHandle();
            DIRECTX.Device->CreateRenderTargetView(TexResource[i], &rtvd, TexRtv[i]);
        }

        if (createDepth)
        {
            ovrTextureSwapChainDesc depthDesc{};
            depthDesc.Type = ovrTexture_2D;
            depthDesc.ArraySize = 1;
            switch (DIRECTX.DepthFormat)
            {
            case DXGI_FORMAT_D16_UNORM:             depthDesc.Format = OVR_FORMAT_D16_UNORM;            break;
            case DXGI_FORMAT_D24_UNORM_S8_UINT:     depthDesc.Format = OVR_FORMAT_D24_UNORM_S8_UINT;    break;
            case DXGI_FORMAT_D32_FLOAT:             depthDesc.Format = OVR_FORMAT_D32_FLOAT;            break;
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  depthDesc.Format = OVR_FORMAT_D32_FLOAT_S8X24_UINT; break;
            default: FATALERROR("Unknown depth format"); break;
            }
            depthDesc.Width = sizeW;
            depthDesc.Height = sizeH;
            depthDesc.MipLevels = 1;
            depthDesc.SampleCount = DIRECTX.EyeMsaaRate;
            depthDesc.MiscFlags = ovrTextureMisc_DX_Typeless;
            depthDesc.StaticImage = ovrFalse;
            depthDesc.BindFlags = ovrTextureBind_DX_DepthStencil;

            result = ovr_CreateTextureSwapChainDX(session, DIRECTX.CommandQueue, &depthDesc, &DepthTextureChain);
            if (!OVR_SUCCESS(result))
                return false;

            DepthTex.resize(textureCount);
            DepthTexDsv.resize(textureCount);
            for (int i = 0; i < textureCount; i++)
            {
                result = ovr_GetTextureSwapChainBufferDX(Session, DepthTextureChain, i, IID_PPV_ARGS(&DepthTex[i]));
                if (!OVR_SUCCESS(result))
                    return false;
                DepthTex[i]->SetName(L"EyeDepthRes");

                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format = DIRECTX.DepthFormat;
                dsvDesc.ViewDimension = DIRECTX.EyeMsaaRate > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                    : D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2D.MipSlice = 0;

                DepthTexDsv[i] = DIRECTX.DsvHandleProvider.AllocCpuHandle();
                DIRECTX.Device->CreateDepthStencilView(DepthTex[i], &dsvDesc, DepthTexDsv[i]);
            }
        }

        return true;
    }

    ~OculusEyeTexture()
    {
        if (TextureChain)
        {
            for (auto& rtv : TexRtv)
            {
                DIRECTX.RtvHandleProvider.FreeCpuHandle(CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv));
            }

            for (size_t i = 0; i < TexResource.size(); ++i)
            {
                Release(TexResource[i]);
            }

            ovr_DestroyTextureSwapChain(Session, TextureChain);
        }

        if (DepthTextureChain)
        {
            for (auto& dsv : DepthTexDsv)
            {
                DIRECTX.DsvHandleProvider.FreeCpuHandle(CD3DX12_CPU_DESCRIPTOR_HANDLE(dsv));
            }

            for (size_t i = 0; i < DepthTex.size(); i++)
            {
                Release(DepthTex[i]);
            }

            ovr_DestroyTextureSwapChain(Session, DepthTextureChain);
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRtv()
    {
        int index = 0;
        ovr_GetTextureSwapChainCurrentIndex(Session, TextureChain, &index);
        return TexRtv[index];
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDsv()
    {
        int index = 0;
        if (DepthTextureChain)
        {
            ovr_GetTextureSwapChainCurrentIndex(Session, DepthTextureChain, &index);
        }
        else
        {
            ovr_GetTextureSwapChainCurrentIndex(Session, TextureChain, &index);
        }
        return DepthTexDsv[index];
    }

    ID3D12Resource* GetD3DColorResource()
    {
        int index = 0;
        ovr_GetTextureSwapChainCurrentIndex(Session, TextureChain, &index);
        return TexResource[index];
    }

    ID3D12Resource* GetD3DDepthResource()
    {
        if (DepthTex.size() > 0)
        {
            int index = 0;
            ovr_GetTextureSwapChainCurrentIndex(Session, TextureChain, &index);
            return DepthTex[index];
        }
        else
        {
            return nullptr;
        }
    }

    // Commit changes
    void Commit()
    {
        ovr_CommitTextureSwapChain(Session, TextureChain);

        if (DepthTextureChain)
        {
            ovr_CommitTextureSwapChain(Session, DepthTextureChain);
        }
    }
};

//-----------------------------------------------------------
struct SceneSphere : Scene
{
    SceneSphere() : Scene() {}
    SceneSphere(bool includeIntesndiveGPUobject) : Scene(includeIntesndiveGPUobject) {}

    void Init(bool includeIntensiveGPUobject) override
    {
        std::vector<ModelComponent> transforms;
        numInstances = 0;

        //transforms.push_back(ModelComponent(0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0xff404040));
        //models.push_back(Model(transforms, Material(Texture::AUTO_CEILING - 1), &globalVertexBuffer, 0));
        Material sphereMat;
        ModelComponent sphereComp(sphereMat, XMMatrixIdentity(), &aabbVertexBuffer, 0, 1, ~0);
        transforms.push_back(sphereComp);
        models.push_back(Model(transforms, sphereMat));
   


        transforms.clear();
        transforms.push_back(ModelComponent(0.05f, -0.01f, 0.1f, -0.05f, +0.01f, -0.1f, 0xffff0000, &globalVertexBuffer));
        transforms.push_back(ModelComponent(0.05f, -0.01f, 0.1f, -0.05f, +0.01f, -0.1f, 0xffff0000, &globalVertexBuffer));
        models.push_back(Model(transforms, Material(Texture::AUTO_WHITE - 1)));


        transforms.clear();
        transforms.push_back(ModelComponent(10.1f, 0.0f, 20.0f, 10.0f, 4.0f, -20.0f, 0xff808080, &globalVertexBuffer));
        transforms.push_back(ModelComponent(10.0f, -0.1f, 20.1f, -10.0f, 4.0f, 20.0f, 0xff808080, &globalVertexBuffer));
        transforms.push_back(ModelComponent(-10.0f, -0.1f, 20.0f, -10.1f, 4.0f, -20.0f, 0xff808080, &globalVertexBuffer));
        models.push_back(Model(transforms, Material((UINT)Texture::AUTO_WALL - 1)));

        transforms.clear();
        transforms.push_back(ModelComponent(10.0f, -0.1f, 20.0f, -10.0f, 0.0f, -20.1f, 0xff808080, &globalVertexBuffer));
        transforms.push_back(ModelComponent(15.0f, -6.1f, -18.0f, -15.0f, -6.0f, -30.0f, 0xff808080, &globalVertexBuffer));
        models.push_back(Model(transforms, Material(Texture::AUTO_FLOOR - 1)));


        transforms.clear();
        transforms.push_back(ModelComponent(10.0f, 4.0f, 20.0f, -10.0f, 4.1f, -20.1f, 0xff808080, &globalVertexBuffer));
        models.push_back(Model(transforms, Material(Texture::AUTO_CEILING - 1)));

        transforms.clear();
        //TriangleSet furniture;
        transforms.push_back(ModelComponent(-9.5f, 0.75f, -3.0f, -10.1f, 2.5f, -3.1f, 0xff383838, &globalVertexBuffer));    // Right side shelf// Verticals
        transforms.push_back(ModelComponent(-9.5f, 0.95f, -3.7f, -10.1f, 2.75f, -3.8f, 0xff383838, &globalVertexBuffer));   // Right side shelf
        transforms.push_back(ModelComponent(-9.55f, 1.20f, -2.5f, -10.1f, 1.30f, -3.75f, 0xff383838, &globalVertexBuffer)); // Right side shelf// Horizontals
        transforms.push_back(ModelComponent(-9.55f, 2.00f, -3.05f, -10.1f, 2.10f, -4.2f, 0xff383838, &globalVertexBuffer)); // Right side shelf
        transforms.push_back(ModelComponent(-5.0f, 1.1f, -20.0f, -10.0f, 1.2f, -20.1f, 0xff383838, &globalVertexBuffer));   // Right railing
        transforms.push_back(ModelComponent(10.0f, 1.1f, -20.0f, 5.0f, 1.2f, -20.1f, 0xff383838, &globalVertexBuffer));   // Left railing
        for (float f = 5; f <= 9; f += 1)
            transforms.push_back(ModelComponent(-f, 0.0f, -20.0f, -f - 0.1f, 1.1f, -20.1f, 0xff505050, &globalVertexBuffer)); // Left Bars
        for (float f = 5; f <= 9; f += 1)
            transforms.push_back(ModelComponent(f, 1.1f, -20.0f, f + 0.1f, 0.0f, -20.1f, 0xff505050, &globalVertexBuffer)); // Right Bars
        transforms.push_back(ModelComponent(1.8f, 0.8f, -1.0f, 0.0f, 0.7f, 0.0f, 0xff505000, &globalVertexBuffer));  // Table
        transforms.push_back(ModelComponent(1.8f, 0.0f, 0.0f, 1.7f, 0.7f, -0.1f, 0xff505000, &globalVertexBuffer)); // Table Leg
        transforms.push_back(ModelComponent(1.8f, 0.7f, -1.0f, 1.7f, 0.0f, -0.9f, 0xff505000, &globalVertexBuffer)); // Table Leg
        transforms.push_back(ModelComponent(0.0f, 0.0f, -1.0f, 0.1f, 0.7f, -0.9f, 0xff505000, &globalVertexBuffer));  // Table Leg
        transforms.push_back(ModelComponent(0.0f, 0.7f, 0.0f, 0.1f, 0.0f, -0.1f, 0xff505000, &globalVertexBuffer));  // Table Leg
        transforms.push_back(ModelComponent(1.4f, 0.5f, 1.1f, 0.8f, 0.55f, 0.5f, 0xff202050, &globalVertexBuffer));  // Chair Set
        transforms.push_back(ModelComponent(1.401f, 0.0f, 1.101f, 1.339f, 1.0f, 1.039f, 0xff202050, &globalVertexBuffer)); // Chair Leg 1
        transforms.push_back(ModelComponent(1.401f, 0.5f, 0.499f, 1.339f, 0.0f, 0.561f, 0xff202050, &globalVertexBuffer)); // Chair Leg 2
        transforms.push_back(ModelComponent(0.799f, 0.0f, 0.499f, 0.861f, 0.5f, 0.561f, 0xff202050, &globalVertexBuffer)); // Chair Leg 2
        transforms.push_back(ModelComponent(0.799f, 1.0f, 1.101f, 0.861f, 0.0f, 1.039f, 0xff202050, &globalVertexBuffer)); // Chair Leg 2
        transforms.push_back(ModelComponent(1.4f, 0.97f, 1.05f, 0.8f, 0.92f, 1.10f, 0xff202050, &globalVertexBuffer)); // Chair Back high bar
        for (float f = 3.0f; f <= 6.6f; f += 0.4f)
            transforms.push_back(ModelComponent(3, 0.0f, -f, 2.9f, 1.3f, -f - 0.1f, 0xff404040, &globalVertexBuffer)); // Posts

        models.push_back(Model(transforms, Material(Texture::AUTO_WHITE - 1)));
        numInstances = ModelComponent::numInstances;

        globalVertexBuffer.InitGlobalVertexBuffers();
        globalVertexBuffer.InitGlobalBottomLevelAccelerationObject();
        aabbVertexBuffer.InitAABBBottomLevelAccelerationObject();


        BuildAccelerationStructures();
    }
};

// return true to retry later (e.g. after display lost)
static bool MainLoop(bool retryCreate)
{
    // Initialize these to nullptr here to handle device lost failures cleanly
    ovrMirrorTexture            mirrorTexture = nullptr;
    OculusEyeTexture* pEyeRenderTexture[2] = { nullptr, nullptr };
    Scene* scene = nullptr;
    Camera* mainCam = nullptr;
    ovrMirrorTextureDesc        mirrorDesc = {};
    ovrInputState inputState;

    int eyeMsaaRate = 4;
    DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;

    long long frameIndex = 0;
    bool drawMirror = true;

    ovrSession session;
    ovrGraphicsLuid luid;
    ovrResult result = ovr_Create(&session, &luid);
    if (!OVR_SUCCESS(result))
        return retryCreate;

    ovrHmdDesc hmdDesc = ovr_GetHmdDesc(session);

    ovrTrackingOrigin origin = ovrTrackingOrigin_FloorLevel;
    ovr_SetTrackingOriginType(session, origin);

    // Setup Device and Graphics
    // Note: the mirror window can be any size, for this sample we use 1/2 the HMD resolution
    ovrSizei idealSize = ovr_GetFovTextureSize(session, (ovrEyeType)0, hmdDesc.DefaultEyeFov[0], 1.0f);
    if (!DIRECTX.InitDevice(hmdDesc.Resolution.w / 2, hmdDesc.Resolution.h / 2, reinterpret_cast<LUID*>(&luid),
        depthFormat, eyeMsaaRate, true, idealSize.w, idealSize.h))
    {
        goto Done;
    }
    float idp;
    {
        // Get the eye render descriptions
        ovrEyeRenderDesc eyeRenderDesc[2];
        eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
        eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);

        idp = fabsf(eyeRenderDesc[0].HmdToEyePose.Position.x);
    }
    // Make the eye render buffers (caution if actual size < requested due to HW limits).
    ovrRecti eyeRenderViewport[2];

    for (int eye = 0; eye < 2; ++eye)
    {
        ovrSizei idealSize = ovr_GetFovTextureSize(session, (ovrEyeType)eye, hmdDesc.DefaultEyeFov[eye], 1.0f);
        pEyeRenderTexture[eye] = new OculusEyeTexture();
        if (!pEyeRenderTexture[eye]->Init(session, idealSize.w, idealSize.h, true))
        {
            if (retryCreate) goto Done;
            FATALERROR("Failed to create eye texture.");
        }

        eyeRenderViewport[eye].Pos.x = 0;
        eyeRenderViewport[eye].Pos.y = 0;
        eyeRenderViewport[eye].Size = idealSize;
        if (!pEyeRenderTexture[eye]->TextureChain)
        {
            if (retryCreate) goto Done;
            FATALERROR("Failed to create texture.");
        }
    }


    // Create a mirror to see on the monitor.
    mirrorDesc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
    mirrorDesc.Width = DIRECTX.WinSizeW;
    mirrorDesc.Height = DIRECTX.WinSizeH;
    mirrorDesc.MiscFlags = ovrTextureMisc_None;
    mirrorDesc.MirrorOptions = ovrMirrorOption_Default;
    result = ovr_CreateMirrorTextureWithOptionsDX(session, DIRECTX.CommandQueue, &mirrorDesc, &mirrorTexture);

    if (!OVR_SUCCESS(result))
    {
        if (retryCreate) goto Done;
        FATALERROR("Failed to create mirror texture.");
    }

    // Create the room model
    scene = new SceneSphere(false);
    scene->Init(false);

    
    // Create camera
    static float Yaw = XM_PI;
    mainCam = new Camera(XMVectorSet(0.0f, 0.0f, 0.0f, 0), XMQuaternionRotationRollPitchYaw(0, Yaw, 0));

    DIRECTX.InitFrame(drawMirror);

    scene->InitTexturesToTexArray();


    // Main loop
    while (DIRECTX.HandleMessages())
    {
        ovrSessionStatus sessionStatus;
        ovr_GetSessionStatus(session, &sessionStatus);
        if (sessionStatus.ShouldQuit)
        {
            // Because the application is requested to quit, should not request retry
            retryCreate = false;
            break;
        }
        if (sessionStatus.ShouldRecenter)
            ovr_RecenterTrackingOrigin(session);

        if (sessionStatus.IsVisible)
        {
            result = ovr_WaitToBeginFrame(session, frameIndex);
            result = ovr_BeginFrame(session, frameIndex);

            XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, -0.05f, 0), mainCam->GetRotVec());
            XMVECTOR right = XMVector3Rotate(XMVectorSet(0.05f, 0, 0, 0), mainCam->GetRotVec());
            XMVECTOR mainCamPos = mainCam->GetPosVec();
            XMVECTOR mainCamRot = mainCam->GetRotVec();
            if (DIRECTX.Key['W'] || DIRECTX.Key[VK_UP])      mainCamPos = XMVectorAdd(mainCamPos, forward);
            if (DIRECTX.Key['S'] || DIRECTX.Key[VK_DOWN])    mainCamPos = XMVectorSubtract(mainCamPos, forward);
            if (DIRECTX.Key['D'])                            mainCamPos = XMVectorAdd(mainCamPos, right);
            if (DIRECTX.Key['A'])                            mainCamPos = XMVectorSubtract(mainCamPos, right);

            
            result = ovr_GetInputState(session, ovrControllerType_Touch, &inputState);
            float thumbstickX = inputState.Thumbstick[ovrHand_Left].x;
            float thumbstickY = inputState.Thumbstick[ovrHand_Left].y;
            XMVECTOR horMovement = XMVectorAdd(XMVectorScale(forward, thumbstickY), XMVectorScale(right, thumbstickX));
            mainCamPos = XMVectorAdd(mainCamPos, horMovement);
            
            if (DIRECTX.Key[VK_LEFT])  mainCamRot = XMQuaternionRotationRollPitchYaw(0, Yaw += 0.02f, 0);
            if (DIRECTX.Key[VK_RIGHT]) mainCamRot = XMQuaternionRotationRollPitchYaw(0, Yaw -= 0.02f, 0);
            thumbstickX = inputState.Thumbstick[ovrHand_Right].x;
            mainCamRot = XMQuaternionRotationRollPitchYaw(0, Yaw -= 0.02f * thumbstickX, 0);

            float leftBottomTrigger = inputState.IndexTrigger[ovrHand_Left];
            float rightBottomTrigger = inputState.IndexTrigger[ovrHand_Right];
            XMVECTOR vertMovement = XMVectorScale({0, 1, 0, 0}, (-leftBottomTrigger+rightBottomTrigger)*0.05f);
            mainCamPos = XMVectorAdd(mainCamPos, vertMovement);

            mainCam->SetPosVec(mainCamPos);
            mainCam->SetRotVec(mainCamRot);

            scene->lights[0].position = { 0,3,0,0 };

            // Animate the cube
            static float cubeClock = 0;
            if (sessionStatus.HasInputFocus) // Pause the application if we are not supposed to have input..
            {
                XMVECTOR cubePos = { 9 * sin(cubeClock), 3, 9 * cos(cubeClock += 0.0015f), 0 };
                XMFLOAT3 cubePosAsFloat3;
                XMStoreFloat3(&cubePosAsFloat3, cubePos);
                scene->UpdateInstancePosition(0, cubePosAsFloat3);
            }

            // Call ovr_GetRenderDesc each frame to get the ovrEyeRenderDesc, as the returned values (e.g. HmdToEyePose) may change at runtime.
            ovrEyeRenderDesc eyeRenderDesc[2];
            eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
            eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);

            // Get both eye poses simultaneously, with IPD offset already included.
            ovrPosef EyeRenderPose[2];
            ovrPosef HmdToEyePose[2] = { eyeRenderDesc[0].HmdToEyePose, eyeRenderDesc[1].HmdToEyePose };

            double sensorSampleTime;    // sensorSampleTime is fed into the layer later
            ovr_GetEyePoses(session, frameIndex, ovrTrue, HmdToEyePose, EyeRenderPose, &sensorSampleTime);

            ovrTrackingState ts = ovr_GetTrackingState(session, ovr_GetTimeInSeconds(), ovrTrue);

            if (ts.StatusFlags & (ovrStatus_OrientationTracked | ovrStatus_PositionTracked)) {
                ovrPosef leftControllerPose = ts.HandPoses[ovrHand_Left].ThePose;
                ovrPosef rightControllerPose = ts.HandPoses[ovrHand_Right].ThePose;

                // Extract position and orientation
                ovrVector3f leftControllerPosition = leftControllerPose.Position;
                ovrQuatf leftControllerOrientation = leftControllerPose.Orientation;

                ovrVector3f rightControllerPosition = rightControllerPose.Position;
                ovrQuatf rightControllerOrientation = rightControllerPose.Orientation;

                // Update the laft hand position and orientation
                XMVECTOR posVec = { leftControllerPosition.x, leftControllerPosition.y, leftControllerPosition.z, 0 };
                posVec = XMVectorAdd(mainCamPos, XMVector3Rotate(posVec, mainCamRot));

                XMVECTOR handQuat = XMVectorSet(leftControllerOrientation.x, leftControllerOrientation.y,
                    leftControllerOrientation.z, leftControllerOrientation.w);

                XMVECTOR scaleFactors = XMVectorSet(0.1f, 0.01f, 0.2f, 1.0f);

                XMMATRIX translationMatrix = XMMatrixTranslationFromVector(posVec);
                XMMATRIX rotationMatrix = XMMatrixRotationQuaternion(XMQuaternionMultiply(handQuat, mainCamRot));
                XMMATRIX scalingMatrix = XMMatrixScalingFromVector(scaleFactors);
                XMMATRIX transformationMatrix = XMMatrixMultiply(scalingMatrix, XMMatrixMultiply(rotationMatrix, translationMatrix));

                scene->UpdateInstanceTransform(1, transformationMatrix);

                // Now update the right hand
                posVec = { rightControllerPosition.x, rightControllerPosition.y, rightControllerPosition.z, 0 };
                posVec = XMVectorAdd(mainCamPos, XMVector3Rotate(posVec, mainCamRot));

                handQuat = XMVectorSet(rightControllerOrientation.x, rightControllerOrientation.y,
                    rightControllerOrientation.z, rightControllerOrientation.w);

                scaleFactors = XMVectorSet(0.1f, 0.01f, 0.2f, 1.0f);

                translationMatrix = XMMatrixTranslationFromVector(posVec);
                rotationMatrix = XMMatrixRotationQuaternion(XMQuaternionMultiply(handQuat, mainCamRot));
                scalingMatrix = XMMatrixScalingFromVector(scaleFactors);
                transformationMatrix = XMMatrixMultiply(scalingMatrix, XMMatrixMultiply(rotationMatrix, translationMatrix));

                scene->UpdateInstanceTransform(2, transformationMatrix);
            }

            ovrTimewarpProjectionDesc PosTimewarpProjectionDesc = {};

            scene->UpdateInstanceDescs();
            scene->UpdateTLAS();
            
            // Render Scene to Eye Buffers
            for (int eye = 0; eye < 2; ++eye)
            {
                DIRECTX.SetActiveContext(eye == 0 ? DrawContext_EyeRenderLeft : DrawContext_EyeRenderRight);

                DIRECTX.SetActiveEye(eye);

                CD3DX12_RESOURCE_BARRIER resBar = CD3DX12_RESOURCE_BARRIER::Transition(pEyeRenderTexture[eye]->GetD3DColorResource(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->ResourceBarrier(1, &resBar);

                if (pEyeRenderTexture[eye]->GetD3DDepthResource())
                {
                    resBar = CD3DX12_RESOURCE_BARRIER::Transition(pEyeRenderTexture[eye]->GetD3DDepthResource(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_DEPTH_WRITE);
                    DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->ResourceBarrier(1, &resBar);
                }

                //Get the pose information in XM format
                XMVECTOR eyeQuat = XMVectorSet(EyeRenderPose[eye].Orientation.x, EyeRenderPose[eye].Orientation.y,
                    EyeRenderPose[eye].Orientation.z, EyeRenderPose[eye].Orientation.w);
                XMVECTOR eyePos = XMVectorSet(EyeRenderPose[eye].Position.x, EyeRenderPose[eye].Position.y, EyeRenderPose[eye].Position.z, 0);

                // Get view and projection matrices for the Rift camera
                Camera finalCam(XMVectorAdd(mainCamPos, XMVector3Rotate(eyePos, mainCamRot)), XMQuaternionMultiply(eyeQuat, mainCamRot));
                XMMATRIX view = finalCam.GetViewMatrix();
                ovrMatrix4f p = ovrMatrix4f_Projection(eyeRenderDesc[eye].Fov, 0.2f, 1000.0f, ovrProjection_None);

                PosTimewarpProjectionDesc = ovrTimewarpProjectionDesc_FromProjection(p, ovrProjection_None);
                XMMATRIX proj = XMMatrixSet(p.M[0][0], p.M[1][0], p.M[2][0], p.M[3][0],
                    p.M[0][1], p.M[1][1], p.M[2][1], p.M[3][1],
                    p.M[0][2], p.M[1][2], p.M[2][2], p.M[3][2],
                    p.M[0][3], p.M[1][3], p.M[2][3], p.M[3][3]);
                XMMATRIX prod = XMMatrixMultiply(view, proj);

                scene->DoRaytracing(XMMatrixInverse(nullptr, XMMatrixTranspose(prod)), finalCam.GetPosVec());
                DIRECTX.CopyRaytracingOutputToBackbuffer(pEyeRenderTexture[eye]->GetD3DColorResource(), pEyeRenderTexture[eye]->GetD3DDepthResource());

                resBar = CD3DX12_RESOURCE_BARRIER::Transition(pEyeRenderTexture[eye]->GetD3DColorResource(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->ResourceBarrier(1, &resBar);

                if (pEyeRenderTexture[eye]->GetD3DDepthResource())
                {
                    resBar = CD3DX12_RESOURCE_BARRIER::Transition(pEyeRenderTexture[eye]->GetD3DDepthResource(),
                        D3D12_RESOURCE_STATE_DEPTH_WRITE,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->ResourceBarrier(1, &resBar);
                }

                // kick off eye render command lists before ovr_SubmitFrame()
                DIRECTX.SubmitCommandList(DIRECTX.ActiveContext);

                // Commit rendering to the swap chain
                pEyeRenderTexture[eye]->Commit();
            }

            // Initialize our single full screen Fov layer.
            ovrLayerEyeFovDepth ld = {};
            ld.Header.Type = ovrLayerType_EyeFov;
           // ld.Header.Type = ovrLayerType_Quad;
            ld.Header.Flags = 0;
            ld.ProjectionDesc = PosTimewarpProjectionDesc;
            ld.SensorSampleTime = sensorSampleTime;

            for (int eye = 0; eye < 2; ++eye)
            {
                ld.ColorTexture[eye] = pEyeRenderTexture[eye]->TextureChain;
                ld.DepthTexture[eye] = pEyeRenderTexture[eye]->DepthTextureChain;
                ld.Viewport[eye] = eyeRenderViewport[eye];
                ld.Viewport[eye].Size.w /= 2;
                ld.Viewport[eye].Size.h /= 2;
                ld.Fov[eye] = hmdDesc.DefaultEyeFov[eye];
                ld.RenderPose[eye] = EyeRenderPose[eye];
            }

            ovrLayerHeader* layers = &ld.Header;
            result = ovr_EndFrame(session, frameIndex, nullptr, &layers, 1);
            // exit the rendering loop if submit returns an error, will retry on ovrError_DisplayLost
            if (!OVR_SUCCESS(result))
                goto Done;

            frameIndex++;
        }

        if (drawMirror)
        {
            DIRECTX.SetActiveContext(DrawContext_Final);

            DIRECTX.SetViewport(0.0f, 0.0f, (float)hmdDesc.Resolution.w / 2, (float)hmdDesc.Resolution.h / 2);

            // Render mirror
            ID3D12Resource* mirrorTexRes = nullptr;
            ovr_GetMirrorTextureBufferDX(session, mirrorTexture, IID_PPV_ARGS(&mirrorTexRes));

            //DIRECTX.SetAndClearRenderTarget(DIRECTX.CurrentFrameResources().SwapChainRtvHandle, nullptr, 1.0f, 0.5f, 0.0f, 1.0f);

            CD3DX12_RESOURCE_BARRIER preMirrorBlitBar[] =
            {
                CD3DX12_RESOURCE_BARRIER::Transition(DIRECTX.CurrentFrameResources().SwapChainBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST),
                CD3DX12_RESOURCE_BARRIER::Transition(mirrorTexRes, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE)
            };

            // Indicate that the back buffer will now be copied into
            DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->ResourceBarrier(ARRAYSIZE(preMirrorBlitBar), preMirrorBlitBar);

            // TODO: Leads to debug layer error messages, so we use CopyTextureRegion instead
            //DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->CopyResource(DIRECTX.CurrentFrameResources().SwapChainBuffer, mirrorTexRes);

            D3D12_TEXTURE_COPY_LOCATION copySrc = {};
            copySrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            copySrc.SubresourceIndex = 0;
            copySrc.pResource = DIRECTX.CurrentFrameResources().SwapChainBuffer;
            D3D12_TEXTURE_COPY_LOCATION copyDst = {};
            copyDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            copyDst.SubresourceIndex = 0;
            copyDst.pResource = mirrorTexRes;
            DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->CopyTextureRegion(&copySrc, 0, 0, 0, &copyDst, nullptr);

            CD3DX12_RESOURCE_BARRIER resBar = CD3DX12_RESOURCE_BARRIER::Transition(mirrorTexRes,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            DIRECTX.CurrentFrameResources().CommandLists[DIRECTX.ActiveContext]->ResourceBarrier(1, &resBar);

            Release(mirrorTexRes);
        }

        DIRECTX.SubmitCommandListAndPresent(drawMirror);
    }

    // Release resources
Done:
    delete mainCam;
    delete scene;
    if (mirrorTexture)
        ovr_DestroyMirrorTexture(session, mirrorTexture);

    for (int eye = 0; eye < 2; ++eye)
    {
        delete pEyeRenderTexture[eye];
    }
    DIRECTX.ReleaseDevice();
    ovr_Destroy(session);

    // Retry on ovrError_DisplayLost
    return retryCreate || (result == ovrError_DisplayLost);
}

//-------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int)
{
    // Initializes LibOVR, and the Rift
    ovrInitParams initParams = { ovrInit_RequestVersion | ovrInit_FocusAware, OVR_MINOR_VERSION, NULL, 0, 0 };
    ovrResult result = ovr_Initialize(&initParams);
    VALIDATE(OVR_SUCCESS(result), "Failed to initialize libOVR.");

    VALIDATE(DIRECTX.InitWindow(hinst, L"Oculus Room Tiny (DX12)"), "Failed to open window.");

    DIRECTX.Run(MainLoop);

    ovr_Shutdown();
    return(0);
}
