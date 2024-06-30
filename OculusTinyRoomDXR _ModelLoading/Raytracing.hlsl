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

struct Ray
{
    float3 origin;
    float3 direction;
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

float4 ReflectRay(float3 rayDir, float3 normal, float3 hitPosition, float reflectanceFactor = 2.0f)
{
    
    float3 reflectDir = reflect(rayDir, normal);
    RayDesc reflectRay;
    reflectRay.Origin = hitPosition;
    reflectRay.Direction = reflectDir;
    reflectRay.TMin = 0.001f;
    reflectRay.TMax = 10000.0f;

            // Increase recursion depth
    RayPayload reflectPayload = { float4(0, 0, 0, 0), 0, reflectRay.Origin, reflectRay.Direction, 2 };

            // Trace reflection ray
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, reflectRay, reflectPayload);
            
    return reflectPayload.color * reflectanceFactor;
}


[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    //payload.color = float4(1, 1, 1, 1);
    //return;
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
            reflectColor = ReflectRay(payload.direction, triangleNormal, hitPoint);
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

bool IsInRange(in float val, in float min, in float max)
{
    return (val >= min && val <= max);
}

// Test if a hit is culled based on specified RayFlags.
bool IsCulled(in Ray ray, in float3 hitSurfaceNormal)
{
    float rayDirectionNormalDot = dot(ray.direction, hitSurfaceNormal);

    bool isCulled =
        ((RayFlags() & RAY_FLAG_CULL_BACK_FACING_TRIANGLES) && (rayDirectionNormalDot > 0))
        ||
        ((RayFlags() & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) && (rayDirectionNormalDot < 0));

    return isCulled;
}

// Test if a hit is valid based on specified RayFlags and <RayTMin, RayTCurrent> range.
bool IsAValidHit(in Ray ray, in float thit, in float3 hitSurfaceNormal)
{
    return IsInRange(thit, RayTMin(), RayTCurrent()) && !IsCulled(ray, hitSurfaceNormal);
    //return IsInRange(thit, RayTMin(), RayTCurrent());
}

void swap(inout float a, inout float b)
{
    float temp = a;
    a = b;
    b = temp;
}


bool RayAABBIntersectionTest(float3 rayOrigin, float3 rayDir, float3 aabb[2], out float tmin, out float tmax)
{
    float3 tmin3, tmax3;
    int3 sign3 = rayDir > 0;

    // Handle rays parallel to any x|y|z slabs of the AABB.
    // If a ray is within the parallel slabs, 
    //  the tmin, tmax will get set to -inf and +inf
    //  which will get ignored on tmin/tmax = max/min.
    // If a ray is outside the parallel slabs, -inf/+inf will
    //  make tmax > tmin fail (i.e. no intersection).
    // TODO: handle cases where ray origin is within a slab 
    //  that a ray direction is parallel to. In that case
    //  0 * INF => NaN
    const float FLT_INFINITY = 1.#INF;
    float3 invRayDirection = 1.0f/rayDir;

    tmin3.x = (aabb[1 - sign3.x].x - rayOrigin.x) * invRayDirection.x;
    tmax3.x = (aabb[sign3.x].x - rayOrigin.x) * invRayDirection.x;

    tmin3.y = (aabb[1 - sign3.y].y - rayOrigin.y) * invRayDirection.y;
    tmax3.y = (aabb[sign3.y].y - rayOrigin.y) * invRayDirection.y;
    
    tmin3.z = (aabb[1 - sign3.z].z - rayOrigin.z) * invRayDirection.z;
    tmax3.z = (aabb[sign3.z].z - rayOrigin.z) * invRayDirection.z;
    
    tmin = max(max(tmin3.x, tmin3.y), tmin3.z);
    tmax = min(min(tmax3.x, tmax3.y), tmax3.z);
    
    return tmax > tmin && tmax >= RayTMin() && tmin <= RayTCurrent();
}

bool SolveQuadraticEqn(float a, float b, float c, out float x0, out float x1)
{
    float discr = b * b - 4 * a * c;
    if (discr < 0)
        return false;
    else if (discr == 0)
        x0 = x1 = -0.5 * b / a;
    else
    {
        float q = (b > 0) ?
            -0.5 * (b + sqrt(discr)) :
            -0.5 * (b - sqrt(discr));
        x0 = q / a;
        x1 = c / q;
    }
    if (x0 > x1)
        swap(x0, x1);

    return true;
}

// Calculate a normal for a hit point on a sphere.
float3 CalculateNormalForARaySphereHit(in Ray ray, in float thit, float3 center)
{
    float3 hitPosition = ray.origin + thit * ray.direction;
    return normalize(hitPosition - center);
}

// Analytic solution of an unbounded ray sphere intersection points.
// Ref: https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection
bool SolveRaySphereIntersectionEquation(in Ray ray, out float tmin, out float tmax, in float3 center, in float radius)
{
    float3 L = ray.origin - center;
    float a = dot(ray.direction, ray.direction);
    float b = 2 * dot(ray.direction, L);
    float c = dot(L, L) - radius * radius;
    return SolveQuadraticEqn(a, b, c, tmin, tmax);
}

// Test if a ray with RayFlags and segment <RayTMin(), RayTCurrent()> intersects a hollow sphere.
bool RaySphereIntersectionTest(in Ray ray, out float thit, out float tmax, out ProceduralAttributes attr, in float3 center = float3(0, 0, 0), in float radius = 1)
{
    float t0, t1; // solutions for t if the ray intersects 

    if (!SolveRaySphereIntersectionEquation(ray, t0, t1, center, radius))
        return false;
    tmax = t1;

    if (t0 < RayTMin())
    {
        // t0 is before RayTMin, let's use t1 instead .
        if (t1 < RayTMin())
            return false; // both t0 and t1 are before RayTMin

        attr.normal = CalculateNormalForARaySphereHit(ray, t1, center);
        if (IsAValidHit(ray, t1, attr.normal))
        {
            thit = t1;
            attr.hitPosition = center + attr.normal * radius;
            return true;
        }
    }
    else
    {
        attr.normal = CalculateNormalForARaySphereHit(ray, t0, center);
        if (IsAValidHit(ray, t0, attr.normal))
        {
            thit = t0;
            attr.hitPosition = center + attr.normal * radius;
            return true;
        }

        attr.normal = CalculateNormalForARaySphereHit(ray, t1, center);
        if (IsAValidHit(ray, t1, attr.normal))
        {
            thit = t1;
            attr.hitPosition = center + attr.normal * radius;
            return true;
        }
    }
    return false;
}

void AABBCollisionTest()
{
    float tmin, tmax;
    float tHit;
    ProceduralAttributes attr;
    float3 aabb[2];
    aabb[0] = float3(0, 0, 0);
    aabb[1] = float3(0.5, 0.5, 0.5);
    float3 rayOrigin = WorldRayOrigin();
    float3 rayDirection = WorldRayDirection();
    if (RayAABBIntersectionTest(rayOrigin, rayDirection, aabb, tmin, tmax))
    {
    // Only consider intersections crossing the surface from the outside.
        if (tmin < RayTMin() || tmin > RayTCurrent())
            return;

        tHit = tmin;

        // Set a normal to the normal of a face the hit point lays on.
        float3 hitPosition = rayOrigin + tHit * rayDirection;
        float3 distanceToBounds[2] =
        {
            abs(aabb[0] - hitPosition),
            abs(aabb[1] - hitPosition)
        };
        const float eps = 0.0001;
        if (distanceToBounds[0].x < eps)
            attr.normal = float3(-1, 0, 0);
        else if (distanceToBounds[0].y < eps)
            attr.normal = float3(0, -1, 0);
        else if (distanceToBounds[0].z < eps)
            attr.normal = float3(0, 0, -1);
        else if (distanceToBounds[1].x < eps)
            attr.normal = float3(1, 0, 0);
        else if (distanceToBounds[1].y < eps)
            attr.normal = float3(0, 1, 0);
        else if (distanceToBounds[1].z < eps)
            attr.normal = float3(0, 0, 1);

        //ReportHit(tHit, /*hitKind*/0, attr);
    }
}

[shader("intersection")]
void MySimpleIntersectionShader()
{
    float tmin, tmax;
    float tHit;
    ProceduralAttributes attr;
    Ray ray;
    ray.origin = WorldRayOrigin();
    ray.direction = WorldRayDirection();
    float3x4 instanceTransform = ObjectToWorld3x4();
    float3 position = float3(instanceTransform[0][3], instanceTransform[1][3], instanceTransform[2][3]);
    // Now assume that it has been scaled uniformly to extract the radius
    float radius = 0.5f * instanceTransform[0][0];
    if (RaySphereIntersectionTest(ray, tHit, tmax, attr, position, radius))
    {
        ReportHit(tHit, /*hitKind*/0, attr);
    }
}



[shader("closesthit")]
void MySphereClosestHitShader(inout RayPayload payload, in ProceduralAttributes attrs)
{
    // PERFORMANCE TIP: it is recommended to minimize values carry over across TraceRay() calls. 
    // Therefore, in cases like retrieving HitWorldPosition(), it is recomputed every time.=
    float3 lightDir = normalize(g_sceneCB.lights[0].position - attrs.hitPosition);
    //float3 lightDir = normalize(float3(0, 0, -1));
    float maxDist = length(g_sceneCB.lights[0].position - attrs.hitPosition);
    
    float lighting = 0.05f;
    if (!IsInShadow(lightDir, attrs.hitPosition, maxDist))
    {
        float NdotL = max(dot(attrs.normal, lightDir), 0.0);
        lighting += NdotL;
    }
    float4 reflectColor = float4(0, 0, 0, 0);
    if (payload.recursionDepth == 0)
    {
        reflectColor = ReflectRay(payload.direction, attrs.normal, attrs.hitPosition);
    }
    payload.color = (float4(0, 0.7, 0.7, 1) + reflectColor)*lighting;
    
}

#endif // RAYTRACING_HLSL