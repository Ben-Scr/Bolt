#include <doctest/doctest.h>

#include "Components/General/NameComponent.hpp"
#include "Scene/Entity.hpp"

#include <string>

using namespace Axiom;

TEST_CASE("Entity null component access is guarded")
{
	Entity entity = Entity::Null;
	CHECK_FALSE(entity.IsValid());
	CHECK_FALSE(entity.HasComponent<NameComponent>());
	CHECK_FALSE(entity.HasAnyComponent<NameComponent>());

	NameComponent* name = nullptr;
	CHECK_FALSE(entity.TryGetComponent(name));
	CHECK(name == nullptr);
	CHECK_NOTHROW(entity.RemoveComponent<NameComponent>());

	CHECK_THROWS(entity.GetComponent<NameComponent>());
	CHECK_THROWS(entity.AddComponent<NameComponent>(std::string("Name")));

	const Entity constEntity = Entity::Null;
	CHECK_THROWS(constEntity.GetComponent<NameComponent>());
	CHECK(constEntity.GetName().find("Unnamed Entity") == 0);
}
