#include <physics/PhysicsWorld.h>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr int cMaxBodies = 1024;
constexpr int cNumBodyMutexes = 0;
constexpr int cMaxBodyPairs = 1024;
constexpr int cMaxContactConstraints = 1024;
constexpr int cMaxPhysicsJobs = 1024;
constexpr int cMaxPhysicsBarriers = 8;

constexpr JPH::ObjectLayer LAYER_NON_MOVING = 0;
constexpr JPH::ObjectLayer LAYER_MOVING = 1;
constexpr JPH::BroadPhaseLayer BP_LAYER_NON_MOVING(0);
constexpr JPH::BroadPhaseLayer BP_LAYER_MOVING(1);

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return inLayer == LAYER_MOVING ? BP_LAYER_MOVING : BP_LAYER_NON_MOVING;
    }
};

class ObjectVsBPLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer inLayer, JPH::BroadPhaseLayer inBroadPhaseLayer) const override {
        if (inLayer == LAYER_NON_MOVING) {
            return inBroadPhaseLayer == BP_LAYER_MOVING;
        }
        return true;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
        if (inLayer1 == LAYER_NON_MOVING) {
            return inLayer2 == LAYER_MOVING;
        }
        return true;
    }
};

inline JPH::Vec3 toJPH(const Vec3& v) { return { v.x, v.y, v.z }; }
inline JPH::RVec3 toJPH_R(const Vec3& v) { return { v.x, v.y, v.z }; }
inline Vec3 fromJPH(const JPH::Vec3& v) { return { v.GetX(), v.GetY(), v.GetZ() }; }

#if defined(JPH_DOUBLE_PRECISION)
inline Vec3 fromJPH(const JPH::RVec3& v) { return { (float)v.GetX(), (float)v.GetY(), (float)v.GetZ() }; }
#endif

bool isFinite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

Vec3 sanitizePosition(Vec3 v) {
    if (!std::isfinite(v.x)) v.x = 0.0f;
    if (!std::isfinite(v.y)) v.y = 0.0f;
    if (!std::isfinite(v.z)) v.z = 0.0f;
    return v;
}

Vec3 sanitizeHalfExtent(Vec3 v) {
    if (!std::isfinite(v.x)) v.x = 0.5f;
    if (!std::isfinite(v.y)) v.y = 0.5f;
    if (!std::isfinite(v.z)) v.z = 0.5f;
    return {
        (std::max)(0.01f, std::abs(v.x)),
        (std::max)(0.01f, std::abs(v.y)),
        (std::max)(0.01f, std::abs(v.z))
    };
}

float sanitizePositive(float value, float fallback = 0.5f) {
    if (!std::isfinite(value)) {
        value = fallback;
    }
    return (std::max)(0.01f, std::abs(value));
}

} // namespace

PhysicsWorld::~PhysicsWorld() {
    shutdown();
}

bool PhysicsWorld::init() {
    if (m_initialized) {
        return true;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    m_tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    m_jobSystem = new JPH::JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers);
    m_broadPhaseLayerInterface = new BPLayerInterface();
    m_objectVsBroadPhaseLayerFilter = new ObjectVsBPLayerFilter();
    m_objectLayerPairFilter = new ObjectLayerPairFilter();

    auto* system = new JPH::PhysicsSystem();
    system->Init(
        cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        *(BPLayerInterface*)m_broadPhaseLayerInterface,
        *(ObjectVsBPLayerFilter*)m_objectVsBroadPhaseLayerFilter,
        *(ObjectLayerPairFilter*)m_objectLayerPairFilter
    );
    m_physicsSystem = system;

    m_initialized = true;
    return true;
}

