#pragma once
#include <entt/entt.hpp>
#include <string>
#include <cstdint>

using EntityID = entt::entity;

struct Position {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Identity {
    std::string name;
    uint64_t    uid = 0;
};

struct Health {
    int current = 100;
    int maximum = 100;
    bool is_alive() const { return current > 0; }
};

class World {
public:
    World();
    ~World();

    EntityID create_entity();
    void     destroy_entity(EntityID id);

    // Verifica se entidade existe no registry
    bool has_entity(EntityID id) const { return m_registry.valid(id); }

    template<typename T, typename... Args>
    T& add(EntityID id, Args&&... args) {
        return m_registry.emplace<T>(id, std::forward<Args>(args)...);
    }

    template<typename T>
    T& get(EntityID id) { return m_registry.get<T>(id); }

    template<typename T>
    bool has(EntityID id) const { return m_registry.all_of<T>(id); }

    template<typename... T, typename Func>
    void each(Func&& func) {
        m_registry.view<T...>().each(std::forward<Func>(func));
    }

    size_t entity_count() const;

private:
    entt::registry m_registry;
};
