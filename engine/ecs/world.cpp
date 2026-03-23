#include "world.hpp"
#include <spdlog/spdlog.h>

World::World()  { spdlog::debug("World criado."); }
World::~World() { spdlog::debug("World destruido. Entidades: {}", entity_count()); }

EntityID World::create_entity()             { return m_registry.create(); }
void     World::destroy_entity(EntityID id) { m_registry.destroy(id); }
size_t   World::entity_count() const        { return m_registry.storage<entt::entity>()->in_use(); }
