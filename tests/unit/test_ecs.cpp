#include <gtest/gtest.h>
#include "engine/ecs/world.hpp"

TEST(WorldTest, CriaEntidade) {
    World world;
    auto entity = world.create_entity();
    // Verifica que a entidade é válida sem acionar o operador de conversão do tombstone
    EXPECT_TRUE(world.has_entity(entity));
}

TEST(WorldTest, AdicionaComponente) {
    World world;
    auto entity = world.create_entity();
    auto& pos   = world.add<Position>(entity, 1.f, 2.f, 3.f);
    EXPECT_FLOAT_EQ(pos.x, 1.f);
    EXPECT_TRUE(world.has<Position>(entity));
}

TEST(WorldTest, ContadorEntidades) {
    World world;
    world.create_entity();
    world.create_entity();
    EXPECT_EQ(world.entity_count(), 2u);
}

TEST(HealthTest, VerificaVivo) {
    Health hp;
    hp.current = 0;
    EXPECT_FALSE(hp.is_alive());
    hp.current = 50;
    EXPECT_TRUE(hp.is_alive());
}
