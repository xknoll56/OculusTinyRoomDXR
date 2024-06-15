//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

struct Viewport
{
    float left;
    float top;
    float right;
    float bottom;
};

struct Texture
{
    uint width;
    uint height;
};

struct InstanceData
{
    uint textureId; 
    float u;
    float v;
    float3 color;
};
#define MAX_INSTANCES 400

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float4 eyePosition;  
    InstanceData instanceData[MAX_INSTANCES];
    Texture texture[6];
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 texcoord;
};



RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
RWTexture2D<float> DepthTarget : register(u1);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
//ConstantBuffer<RayGenConstantBuffer> g_rayGenCB : register(b0);

StructuredBuffer<uint> Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);

Texture2DArray<float4> g_texture : register(t3);

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
    float depth;
    float3 rayOrigin;
    float3 rayDirection;
};

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.eyePosition.xyz;
    direction = normalize(world.xyz - origin);
}


[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;
    
    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0), 0, ray.Origin, ray.Direction };
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    // Write the raytraced color to the output texture.
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
    
        // Write the depth to the depth texture.
    DepthTarget[DispatchRaysIndex().xy] = payload.depth;
}

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's barycentrics.
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    
    uint indicesPerTriangle = 3;
    uint baseIndex = PrimitiveIndex() * indicesPerTriangle;

    uint3 indices;
    indices.x = Indices[baseIndex];
    indices.y = Indices[baseIndex + 1];
    indices.z = Indices[baseIndex + 2];


    float3 vertexNormals[3] =
    {
        Vertices[indices.x].normal,
        Vertices[indices.y].normal,
        Vertices[indices.z].normal 
    };
    
    float2 vertexTexcoords[3] =
    {
        Vertices[indices.x].texcoord,
        Vertices[indices.y].texcoord,
        Vertices[indices.z].texcoord
    };

    float3 triangleNormal = HitAttribute(vertexNormals, attr);
    float2 interpolatedTexcoord = vertexTexcoords[0] * barycentrics.x + vertexTexcoords[1] * barycentrics.y + vertexTexcoords[2] * barycentrics.z;
    // Assuming interpolatedTexcoord ranges from (0,0) to (1,1)
    float2 texcoord = interpolatedTexcoord.xy;
    texcoord.x *= g_sceneCB.instanceData[InstanceID()].u;
    texcoord.y *= g_sceneCB.instanceData[InstanceID()].v;
    // Perform wrap manually
    texcoord = frac(texcoord); // Keep the fractional part only, effectively wrapping the texture
    
    // Calculate the distance from the camera/eye to the intersection point
    float3 hitPoint = payload.rayOrigin + RayTCurrent() * payload.rayDirection;
    float distance = length(hitPoint - g_sceneCB.eyePosition);

    
    // Sample the texture
    float4 sampledColor = g_texture.Load(int4(texcoord.x * g_sceneCB.texture[0].width, texcoord.y * g_sceneCB.texture[0].height, g_sceneCB.instanceData[InstanceID()].textureId, 0));
    //float4 sampledColor = g_texture.Load(int4(texcoord.x * g_sceneCB.texture[0].width, texcoord.y * g_sceneCB.texture[0].height, 2, 0));
    
    payload.color = sampledColor * float4(g_sceneCB.instanceData[InstanceID()].color, 1.0f);
    payload.color *= 2.0f;

     // Calculate depth as the distance from the eye position to the hit point
    payload.depth = RayTCurrent();
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.color = float4(0, 0, 0, 1);
    payload.depth = 10000.0f;
}

#endif // RAYTRACING_HLSL