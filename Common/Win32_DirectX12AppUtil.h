/************************************************************************************
Filename    :   Win32_DirectX12AppUtil.h
Content     :   D3D12 application/Window setup functionality for RoomTiny raytracing
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

#ifndef OVR_Win32_DirectX12AppUtil_h
#define OVR_Win32_DirectX12AppUtil_h

#include <cstdint>
#include <vector>
#include <vector>
#include <fstream>
#include <sstream>
#include <d3dcompiler.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <new>
#include <stdio.h>
#include "DirectXMath.h"
using namespace DirectX;

#include "Win32_d3dx12.h"
#include <Windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#include "CompiledShaders\Raytracing.hlsl.h"
#define  TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef VALIDATE
#define VALIDATE(x, msg) if (!(x)) { MessageBoxA(NULL, (msg), "OculusRoomTiny", MB_ICONERROR | MB_OK); exit(-1); }
#endif

#ifndef FATALERROR
#define FATALERROR(msg) { MessageBoxA(NULL, (msg), "OculusRoomTiny", MB_ICONERROR | MB_OK); exit(-1); }
#endif


// clean up member COM pointers
template<typename T> void Release(T*& obj)
{
    if (!obj) return;
    obj->Release();
    obj = nullptr;
}

//------------------------------------------------------------
struct DepthBuffer
{
    ID3D12Resource* TextureRes;
    D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle;

    DepthBuffer(ID3D12Device* Device, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle, int sizeW, int sizeH,
        DXGI_FORMAT depthFormat, int sampleCount)
    {
        D3D12_RESOURCE_DESC dsDesc = {};
        dsDesc.Width = sizeW;
        dsDesc.Height = sizeH;
        dsDesc.MipLevels = 1;
        dsDesc.DepthOrArraySize = 1;
        dsDesc.Format = depthFormat;
        dsDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        dsDesc.SampleDesc.Count = sampleCount;
        dsDesc.SampleDesc.Quality = 0;
        dsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_CLEAR_VALUE clearValue(depthFormat, 1.0f, 0);

        HRESULT hr = Device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &dsDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&TextureRes));
        VALIDATE((hr == ERROR_SUCCESS), "CreateCommittedResource failed");

        DsvHandle = dsvHandle;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = depthFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        Device->CreateDepthStencilView(TextureRes, &dsvDesc, DsvHandle);
    }

    ~DepthBuffer()
    {
        Release(TextureRes);
    }
};

////----------------------------------------------------------------
struct DataBuffer
{
    ID3D12Resource* D3DBuffer;
    size_t           BufferSize;

    DataBuffer(ID3D12Device* Device, const void* rawData, size_t bufferSize) : BufferSize(bufferSize)
    {
        CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC buf = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);

        HRESULT hr = Device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &buf,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&D3DBuffer));
        VALIDATE((hr == ERROR_SUCCESS), "CreateCommittedResource failed");

        // Copy the triangle data to the vertex buffer.
        UINT8* pBufferHead;
        hr = D3DBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pBufferHead));
        VALIDATE((hr == ERROR_SUCCESS), "Vertex buffer map failed");
        memcpy(pBufferHead, rawData, bufferSize);
        D3DBuffer->Unmap(0, nullptr);
    }

    ~DataBuffer()
    {
        Release(D3DBuffer);
    }
};

////----------------------------------------------------------------
struct DescHandleProvider
{
    ID3D12DescriptorHeap* DescHeap;
    CD3DX12_CPU_DESCRIPTOR_HANDLE NextAvailableCpuHandle;
    UINT IncrementSize;
    UINT CurrentHandleCount;
    UINT MaxHandleCount;
    std::vector<CD3DX12_CPU_DESCRIPTOR_HANDLE> FreeHandles;
    DescHandleProvider() {};
    DescHandleProvider(ID3D12DescriptorHeap* descHeap, UINT incrementSize, UINT handleCount)
        : DescHeap(descHeap), IncrementSize(incrementSize)
        , CurrentHandleCount(0), MaxHandleCount(handleCount), FreeHandles()
    {
        VALIDATE((descHeap), "NULL heap provided");
        NextAvailableCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(descHeap->GetCPUDescriptorHandleForHeapStart());
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE AllocCpuHandle(UINT* pCurrentHandleCount = nullptr)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE newHandle{};
        if (FreeHandles.size() > 0)
        {
            newHandle = FreeHandles.back();
            FreeHandles.pop_back();
        }
        else
        {
            VALIDATE((CurrentHandleCount < MaxHandleCount), "Hit maximum number of handles available");
            newHandle = NextAvailableCpuHandle;
            NextAvailableCpuHandle.Offset(IncrementSize);
            if(pCurrentHandleCount != nullptr)
                *pCurrentHandleCount = CurrentHandleCount;
            ++CurrentHandleCount;
        }
        return newHandle;
    }

    void FreeCpuHandle(CD3DX12_CPU_DESCRIPTOR_HANDLE handle)
    {
        FreeHandles.push_back(handle);
    }

    CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandleFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
    {
        int offset = (int)(cpuHandle.ptr - DescHeap->GetCPUDescriptorHandleForHeapStart().ptr);
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(DescHeap->GetGPUDescriptorHandleForHeapStart(), offset);
    }
};

enum DrawContext
{
    DrawContext_EyeRenderLeft = 0,
    DrawContext_EyeRenderRight,
    DrawContext_Final,

    DrawContext_Count,
};

static void ThrowIfFailed(HRESULT hr, const wchar_t* message = L"")
{

    if (hr != S_OK)
    {
        printf("%s\n", message);
        exit(1);
    }
}

static void ThrowIfFalse(bool condition)
{

    if (!condition)
    {
        exit(1);
    }
}


//---------------------------------------------------------------------
struct DirectX12
{
    struct Viewport
    {
        float left;
        float top;
        float right;
        float bottom;
    };


    HWND                        Window;
    bool                        Running;
    bool                        Key[256];
    int                         WinSizeW;
    int                         WinSizeH;

    ID3D12Debug* DebugController;
    ID3D12Device* Device;
    ID3D12CommandQueue* CommandQueue;
    //DepthBuffer*                MainDepthBuffer;
    D3D12_RECT                  ScissorRect;

    HINSTANCE                   hInstance;

    // DirectX Raytracing (DXR) attributes
    ComPtr<ID3D12Device5> m_dxrDevice;
    ComPtr<ID3D12StateObject> m_dxrStateObject;

    // Root signatures
    ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
    //ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;
    //ComPtr<ID3D12RootSignature> m_raytracingAABBLocalRootSignature;

    //Raytracing Descriptors
    //ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
    //UINT m_descriptorsAllocated;
    //UINT m_descriptorSize;





    // Raytracing output
    ComPtr<ID3D12Resource> m_raytracingOutputs[2];
    D3D12_GPU_DESCRIPTOR_HANDLE m_raytracingOutputResourceUAVGpuDescriptors[2];
    UINT m_raytracingOutputResourceUAVDescriptorHeapIndexs[2];

    //Depth outputs
    ComPtr<ID3D12Resource> m_raytracingDepthOutputs[2];
    D3D12_GPU_DESCRIPTOR_HANDLE m_raytracingDepthOutputResourceUAVGpuDescriptors[2];
    UINT m_raytracingDepthOutputResourceUAVDescriptorHeapIndexs[2];

    UINT eyeWidth;
    UINT eyeHeight;

    ComPtr<ID3D12Resource> m_missShaderTable;
    ComPtr<ID3D12Resource> m_hitGroupShaderTable;
    ComPtr<ID3D12Resource> m_rayGenShaderTable;

    

    ID3D12DescriptorHeap* RtvHeap;
    ID3D12DescriptorHeap* DsvHeap;
    ID3D12DescriptorHeap* CbvSrvHeap;

    DescHandleProvider          RtvHandleProvider;
    DescHandleProvider          DsvHandleProvider;
    DescHandleProvider          CbvSrvHandleProvider;

    IDXGISwapChain3* SwapChain;
    static const int            SwapChainNumFrames = 4;
    UINT                        SwapChainFrameIndex;

    int                         EyeMsaaRate;  // not for the back buffer, just the eye textures
    DXGI_FORMAT                 DepthFormat;
    UINT                        ActiveEyeIndex;
    DrawContext                 ActiveContext;

    

    ComPtr<ID3D12Resource> textureArray;
    D3D12_GPU_DESCRIPTOR_HANDLE texArrayGpuHandle;

    // per-swap-chain-frame resources
    struct SwapChainFrameResources
    {
        ID3D12CommandAllocator* CommandAllocators[DrawContext_Count];
        ID3D12GraphicsCommandList* CommandLists[DrawContext_Count];
        ComPtr<ID3D12GraphicsCommandList4> m_dxrCommandList[DrawContext_Count];
        bool                            CommandListSubmitted[DrawContext_Count];

        ID3D12Resource* SwapChainBuffer;
        CD3DX12_CPU_DESCRIPTOR_HANDLE   SwapChainRtvHandle;

        // Synchronization objects.
        HANDLE                          PresentFenceEvent;
        ID3D12Fence* PresentFenceRes;
        UINT64                          PresentFenceValue;
        UINT64                          PresentFenceWaitValue;
    };
    SwapChainFrameResources             PerFrameResources[SwapChainNumFrames];

    static LRESULT CALLBACK WindowProc(_In_ HWND hWnd, _In_ UINT Msg, _In_ WPARAM wParam, _In_ LPARAM lParam)
    {
        auto p = reinterpret_cast<DirectX12*>(GetWindowLongPtr(hWnd, 0));
        switch (Msg)
        {
        case WM_KEYDOWN:
            p->Key[wParam] = true;
            break;
        case WM_KEYUP:
            p->Key[wParam] = false;
            break;
        case WM_DESTROY:
            p->Running = false;
            break;
        default:
            return DefWindowProcW(hWnd, Msg, wParam, lParam);
        }
        if ((p->Key['Q'] && p->Key[VK_CONTROL]) || p->Key[VK_ESCAPE])
        {
            p->Running = false;
        }
        return 0;
    }

    DirectX12() :
        Window(nullptr),
        Running(false),
        WinSizeW(0),
        WinSizeH(0),
        Device(nullptr),
        hInstance(nullptr),
        SwapChain(nullptr),
        //MainDepthBuffer(nullptr),
        EyeMsaaRate(1),
        DepthFormat(DXGI_FORMAT_D32_FLOAT),
        ActiveEyeIndex(UINT(-1)),           // require init by app
        ActiveContext(DrawContext_Count)    // require init by app
    {
        // Clear input
        for (int i = 0; i < _countof(Key); ++i)
            Key[i] = false;
    }

    ~DirectX12()
    {
        ReleaseDevice();
        CloseWindow();
    }

    bool InitWindow(HINSTANCE hinst, LPCWSTR title)
    {
        hInstance = hinst;
        Running = true;

        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpszClassName = L"App";
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.cbWndExtra = sizeof(this);
        RegisterClassW(&wc);

        // adjust the window size and show at InitDevice time
        Window = CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, 0, 0, hinst, 0);
        if (!Window)
            return false;

        SetWindowLongPtr(Window, 0, LONG_PTR(this));

        return true;
    }

    void CloseWindow()
    {
        if (Window)
        {
            DestroyWindow(Window);
            Window = nullptr;
            UnregisterClassW(L"App", hInstance);
        }
    }

    // Geometry
    struct D3DBuffer
    {
        ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;
    };

    inline void AllocateUAVBuffer(ID3D12Device* pDevice, UINT64 bufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON, const wchar_t* resourceName = nullptr)
    {
        auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ThrowIfFailed(pDevice->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            initialResourceState,
            nullptr,
            IID_PPV_ARGS(ppResource)));
        if (resourceName)
        {
            (*ppResource)->SetName(resourceName);
        }
    }

    // Create SRV for a buffer.
    UINT CreateBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize)
    {

        // SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.NumElements = numElements;
        if (elementSize == 0)
        {
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            srvDesc.Buffer.StructureByteStride = 0;
        }
        else
        {
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            srvDesc.Buffer.StructureByteStride = elementSize;
        }
        UINT descriptorIndex;
        buffer->cpuDescriptorHandle = CbvSrvHandleProvider.AllocCpuHandle(&descriptorIndex);
        Device->CreateShaderResourceView(buffer->resource.Get(), &srvDesc, buffer->cpuDescriptorHandle);
        buffer->gpuDescriptorHandle = CbvSrvHandleProvider.GpuHandleFromCpuHandle(buffer->cpuDescriptorHandle);
        return descriptorIndex;
    }

    // Returns bool whether the device supports DirectX Raytracing tier.
    inline bool IsDirectXRaytracingSupported(IDXGIAdapter1* adapter)
    {
        ComPtr<ID3D12Device> testDevice;
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};

        return SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice)))
            && SUCCEEDED(testDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData)))
            && featureSupportData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }

    // Create raytracing device and command list.
    void CreateRaytracingInterfaces()
    {

        HRESULT hr = Device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice));
        if (FAILED(hr))
            exit(1);
        //hr = PerFrameResources[0].CommandLists[0]->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList));
        //if (FAILED(hr))
        //    exit(1);
    }

    struct GlobalRootSignatureParams {
        enum Value {
            OutputViewSlot = 0,
            OutputDepthSlot,
            AccelerationStructureSlot,
            SceneConstantSlot,
            VertexBufferSlot,
            TextureSlot,
            Count
        };
    };

    struct LocalRootSignatureParams {
        enum Value {
            ViewportConstantSlot = 0,
            Count
        };
    };


    void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
    {

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;

        ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
        ThrowIfFailed(Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
    }

#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)

    void CreateRootSignatures()
    {
        // Global Root Signature
        // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
        {
            CD3DX12_DESCRIPTOR_RANGE UAVDescriptor;
            UAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
            CD3DX12_DESCRIPTOR_RANGE UAVDescriptor1;
            UAVDescriptor1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
            CD3DX12_DESCRIPTOR_RANGE vertexBufferDescriptors;
            vertexBufferDescriptors.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);
            CD3DX12_DESCRIPTOR_RANGE textureDescriptorRange;
            textureDescriptorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
            CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
            rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &UAVDescriptor);
            rootParameters[GlobalRootSignatureParams::OutputDepthSlot].InitAsDescriptorTable(1, &UAVDescriptor1);
            rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
            rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);
            rootParameters[GlobalRootSignatureParams::VertexBufferSlot].InitAsDescriptorTable(1, &vertexBufferDescriptors);
            rootParameters[GlobalRootSignatureParams::TextureSlot].InitAsDescriptorTable(1, &textureDescriptorRange, D3D12_SHADER_VISIBILITY_ALL);
            CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
            SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
        }

        //// Local Root Signature
        //// This is a root signature that enables a shader to have unique arguments that come from shader tables.
        //{
        //    //m_rayGenCB.viewport = { -1.0f, -1.0f, 1.0f, 1.0f };
        //    //m_rayGenCB.stencil = m_rayGenCB.viewport;
        //    CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
        //    //rootParameters[LocalRootSignatureParams::ViewportConstantSlot].InitAsConstants(SizeOfInUint32(m_rayGenCB), 0, 0);
        //    CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
        //    localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        //    SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
        //}

        // Local Root Signature
        // This is a root signature that enables a shader to have unique arguments that come from shader tables.
        //{
        //    CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc;
        //    localRootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        //    // Serialize and create the local root signature
        //    SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
        //}

        //// This is a root signature that enables a shader to have unique arguments that come from shader tables.
        //{
        //    CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc;
        //    localRootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

        //    // Serialize and create the local root signature
        //    SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingAABBLocalRootSignature);
        //}
    }


    std::vector<char> LoadFile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) throw std::runtime_error("Failed to open file: " + filename);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(size);
        if (!file.read(buffer.data(), size)) throw std::runtime_error("Failed to read file: " + filename);
        return buffer;
    }

    const wchar_t* c_raygenShaderName = L"MyRaygenShader";
    const wchar_t* c_closestHitShaderName = L"MyClosestHitShader";
    const wchar_t* c_aabbClosestHitShaderName = L"MySphereClosestHitShader";
    const wchar_t* c_intersectionShaderName = L"MySimpleIntersectionShader";
    const wchar_t* c_missShaderName = L"MyMissShader";
    const wchar_t* c_triangleHitGroupName = L"TriangleHitGroup";
    const wchar_t* c_aabbHitGroupName = L"AABBHitGroup";



    // Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
    void CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline)
    {
        // Hit group and miss shaders in this sample are not using a local root signature and thus one is not associated with them.

        // Local root signature to be used in a ray gen shader.
        //{
        //    auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        //    localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
        //    // Shader association
        //    auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        //    rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        //    rootSignatureAssociation->AddExport(c_raygenShaderName);
        //}


        //// Create local root signiture for AABB geometry
        //{
        //    auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        //    localRootSignature->SetRootSignature(m_raytracingAABBLocalRootSignature.Get());
        //    // Shader association
        //    auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        //    rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        //    rootSignatureAssociation->AddExport(c_raygenShaderName);
        //}
    }

    // Pretty-print a state object tree.
    inline void PrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
    {
        std::wstringstream wstr;
        wstr << L"\n";
        wstr << L"--------------------------------------------------------------------\n";
        wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
        if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
        if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

        auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports)
            {
                std::wostringstream woss;
                for (UINT i = 0; i < numExports; i++)
                {
                    woss << L"|";
                    if (depth > 0)
                    {
                        for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
                    }
                    woss << L" [" << i << L"]: ";
                    if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
                    woss << exports[i].Name << L"\n";
                }
                return woss.str();
            };

        for (UINT i = 0; i < desc->NumSubobjects; i++)
        {
            wstr << L"| [" << i << L"]: ";
            switch (desc->pSubobjects[i].Type)
            {
            case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
                wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
                wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
               // wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
            {
                wstr << L"DXIL Library 0x";
                auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
                wstr << ExportTree(1, lib->NumExports, lib->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
            {
                wstr << L"Existing Library 0x";
                auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << collection->pExistingCollection << L"\n";
                wstr << ExportTree(1, collection->NumExports, collection->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                wstr << L"Subobject to Exports Association (Subobject [";
                auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
                wstr << index << L"])\n";
                for (UINT j = 0; j < association->NumExports; j++)
                {
                    wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                wstr << L"DXIL Subobjects to Exports Association (";
                auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                wstr << association->SubobjectToAssociate << L")\n";
                for (UINT j = 0; j < association->NumExports; j++)
                {
                    wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
            {
                wstr << L"Raytracing Shader Config\n";
                auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
                wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
                wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
            {
                wstr << L"Raytracing Pipeline Config\n";
                auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
                wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
            {
                wstr << L"Hit Group (";
                auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
                wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
                wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
                wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
                break;
            }
            }
            wstr << L"|--------------------------------------------------------------------\n";
        }
        wstr << L"\n";
        OutputDebugStringW(wstr.str().c_str());
    }
    // Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a DispatchRays() call,
// with all configuration options resolved, such as local signatures and other state.
    void CreateRaytracingPipelineStateObject()
    {
        // Create 7 subobjects that combine into a RTPSO:
        // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
        // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
        // This simple sample utilizes default shader association except for local root signature subobject
        // which has an explicit association specified purely for demonstration purposes.
        // 1 - DXIL library
        // 1 - Triangle hit group
        // 1 - Shader config
        // 2 - Local root signature and association
        // 1 - Global root signature
        // 1 - Pipeline config
        CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };


        // DXIL library
        // This contains the shaders and their entrypoints for the state object.
        // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
        auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
        D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*)g_pRaytracing, ARRAYSIZE(g_pRaytracing));
        lib->SetDXILLibrary(&libdxil);


        // Triangle hit group
        // A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB.
        // In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
        auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetClosestHitShaderImport(c_closestHitShaderName);
        hitGroup->SetHitGroupExport(c_triangleHitGroupName);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

        auto aabbGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        aabbGroup->SetIntersectionShaderImport(c_intersectionShaderName);
        aabbGroup->SetClosestHitShaderImport(c_aabbClosestHitShaderName);
        aabbGroup->SetHitGroupExport(c_aabbHitGroupName);
        aabbGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE);

        // Shader config
        // Defines the maximum sizes in bytes for the ray payload and attribute structure.
        auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
        UINT payloadSize = 11 * sizeof(float) + sizeof(UINT);   // float4 color, float depth, float3 origin, float3 direction
        UINT attributeSize = 6 * sizeof(float); // float2 barycentrics
        shaderConfig->Config(payloadSize, attributeSize);

        // Local root signature and shader association
        CreateLocalRootSignatureSubobjects(&raytracingPipeline);
        // This is a root signature that enables a shader to have unique arguments that come from shader tables.

        // Global root signature
        // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
        auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
        globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

        // Pipeline config
        // Defines the maximum TraceRay() recursion depth.
        auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
        // PERFOMANCE TIP: Set max recursion depth as low as needed 
        // as drivers may apply optimization strategies for low recursion depths. 
        UINT maxRecursionDepth = 3; // ~ primary rays only. 
        pipelineConfig->Config(maxRecursionDepth);

#if _DEBUG
        PrintStateObjectDesc(raytracingPipeline);
#endif

        // Create the state object.
        ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
    }

    //void CreateRaytracingDescriptorHeap()
    //{

    //    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    //    // Allocate a heap for a single descriptor:
    //    // 1 - raytracing output texture UAV
    //    descriptorHeapDesc.NumDescriptors = 20;
    //    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    //    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    //    descriptorHeapDesc.NodeMask = 0;
    //    Device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_descriptorHeap));
    //    //NAME_D3D12_OBJECT(m_descriptorHeap);

    //    m_descriptorSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    //}


    inline void AllocateUploadBuffer(ID3D12Device* pDevice, void* pData, UINT64 datasize, ID3D12Resource** ppResource, const wchar_t* resourceName = nullptr)
    {
        auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(datasize);
        ThrowIfFailed(pDevice->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(ppResource)));
        if (resourceName)
        {
            (*ppResource)->SetName(resourceName);
        }
        void* pMappedData;
        (*ppResource)->Map(0, nullptr, &pMappedData);
        memcpy(pMappedData, pData, datasize);
        (*ppResource)->Unmap(0, nullptr);
    }

    inline void UpdateUploadBuffer(ID3D12Device* pDevice, void* pData, UINT64 datasize, ID3D12Resource** ppResource)
    {
        void* pMappedData;
        (*ppResource)->Map(0, nullptr, &pMappedData);
        memcpy(pMappedData, pData, datasize);
        (*ppResource)->Unmap(0, nullptr);
    }



    bool InitDevice(int vpW, int vpH, const LUID* pLuid, DXGI_FORMAT depthFormat, int eyeMsaaRate, bool windowed = true, UINT eyeWidth=1024, UINT eyeHeight=768)
    {

        this->eyeWidth = eyeWidth;
        this->eyeHeight = eyeHeight;

        EyeMsaaRate = eyeMsaaRate;
        DepthFormat = depthFormat;

        WinSizeW = vpW;
        WinSizeH = vpH;

        ScissorRect.right = static_cast<LONG>(WinSizeW);
        ScissorRect.bottom = static_cast<LONG>(WinSizeH);

        RECT size = { 0, 0, vpW, vpH };
        AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, false);
        const UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW;
        if (!SetWindowPos(Window, nullptr, 0, 0, size.right - size.left, size.bottom - size.top, flags))
            return false;

        ComPtr<IDXGIFactory4> DXGIFactory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&(DXGIFactory)));
        VALIDATE((hr == ERROR_SUCCESS), "CreateDXGIFactory1 failed");

        ComPtr<IDXGIAdapter1> Adapter;

        ComPtr<IDXGIFactory6> factory6;
        hr = DXGIFactory.As(&factory6);
        if (FAILED(hr))
        {
            exit(1);
        }

        bool found = false;
        for (UINT adapterID = 0; DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterID, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&Adapter)); ++adapterID)
        {
            DXGI_ADAPTER_DESC adapterDesc;
            Adapter->GetDesc(&adapterDesc);
            if (IsDirectXRaytracingSupported(Adapter.Get()))
            {
                found = true;
                break;
            }
        }
        if (!found)
            exit(1);

#ifdef _DEBUG
        // Enable the D3D12 debug layer.
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController))))
        {
            DebugController->EnableDebugLayer();
        }
#endif

        hr = D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&Device));
        VALIDATE((hr == ERROR_SUCCESS), "D3D12CreateDevice failed");


        //{
        //    // Set max frame latency to 1
        //    IDXGIDevice1* DXGIDevice1 = nullptr;
        //    hr = Device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&DXGIDevice1);
        //    VALIDATE((hr == ERROR_SUCCESS), "QueryInterface failed");
        //    DXGIDevice1->SetMaximumFrameLatency(1);
        //    Release(DXGIDevice1);
        //}

        // Describe and create the command queue.
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        hr = Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&CommandQueue));
        VALIDATE((hr == ERROR_SUCCESS), "CreateCommandQueue failed");

        // Create swap chain
        DXGI_SWAP_CHAIN_DESC scDesc = {};
        scDesc.BufferCount = SwapChainNumFrames;
        scDesc.BufferDesc.Width = WinSizeW;
        scDesc.BufferDesc.Height = WinSizeH;
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        //scDesc.BufferDesc.RefreshRate.Denominator = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow = Window;
        scDesc.SampleDesc.Count = 1;
        scDesc.Windowed = windowed;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain* swapChainBase;
        hr = DXGIFactory->CreateSwapChain(CommandQueue, &scDesc, &swapChainBase);
        VALIDATE((hr == ERROR_SUCCESS), "CreateSwapChain failed");
        SwapChain = (IDXGISwapChain3*)swapChainBase;

        // This sample does not support fullscreen transitions.
        hr = DXGIFactory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
        VALIDATE((hr == ERROR_SUCCESS), "MakeWindowAssociation failed");

        SwapChainFrameIndex = SwapChain->GetCurrentBackBufferIndex();

        const UINT maxConcurrentDescriptors = 10;

        // Create descriptor heaps.
        {
            D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
            rtvHeapDesc.NumDescriptors = SwapChainNumFrames * maxConcurrentDescriptors;
            rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            hr = Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&RtvHeap));
            VALIDATE((hr == ERROR_SUCCESS), "CreateDescriptorHeap failed");

            RtvHandleProvider = DescHandleProvider(
                RtvHeap,
                Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
                rtvHeapDesc.NumDescriptors);
        }

        {
            D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
            dsvHeapDesc.NumDescriptors = SwapChainNumFrames * maxConcurrentDescriptors;
            dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            hr = Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&DsvHeap));
            VALIDATE((hr == ERROR_SUCCESS), "CreateDescriptorHeap failed");

            DsvHandleProvider = DescHandleProvider(
                DsvHeap,
                Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
                dsvHeapDesc.NumDescriptors);
        }

        {
            UINT maxNumCbvSrvHandles = 1000;
            D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
            cbvSrvHeapDesc.NumDescriptors = maxNumCbvSrvHandles * maxConcurrentDescriptors;
            cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            hr = Device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&CbvSrvHeap));
            VALIDATE((hr == ERROR_SUCCESS), "CreateDescriptorHeap failed");

            CbvSrvHandleProvider = DescHandleProvider(
                CbvSrvHeap,
                Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
                cbvSrvHeapDesc.NumDescriptors);
        }

        // Create frame resources.
        for (int frameIdx = 0; frameIdx < SwapChainNumFrames; frameIdx++)
        {
            SwapChainFrameResources& frameRes = PerFrameResources[frameIdx];

            // Create a RTV for buffer in swap chain
            frameRes.SwapChainRtvHandle = RtvHandleProvider.AllocCpuHandle();

            hr = SwapChain->GetBuffer(frameIdx, IID_PPV_ARGS(&frameRes.SwapChainBuffer));
            VALIDATE((hr == ERROR_SUCCESS), "SwapChain GetBuffer failed");

            Device->CreateRenderTargetView(frameRes.SwapChainBuffer, nullptr, frameRes.SwapChainRtvHandle);

            for (int contextIdx = 0; contextIdx < DrawContext_Count; contextIdx++)
            {
                Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameRes.CommandAllocators[contextIdx]));
            }

            // Create an event handle to use for frame synchronization.
            frameRes.PresentFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            hr = HRESULT_FROM_WIN32(GetLastError());
            VALIDATE((hr == ERROR_SUCCESS), "CreateEvent failed");

            hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frameRes.PresentFenceRes));
            VALIDATE((hr == ERROR_SUCCESS), "CreateFence failed");

            frameRes.PresentFenceWaitValue = UINT64(-1);

            // Create the command lists
            for (int contextIdx = 0; contextIdx < DrawContext_Count; contextIdx++)
            {
                hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frameRes.CommandAllocators[contextIdx],
                    nullptr, IID_PPV_ARGS(&frameRes.CommandLists[contextIdx]));
                VALIDATE((hr == ERROR_SUCCESS), "CreateCommandList failed");
                frameRes.CommandLists[contextIdx]->Close();
                frameRes.CommandLists[contextIdx]->SetName(L"SwappedCommandList");

                frameRes.CommandListSubmitted[contextIdx] = true;   // to make sure we reset it properly first time thru

                hr = frameRes.CommandLists[contextIdx]->QueryInterface(IID_PPV_ARGS(&frameRes.m_dxrCommandList[contextIdx]));
                VALIDATE((hr == ERROR_SUCCESS), "CreateCommandList failed");
            }
        }

        // Main depth buffer
        //D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle = DsvHandleProvider.AllocCpuHandle();
        //MainDepthBuffer = new DepthBuffer(Device, DsvHandle, WinSizeW, WinSizeH, DepthFormat, 1);

        CreateRaytracingInterfaces();
        CreateRootSignatures();
        CreateRaytracingPipelineStateObject();
        BuildShaderTables();
        CreateRaytracingOutputResource(eyeWidth, eyeHeight);

        return true;
    }



    void CreateTextureArray(UINT maxWidth, UINT maxHeight, UINT textureCount, UINT mipLevels = 1)
    {
        // Create texture array resource
        D3D12_RESOURCE_DESC textureArrayDesc = {};
        textureArrayDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureArrayDesc.Alignment = 0;
        textureArrayDesc.Width = maxWidth;
        textureArrayDesc.Height = maxHeight;
        textureArrayDesc.DepthOrArraySize = textureCount;
        textureArrayDesc.MipLevels = mipLevels;
        textureArrayDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureArrayDesc.SampleDesc.Count = 1;
        textureArrayDesc.SampleDesc.Quality = 0;
        textureArrayDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        textureArrayDesc.Flags = D3D12_RESOURCE_FLAG_NONE;


        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM, maxWidth, maxHeight, textureCount, mipLevels);

        Device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&textureArray));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureArrayDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MipLevels = mipLevels;
        srvDesc.Texture2DArray.ArraySize = textureCount;

        D3D12_CPU_DESCRIPTOR_HANDLE texArrayCpuHandle = CbvSrvHandleProvider.AllocCpuHandle();
        Device->CreateShaderResourceView(textureArray.Get(), &srvDesc, texArrayCpuHandle);
        texArrayGpuHandle = CbvSrvHandleProvider.GpuHandleFromCpuHandle(texArrayCpuHandle);
    }



    void CopyTextureSubresource(
        ID3D12GraphicsCommandList* commandList,
        UINT destSubresourceIndex,
        ID3D12Resource* srcResource)
    {
        // Describe the destination texture array
        D3D12_RESOURCE_DESC destDesc = textureArray->GetDesc();
        UINT destMipLevels = destDesc.MipLevels;

        // Describe the source texture
        D3D12_RESOURCE_DESC srcDesc = srcResource->GetDesc();
        UINT srcMipLevels = srcDesc.MipLevels;

        // Transition source resource state to COPY_SOURCE
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            srcResource,
            D3D12_RESOURCE_STATE_COMMON, // Assuming it's in a general state initially
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->ResourceBarrier(1, &barrier);

        // Transition destination resource state to COPY_DEST
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            textureArray.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, // Assuming it's in a general state initially
            D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->ResourceBarrier(1, &barrier);

        // Loop through each mip level of the source and copy to corresponding mip level of destination
        for (UINT mipLevel = 0; mipLevel < min(srcMipLevels, destMipLevels); ++mipLevel)
        {
            // Describe the destination subresource (specific mip level of the array slice)
            D3D12_TEXTURE_COPY_LOCATION destLocation = {};
            destLocation.pResource = textureArray.Get();
            destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destLocation.SubresourceIndex = destSubresourceIndex * destMipLevels + mipLevel;

            // Describe the source subresource (specific mip level of the texture)
            D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
            srcLocation.pResource = srcResource;
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcLocation.SubresourceIndex = mipLevel;

            // Copy the texture data
            commandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);
        }

        // Transition destination resource state back to NON_PIXEL_SHADER_RESOURCE for raytracing shader
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            textureArray.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);

        // Transition source resource state back to COMMON
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            srcResource,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_COMMON);
        commandList->ResourceBarrier(1, &barrier);
    }

    class GpuUploadBuffer
    {
    public:
        ComPtr<ID3D12Resource> GetResource() { return m_resource; }

    protected:
        ComPtr<ID3D12Resource> m_resource;

        GpuUploadBuffer() {}
        ~GpuUploadBuffer()
        {
            if (m_resource.Get())
            {
                m_resource->Unmap(0, nullptr);
            }
        }

        void Allocate(ID3D12Device* device, UINT bufferSize, LPCWSTR resourceName = nullptr)
        {
            auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

            auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
            ThrowIfFailed(device->CreateCommittedResource(
                &uploadHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_resource)));
            m_resource->SetName(resourceName);
        }

        uint8_t* MapCpuWriteOnly()
        {
            uint8_t* mappedData;
            // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
            CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
            ThrowIfFailed(m_resource->Map(0, &readRange, reinterpret_cast<void**>(&mappedData)));
            return mappedData;
        }
    };

    // Shader record = {{Shader ID}, {RootArguments}}
    class ShaderRecord
    {
    public:
        ShaderRecord(void* pShaderIdentifier, UINT shaderIdentifierSize) :
            shaderIdentifier(pShaderIdentifier, shaderIdentifierSize)
        {
        }

        ShaderRecord(void* pShaderIdentifier, UINT shaderIdentifierSize, void* pLocalRootArguments, UINT localRootArgumentsSize) :
            shaderIdentifier(pShaderIdentifier, shaderIdentifierSize),
            localRootArguments(pLocalRootArguments, localRootArgumentsSize)
        {
        }

        void CopyTo(void* dest) const
        {
            uint8_t* byteDest = static_cast<uint8_t*>(dest);
            memcpy(byteDest, shaderIdentifier.ptr, shaderIdentifier.size);
            if (localRootArguments.ptr)
            {
                memcpy(byteDest + shaderIdentifier.size, localRootArguments.ptr, localRootArguments.size);
            }
        }

        struct PointerWithSize {
            void* ptr;
            UINT size;

            PointerWithSize() : ptr(nullptr), size(0) {}
            PointerWithSize(void* _ptr, UINT _size) : ptr(_ptr), size(_size) {};
        };
        PointerWithSize shaderIdentifier;
        PointerWithSize localRootArguments;
    };

    static inline UINT Align(UINT size, UINT alignment)
    {
        return (size + (alignment - 1)) & ~(alignment - 1);
    }


    // Shader table = {{ ShaderRecord 1}, {ShaderRecord 2}, ...}
    class ShaderTable : public GpuUploadBuffer
    {
        uint8_t* m_mappedShaderRecords;
        UINT m_shaderRecordSize;

        // Debug support
        std::wstring m_name;
        std::vector<ShaderRecord> m_shaderRecords;

        ShaderTable() {}
    public:
        ShaderTable(ID3D12Device* device, UINT numShaderRecords, UINT shaderRecordSize, LPCWSTR resourceName = nullptr)
            : m_name(resourceName)
        {
            m_shaderRecordSize = Align(shaderRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
            m_shaderRecords.reserve(numShaderRecords);
            UINT bufferSize = numShaderRecords * m_shaderRecordSize;
            Allocate(device, bufferSize, resourceName);
            m_mappedShaderRecords = MapCpuWriteOnly();
        }

        void push_back(const ShaderRecord& shaderRecord)
        {
            if (!(m_shaderRecords.size() < m_shaderRecords.capacity()))
                exit(1);
            m_shaderRecords.push_back(shaderRecord);
            shaderRecord.CopyTo(m_mappedShaderRecords);
            m_mappedShaderRecords += m_shaderRecordSize;
        }

        UINT GetShaderRecordSize() { return m_shaderRecordSize; }
    };


    // Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
    void BuildShaderTables()
    {

        void* rayGenShaderIdentifier;
        void* missShaderIdentifier;
        void* hitGroupShaderIdentifier;
        void* hitGroupShaderIdentifier1;

        auto GetShaderIdentifiers = [&](auto* stateObjectProperties)
            {
                rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_raygenShaderName);
                missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
                hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(c_triangleHitGroupName);
                hitGroupShaderIdentifier1 = stateObjectProperties->GetShaderIdentifier(c_aabbHitGroupName);
            };

        // Get shader identifiers.
        UINT shaderIdentifierSize;
        {
            ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
            ThrowIfFailed(m_dxrStateObject.As(&stateObjectProperties));
            GetShaderIdentifiers(stateObjectProperties.Get());
            shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        }

        // Ray gen shader table
        {
            //struct RootArguments {
            //    RayGenConstantBuffer cb;
            //} rootArguments;
            //rootArguments.cb = m_rayGenCB;

            UINT numShaderRecords = 1;
            //UINT shaderRecordSize = shaderIdentifierSize + sizeof(rootArguments);
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable rayGenShaderTable(Device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
            //rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));
            rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
            m_rayGenShaderTable = rayGenShaderTable.GetResource();
        }

        // Miss shader table
        {
            UINT numShaderRecords = 1;
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable missShaderTable(Device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
            missShaderTable.push_back(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
            m_missShaderTable = missShaderTable.GetResource();
        }

        // Hit group shader table
        {
            UINT numShaderRecords = 3;
            UINT shaderRecordSize = shaderIdentifierSize;
            ShaderTable hitGroupShaderTable(Device, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
            hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize));
            hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderIdentifier1, shaderIdentifierSize));
            m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
        }
    }

    //UINT AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor)
    //{
    //    auto descriptorHeapCpuBase = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    //    UINT descriptorIndexToUse = m_descriptorsAllocated++;
    //    *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, m_descriptorSize);
    //    return descriptorIndexToUse;
    //}

    void CreateRaytracingOutputResource(UINT width, UINT height)
    {
        //auto device = m_deviceResources->GetD3DDevice();
        auto backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        // Create the output resource. The dimensions and format should match the swap-chain.
        auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(backbufferFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        {
            ThrowIfFailed(Device->CreateCommittedResource(
                &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutputs[0])));
            //NAME_D3D12_OBJECT(m_raytracingOutput);
            D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle = CbvSrvHandleProvider.AllocCpuHandle(&m_raytracingOutputResourceUAVDescriptorHeapIndexs[0]);
            D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            Device->CreateUnorderedAccessView(m_raytracingOutputs[0].Get(), nullptr, &UAVDesc, uavDescriptorHandle);
            m_raytracingOutputResourceUAVGpuDescriptors[0] = CbvSrvHandleProvider.GpuHandleFromCpuHandle(uavDescriptorHandle);
        }

        {
            ThrowIfFailed(Device->CreateCommittedResource(
                &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingOutputs[1])));
            //NAME_D3D12_OBJECT(m_raytracingOutput);

            D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle = CbvSrvHandleProvider.AllocCpuHandle(&m_raytracingOutputResourceUAVDescriptorHeapIndexs[1]);
            D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            Device->CreateUnorderedAccessView(m_raytracingOutputs[1].Get(), nullptr, &UAVDesc, uavDescriptorHandle);
            m_raytracingOutputResourceUAVGpuDescriptors[1] = CbvSrvHandleProvider.GpuHandleFromCpuHandle(uavDescriptorHandle);
        }

        // Now create the depth buffers
        backbufferFormat = DXGI_FORMAT_R32_FLOAT;
        uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(backbufferFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        {
            ThrowIfFailed(Device->CreateCommittedResource(
                &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingDepthOutputs[0])));
            //NAME_D3D12_OBJECT(m_raytracingOutput);

            D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle = CbvSrvHandleProvider.AllocCpuHandle(&m_raytracingDepthOutputResourceUAVDescriptorHeapIndexs[0]);
            D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            Device->CreateUnorderedAccessView(m_raytracingDepthOutputs[0].Get(), nullptr, &UAVDesc, uavDescriptorHandle);
            m_raytracingDepthOutputResourceUAVGpuDescriptors[0] = CbvSrvHandleProvider.GpuHandleFromCpuHandle(uavDescriptorHandle);
        }

        {
            ThrowIfFailed(Device->CreateCommittedResource(
                &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &uavDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_raytracingDepthOutputs[1])));
            //NAME_D3D12_OBJECT(m_raytracingOutput);

            D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle = CbvSrvHandleProvider.AllocCpuHandle(&m_raytracingDepthOutputResourceUAVDescriptorHeapIndexs[1]);
            D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
            UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            Device->CreateUnorderedAccessView(m_raytracingDepthOutputs[1].Get(), nullptr, &UAVDesc, uavDescriptorHandle);
            m_raytracingDepthOutputResourceUAVGpuDescriptors[1] = CbvSrvHandleProvider.GpuHandleFromCpuHandle(uavDescriptorHandle);
        }
    }

    SwapChainFrameResources& CurrentFrameResources()
    {
        return PerFrameResources[SwapChainFrameIndex];
    }

    void SetActiveContext(DrawContext context)
    {
        ActiveContext = context;
    }

    void SetActiveEye(int eye)
    {
        ActiveEyeIndex = eye;
    }

    void SetAndClearRenderTarget(const D3D12_CPU_DESCRIPTOR_HANDLE* rendertarget, const D3D12_CPU_DESCRIPTOR_HANDLE* depthbuffer, float R = 0, float G = 0, float B = 0, float A = 1)
    {
        float black[] = { R, G, B, A }; // Important that alpha=0, if want pixels to be transparent, for manual layers
        CurrentFrameResources().CommandLists[ActiveContext]->OMSetRenderTargets(1, rendertarget, false, depthbuffer);
        CurrentFrameResources().CommandLists[ActiveContext]->ClearRenderTargetView(*rendertarget, black, 0, nullptr);
        if (depthbuffer)
            CurrentFrameResources().CommandLists[ActiveContext]->ClearDepthStencilView(*depthbuffer, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    }

    void SetAndClearRenderTarget(const D3D12_CPU_DESCRIPTOR_HANDLE& rendertarget, const D3D12_CPU_DESCRIPTOR_HANDLE& depthbuffer, float R = 0, float G = 0, float B = 0, float A = 1)
    {
        SetAndClearRenderTarget(&rendertarget, &depthbuffer, R, G, B, A);
    }

    void SetViewport(float vpX, float vpY, float vpW, float vpH)
    {
        D3D12_VIEWPORT D3Dvp;
        D3Dvp.Width = vpW;    D3Dvp.Height = vpH;
        D3Dvp.MinDepth = 0;   D3Dvp.MaxDepth = 1;
        D3Dvp.TopLeftX = vpX; D3Dvp.TopLeftY = vpY;
        CurrentFrameResources().CommandLists[ActiveContext]->RSSetViewports(1, &D3Dvp);

        D3D12_RECT scissorRect;
        scissorRect.left = static_cast<LONG>(vpX);
        scissorRect.right = static_cast<LONG>(vpX + vpW);
        scissorRect.top = static_cast<LONG>(vpY);
        scissorRect.bottom = static_cast<LONG>(vpY + vpH);

        CurrentFrameResources().CommandLists[ActiveContext]->RSSetScissorRects(1, &scissorRect);
    }

    bool HandleMessages(void)
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        // This is to provide a means to terminate after a maximum number of frames
        // to facilitate automated testing
#ifdef MAX_FRAMES_ACTIVE
        if (maxFrames > 0)
        {
            if (--maxFrames <= 0)
                Running = false;
        }
#endif
        return Running;
    }

    void Run(bool (*MainLoop)(bool retryCreate))
    {
        while (HandleMessages())
        {
            // true => we'll attempt to retry for ovrError_DisplayLost
            if (!MainLoop(true))
                break;
            // Sleep a bit before retrying to reduce CPU load while the HMD is disconnected
            Sleep(10);
        }
    }

    void ReleaseDevice()
    {
        if (SwapChain)
        {
            SwapChain->SetFullscreenState(FALSE, NULL);
            Release(SwapChain);
        }
        for (int i = 0; i < SwapChainNumFrames; i++)
        {
            SwapChainFrameResources& currFrameRes = PerFrameResources[i];
            Release(currFrameRes.SwapChainBuffer);
            RtvHandleProvider.FreeCpuHandle(currFrameRes.SwapChainRtvHandle);

            for (int contextIdx = 0; contextIdx < DrawContext_Count; contextIdx++)
            {
                Release(currFrameRes.CommandAllocators[contextIdx]);
                Release(currFrameRes.CommandLists[contextIdx]);
            }
            Release(currFrameRes.PresentFenceRes);

            CloseHandle(currFrameRes.PresentFenceEvent);
            currFrameRes.PresentFenceEvent = INVALID_HANDLE_VALUE;
        }
        Release(RtvHeap);
        Release(DsvHeap);
        Release(CbvSrvHeap);
        Release(CommandQueue);
        Release(Device);
        Release(DebugController);
        //delete MainDepthBuffer;
        //MainDepthBuffer = nullptr;
    }

    void InitCommandList(DrawContext context)
    {
        SwapChainFrameResources& currFrameRes = CurrentFrameResources();

        if (currFrameRes.CommandListSubmitted[context])
        {
            HRESULT hr = currFrameRes.CommandAllocators[context]->Reset();
            VALIDATE((hr == ERROR_SUCCESS), "CommandAllocator Reset failed");

            hr = currFrameRes.CommandLists[context]->Reset(currFrameRes.CommandAllocators[context], nullptr);
            VALIDATE((hr == ERROR_SUCCESS), "CommandList Reset failed");

            ID3D12DescriptorHeap* heaps[] = { CbvSrvHeap };
            currFrameRes.CommandLists[context]->SetDescriptorHeaps(_countof(heaps), heaps);

            currFrameRes.CommandListSubmitted[context] = false;
        }
    }

    void InitFrame(bool finalContextUsed)
    {
        for (int bufIdx = 0; bufIdx < DrawContext_Count; bufIdx++)
        {
            if (!finalContextUsed && bufIdx == DrawContext_Final)
                continue;

            InitCommandList((DrawContext)bufIdx);
        }

        SwapChainFrameResources& currFrameRes = CurrentFrameResources();

        if (finalContextUsed)
        {
            CD3DX12_RESOURCE_BARRIER rb = CD3DX12_RESOURCE_BARRIER::Transition(
                currFrameRes.SwapChainBuffer,
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            currFrameRes.CommandLists[DrawContext_Final]->ResourceBarrier(1, &rb);
        }
    }

    void WaitForPreviousFrame()
    {
        {
            DirectX12::SwapChainFrameResources& currFrameRes = CurrentFrameResources();

            // Signal and increment the fence value.
            currFrameRes.PresentFenceWaitValue = currFrameRes.PresentFenceValue;
            HRESULT hr = CommandQueue->Signal(currFrameRes.PresentFenceRes, currFrameRes.PresentFenceWaitValue);
            VALIDATE((hr == ERROR_SUCCESS), "CommandQueue Signal failed");
            currFrameRes.PresentFenceValue++;

            currFrameRes.PresentFenceRes->SetEventOnCompletion(currFrameRes.PresentFenceWaitValue, currFrameRes.PresentFenceEvent);
            VALIDATE((hr == ERROR_SUCCESS), "SetEventOnCompletion failed");
        }

        // goto next frame index and start waiting for the fence - ideally we don't wait at all
        {
            SwapChainFrameIndex = (SwapChainFrameIndex + 1) % SwapChainNumFrames;
            DirectX12::SwapChainFrameResources& currFrameRes = CurrentFrameResources();

            // Wait until the previous frame is finished.
            if (currFrameRes.PresentFenceWaitValue != -1) // -1 means we never kicked off this frame
            {
                WaitForSingleObject(currFrameRes.PresentFenceEvent, INFINITE);
            }


                VALIDATE((SwapChainFrameIndex == SwapChain->GetCurrentBackBufferIndex()), "Swap chain index validation failed");
            //SwapChainFrameIndex = SwapChain->GetCurrentBackBufferIndex();
        }
    }

    void WaitForGpu() noexcept
    {
        DirectX12::SwapChainFrameResources& currFrameRes = CurrentFrameResources();

        // Signal and increment the fence value.
        currFrameRes.PresentFenceWaitValue = currFrameRes.PresentFenceValue;
        HRESULT hr = CommandQueue->Signal(currFrameRes.PresentFenceRes, currFrameRes.PresentFenceWaitValue);
        VALIDATE((hr == ERROR_SUCCESS), "CommandQueue Signal failed");
        currFrameRes.PresentFenceValue++;

        currFrameRes.PresentFenceRes->SetEventOnCompletion(currFrameRes.PresentFenceWaitValue, currFrameRes.PresentFenceEvent);
        VALIDATE((hr == ERROR_SUCCESS), "SetEventOnCompletion failed");

        // Wait until the GPU has finished processing commands for the current frame.
        WaitForSingleObject(currFrameRes.PresentFenceEvent, INFINITE);

    }

    void SubmitCommandList(DrawContext context)
    {
        DirectX12::SwapChainFrameResources& currFrameRes = CurrentFrameResources();

        HRESULT hr = currFrameRes.CommandLists[context]->Close();
        VALIDATE((hr == ERROR_SUCCESS), "CommandList Close failed");

        ID3D12CommandList* ppCommandLists[] = { currFrameRes.CommandLists[context] };
        CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        currFrameRes.CommandListSubmitted[context] = true;
    }

    void SubmitCommandListAndPresent(bool finalContextUsed)
    {
        if (finalContextUsed)
        {
            DirectX12::SwapChainFrameResources& currFrameRes = CurrentFrameResources();

            VALIDATE(ActiveContext == DrawContext_Final, "Invalid context set before Present");

            // Indicate that the back buffer will now be used to present.
            CD3DX12_RESOURCE_BARRIER rb = CD3DX12_RESOURCE_BARRIER::Transition(
                currFrameRes.SwapChainBuffer,
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PRESENT);
            currFrameRes.CommandLists[ActiveContext]->ResourceBarrier(1, &rb);

            SubmitCommandList(DrawContext_Final);

            // Present the frame.
            HRESULT hr = SwapChain->Present(0, 0);
            VALIDATE((hr == ERROR_SUCCESS), "SwapChain Present failed");

            WaitForPreviousFrame();
        }


        InitFrame(finalContextUsed);
    }

    // Copy the raytracing output to the backbuffer.
    void CopyRaytracingOutputToBackbuffer(ID3D12Resource* renderTarget, ID3D12Resource* depthTarget)
    {
        //auto commandList = m_deviceResources->GetCommandList();
        //auto renderTarget = m_deviceResources->GetRenderTarget();

        DirectX12::SwapChainFrameResources& currFrameRes = CurrentFrameResources();

        D3D12_RESOURCE_BARRIER preCopyBarriers[4];
        preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(depthTarget, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_DEST);
        preCopyBarriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutputs[ActiveContext].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        preCopyBarriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingDepthOutputs[ActiveContext].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        currFrameRes.CommandLists[ActiveContext]->ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);

        currFrameRes.CommandLists[ActiveContext]->CopyResource(renderTarget, m_raytracingOutputs[ActiveContext].Get());
        currFrameRes.CommandLists[ActiveContext]->CopyResource(depthTarget, m_raytracingDepthOutputs[ActiveContext].Get());

        D3D12_RESOURCE_BARRIER postCopyBarriers[4];
        postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(depthTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        postCopyBarriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingOutputs[ActiveContext].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        postCopyBarriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(m_raytracingDepthOutputs[ActiveContext].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        currFrameRes.CommandLists[ActiveContext]->ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
    }

    // Update camera matrices passed into the shader.
    void UpdateCameraMatrices()
    {
        //m_sceneCB[SwapChainFrameIndex].cameraPosition = { 1,1,1,1 };
        //float fovAngleY = 45.0f;
        //XMMATRIX view = XMMatrixLookAtLH({ 1,1,1,1 }, { 0,0,0,1 }, { 0,1,0,1 });
        //XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(fovAngleY), m_aspectRatio, 1.0f, 125.0f);
        //XMMATRIX viewProj = view * proj;

        //m_sceneCB[SwapChainFrameIndex].projectionToWorld = XMMatrixInverse(nullptr, viewProj);
    }



};

// global DX12 state
static struct DirectX12 DIRECTX;

//------------------------------------------------------------
struct Texture
{
    ID3D12Resource* TextureRes;
    CD3DX12_CPU_DESCRIPTOR_HANDLE SrvHandle;
    CD3DX12_CPU_DESCRIPTOR_HANDLE RtvHandle;

    int SizeW, SizeH;
    UINT MipLevels;

    enum AutoFill { AUTO_WHITE = 1, AUTO_WALL, AUTO_FLOOR, AUTO_CEILING, AUTO_GRID, AUTO_GRADE_256 };
    const static UINT numTextures = 6;
    static UINT maxWidth;
    static UINT maxHeight;

private:
    Texture()
    {
    }

public:
    void Init(int sizeW, int sizeH, bool rendertarget, UINT mipLevels, int sampleCount)
    {
        maxWidth = sizeW > maxWidth ? sizeW : maxWidth;
        maxHeight = sizeH > maxHeight ? sizeH : maxHeight;
        SizeW = sizeW;
        SizeH = sizeH;
        MipLevels = mipLevels;

        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = UINT16(MipLevels);
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Width = SizeW;
        textureDesc.Height = SizeH;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = sampleCount;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        if (rendertarget) textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearVal = { DXGI_FORMAT_R8G8B8A8_UNORM, { { 0.0f, 0.0f, 0.0f, 1.0f } } };

        CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);
        HRESULT hr = DIRECTX.Device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            rendertarget ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST,
            rendertarget ? &clearVal : nullptr,
            IID_PPV_ARGS(&TextureRes));
        VALIDATE((hr == ERROR_SUCCESS), "CreateCommittedResource failed");

        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = MipLevels;
        SrvHandle = DIRECTX.CbvSrvHandleProvider.AllocCpuHandle();
        DIRECTX.Device->CreateShaderResourceView(TextureRes, &srvDesc, SrvHandle);

        if (rendertarget)
        {
            RtvHandle = DIRECTX.RtvHandleProvider.AllocCpuHandle();
            DIRECTX.Device->CreateRenderTargetView(TextureRes, nullptr, RtvHandle);
        }
    }

#if 0
    Texture(int sizeW, int sizeH, bool rendertarget, int mipLevels = 1, int sampleCount = 1)
    {
        Init(sizeW, sizeH, rendertarget, mipLevels, sampleCount);
    }
#endif

    Texture(bool rendertarget, int sizeW, int sizeH, AutoFill autoFillData = (AutoFill)0, int sampleCount = 1)
    {
        Init(sizeW, sizeH, rendertarget, 1, sampleCount);
        if (!rendertarget && autoFillData)
            AutoFillTexture(autoFillData);
    }

    ~Texture()
    {
        DIRECTX.CbvSrvHandleProvider.FreeCpuHandle(SrvHandle);
        Release(TextureRes);
        DIRECTX.RtvHandleProvider.FreeCpuHandle(RtvHandle);
    }

    Texture(const char* filePath)
    {
        int channels;
        int width, height;
        uint8_t* data = stbi_load(filePath, &width, &height, &channels, 4);
        ThrowIfFalse(data != nullptr);
        uint32_t* pixels = (uint32_t*)malloc(sizeof(uint32_t) * width * height);
        ThrowIfFalse(pixels != nullptr);

        for (int i = 0; i < width * height; ++i)
        {
            uint8_t r = data[i * 4];
            uint8_t g = data[i * 4 + 1];
            uint8_t b = data[i * 4 + 2];
            uint8_t a = data[i * 4 + 3];
            pixels[i] = (a << 24) | (b << 16) | (g << 8) | r;
        }

        stbi_image_free(data);

        Init(width, height, false, 1, 1);
        FillTexture(pixels);
    }

    void FillTexture(uint32_t* pix)
    {
        HRESULT hr;
        std::vector<ID3D12Resource*> textureUploadHeap(MipLevels);

        // Make local ones, because will be reducing them
        int sizeW = SizeW;
        int sizeH = SizeH;
        for (UINT level = 0; level < MipLevels; level++)
        {
            // Push data into the texture
            {
                DirectX12::SwapChainFrameResources& currFrameRes = DIRECTX.CurrentFrameResources();
                currFrameRes.CommandLists[DrawContext_Final]->Reset(currFrameRes.CommandAllocators[DrawContext_Final], nullptr);

                {
                    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(TextureRes, 0, 1);

                    // Create the GPU upload buffer.
                    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_UPLOAD);
                    CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
                    hr = DIRECTX.Device->CreateCommittedResource(
                        &heapProp,
                        D3D12_HEAP_FLAG_NONE,
                        &resDesc,
                        D3D12_RESOURCE_STATE_GENERIC_READ,
                        nullptr,
                        IID_PPV_ARGS(&textureUploadHeap[level]));
                    VALIDATE((hr == ERROR_SUCCESS), "CreateCommittedResource upload failed");

                    // Copy data to the intermediate upload heap and then schedule a copy from the upload heap to the Texture2D
                    uint8_t* textureByte = (uint8_t*)pix;

                    D3D12_SUBRESOURCE_DATA textureData = {};
                    textureData.pData = textureByte;
                    textureData.RowPitch = sizeW * sizeof(uint32_t);
                    textureData.SlicePitch = textureData.RowPitch * sizeH;

                    UpdateSubresources(currFrameRes.CommandLists[DrawContext_Final], TextureRes, textureUploadHeap[level], 0, level, 1, &textureData);

                    // transition resource on last mip-level
                    if (level == MipLevels - 1)
                    {
                        CD3DX12_RESOURCE_BARRIER resBar = CD3DX12_RESOURCE_BARRIER::Transition(TextureRes,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_COMMON);
                        currFrameRes.CommandLists[DrawContext_Final]->ResourceBarrier(1, &resBar);
                    }
                }


                // Close the command list and execute it to begin the initial GPU setup.
                hr = currFrameRes.CommandLists[DrawContext_Final]->Close();
                VALIDATE((hr == ERROR_SUCCESS), "CommandList Close failed");

                ID3D12CommandList* ppCommandLists[] = { currFrameRes.CommandLists[DrawContext_Final] };
                DIRECTX.CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

                // Create synchronization objects and wait until assets have been uploaded to the GPU.
                {
                    // Wait for the command list to execute; we are reusing the same command
                    // list in our main loop but for now, we just want to wait for setup to
                    // complete before continuing.
                    {
                        // Signal and increment the fence value.
                        currFrameRes.PresentFenceWaitValue = currFrameRes.PresentFenceValue;
                        hr = DIRECTX.CommandQueue->Signal(currFrameRes.PresentFenceRes, currFrameRes.PresentFenceWaitValue);
                        VALIDATE((hr == ERROR_SUCCESS), "CommandQueue Signal failed");
                        currFrameRes.PresentFenceValue++;

                        // Wait until the copy is finished.
                        if (currFrameRes.PresentFenceRes->GetCompletedValue() < currFrameRes.PresentFenceWaitValue)
                        {
                            hr = currFrameRes.PresentFenceRes->SetEventOnCompletion(currFrameRes.PresentFenceWaitValue, currFrameRes.PresentFenceEvent);
                            VALIDATE((hr == ERROR_SUCCESS), "SetEventOnCompletion failed");
                            WaitForSingleObject(currFrameRes.PresentFenceEvent, INFINITE);
                        }
                    }
                }
            }

            for (int j = 0; j < (sizeH & ~1); j += 2)
            {
                uint8_t* psrc = (uint8_t*)pix + (sizeW * j * 4);
                uint8_t* pdest = (uint8_t*)pix + (sizeW * j);
                for (int i = 0; i < sizeW >> 1; i++, psrc += 8, pdest += 4)
                {
                    pdest[0] = (((int)psrc[0]) + psrc[4] + psrc[sizeW * 4 + 0] + psrc[sizeW * 4 + 4]) >> 2;
                    pdest[1] = (((int)psrc[1]) + psrc[5] + psrc[sizeW * 4 + 1] + psrc[sizeW * 4 + 5]) >> 2;
                    pdest[2] = (((int)psrc[2]) + psrc[6] + psrc[sizeW * 4 + 2] + psrc[sizeW * 4 + 6]) >> 2;
                    pdest[3] = (((int)psrc[3]) + psrc[7] + psrc[sizeW * 4 + 3] + psrc[sizeW * 4 + 7]) >> 2;
                }
            }
            sizeW >>= 1;
            sizeH >>= 1;
        }

        for (size_t i = 0; i < textureUploadHeap.size(); ++i)
        {
            Release(textureUploadHeap[i]);
        }
    }

    static void ConvertToSRGB(uint32_t* linear)
    {
        uint32_t drgb[3];
        for (int k = 0; k < 3; k++)
        {
            float rgb = ((float)((*linear >> (k * 8)) & 0xff)) / 255.0f;
            rgb = pow(rgb, 2.2f);
            drgb[k] = (uint32_t)(rgb * 255.0f);
        }
        *linear = (*linear & 0xff000000) + (drgb[2] << 16) + (drgb[1] << 8) + (drgb[0] << 0);
    }

    void AutoFillTexture(AutoFill autoFillData)
    {
        uint32_t* pix = (uint32_t*)malloc(sizeof(uint32_t) * SizeW * SizeH);
        for (int j = 0; j < SizeH; j++)
        {
            for (int i = 0; i < SizeW; i++)
            {
                uint32_t* curr = &pix[j * SizeW + i];
                switch (autoFillData)
                {
                case(AUTO_WALL): *curr = (((j / 4 & 15) == 0) || (((i / 4 & 15) == 0) && ((((i / 4 & 31) == 0) ^ ((j / 4 >> 4) & 1)) == 0))) ? 0xff3c3c3c : 0xffb4b4b4; break;
                case(AUTO_FLOOR): *curr = (((i >> 7) ^ (j >> 7)) & 1) ? 0xffb4b4b4 : 0xff505050; break;
                case(AUTO_CEILING): *curr = (i / 4 == 0 || j / 4 == 0) ? 0xff505050 : 0xffb4b4b4; break;
                case(AUTO_WHITE): *curr = 0xffffffff; break;
                case(AUTO_GRADE_256): *curr = 0xff000000 + i * 0x010101; break;
                case(AUTO_GRID): *curr = (i < 4) || (i > (SizeW - 5)) || (j < 4) || (j > (SizeH - 5)) ? 0xffffffff : 0xff000000; break;
                default: *curr = 0xffffffff; break;
                }
            }
        }
        FillTexture(pix);
        free(pix);
    }

};

UINT Texture::maxHeight = 0;
UINT Texture::maxWidth = 0;

//-----------------------------------------------------
struct Material
{
    UINT TexIndex;

    Material()
    {
        this->TexIndex = 0;
    }
    Material(UINT TexIndex)
    {
        this->TexIndex = TexIndex;
    }
};

//-----------------------------------------------------
struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;

    Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v)
    {
        position = { x, y, z };
        normal = { nx, ny, nz };
        uv = { u, v };
    }

    bool operator==(const Vertex& other) const {
        return position.x == other.position.x && position.y == other.position.y && position.z == other.position.z &&
            normal.x == other.normal.x && normal.y == other.normal.y && normal.z == other.normal.z &&
            uv.x == other.uv.x && uv.y == other.uv.y;
    }
};


#include <unordered_map>
//-----------------------------------------------------
struct VertexBuffer
{
    DirectX12::D3DBuffer indexBuffer;
    DirectX12::D3DBuffer vertexBuffer;
    std::vector<Vertex> globalVertices;
    std::vector<UINT> globalIndices;
    std::vector<std::pair<UINT, UINT>> globalStartVBIndices;
    std::vector<std::pair<UINT, UINT>> globalStartIBIndices;
    std::vector<ID3D12Resource*> m_globalBottomLevelAccelerationStructures;
    UINT numVertexBuffers = 0;

    void InitBox()
    {

        UINT indices[] =
        {
            0, 2, 1,
            0, 3, 2,

            4, 5, 6,
            4, 6, 7,

            8, 9, 10,
            8, 10, 11,

            12, 14, 13,
            12, 15, 14,

            16, 18, 17,
            16, 19, 18,

            20, 21, 22,
            20, 22, 23,
        };


        Vertex vertices[] =
        {
            // Back face
            { -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,  0.0f, 0.0f},
            { 0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,  1.0f, 0.0f},
            { 0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,   1.0f, 1.0f},
            { -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,  0.0f, 1.0f},

            // Front face
            { -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f , 0.0f, 0.0f},
            { 0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f  ,1.0f, 0.0f},
            { 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f   ,1.0f, 1.0f},
            { -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f  ,0.0f, 1.0f},

            // Right face
            { 0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f , 0.0f, 0.0f},
            { 0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f  ,0.0f, 1.0f},
            { 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f   ,1.0f, 1.0f},
            { 0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f  ,1.0f, 0.0f},

            // Left face
            { -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f , 0.0f, 0.0f},
            { -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f  ,0.0f, 1.0f},
            { -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f   ,1.0f, 1.0f},
            { -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f  ,1.0f, 0.0f},

            // Top face
            { -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f , 0.0f, 0.0f},
            { 0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f  ,1.0f, 0.0f},
            { 0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f   ,1.0f, 1.0f},
            { -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f  ,0.0f, 1.0f},

            // Bottom face
            { -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f , 0.0f, 0.0f},
            { 0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f  ,1.0f, 0.0f},
            { 0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f   ,1.0f, 1.0f},
            { -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f  ,0.0f, 1.0f},
        };

        DIRECTX.AllocateUploadBuffer(DIRECTX.Device, vertices, sizeof(vertices), &vertexBuffer.resource);
        DIRECTX.AllocateUploadBuffer(DIRECTX.Device, indices, sizeof(indices), &indexBuffer.resource);

        // Vertex buffer is passed to the shader along with index buffer as a descriptor table.
        // Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
        UINT descriptorIndexIB = DIRECTX.CreateBufferSRV(&indexBuffer, sizeof(indices) / 4, 0);
        UINT descriptorIndexVB = DIRECTX.CreateBufferSRV(&vertexBuffer, ARRAYSIZE(vertices), sizeof(vertices[0]));
        ThrowIfFalse(descriptorIndexVB == descriptorIndexIB + 1);
    }

    std::pair<UINT, UINT> AddBoxToGlobal()
    {
        UINT indices[] =
        {
            0, 2, 1,
            0, 3, 2,

            4, 5, 6,
            4, 6, 7,

            8, 9, 10,
            8, 10, 11,

            12, 14, 13,
            12, 15, 14,

            16, 18, 17,
            16, 19, 18,

            20, 21, 22,
            20, 22, 23,
        };


        Vertex vertices[] =
        {
            // Back face
            { -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,  0.0f, 0.0f},
            { 0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f,  1.0f, 0.0f},
            { 0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,   1.0f, 1.0f},
            { -0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f,  0.0f, 1.0f},

            // Front face
            { -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f , 0.0f, 0.0f},
            { 0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f  ,1.0f, 0.0f},
            { 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f   ,1.0f, 1.0f},
            { -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f  ,0.0f, 1.0f},

            // Right face
            { 0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f , 0.0f, 0.0f},
            { 0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f  ,0.0f, 1.0f},
            { 0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f   ,1.0f, 1.0f},
            { 0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f  ,1.0f, 0.0f},

            // Left face
            { -0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f , 0.0f, 0.0f},
            { -0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f  ,0.0f, 1.0f},
            { -0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f   ,1.0f, 1.0f},
            { -0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f  ,1.0f, 0.0f},

            // Top face
            { -0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f , 0.0f, 0.0f},
            { 0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f  ,1.0f, 0.0f},
            { 0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f   ,1.0f, 1.0f},
            { -0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f  ,0.0f, 1.0f},

            // Bottom face
            { -0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f , 0.0f, 0.0f},
            { 0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f  ,1.0f, 0.0f},
            { 0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f   ,1.0f, 1.0f},
            { -0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f  ,0.0f, 1.0f},
        };


        std::pair<UINT, UINT> startIndices;
        startIndices.second = globalIndices.size();
        startIndices.first = globalVertices.size();

        std::pair<UINT, UINT> ibStartIndices;
        ibStartIndices.first = globalIndices.size();
        for (int i = 0; i < 36; i++)
        {
            globalIndices.push_back(indices[i]);
        }

        std::pair<UINT, UINT> vbStartIndices;
        vbStartIndices.first = globalVertices.size();
        for (int i = 0; i < 24; i++)
        {
            globalVertices.push_back(vertices[i]);
        }

        ibStartIndices.second = globalIndices.size() - ibStartIndices.first;
        vbStartIndices.second = globalVertices.size() - vbStartIndices.first;
        globalStartIBIndices.push_back(ibStartIndices);
        globalStartVBIndices.push_back(vbStartIndices);
        numVertexBuffers++;
        return startIndices;
    }

    std::pair<UINT, UINT> AddVerticeAndIndicesToGlobal(std::vector<Vertex> vertices, std::vector<UINT> indices)
    {
        std::pair<UINT, UINT> startIndices;
        startIndices.second = globalIndices.size();
        startIndices.first = globalVertices.size();

        std::pair<UINT, UINT> ibStartIndices;
        ibStartIndices.first = globalIndices.size();
        for (int i = 0; i < indices.size(); i++)
        {
            globalIndices.push_back(indices[i]);
        }

        std::pair<UINT, UINT> vbStartIndices;
        vbStartIndices.first = globalVertices.size();
        for (int i = 0; i < vertices.size(); i++)
        {
            globalVertices.push_back(vertices[i]);
        }

        ibStartIndices.second = globalIndices.size() - ibStartIndices.first;
        vbStartIndices.second = globalVertices.size() - vbStartIndices.first;
        globalStartIBIndices.push_back(ibStartIndices);
        globalStartVBIndices.push_back(vbStartIndices);
        numVertexBuffers++;
        return startIndices;
    }

    void InitGlobalVertexBuffers()
    {
        DIRECTX.AllocateUploadBuffer(DIRECTX.Device, globalVertices.data(), globalVertices.size()*sizeof(Vertex), &vertexBuffer.resource);
        DIRECTX.AllocateUploadBuffer(DIRECTX.Device, globalIndices.data(), globalIndices.size()*sizeof(UINT), &indexBuffer.resource);

        // Vertex buffer is passed to the shader along with index buffer as a descriptor table.
        // Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
        UINT descriptorIndexIB = DIRECTX.CreateBufferSRV(&indexBuffer, globalIndices.size(), 0);
        UINT descriptorIndexVB = DIRECTX.CreateBufferSRV(&vertexBuffer, globalVertices.size(), sizeof(Vertex));
        ThrowIfFalse(descriptorIndexVB == descriptorIndexIB + 1);
    }

    void InitGlobalBottomLevelAccelerationObject()
    {


        m_globalBottomLevelAccelerationStructures.reserve(globalStartIBIndices.size());
        ID3D12Resource* dummyResource;

        for (int i = 0; i < globalStartIBIndices.size(); i++)
        {
            // Reset the command list for the acceleration structure construction.
            DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->Reset(DIRECTX.CurrentFrameResources().CommandAllocators[DrawContext_Final], nullptr);


            m_globalBottomLevelAccelerationStructures.push_back(dummyResource);

            D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
            geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometryDesc.Triangles.IndexBuffer = indexBuffer.resource->GetGPUVirtualAddress() + sizeof(UINT)*globalStartIBIndices[i].first;
            geometryDesc.Triangles.IndexCount = globalStartIBIndices[i].second;
            geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            geometryDesc.Triangles.Transform3x4 = 0;
            geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometryDesc.Triangles.VertexCount = globalStartVBIndices[i].second;
            geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.resource->GetGPUVirtualAddress() + sizeof(Vertex) * globalStartVBIndices[i].first;
            geometryDesc.Triangles.VertexBuffer.StrideInBytes = (sizeof(Vertex));

            // Mark the geometry as opaque. 
            // PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
            // Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
            geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

            // Get required sizes for an acceleration structure.
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = {};
            bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            bottomLevelInputs.Flags = buildFlags;
            bottomLevelInputs.NumDescs = 1;
            bottomLevelInputs.pGeometryDescs = &geometryDesc;

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
            DIRECTX.m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
            ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

            ID3D12Resource* scratchResource;
            DIRECTX.AllocateUAVBuffer(DIRECTX.Device, bottomLevelPrebuildInfo.ScratchDataSizeInBytes, &scratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            // Allocate resources for acceleration structures.
            // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
            // Default heap is OK since the application doesn�t need CPU read/write access to them. 
            // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
            // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
            //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
            //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
            {
                D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

                DIRECTX.AllocateUAVBuffer(DIRECTX.Device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_globalBottomLevelAccelerationStructures[i], initialResourceState, L"BottomLevelAccelerationStructure");
            }

            // Bottom Level Acceleration Structure desc
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
            {
                bottomLevelBuildDesc.Inputs = bottomLevelInputs;
                bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
                bottomLevelBuildDesc.DestAccelerationStructureData = m_globalBottomLevelAccelerationStructures[i]->GetGPUVirtualAddress();
            }


            auto BuildAccelerationStructure = [&](auto* raytracingCommandList)
                {
                    raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
                    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_globalBottomLevelAccelerationStructures[i]);
                    DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->ResourceBarrier(1, &barrier);
                };

            // Build acceleration structure.
            BuildAccelerationStructure(DIRECTX.CurrentFrameResources().m_dxrCommandList[DrawContext_Final].Get());


            // Kick off acceleration structure construction.
            DIRECTX.SubmitCommandList(DrawContext_Final);

            // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
            DIRECTX.WaitForGpu();
            scratchResource->Release();
        }
    }

    void InitBottomLevelAccelerationObject()
    {
        // Reset the command list for the acceleration structure construction.
        DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->Reset(DIRECTX.CurrentFrameResources().CommandAllocators[DrawContext_Final], nullptr);

        ID3D12Resource* pResource;
        m_globalBottomLevelAccelerationStructures.push_back(pResource);

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Triangles.IndexBuffer = indexBuffer.resource->GetGPUVirtualAddress();
        geometryDesc.Triangles.IndexCount = static_cast<UINT>(indexBuffer.resource->GetDesc().Width) / sizeof(UINT);
        geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometryDesc.Triangles.Transform3x4 = 0;
        geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.VertexCount = static_cast<UINT>(vertexBuffer.resource->GetDesc().Width) / (sizeof(Vertex));
        geometryDesc.Triangles.VertexBuffer.StartAddress = vertexBuffer.resource->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = (sizeof(Vertex));

        // Mark the geometry as opaque. 
        // PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
        // Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        // Get required sizes for an acceleration structure.
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = {};
        bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottomLevelInputs.Flags = buildFlags;
        bottomLevelInputs.NumDescs = 1;
        bottomLevelInputs.pGeometryDescs = &geometryDesc;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
        DIRECTX.m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
        ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

        ID3D12Resource* scratchResource;
        DIRECTX.AllocateUAVBuffer(DIRECTX.Device, bottomLevelPrebuildInfo.ScratchDataSizeInBytes, &scratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

        // Allocate resources for acceleration structures.
        // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
        // Default heap is OK since the application doesn�t need CPU read/write access to them. 
        // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
        // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
        //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
        //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
        {
            D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

            DIRECTX.AllocateUAVBuffer(DIRECTX.Device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_globalBottomLevelAccelerationStructures[0], initialResourceState, L"BottomLevelAccelerationStructure");
        }

        // Bottom Level Acceleration Structure desc
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
        {
            bottomLevelBuildDesc.Inputs = bottomLevelInputs;
            bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
            bottomLevelBuildDesc.DestAccelerationStructureData = m_globalBottomLevelAccelerationStructures[0]->GetGPUVirtualAddress();
        }


        auto BuildAccelerationStructure = [&](auto* raytracingCommandList)
            {
                raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
                CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_globalBottomLevelAccelerationStructures[0]);
                DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->ResourceBarrier(1, &barrier);
            };

        // Build acceleration structure.
        BuildAccelerationStructure(DIRECTX.CurrentFrameResources().m_dxrCommandList[DrawContext_Final].Get());

        // Kick off acceleration structure construction.
        DIRECTX.SubmitCommandList(DrawContext_Final);

        // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
        DIRECTX.WaitForGpu();
        scratchResource->Release();
    }

    void InitAABBBottomLevelAccelerationObject()
    {
        // Reset the command list for the acceleration structure construction.
        DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->Reset(DIRECTX.CurrentFrameResources().CommandAllocators[DrawContext_Final], nullptr);

        ID3D12Resource* pResource;
        m_globalBottomLevelAccelerationStructures.push_back(pResource);

        D3D12_RAYTRACING_AABB aabb = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
        DIRECTX.AllocateUploadBuffer(DIRECTX.Device, &aabb, sizeof(D3D12_RAYTRACING_AABB), &vertexBuffer.resource);

        D3D12_RAYTRACING_GEOMETRY_DESC aabbDescTemplate = {};
        aabbDescTemplate.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
        aabbDescTemplate.AABBs.AABBCount = 1;
        aabbDescTemplate.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
        aabbDescTemplate.AABBs.AABBs.StartAddress = vertexBuffer.resource->GetGPUVirtualAddress();
        aabbDescTemplate.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;


        // Get required sizes for an acceleration structure.
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = {};
        bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bottomLevelInputs.Flags = buildFlags;
        bottomLevelInputs.NumDescs = 1;
        bottomLevelInputs.pGeometryDescs = &aabbDescTemplate;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
        DIRECTX.m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelPrebuildInfo);
        ThrowIfFalse(bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

        ID3D12Resource* scratchResource;
        DIRECTX.AllocateUAVBuffer(DIRECTX.Device, bottomLevelPrebuildInfo.ScratchDataSizeInBytes, &scratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

        {
            D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

            DIRECTX.AllocateUAVBuffer(DIRECTX.Device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_globalBottomLevelAccelerationStructures[0], initialResourceState, L"BottomLevelAccelerationStructure");
        }

        // Bottom Level Acceleration Structure desc
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
        {
            bottomLevelBuildDesc.Inputs = bottomLevelInputs;
            bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();
            bottomLevelBuildDesc.DestAccelerationStructureData = m_globalBottomLevelAccelerationStructures[0]->GetGPUVirtualAddress();
        }


        auto BuildAccelerationStructure = [&](auto* raytracingCommandList)
            {
                raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);
                CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_globalBottomLevelAccelerationStructures[0]);
                DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->ResourceBarrier(1, &barrier);
            };

        // Build acceleration structure.
        BuildAccelerationStructure(DIRECTX.CurrentFrameResources().m_dxrCommandList[DrawContext_Final].Get());

        // Kick off acceleration structure construction.
        DIRECTX.SubmitCommandList(DrawContext_Final);

        // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
        DIRECTX.WaitForGpu();
        scratchResource->Release();
    }

    std::pair<UINT, UINT> AddGlobalObj(const std::string& filename)
	{
		std::vector<UINT> indices;
		tinyobj::ObjReaderConfig reader_config;
		reader_config.mtl_search_path = ""; // Path to material files

		tinyobj::ObjReader reader;

		if (!reader.ParseFromFile(filename, reader_config)) {
			if (!reader.Error().empty()) {
				//std::cerr << "TinyObjReader: " << reader.Error();
			}
			exit(1);
		}

		if (!reader.Warning().empty()) {
			//std::cout << "TinyObjReader: " << reader.Warning();
		}

		auto& attrib = reader.GetAttrib();
		auto& shapes = reader.GetShapes();
		auto& materials = reader.GetMaterials();

		std::vector<Vertex> vertices;

		// Create a hash function for the Vertex
		struct VertexHash {
			std::size_t operator()(const Vertex& vertex) const {
				return ((std::hash<float>()(vertex.position.x) ^ (std::hash<float>()(vertex.position.y) << 1)) >> 1) ^
					((std::hash<float>()(vertex.position.z) ^ (std::hash<float>()(vertex.normal.x) << 1)) >> 1) ^
					((std::hash<float>()(vertex.normal.y) ^ (std::hash<float>()(vertex.normal.z) << 1)) >> 1) ^
					((std::hash<float>()(vertex.uv.x) ^ (std::hash<float>()(vertex.uv.y) << 1)) >> 1);
			}
		};

		std::unordered_map<Vertex, unsigned int, VertexHash> uniqueVertices;

		// Loop over shapes
		for (const auto& shape : shapes) {
			// Loop over faces (polygons)
			for (const auto& index : shape.mesh.indices) {
				Vertex vertex = { 0,0,0,0,0,0,0,0 };

				vertex.position.x = attrib.vertices[3 * index.vertex_index + 0];
				vertex.position.y = attrib.vertices[3 * index.vertex_index + 1];
				vertex.position.z = attrib.vertices[3 * index.vertex_index + 2];

				if (index.normal_index >= 0) {
					vertex.normal.x = attrib.normals[3 * index.normal_index + 0];
					vertex.normal.y = attrib.normals[3 * index.normal_index + 1];
					vertex.normal.z = attrib.normals[3 * index.normal_index + 2];
				}

				if (index.texcoord_index >= 0) {
					vertex.uv.x = attrib.texcoords[2 * index.texcoord_index + 0];
					vertex.uv.y = attrib.texcoords[2 * index.texcoord_index + 1];
				}

				// Check if the vertex is unique
				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = static_cast<unsigned int>(vertices.size());
					vertices.push_back(vertex);
				}

				indices.push_back(uniqueVertices[vertex]);
			}
		}

        std::pair<UINT, UINT> startIndices;
        startIndices.second = globalIndices.size();
        startIndices.first = globalVertices.size();

        std::pair<UINT, UINT> ibStartIndices;
        ibStartIndices.first = globalIndices.size();
        for (int i = 0; i < indices.size(); i++)
        {
            globalIndices.push_back(indices[i]);
        }

        std::pair<UINT, UINT> vbStartIndices;
        vbStartIndices.first = globalVertices.size();
        for (int i = 0; i < vertices.size(); i++)
        {
            globalVertices.push_back(vertices[i]);
        }

        ibStartIndices.second = globalIndices.size() - ibStartIndices.first;
        vbStartIndices.second = globalVertices.size() - vbStartIndices.first;
        globalStartIBIndices.push_back(ibStartIndices);
        globalStartVBIndices.push_back(vbStartIndices);
        numVertexBuffers++;
        return startIndices;
	}
};
//-----------------------------------------------------
struct ModelComponent
{
    XMMATRIX transform;
    XMFLOAT4 color;
    UINT instanceIndex;
    UINT vbIndex;
    VertexBuffer* pVertexBuffer;
    Material material;
    static UINT numInstances;
    bool scaleUvs = false;
    UINT hitShaderIndex;
    UINT layerMask;

    void SetIdentity()
    {
        transform = XMMatrixIdentity();
    }

    ModelComponent()
    {
        SetIdentity();
        GetNormalizedRGB(0xffffffff);
        pVertexBuffer = nullptr;
        instanceIndex = numInstances++;
        vbIndex = 0;
        hitShaderIndex = 0;
        layerMask = ~0;
    }

    ModelComponent(Material mat, XMMATRIX transform, VertexBuffer* pVertexBuffer, UINT vbIndex, UINT hitShaderIndex, UINT layerMask)
    {
        SetIdentity();
        GetNormalizedRGB(0xffffffff);
        instanceIndex = numInstances++;
        material = mat;
        this->pVertexBuffer = pVertexBuffer;
        this->vbIndex = vbIndex;
        this->hitShaderIndex = hitShaderIndex;
        this->layerMask = layerMask;
        this->transform = transform;
    }

    void SetAsBox(float x1, float y1, float z1, float x2, float y2, float z2)
    {
        // Set position
        transform.r[3].m128_f32[0] = (x1 + x2) * 0.5f;
        transform.r[3].m128_f32[1] = (y1 + y2) * 0.5f;
        transform.r[3].m128_f32[2] = (z1 + z2) * 0.5f;

        // Set scale
        transform.r[0].m128_f32[0] = fabsf(x2 - x1);
        transform.r[1].m128_f32[1] = fabsf(y2 - y1);
        transform.r[2].m128_f32[2] = fabsf(z2 - z1);
    }

    void GetNormalizedRGB(uint32_t color) {
        // Extract individual color components
        uint8_t r = (color >> 16) & 0xFF; // Red component
        uint8_t g = (color >> 8) & 0xFF;  // Green component
        uint8_t b = color & 0xFF;         // Blue component

        // Normalize to range [0, 1]
        this->color.x = static_cast<float>(r) / 255.0f;
        this->color.y = static_cast<float>(g) / 255.0f;
        this->color.z = static_cast<float>(b) / 255.0f;
        this->color.w = 1.0f;
    }

    ModelComponent(float x1, float y1, float z1, float x2, float y2, float z2, uint32_t color, VertexBuffer* pVertexBuffer)
    {
        SetIdentity();
        SetAsBox(x1, y1, z1, x2, y2, z2);
        GetNormalizedRGB(color);
        this->pVertexBuffer = pVertexBuffer;
        vbIndex = 0;
        instanceIndex = numInstances++;
        scaleUvs = true;
        hitShaderIndex = 0;
        layerMask = ~0;
    }

};

UINT ModelComponent::numInstances = 0;

//-----------------------------------------------------
struct Model
{
    std::vector<ModelComponent> components;
    XMMATRIX transform;


    Model() 
    {
        transform = XMMatrixIdentity();
    }
    
    Model(std::vector<ModelComponent> components, Material material)
    {
        this->components = components;
        for (int i = 0; i < components.size(); i++)
            this->components[i].material = material;
        transform = XMMatrixIdentity();
    }

    //Model(std::vector<ModelComponent> components)
    //{
    //    this->components = components;
    //    transform = XMMatrixIdentity();
    //}

    //void Translate(XMFLOAT3 translation)
    //{
    //    for (int i = 0; i < components.size(); i++)
    //    {
    //        components[i].transform.m[0][3] += translation.x;
    //        components[i].transform.m[1][3] += translation.y;
    //        components[i].transform.m[2][3] += translation.z;
    //    }
    //}

    //void ApplyTransformation(XMMATRIX transformation)
    //{
    //    for (int i = 0; i < components.size(); i++)
    //    {
    //        components[i].transform = XMMatrixMultiply(transformation, components[i].transform);
    //    }
    //    transform = XMMatrixMultiply(transformation, transform);
    //}

    void SetPosition(XMFLOAT3 position)
    {
        transform.r[3].m128_f32[0] = position.x;
        transform.r[3].m128_f32[1] = position.y;
        transform.r[3].m128_f32[2] = position.z;
    }

    static std::pair<Model, std::vector<Texture*>> InitFromObj(std::string filePath, std::string texturesDir, VertexBuffer& vertexBuffer, UINT textureOffset)
    {
        Model model;

        tinyobj::ObjReaderConfig reader_config;
        reader_config.mtl_search_path = ""; // Path to material files

        tinyobj::ObjReader reader;

        if (!reader.ParseFromFile(filePath, reader_config)) {
            if (!reader.Error().empty()) {
            }
            exit(1);
        }

        if (!reader.Warning().empty()) {
            //std::cout << "TinyObjReader: " << reader.Warning();
        }

        auto& attrib = reader.GetAttrib();
        auto& shapes = reader.GetShapes();
        std::vector<tinyobj::material_t> materials = reader.GetMaterials();

        std::vector<Texture*> materialTextures;
        std::unordered_map<int, int> materialToTextureIndex;

        for (int i = 0; i < materials.size(); i++)
        {
            if (materials[i].diffuse_texname.size() > 0)
            {
                std::string textPath = texturesDir + "/" + materials[i].diffuse_texname;
                materialTextures.push_back(new Texture(textPath.c_str()));
                materialToTextureIndex[i] = materialTextures.size() - 1 + textureOffset; // Map material ID to texture index
            }
        }

        // Create a hash function for the Vertex
        struct VertexHash {
            std::size_t operator()(const Vertex& vertex) const {
                return ((std::hash<float>()(vertex.position.x) ^ (std::hash<float>()(vertex.position.y) << 1)) >> 1) ^
                    ((std::hash<float>()(vertex.position.z) ^ (std::hash<float>()(vertex.normal.x) << 1)) >> 1) ^
                    ((std::hash<float>()(vertex.normal.y) ^ (std::hash<float>()(vertex.normal.z) << 1)) >> 1) ^
                    ((std::hash<float>()(vertex.uv.x) ^ (std::hash<float>()(vertex.uv.y) << 1)) >> 1);
            }
        };

        std::unordered_map<Vertex, unsigned int, VertexHash> uniqueVertices;

        // Loop over shapes
        for (const auto& shape : shapes) {
            std::vector<UINT> indices;
            std::vector<Vertex> vertices;
            int currentMaterialId = -1;

            size_t index_offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
                size_t fv = shape.mesh.num_face_vertices[f];
                int materialId = shape.mesh.material_ids[f];

                // Check if the material has changed
                if (materialId != currentMaterialId) {
                    if (!indices.empty()) {
                        ModelComponent component;
                        component.pVertexBuffer = &vertexBuffer;
                        component.layerMask = ~0;
                        component.hitShaderIndex = 0;
                        component.vbIndex = vertexBuffer.globalStartVBIndices.size();
                        if (materialToTextureIndex.find(currentMaterialId) != materialToTextureIndex.end()) {
                            component.material.TexIndex = materialToTextureIndex[currentMaterialId];
                        }
                        else {
                            component.material.TexIndex = -1; // No texture
                        }
                        vertexBuffer.AddVerticeAndIndicesToGlobal(vertices, indices);
                        model.components.push_back(component);

                        indices.clear();
                        vertices.clear();
                        uniqueVertices.clear();
                    }
                    currentMaterialId = materialId;
                }

                // Loop over vertices in the face
                for (size_t v = 0; v < fv; v++) {
                    tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                    Vertex vertex = { 0, 0, 0, 0, 0, 0, 0, 0 };

                    vertex.position.x = attrib.vertices[3 * idx.vertex_index + 0];
                    vertex.position.y = attrib.vertices[3 * idx.vertex_index + 1];
                    vertex.position.z = attrib.vertices[3 * idx.vertex_index + 2];

                    if (idx.normal_index >= 0) {
                        vertex.normal.x = attrib.normals[3 * idx.normal_index + 0];
                        vertex.normal.y = attrib.normals[3 * idx.normal_index + 1];
                        vertex.normal.z = attrib.normals[3 * idx.normal_index + 2];
                    }

                    if (idx.texcoord_index >= 0) {
                        vertex.uv.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                        vertex.uv.y = attrib.texcoords[2 * idx.texcoord_index + 1];
                    }

                    // Check if the vertex is unique
                    if (uniqueVertices.count(vertex) == 0) {
                        uniqueVertices[vertex] = static_cast<unsigned int>(vertices.size());
                        vertices.push_back(vertex);
                    }

                    indices.push_back(uniqueVertices[vertex]);
                }
                index_offset += fv;
            }

            // Add the remaining vertices and indices to the model
            if (!indices.empty()) {
                ModelComponent component;
                component.pVertexBuffer = &vertexBuffer;
                component.layerMask = ~0;
                component.hitShaderIndex = 0;
                component.vbIndex = vertexBuffer.globalStartVBIndices.size();
                if (materialToTextureIndex.find(currentMaterialId) != materialToTextureIndex.end()) {
                    component.material.TexIndex = materialToTextureIndex[currentMaterialId];
                }
                else {
                    component.material.TexIndex = -1; // No texture
                }
                vertexBuffer.AddVerticeAndIndicesToGlobal(vertices, indices);
                model.components.push_back(component);
            }
        }

        std::pair<Model, std::vector<Texture*>> retVal;
        retVal.first = model;
        retVal.second = materialTextures;
        return retVal;
    }


    
};

#define MAX_INSTANCES 400
#define MAX_VBS 400
#define MAX_TEXTURES 60
//-------------------------------------------------------------------------
struct Scene
{
    struct TextureData
    {
        UINT width;
        UINT height;
        XMFLOAT2 padding;
    };

    struct InstanceData
    {
        UINT textureId;
        UINT vertexBufferId;
        XMFLOAT2 uv;
        XMFLOAT4 color;
    };

    struct Light
    {
        XMVECTOR position;
        XMFLOAT3 color;
        float intensity;
    };

    struct VertexBufferData
    {
        UINT vertexOffset;
        UINT indexOffset;
        XMFLOAT2 padding;
    };

    struct alignas(256) SceneConstantBuffer
    {
        XMMATRIX projectionToWorld;
        XMVECTOR eyePosition;
        InstanceData instanceData[MAX_INSTANCES];
        Light lights[4];
        VertexBufferData vertexBufferDatas[MAX_VBS];
        TextureData textureResources[MAX_TEXTURES];
    };

    SceneConstantBuffer* m_mappedConstantData[2];
    ComPtr<ID3D12Resource>       m_perFrameConstants[2];
    SceneConstantBuffer m_sceneCB[2][DIRECTX.SwapChainNumFrames];
    InstanceData instanceData[MAX_INSTANCES];
    UINT numInstances;
    Light lights[4];
    VertexBufferData vertexBufferDatas[MAX_VBS];
    TextureData textureResources[MAX_TEXTURES];

    VertexBuffer globalVertexBuffer;
    VertexBuffer aabbVertexBuffer;


    std::vector<Model> models;

    ID3D12Resource* instanceDescs;
    D3D12_RAYTRACING_INSTANCE_DESC* instanceDescsArray;
    ID3D12Resource* ScratchAccelerationStructureData;

    // Acceleration structure
    ComPtr<ID3D12Resource> m_topLevelAccelerationStructure;

    // Textures
    std::vector<Texture*> textures;



    void UpdateInstancePosition(UINT instanceIndex, XMFLOAT3 position)
    {
        instanceDescsArray[instanceIndex].Transform[0][3] = position.x;
        instanceDescsArray[instanceIndex].Transform[1][3] = position.y;
        instanceDescsArray[instanceIndex].Transform[2][3] = position.z;
    }


    void UpdateModelPosition(UINT modelIndex, XMFLOAT3 position)
    {
        if (modelIndex < models.size())
        {
            models[modelIndex].SetPosition(position);
            XMMATRIX transform;
            for (int i = 0; i < models[modelIndex].components.size(); i++)
            {
                transform = XMMatrixMultiply(models[modelIndex].transform, models[modelIndex].components[i].transform);
                UpdateInstanceTransform(models[modelIndex].components[i].instanceIndex, transform);
            }
        }
    }

    void ApplyModelTransformation(UINT modelIndex, XMMATRIX transformation)
    {
        if (modelIndex < models.size())
        {
            models[modelIndex].transform = XMMatrixMultiply(transformation, models[modelIndex].transform);
            XMMATRIX transform;
            for (int i = 0; i < models[modelIndex].components.size(); i++)
            {
                transform = XMMatrixMultiply(models[modelIndex].transform, models[modelIndex].components[i].transform);
                UpdateInstanceTransform(models[modelIndex].components[i].instanceIndex, transform);
            }
        }
    }

    void UpdateModelTransformation(UINT modelIndex, XMMATRIX transform)
    {
        if (modelIndex < models.size())
        {
            models[modelIndex].transform = transform;
            XMMATRIX overallTransform;
            for (int i = 0; i < models[modelIndex].components.size(); i++)
            {
                overallTransform = XMMatrixMultiply(models[modelIndex].components[i].transform, models[modelIndex].transform);
                UpdateInstanceTransform(models[modelIndex].components[i].instanceIndex, overallTransform);
            }
        }
    }

    void UpdateInstanceTransform(UINT instanceIndex, XMMATRIX transformMatrix)
    {
        transformMatrix = XMMatrixTranspose(transformMatrix);
        for (int i = 0; i < 3; i++)
        {
            memcpy(instanceDescsArray[instanceIndex].Transform[i], transformMatrix.r[i].m128_f32, 4 * sizeof(float));
        }
    }

    void UpdateInstanceDescs()
    {
        DIRECTX.UpdateUploadBuffer(DIRECTX.Device, instanceDescsArray, numInstances * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &instanceDescs);
    }

    void UpdateTLAS()
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
        topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        topLevelInputs.NumDescs = numInstances;
        topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        // Top Level Acceleration Structure desc
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
        {
            topLevelInputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
            topLevelBuildDesc.Inputs = topLevelInputs;
            topLevelBuildDesc.DestAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
            topLevelBuildDesc.ScratchAccelerationStructureData = ScratchAccelerationStructureData->GetGPUVirtualAddress();
        }

        DIRECTX.CurrentFrameResources().m_dxrCommandList[DrawContext_Final].Get()->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = m_topLevelAccelerationStructure.Get();
        DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->ResourceBarrier(1, &uavBarrier);

    }

    // Build acceleration structures needed for raytracing.
    void BuildAccelerationStructures()
    {
       
        // Reset the command list for the acceleration structure construction.
        DIRECTX.CurrentFrameResources().CommandLists[DrawContext_Final]->Reset(DIRECTX.CurrentFrameResources().CommandAllocators[DrawContext_Final], nullptr);

        //numInstances++;
        instanceDescsArray = new D3D12_RAYTRACING_INSTANCE_DESC[numInstances];
        UINT index = 0;
        for (int i = 0; i < models.size(); ++i) {
            for (int j = 0; j < models[i].components.size(); j++)
            {
                instanceDescsArray[index] = D3D12_RAYTRACING_INSTANCE_DESC();
                float x = models[i].components[j].transform.r[0].m128_f32[0];
                float y = models[i].components[j].transform.r[1].m128_f32[1];
                float z = models[i].components[j].transform.r[2].m128_f32[2];
                //for (int x = 0; x < 3; x++)
                //{
                //    for (int y = 0; y < 4; y++)
                //    {
                //        instanceDescsArray[index].Transform[x][y] = models[i].components[j].transform.r[x].m128_f32[y];
                //    }
                //}
                XMMATRIX transform = XMMatrixMultiply(models[i].transform, models[i].components[j].transform);
                UpdateInstanceTransform(index, transform);
                instanceDescsArray[index].InstanceMask = models[i].components[j].layerMask;
                instanceDescsArray[index].InstanceID = index; // Assign unique instance IDs
                instanceDescsArray[index].AccelerationStructure = models[i].components[j].pVertexBuffer->m_globalBottomLevelAccelerationStructures[models[i].components[j].vbIndex]->GetGPUVirtualAddress();
                instanceDescsArray[index].InstanceContributionToHitGroupIndex = models[i].components[j].hitShaderIndex;
                instanceData[index].vertexBufferId = models[i].components[j].vbIndex;
                instanceData[index].textureId = models[i].components[j].material.TexIndex;

                if (models[i].components[j].scaleUvs)
                {
                    if (x >= z && y >= z)
                    {
                        instanceData[index].uv.x = x;
                        instanceData[index].uv.y = y;
                    }
                    else if (y >= x && z >= x)
                    {
                        instanceData[index].uv.x = z;
                        instanceData[index].uv.y = y;
                    }
                    else
                    {
                        instanceData[index].uv.x = x;
                        instanceData[index].uv.y = z;
                    }
                }
                else
                {
                    instanceData[index].uv.x = 1.0f;
                    instanceData[index].uv.y = 1.0f;
                }
                instanceData[index].color = models[i].components[j].color;
                index++;
            }
        }

        DIRECTX.AllocateUploadBuffer(DIRECTX.Device, instanceDescsArray, numInstances*sizeof(D3D12_RAYTRACING_INSTANCE_DESC), &instanceDescs, L"InstanceDescs");



        // Get required sizes for an acceleration structure.
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
        topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        topLevelInputs.Flags = buildFlags;
        topLevelInputs.NumDescs = numInstances;
        topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
        DIRECTX.m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
        ThrowIfFalse(topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);


        
        DIRECTX.AllocateUAVBuffer(DIRECTX.Device, topLevelPrebuildInfo.ScratchDataSizeInBytes, &ScratchAccelerationStructureData, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

        // Allocate resources for acceleration structures.
        // Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
        // Default heap is OK since the application doesn�t need CPU read/write access to them. 
        // The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
        // and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
        //  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
        //  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
        {
            D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

            // DIRECTX.AllocateUAVBuffer(DIRECTX.Device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &DIRECTX.m_bottomLevelAccelerationStructure, initialResourceState, L"BottomLevelAccelerationStructure");
            DIRECTX.AllocateUAVBuffer(DIRECTX.Device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_topLevelAccelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
        }

        // Top Level Acceleration Structure desc
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
        {
            topLevelInputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress();
            topLevelBuildDesc.Inputs = topLevelInputs;
            topLevelBuildDesc.DestAccelerationStructureData = m_topLevelAccelerationStructure->GetGPUVirtualAddress();
            topLevelBuildDesc.ScratchAccelerationStructureData = ScratchAccelerationStructureData->GetGPUVirtualAddress();
        }


         DIRECTX.CurrentFrameResources().m_dxrCommandList[DrawContext_Final].Get()->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);


        // Kick off acceleration structure construction.
        DIRECTX.SubmitCommandList(DrawContext_Final);

        // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
        DIRECTX.WaitForGpu();
        //scratchResource->Release();
    }

    void RebuildAccelerationStructure()
    {

    }



    void DoRaytracing(XMMATRIX projectionToWorld, XMVECTOR eyePos)
    {
        DirectX12::SwapChainFrameResources& currFrameRes = DIRECTX.CurrentFrameResources();
        //FrameResources& currConstantRes = PerFrameRes[DIRECTX.SwapChainFrameIndex][DIRECTX.ActiveEyeIndex];

        auto DispatchRays = [&](auto* commandList, auto* stateObject, auto* dispatchDesc)
            {
                // Since each shader table has only one shader record, the stride is same as the size.
                dispatchDesc->HitGroupTable.StartAddress = DIRECTX.m_hitGroupShaderTable->GetGPUVirtualAddress();
                dispatchDesc->HitGroupTable.SizeInBytes = DIRECTX.m_hitGroupShaderTable->GetDesc().Width;
                // We don't have any root signiture so the stride is just the identifier size
                dispatchDesc->HitGroupTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
                dispatchDesc->MissShaderTable.StartAddress = DIRECTX.m_missShaderTable->GetGPUVirtualAddress();
                dispatchDesc->MissShaderTable.SizeInBytes = DIRECTX.m_missShaderTable->GetDesc().Width;
                // We don't have any root signiture so the stride is just the identifier size
                dispatchDesc->MissShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
                dispatchDesc->RayGenerationShaderRecord.StartAddress = DIRECTX.m_rayGenShaderTable->GetGPUVirtualAddress();
                dispatchDesc->RayGenerationShaderRecord.SizeInBytes = DIRECTX.m_rayGenShaderTable->GetDesc().Width;
                dispatchDesc->Width = DIRECTX.eyeWidth;
                dispatchDesc->Height = DIRECTX.eyeHeight;
                dispatchDesc->Depth = 1;
                commandList->SetPipelineState1(stateObject);
                commandList->DispatchRays(dispatchDesc);
            };

        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetComputeRootSignature(DIRECTX.m_raytracingGlobalRootSignature.Get());

        m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].projectionToWorld = projectionToWorld;
        m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].eyePosition = eyePos;
        m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].textureResources[0].width = 256;
        m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].textureResources[0].height = 256;
        memcpy(&m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].instanceData[0], &instanceData[0], numInstances * sizeof(InstanceData));
        memcpy(&m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].lights[0], &lights[0], 4 * sizeof(Light));
        memcpy(&m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].vertexBufferDatas[0], &vertexBufferDatas[0], MAX_VBS * sizeof(VertexBufferData));
        memcpy(&m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex].textureResources[0], &textureResources[0], MAX_TEXTURES * sizeof(TextureData));

        // Copy the updated scene constant buffer to GPU.
        memcpy(&m_mappedConstantData[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex], &m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex], 
            sizeof(m_sceneCB[DIRECTX.ActiveContext][DIRECTX.SwapChainFrameIndex]));
        auto cbGpuAddress = m_perFrameConstants[DIRECTX.ActiveContext]->GetGPUVirtualAddress() + DIRECTX.SwapChainFrameIndex * sizeof(m_mappedConstantData[0][0]);
        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetComputeRootConstantBufferView(DirectX12::GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);

        // Bind the heaps, acceleration structure and dispatch rays.    
        D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetDescriptorHeaps(1, &DIRECTX.CbvSrvHeap);
        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetComputeRootDescriptorTable(DirectX12::GlobalRootSignatureParams::OutputViewSlot, DIRECTX.m_raytracingOutputResourceUAVGpuDescriptors[DIRECTX.ActiveContext]);
        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetComputeRootDescriptorTable(DirectX12::GlobalRootSignatureParams::OutputDepthSlot, DIRECTX.m_raytracingDepthOutputResourceUAVGpuDescriptors[DIRECTX.ActiveContext]);
        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetComputeRootDescriptorTable(DirectX12::GlobalRootSignatureParams::VertexBufferSlot, globalVertexBuffer.indexBuffer.gpuDescriptorHandle);
        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetComputeRootDescriptorTable(DirectX12::GlobalRootSignatureParams::TextureSlot, DIRECTX.texArrayGpuHandle);
        currFrameRes.CommandLists[DIRECTX.ActiveContext]->SetComputeRootShaderResourceView(DirectX12::GlobalRootSignatureParams::AccelerationStructureSlot, m_topLevelAccelerationStructure->GetGPUVirtualAddress());
       DispatchRays(currFrameRes.m_dxrCommandList[DIRECTX.ActiveContext].Get(), DIRECTX.m_dxrStateObject.Get(), &dispatchDesc);
    }

    virtual void Init(bool includeIntensiveGPUobject)
    {
        std::vector<ModelComponent> transforms;
        numInstances = 0;

        transforms.push_back(ModelComponent(0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0xff404040, &globalVertexBuffer));
        models.push_back(Model(transforms, Material(Texture::AUTO_CEILING - 1)));
        
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
       // aabbVertexBuffer.InitAABBBottomLevelAccelerationObject();


        BuildAccelerationStructures();
    }

    // Create constant buffers.
    void CreateConstantBuffers()
    {

        auto frameCount = DIRECTX.SwapChainNumFrames;

        // Create the constant buffer memory and map the CPU and GPU addresses
        const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

        // Allocate one constant buffer per frame, since it gets updated every frame.
        size_t cbSize = frameCount * sizeof(SceneConstantBuffer);
        const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

        ThrowIfFailed(DIRECTX.Device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &constantBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_perFrameConstants[0])));

        // Map the constant buffer and cache its heap pointers.
        // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_perFrameConstants[0]->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData[0])));

        ThrowIfFailed(DIRECTX.Device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &constantBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_perFrameConstants[1])));

        // Map the constant buffer and cache its heap pointers.
        // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
        readRange = CD3DX12_RANGE(0, 0);        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_perFrameConstants[1]->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData[1])));
    }

    void PushBackTexture(Texture* pTexture)
    {
        textureResources[textures.size()].width = pTexture->SizeW;
        textureResources[textures.size()].height = pTexture->SizeH;
        textures.push_back(pTexture);
    }

    void CreateDefaultTextures()
    {
        for (int i = 0; i < Texture::numTextures; i++)
        {
            PushBackTexture(new Texture(false, 256, 256, (Texture::AutoFill)(i + 1)));
        }
    }

    void InitTexturesToTexArray()
    {
        DIRECTX.CreateTextureArray(Texture::maxWidth, Texture::maxHeight, textures.size());
        for (int i = 0; i < textures.size(); i++)
        {
            DIRECTX.CopyTextureSubresource(DIRECTX.CurrentFrameResources().CommandLists[0], i, textures[i]->TextureRes);
        }
    }

    Model AddObjModelToScene(std::string fileName, std::string texturesDir)
    {
        UINT numModels = globalVertexBuffer.numVertexBuffers;
        std::pair<Model, std::vector<Texture*>> modelAndTextures = Model::InitFromObj(fileName, texturesDir, globalVertexBuffer, textures.size());
        for (int i = numModels; i < globalVertexBuffer.numVertexBuffers; i++)
        {
            vertexBufferDatas[i].vertexOffset = globalVertexBuffer.globalStartVBIndices[i].first;
            vertexBufferDatas[i].indexOffset = globalVertexBuffer.globalStartIBIndices[i].first;
        }
        for (int i = 0; i < modelAndTextures.second.size(); i++)
        {
            PushBackTexture(modelAndTextures.second[i]);
        }
        return modelAndTextures.first;
    }

    Scene() : numInstances(0) {}
    Scene(bool includeIntensiveGPUobject) :
        numInstances(0)
    {
        CreateDefaultTextures();
        CreateConstantBuffers();
        std::pair<UINT, UINT> indexData = globalVertexBuffer.AddBoxToGlobal();
        vertexBufferDatas[globalVertexBuffer.numVertexBuffers - 1].vertexOffset = indexData.first;
        vertexBufferDatas[globalVertexBuffer.numVertexBuffers - 1].indexOffset = indexData.second;
    }
    void Release()
    {

    }
    ~Scene()
    {
        Release();
    }
};

//-----------------------------------------------------------
struct Camera
{
    XMFLOAT4 Pos;
    XMFLOAT4 Rot;
    Camera() {};
    Camera(XMVECTOR pos, XMVECTOR rot)
    {
        XMStoreFloat4(&Pos, pos);
        XMStoreFloat4(&Rot, rot);
    }

    XMMATRIX GetViewMatrix()
    {
        XMVECTOR posVec = XMLoadFloat4(&Pos);
        XMVECTOR rotVec = XMLoadFloat4(&Rot);
        XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), rotVec);
        return(XMMatrixLookAtRH(posVec, XMVectorAdd(posVec, forward), XMVector3Rotate(XMVectorSet(0, 1, 0, 0), rotVec)));
    }

    XMVECTOR GetPosVec() { return XMLoadFloat4(&Pos); }
    XMVECTOR GetRotVec() { return XMLoadFloat4(&Rot); }

    void SetPosVec(XMVECTOR posVec) { XMStoreFloat4(&Pos, posVec); }
    void SetRotVec(XMVECTOR rotVec) { XMStoreFloat4(&Rot, rotVec); }
};

//----------------------------------------------------
struct Utility
{
    void Output(const char* fnt, ...)
    {
        static char string_text[1000];
        va_list args; va_start(args, fnt);
        vsprintf_s(string_text, fnt, args);
        va_end(args);
        OutputDebugStringA(string_text);
    }
} static Util;

#endif // OVR_Win32_DirectX12AppUtil_h
