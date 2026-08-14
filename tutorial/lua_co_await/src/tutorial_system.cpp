// tutorial_system.cpp
// PaintDream (paintdream@paintdream.com)
// 2026
//

#include "tutorial_system.h"

namespace iris {
	void tutorial_system_t::lua_registar(iris_lua_t&& lua, std::nullptr_t) {
		lua.set_current_new<&iris_lua_t::place_new_object<tutorial_system_t>>("new");
		lua.set_current<&tutorial_system_t::create_entity>("create_entity");
		lua.set_current<&tutorial_system_t::remove_entity>("remove_entity");
		lua.set_current<&tutorial_system_t::get_entity_count>("get_entity_count");
		lua.set_current<&tutorial_system_t::has_component>("has_component");
		lua.set_current<&tutorial_system_t::add_components>("add_components");
		lua.set_current<&tutorial_system_t::set_velocity>("set_velocity");
		lua.set_current<&tutorial_system_t::remove_components>("remove_components");
		lua.set_current<&tutorial_system_t::get_position>("get_position");
		lua.set_current<&tutorial_system_t::get_velocity>("get_velocity");
		lua.set_current<&tutorial_system_t::tick>("tick");
		lua.set_current<&tutorial_system_t::get_moved_count>("get_moved_count");
		lua.set_current("run", lua.load("local self = ...\n\
print('[tutorial_system] ')\n\
-- entities are plain ids from the entity allocator\n\
local e1 = self:create_entity()\n\
local e2 = self:create_entity()\n\
print('\\tcreated entities ' .. tostring(e1) .. ' and ' .. tostring(e2) .. ', count = ' .. tostring(self:get_entity_count()))\n\
-- insert() sets the WHOLE component set (the archetype)\n\
self:add_components(e1, 0, 0, 0, 1, 2, 3)\n\
self:add_components(e2, 10, 20, 30, -1, 0, 0)\n\
-- single-component modification goes through the get() reference\n\
self:set_velocity(e1, 2, 2, 2)\n\
-- tick advances every entity that has BOTH position and velocity\n\
self:tick()\n\
local x, y, z = self:get_position(e1)\n\
print('\\tafter tick #1: e1 = (' .. tostring(x) .. ', ' .. tostring(y) .. ', ' .. tostring(z) .. ')')\n\
self:tick()\n\
x, y, z = self:get_position(e2)\n\
print('\\tafter tick #2: e2 = (' .. tostring(x) .. ', ' .. tostring(y) .. ', ' .. tostring(z) .. ')')\n\
-- an entity without the full archetype is invisible to the query\n\
local e3 = self:create_entity()\n\
self:add_components(e3, 0, 0, 0, 5, 0, 0)\n\
self:remove_components(e3)\n\
print('\\te3 has components after remove_components: ' .. tostring(self:has_component(e3)))\n\
-- component removal and entity recycling\n\
self:remove_components(e2)\n\
self:remove_entity(e2)\n\
print('\\tcount after remove_entity: ' .. tostring(self:get_entity_count()))\n\
print('\\tmoved_count = ' .. tostring(self:get_moved_count()))\n\
print('[tutorial_system] complete!')\n"));
	}

	tutorial_system_t::tutorial_system_t() {}
	tutorial_system_t::~tutorial_system_t() noexcept {}

	int tutorial_system_t::create_entity() {
		return static_cast<int>(allocator.allocate());
	}

	void tutorial_system_t::remove_entity(int entity) {
		system.remove(static_cast<entity_t>(entity));
		allocator.free(static_cast<entity_t>(entity));
	}

	int tutorial_system_t::get_entity_count() const noexcept {
		return static_cast<int>(system.size());
	}

	bool tutorial_system_t::has_component(int entity) const noexcept {
		return system.valid(static_cast<entity_t>(entity));
	}

	void tutorial_system_t::add_components(int entity, float px, float py, float pz, float vx, float vy, float vz) {
		position_t position;
		position.x = px;
		position.y = py;
		position.z = pz;
		velocity_t velocity;
		velocity.x = vx;
		velocity.y = vy;
		velocity.z = vz;
		system.insert(static_cast<entity_t>(entity), position, velocity);
	}

	void tutorial_system_t::set_velocity(int entity, float vx, float vy, float vz) {
		// get() returns a reference: modify the single component in place
		// without touching the rest of the archetype
		velocity_t& velocity = system.get<velocity_t>(static_cast<entity_t>(entity));
		velocity.x = vx;
		velocity.y = vy;
		velocity.z = vz;
	}

	void tutorial_system_t::remove_components(int entity) {
		system.remove(static_cast<entity_t>(entity));
	}

	std::tuple<float, float, float> tutorial_system_t::get_position(int entity) const {
		// the entity must have the component (check has_component first)
		const position_t& position = system.get<position_t>(static_cast<entity_t>(entity));
		return std::make_tuple(position.x, position.y, position.z);
	}

	std::tuple<float, float, float> tutorial_system_t::get_velocity(int entity) const {
		const velocity_t& velocity = system.get<velocity_t>(static_cast<entity_t>(entity));
		return std::make_tuple(velocity.x, velocity.y, velocity.z);
	}

	void tutorial_system_t::tick() {
		// archetype-based iteration: only entities having BOTH components are
		// visited, in one pass, no per-entity lookup
		system.iterate<position_t, velocity_t>([](position_t& position, velocity_t& velocity) {
			position.x += velocity.x;
			position.y += velocity.y;
			position.z += velocity.z;
		});

		moved_count++;
	}

	int tutorial_system_t::get_moved_count() const noexcept {
		return moved_count;
	}
}