void PhysicsWorld::shutdown() {
    if (!m_initialized) return;

    delete (JPH::PhysicsSystem*)m_physicsSystem;
    delete (ObjectLayerPairFilter*)m_objectLayerPairFilter;
    delete (ObjectVsBPLayerFilter*)m_objectVsBroadPhaseLayerFilter;
    delete (BPLayerInterface*)m_broadPhaseLayerInterface;
    delete (JPH::JobSystemThreadPool*)m_jobSystem;
    delete (JPH::TempAllocatorImpl*)m_tempAllocator;

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    m_physicsSystem = nullptr;
    m_tempAllocator = nullptr;
    m_jobSystem = nullptr;
    m_broadPhaseLayerInterface = nullptr;
    m_objectVsBroadPhaseLayerFilter = nullptr;
    m_objectLayerPairFilter = nullptr;
    m_initialized = false;
}

void PhysicsWorld::step(float deltaTime) {
    if (!m_initialized) return;
    if (deltaTime > 0.05f) deltaTime = 0.05f;

    ((JPH::PhysicsSystem*)m_physicsSystem)->Update(
        deltaTime, 1,
        (JPH::TempAllocatorImpl*)m_tempAllocator,
        (JPH::JobSystemThreadPool*)m_jobSystem
    );
}

uint32_t PhysicsWorld::addBox(const Vec3& position, const Vec3& halfExtent, bool dynamic) {
    if (!m_initialized) return InvalidBodyId;
    Vec3 safePosition = sanitizePosition(position);
    Vec3 safeHalfExtent = sanitizeHalfExtent(halfExtent);

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::ObjectLayer layer = dynamic ? LAYER_MOVING : LAYER_NON_MOVING;

    auto shape = new JPH::BoxShape(toJPH(safeHalfExtent));
    JPH::BodyCreationSettings settings(shape, toJPH_R(safePosition), JPH::Quat::sIdentity(),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, layer);

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) return InvalidBodyId;

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::addKinematicBox(const Vec3& position, const Vec3& halfExtent) {
    if (!m_initialized) return InvalidBodyId;
    Vec3 safePosition = sanitizePosition(position);
    Vec3 safeHalfExtent = sanitizeHalfExtent(halfExtent);

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();

    auto shape = new JPH::BoxShape(toJPH(safeHalfExtent));
    JPH::BodyCreationSettings settings(shape, toJPH_R(safePosition), JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic, LAYER_MOVING);

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) return InvalidBodyId;

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::addSphere(const Vec3& position, float radius, bool dynamic) {
    if (!m_initialized) return InvalidBodyId;
    Vec3 safePosition = sanitizePosition(position);
    float safeRadius = sanitizePositive(radius);

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::ObjectLayer layer = dynamic ? LAYER_MOVING : LAYER_NON_MOVING;

    auto shape = new JPH::SphereShape(safeRadius);
    JPH::BodyCreationSettings settings(shape, toJPH_R(safePosition), JPH::Quat::sIdentity(),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, layer);

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) return InvalidBodyId;

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::addCapsule(const Vec3& position, float halfHeight, float radius, bool dynamic) {
    if (!m_initialized) return InvalidBodyId;
    Vec3 safePosition = sanitizePosition(position);
    float safeHalfHeight = sanitizePositive(halfHeight);
    float safeRadius = sanitizePositive(radius);

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::ObjectLayer layer = dynamic ? LAYER_MOVING : LAYER_NON_MOVING;

    auto shape = new JPH::CapsuleShape(safeHalfHeight, safeRadius);
    JPH::BodyCreationSettings settings(shape, toJPH_R(safePosition), JPH::Quat::sIdentity(),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, layer);

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) return InvalidBodyId;

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::addCylinder(const Vec3& position, float halfHeight, float radius, bool dynamic) {
    if (!m_initialized) return InvalidBodyId;
    Vec3 safePosition = sanitizePosition(position);
    float safeHalfHeight = sanitizePositive(halfHeight);
    float safeRadius = sanitizePositive(radius);

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::ObjectLayer layer = dynamic ? LAYER_MOVING : LAYER_NON_MOVING;

    auto shape = new JPH::CylinderShape(safeHalfHeight, safeRadius);
    JPH::BodyCreationSettings settings(shape, toJPH_R(safePosition), JPH::Quat::sIdentity(),
        dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, layer);

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) return InvalidBodyId;

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::addPlane(const Vec3& position, const Vec3& normal) {
    if (!m_initialized) return InvalidBodyId;
    Vec3 safePosition = sanitizePosition(position);
    Vec3 safeNormal = isFinite(normal) && normal.length() > 0.0001f ? normal.normalized() : Vec3{ 0.0f, 1.0f, 0.0f };

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();

    auto shape = new JPH::PlaneShape(JPH::Plane(toJPH(safeNormal), 0.0f));
    JPH::BodyCreationSettings settings(shape, toJPH_R(safePosition), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, LAYER_NON_MOVING);

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) return InvalidBodyId;

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return body->GetID().GetIndexAndSequenceNumber();
}

