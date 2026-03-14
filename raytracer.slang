[[vk::binding(0)]]
RWStructuredBuffer<float4> pixels;

struct Sphere {
    float4 centerAndRadius; // xyz = center, w = radius
    float4 colorAndReflectivity; // xyz = color, w = reflectivity
};

[[vk::binding(1)]]
StructuredBuffer<Sphere> spheres;

struct PushConstants {
    uint width;
    uint height;
    uint num_spheres;
    float padding; // keep float4 alignment
    float4 cameraPos; // xyz = pos, w = unused
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pcs;

struct Ray {
    float3 origin;
    float3 dir;
};

struct Hit {
    float t;
    float3 normal;
    int sphereIndex;
};

Hit intersectSphere(Ray ray, float3 center, float radius, int index) {
    float3 oc = ray.origin - center;
    float a = dot(ray.dir, ray.dir);
    float b = 2.0 * dot(oc, ray.dir);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - 4.0 * a * c;

    Hit hit;
    hit.t = -1.0;
    hit.sphereIndex = -1;

    if (discriminant > 0.0) {
        float temp = (-b - sqrt(discriminant)) / (2.0 * a);
        if (temp > 0.001) {
            hit.t = temp;
            hit.normal = normalize((ray.origin + ray.dir * temp) - center);
            hit.sphereIndex = index;
            return hit;
        }
        temp = (-b + sqrt(discriminant)) / (2.0 * a);
        if (temp > 0.001) {
            hit.t = temp;
            hit.normal = normalize((ray.origin + ray.dir * temp) - center);
            hit.sphereIndex = index;
            return hit;
        }
    }
    return hit;
}

Hit intersectScene(Ray ray) {
    Hit closestHit;
    closestHit.t = 1000000.0;
    closestHit.sphereIndex = -1;
    closestHit.normal = float3(0.0);

    for (int i = 0; i < pcs.num_spheres; ++i) {
        float3 center = spheres[i].centerAndRadius.xyz;
        float radius = spheres[i].centerAndRadius.w;
        Hit hit = intersectSphere(ray, center, radius, i);
        if (hit.t > 0.0 && hit.t < closestHit.t) {
            closestHit = hit;
        }
    }

    // Ground plane
    if (ray.dir.y < 0.0) {
        float t = -ray.origin.y / ray.dir.y;
        if (t > 0.001 && t < closestHit.t) {
            closestHit.t = t;
            closestHit.normal = float3(0.0, 1.0, 0.0);
            closestHit.sphereIndex = -2; // Ground
        }
    }

    return closestHit;
}

float3 trace(Ray startRay) {
    float3 color = float3(0.0);
    float3 attenuation = float3(1.0);
    Ray currentRay = startRay;

    for (int bounce = 0; bounce < 3; ++bounce) {
        Hit hit = intersectScene(currentRay);

        if (hit.sphereIndex == -1) {
            // Background color (sky)
            float3 unit_direction = normalize(currentRay.dir);
            float t = 0.5 * (unit_direction.y + 1.0);
            float3 skyColor = (1.0 - t) * float3(1.0, 1.0, 1.0) + t * float3(0.5, 0.7, 1.0);
            color += attenuation * skyColor;
            break;
        }

        float3 hitPoint = currentRay.origin + currentRay.dir * hit.t;
        float3 lightDir = normalize(float3(1.0, 1.0, 1.0));
        float diff = max(dot(hit.normal, lightDir), 0.0);

        float3 surfaceColor;
        float reflectivity = 0.0;

        if (hit.sphereIndex == -2) {
            // Ground
            float checker = fmod(floor(hitPoint.x * 0.5) + floor(hitPoint.z * 0.5), 2.0);
            surfaceColor = checker < 1.0 ? float3(0.2) : float3(0.8);
            reflectivity = 0.3; // slightly reflective ground
        } else {
            surfaceColor = spheres[hit.sphereIndex].colorAndReflectivity.xyz;
            reflectivity = spheres[hit.sphereIndex].colorAndReflectivity.w;
        }

        // Ambient + Diffuse
        float3 directLighting = surfaceColor * (diff * 0.8 + 0.2);

        // Add color proportional to non-reflected part
        color += attenuation * directLighting * (1.0 - reflectivity);

        // Update ray for next bounce
        attenuation *= surfaceColor * reflectivity;
        currentRay.origin = hitPoint + hit.normal * 0.001;
        currentRay.dir = reflect(currentRay.dir, hit.normal);
    }

    return color;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint x = dispatchThreadID.x;
    uint y = dispatchThreadID.y;

    if (x >= pcs.width || y >= pcs.height) {
        return;
    }

    // Centered coordinates from -1 to 1
    float u = (float(x) / float(pcs.width - 1)) * 2.0 - 1.0;
    float v = (float(y) / float(pcs.height - 1)) * 2.0 - 1.0;

    // simple camera
    float fov = 1.0;
    float aspect = float(pcs.width) / float(pcs.height);

    float3 rayDir = normalize(float3(
        u * aspect * fov,
        v * fov,
        -1.0
    ));

    // Top-down camera setup
    // Rotate ray direction to look down from top
    float3 rotatedDir;
    rotatedDir.x = rayDir.x;
    rotatedDir.y = rayDir.z; // mapped from -Z
    rotatedDir.z = -rayDir.y; // mapped from -Y -> down is negative Z after mapping?
    // Wait, standard looking -Z. If looking down -Y, Z becomes Y. Y becomes Z.
    // Let's do a proper rotation around X axis by -90 degrees
    // (x, y, z) -> (x, z, -y)
    rotatedDir.x = rayDir.x;
    rotatedDir.y = rayDir.z;
    rotatedDir.z = -rayDir.y;

    Ray ray;
    ray.origin = pcs.cameraPos.xyz;
    ray.dir = rotatedDir;

    float3 color = trace(ray);

    // simple gamma correction
    color = pow(color, float3(1.0 / 2.2));

    uint index = y * pcs.width + x;
    pixels[index] = float4(color, 1.0);
}
