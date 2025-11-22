#pragma once

#include "SpartSets.h"

#include <vector>
#include <algorithm>
#include <cassert>
#include <unordered_map>

#define ECS

#define ASSERTM(msg, expr) assert(((void)msg, (expr)))

namespace ecs {

    using ComponentID = uint32_t;
    using Entity = uint32_t;

    struct Resource {};
    struct Component {};

    template<typename Category>
    class IndexGetter final {
    public:
        template<typename T>
        static uint32_t Get() {
            static uint32_t id = m_curIdx++;
            return id;
        }
    private:
        inline static uint32_t m_curIdx = 0;
    };

    template<typename T, typename = std::enable_if<std::is_integral_v<T>>>
    struct IDGenerator final {
    public:
        static T Gen() {
            return m_curId++;
        }
    private:
        inline static T m_curId = {};
    };

    using EntityGenerator = IDGenerator<Entity>;

    class Commands;
    class Resources;
    class Queryer;

    class World final {
    public:
        friend class Commands;
        friend class Resources;
        friend class Queryer;

        using ComponentContainer = std::unordered_map<ComponentID, void*>;

        World() = default;
        World(const World&) = delete;
        World& operator=(const World&) = delete;
    private:
        struct Pool final {
            std::vector<void*> instances;
            std::vector<void*> cache;

            using CreateFunc = void* (*)(void);
            using DestroyFunc = void(*)(void*);

            CreateFunc create{};
            DestroyFunc destroy{};

            Pool(CreateFunc create, DestroyFunc destroy) : create(create), destroy(destroy) {
                ASSERTM("must give a non-null create function", create);
                ASSERTM("must give a non-null destroy function", destroy);
            }

            void* Create() {
                if (!cache.empty()) {
                    instances.push_back(cache.back());
                    cache.pop_back();
                }
                else {
                    instances.push_back(create());
                }
                return instances.back();
            }

            void Destroy(void* elem) {
                if (auto it = std::find(instances.begin(), instances.end(), elem); it != instances.end()) {
                    cache.push_back(*it);
                    std::swap(*it, instances.back());
                    instances.pop_back();
                }
                else {
                    ASSERTM("element not in pool", false);
                }
            }
        };

        struct ComponentInfo {
            Pool pool;
            SparseSets<Entity, 32> sparseSet;

            ComponentInfo(Pool::CreateFunc create, Pool::DestroyFunc destroy) : pool(create, destroy) {}
            ComponentInfo() : pool(nullptr, nullptr) {}
        };

        using ComponentMap = std::unordered_map<ComponentID, ComponentInfo>;
        ComponentMap m_componentMap;
        std::unordered_map<Entity, ComponentContainer> m_entities;

        struct ResourceInfo {
            void* resource = nullptr;
            using DestroyFunc = void(*)(void*);
            DestroyFunc destroy = nullptr;

            ResourceInfo(DestroyFunc destroy) : destroy(destroy) {
                ASSERTM("must give a non-null destroy function", destroy);
            }
            ResourceInfo() = default;
            ~ResourceInfo() {
                if (destroy && resource) {
                    destroy(resource);
                    resource = nullptr;
                }
            }
        };

        std::unordered_map<ComponentID, ResourceInfo> m_resources;
    };

    class Commands final {
    public:
        Commands(World& world) : m_world(world) {}

        template<typename ... ComponentTypes>
        Commands& Spawn(ComponentTypes&& ... components) {
            SpawnAndReturn<ComponentTypes ...>(std::forward<ComponentTypes>(components) ...);
            return *this;
        }

        /*function should be renamed*/
        template<typename ... ComponentTypes>
        Entity SpawnAndReturn(ComponentTypes&& ... components) {
            Entity entity = EntityGenerator::Gen();
            doSpawn(entity, std::forward<ComponentTypes>(components) ...);
            return entity;
        }

        Commands& Destroy(Entity entity) {
            if (auto it = m_world.m_entities.find(entity); it != m_world.m_entities.end()) {
                for (auto& kv : it->second) {
                    auto id = kv.first;
                    auto component = kv.second;
                    auto& componentInfo = m_world.m_componentMap[id];
                    componentInfo.pool.Destroy(component);
                    componentInfo.sparseSet.Remove(entity);
                }
                m_world.m_entities.erase(it);
            }
            return *this;
        }