Vec3 PhysicsWorld::getPosition(uint32_t bodyId) const {
    if (!m_initialized || bodyId == InvalidBodyId) return {};

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return {};

    return fromJPH(bodyInterface.GetCenterOfMassPosition(id));
}

void PhysicsWorld::setPosition(uint32_t bodyId, const Vec3& position) {
    if (!m_initialized || bodyId == InvalidBodyId) return;
    Vec3 safePosition = sanitizePosition(position);

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return;
    bodyInterface.SetPosition(id, toJPH_R(safePosition), JPH::EActivation::Activate);
}

Vec3 PhysicsWorld::getLinearVelocity(uint32_t bodyId) const {
    if (!m_initialized || bodyId == InvalidBodyId) return {};
    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return {};
    return fromJPH(bodyInterface.GetLinearVelocity(id));
}

void PhysicsWorld::setLinearVelocity(uint32_t bodyId, const Vec3& velocity) {
    if (!m_initialized || bodyId == InvalidBodyId) return;
    if (!isFinite(velocity)) return;
    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return;
    bodyInterface.SetLinearAndAngularVelocity(id, toJPH(velocity), JPH::Vec3::sZero());
}

void PhysicsWorld::applyForce(uint32_t bodyId, const Vec3& force) {
    if (!m_initialized || bodyId == InvalidBodyId) return;
    if (!isFinite(force)) return;
    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return;
    bodyInterface.AddForce(id, toJPH(force));
}

void PhysicsWorld::applyImpulse(uint32_t bodyId, const Vec3& impulse) {
    if (!m_initialized || bodyId == InvalidBodyId) return;
    if (!isFinite(impulse)) return;
    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return;
    bodyInterface.AddImpulse(id, toJPH(impulse));
}

void PhysicsWorld::resetMotion(uint32_t bodyId) {
    if (!m_initialized || bodyId == InvalidBodyId) return;

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return;
    bodyInterface.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
}

void PhysicsWorld::setFrozen(uint32_t bodyId, bool frozen) {
    if (!m_initialized || bodyId == InvalidBodyId) return;

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return;

    bodyInterface.SetObjectLayer(id, LAYER_MOVING);
    resetMotion(bodyId);
    bodyInterface.SetMotionType(
        id,
        frozen ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
        JPH::EActivation::Activate);
    resetMotion(bodyId);
}

void PhysicsWorld::removeBody(uint32_t bodyId) {
    if (!m_initialized || bodyId == InvalidBodyId) return;

    auto& bodyInterface = ((JPH::PhysicsSystem*)m_physicsSystem)->GetBodyInterface();
    JPH::BodyID id(bodyId);
    if (!bodyInterface.IsAdded(id)) return;
    bodyInterface.RemoveBody(id);
    bodyInterface.DestroyBody(id);
}

void PhysicsWorld::setGravity(const Vec3& gravity) {
    if (!m_initialized) return;
    if (!isFinite(gravity)) return;
    ((JPH::PhysicsSystem*)m_physicsSystem)->SetGravity(toJPH(gravity));
}

Vec3 PhysicsWorld::gravity() const {
    if (!m_initialized) return { 0, -9.81f, 0 };
    return fromJPH(((JPH::PhysicsSystem*)m_physicsSystem)->GetGravity());
}
