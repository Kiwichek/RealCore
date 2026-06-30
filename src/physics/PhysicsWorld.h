#pragma once
#include <core/math.h>
#include <cstdint>

class PhysicsWorld {
public:
    static constexpr uint32_t InvalidBodyId = 0xffffffffu;

    PhysicsWorld() = default;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;
    ~PhysicsWorld();

    bool init();
    void shutdown();
    void step(float deltaTime);

    uint32_t addBox(const Vec3& position, const Vec3& halfExtent, bool dynamic = true);
    uint32_t addSphere(const Vec3& position, float radius, bool dynamic = true);
    uint32_t addCapsule(const Vec3& position, float halfHeight, float radius, bool dynamic = true);
    uint32_t addCylinder(const Vec3& position, float halfHeight, float radius, bool dynamic = true);
    uint32_t addPlane(const Vec3& position, const Vec3& normal);
    uint32_t addKinematicBox(const Vec3& position, const Vec3& halfExtent);

    Vec3 getPosition(uint32_t bodyId) const;
    void setPosition(uint32_t bodyId, const Vec3& position);
    Vec3 getLinearVelocity(uint32_t bodyId) const;
    void setLinearVelocity(uint32_t bodyId, const Vec3& velocity);
    void resetMotion(uint32_t bodyId);
    void applyForce(uint32_t bodyId, const Vec3& force);
    void applyImpulse(uint32_t bodyId, const Vec3& impulse);
    void setFrozen(uint32_t bodyId, bool frozen);
    void removeBody(uint32_t bodyId);

    void setGravity(const Vec3& gravity);
    Vec3 gravity() const;

private:
    void* m_physicsSystem = nullptr;
    void* m_tempAllocator = nullptr;
    void* m_jobSystem = nullptr;
    void* m_broadPhaseLayerInterface = nullptr;
    void* m_objectVsBroadPhaseLayerFilter = nullptr;
    void* m_objectLayerPairFilter = nullptr;
    bool m_initialized = false;
};