        template<typename T>
        Commands& SetResource(T&& resource) {
            auto index = IndexGetter<Resource>::Get<T>();
            if (auto it = m_world.m_resources.find(index); it != m_world.m_resources.end()) {
#if 0
                ASSERTM("resource already exist", it->second.resource);
                it->second.resource = new T(std::forward<T>(resource));
#else
                // 已经有资源：先销毁旧的，再换新值，防止内存泄漏
                if (it->second.resource) {
                    it->second.destroy(it->second.resource);
                }
                it->second.resource = new T(std::forward<T>(resource));
#endif
            }
            else {
#if 0
                auto newIt = m_world.m_resources.emplace(index, World::ResourceInfo([](void* elem) {delete (T*)elem; }));
                newIt.first->second.resource = new T;
                *(T*)(newIt.first->second.resource) = std::forward<T>(resource);
#else
                // 第一次创建：注册 destroy 函数
                auto [iter, ok] = m_world.m_resources.emplace(
                    index,
                    World::ResourceInfo([](void* elem) { delete static_cast<T*>(elem); })
                );
                iter->second.resource = new T(std::forward<T>(resource));
#endif
            }
            return *this;
        }

        template<typename T>
        Commands& RemoveResource() {
            auto index = IndexGetter<Resource>::Get<T>();
            if (auto it = m_world.m_resources.find(index); it != m_world.m_resources.end()) {
#if 0
                delete (T*)it->second.resource;
                it->second.resource = nullptr;
#else
                if (it->second.resource) {
                    it->second.destroy(it->second.resource);
                    it->second.resource = nullptr;
                }
#endif
            }
            return *this;
        }

    private:
        World& m_world;

        template<typename T, typename ... Remains>
        void doSpawn(Entity entity, T&& component, Remains&& ... remains) {
            auto index = IndexGetter<Component>::Get<T>();
            if (auto it = m_world.m_componentMap.find(index); it == m_world.m_componentMap.end()) {
                m_world.m_componentMap.emplace(index,
                    World::ComponentInfo([]()->void* { return new T; },
                        [](void* elem) { delete (T*)(elem); }));
            }
            World::ComponentInfo& componentInfo = m_world.m_componentMap[index];
            void* element = componentInfo.pool.Create();
            *((T*)element) = std::forward<T>(component);
            componentInfo.sparseSet.Add(entity);

            auto it = m_world.m_entities.emplace(entity, World::ComponentContainer{});
            it.first->second[index] = element;

            if constexpr (sizeof ... (remains) != 0) {
                doSpawn<Remains...>(entity, std::forward<Remains>(remains) ...);
            }
        }
    };

    class Resources final {
    public:
        Resources(World& world) : m_world(world) {}

        template<typename T>
        bool Has() const {
            auto index = IndexGetter<Resource>::Get<T>();
            auto it = m_world.m_resources.find(index);
            return it != m_world.m_resources.end() && it->second.resource;
        }

        /*must use Has before Get*/
        template<typename T>
        T& Get() {
#if 0
            auto index = IndexGetter<Resource>::Get<T>();
            return *((T*)m_world.m_resources[index].resource);
#else
            auto index = IndexGetter<Resource>::Get<T>();
            auto it = m_world.m_resources.find(index);

            ASSERTM("resource not exist",
                it != m_world.m_resources.end() && it->second.resource);

            return *static_cast<T*>(it->second.resource);
#endif
        }
    private:
        World& m_world;
    };

    class Queryer final {
    public:
        Queryer(World& world) : m_world(world) {}

        template<typename ... Components>
        std::vector<Entity> Query() {
            std::vector<Entity> entities;
            doQuery<Components ...>(entities);
            return entities;
        }

        template<typename T>
        bool Has(Entity entity) {
            auto it = m_world.m_entities.find(entity);
            auto index = IndexGetter<Component>::Get<T>();
            return it != m_world.m_entities.end() && it->second.find(index) != it->second.end();
        }

        /*must use Has before Get*/
        template<typename T>
        T& Get(Entity entity) {
            auto index = IndexGetter<Component>::Get<T>();
            return *(T*)m_world.m_entities[entity][index];
        }
    private:
        World& m_world;

        template<typename T, typename ... Remains>
        bool doQuery(std::vector<Entity>& outEntities) {
            auto index = IndexGetter<Component>::Get<T>();
            World::ComponentInfo& info = m_world.m_componentMap[index];

            
            for (auto e : info.sparseSet) {
                if constexpr (sizeof ... (Remains) != 0) {
                    return doQueryRemains<Remains ...>(e, outEntities);
                }
                else {
                    outEntities.push_back(e);
                }
            }
            return !outEntities.empty();
        }

        template<typename T, typename ... Remains>
        bool doQueryRemains(Entity entity, std::vector<Entity>& outEntities) {
            auto index = IndexGetter<Component>::Get<T>();
            auto& componentContainer = m_world.m_entities[entity];
            if (auto it = componentContainer.find(index); it == componentContainer.end()) {
                return false;
            }
            
            if constexpr (sizeof ... (Remains) == 0) {
                outEntities.push_back(entity);
                return true;
            }
            else {
                return doQueryRemains<Remains ...>(entity, outEntities);
            }
        }
    };
}