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

struct Light
{
    float3 position;
    float3 color;
    float intensity;
};

#define MAX_INSTANCES 400

struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float4 eyePosition;
    InstanceData instanceData[MAX_INSTANCES];
    Light lights[4];
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
    float3 origin;
    float3 direction;
    uint recursionDepth;
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
    RayPayload payload = { float4(0, 0, 0, 0), 0, ray.Origin, ray.Direction, 0 };
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

bool IsInShadow(float3 lightDir, float3 hitPoint, float maxDist)
{
    RayDesc shadowRay;
    shadowRay.Origin = hitPoint;
    shadowRay.Direction = lightDir;
    shadowRay.TMin = 0.001f;
    shadowRay.TMax = maxDist;

    RayPayload payload = { float4(0, 0, 0, 0), 0, shadowRay.Origin, shadowRay.Direction, 1 };

    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, shadowRay, payload);

    return payload.depth < shadowRay.TMax;
}

[shader("closesthit")]
void SimpleHitShader(inout RayPayload payload, in MyAttributes attr)
{
    payload.depth = RayTCurrent();
    payload.color = float4(1, 1, 0, 1);

}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    if (payload.recursionDepth == 1 && InstanceID() != 6)
    {
        payload.depth = RayTCurrent();
    }
    else
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
   
    
    // Sample the texture
        float4 sampledColor = g_texture.Load(int4(texcoord.x * g_sceneCB.texture[0].width, texcoord.y * g_sceneCB.texture[0].height, g_sceneCB.instanceData[InstanceID()].textureId, 0));
    //float4 sampledColor = g_texture.Load(int4(texcoord.x * g_sceneCB.texture[0].width, texcoord.y * g_sceneCB.texture[0].height, 2, 0));
    
        float4 instanceColor = saturate(float4(g_sceneCB.instanceData[InstanceID()].color, 1.0f) * 2.0f);
        float4 color = sampledColor * instanceColor;
    

    //float3 diffuse = 0.5 * NdotL;
    
     // Calculate depth as the distance from the eye position to the hit point
        payload.depth = RayTCurrent();
    
        float3 hitPoint = payload.origin + payload.direction * payload.depth;
        float3 lightDir = normalize(g_sceneCB.lights[0].position - hitPoint);
        float maxDist = length(g_sceneCB.lights[0].position - hitPoint);
    
        float lighting = 0.05f;
        if ((payload.recursionDepth == 0) &&
            !IsInShadow(lightDir, hitPoint, maxDist))
        {
    // Access the instance transformation matrix
            float3x4 instanceTransform = ObjectToWorld3x4();

    // Extract the 3x3 rotation matrix from the 3x4 transformation matrix and transpose it
            float3x3 rotationMatrix;
            rotationMatrix[0] = float3(instanceTransform[0].x, instanceTransform[1].x, instanceTransform[2].x);
            rotationMatrix[1] = float3(instanceTransform[0].y, instanceTransform[1].y, instanceTransform[2].y);
            rotationMatrix[2] = float3(instanceTransform[0].z, instanceTransform[1].z, instanceTransform[2].z);
    
            triangleNormal = normalize(mul(triangleNormal, rotationMatrix));
    
    // Diffuse
            float NdotL = max(dot(triangleNormal, lightDir), 0.0);
            lighting += NdotL;
        }
        
        float4 reflectColor = float4(0, 0, 0, 0);
        if ((payload.recursionDepth == 0) && InstanceID() == 6)
        {
            float3 reflectDir = reflect(payload.direction, triangleNormal);
            RayDesc reflectRay;
            reflectRay.Origin = hitPoint;
            reflectRay.Direction = reflectDir;
            reflectRay.TMin = 0.001f;
            reflectRay.TMax = 10000.0f;

            // Increase recursion depth
            RayPayload reflectPayload = { float4(0, 0, 0, 0), 0, reflectRay.Origin, reflectRay.Direction, 2 };

            // Trace reflection ray
            TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, reflectRay, reflectPayload);
            
            float reflectance = 3.0f;
            reflectColor = reflectPayload.color * reflectance;
        }
    
        payload.color = (sampledColor * instanceColor + reflectColor) * lighting;
    }
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    payload.color = float4(0, 0, 0, 1);
    payload.depth = 10000.0f;
}

struct ProceduralAttributes
{
    float3 hitPosition;
    float3 normal;
    // Add other attributes as needed
};

[shader("intersection")]
void MySimpleIntersectionShader()
{
    // Fixed hit distance
    float thit = 1.0f;

    // Define default attributes
    ProceduralAttributes attr;
    attr.hitPosition = float3(0.0f, 0.0f, 0.0f); // A fixed hit position
    attr.normal = float3(0.0f, 1.0f, 0.0f); // A fixed normal

    // Report the hit with the fixed attributes
    ReportHit(thit, /*hitKind*/0, attr);
}


[shader("closesthit")]
void MyClosestHitShader_AABB(inout RayPayload rayPayload, in ProceduralAttributes attrs)
{
    // PERFORMANCE TIP: it is recommended to minimize values carry over across TraceRay() calls. 
    // Therefore, in cases like retrieving HitWorldPosition(), it is recomputed every time.=

    rayPayload.color = float4(1, 0, 0, 1);
}

#endif // RAYTRACING_HLSL