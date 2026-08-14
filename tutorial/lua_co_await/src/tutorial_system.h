// tutorial_system.h
// PaintDream (paintdream@paintdream.com)
// 2026
//
// Entity Component System tutorial (distilled from test/iris_system_demo.cpp;
// full-scale: paintsnownext plugin/space). Demonstrates:
//   - iris_entity_allocator_t: entity id allocation/free
//   - iris_system_t: archetype-based component storage -- insert() sets a
//     WHOLE component set (it replaces the archetype), while get<comp>()
//     returns a reference for single-component modification
//   - iterate<components...>: visit only entities having all listed
//     components, in one pass (cache-friendly archetype iteration)
//   - binding the ECS to Lua (multi-value returns via std::tuple)

#pragma once

#include "common.h"
#include <iris_system.h>

#include <tuple>

namespace iris {
	using entity_t = uint32_t;

	template <typename element_t>
	using block_allocator_t = iris_block_allocator_t<element_t>;

	struct position_t {
		float x = 0, y = 0, z = 0;
	};

	struct velocity_t {
		float x = 0, y = 0, z = 0;
	};

	class tutorial_system_t {
	public:
		static void lua_registar(iris_lua_t&& lua, std::nullptr_t);

		tutorial_system_t();
		~tutorial_system_t() noexcept;

		// entities
		int create_entity();
		void remove_entity(int entity);
		int get_entity_count() const noexcept;
		// components
		bool has_component(int entity) const noexcept;
		// insert the WHOLE component set (archetype = position + velocity)
		void add_components(int entity, float px, float py, float pz, float vx, float vy, float vz);
		// modify a SINGLE component through the returned reference
		void set_velocity(int entity, float vx, float vy, float vz);
		void remove_components(int entity);
		// queries (tuple -> multiple Lua return values)
		std::tuple<float, float, float> get_position(int entity) const;
		std::tuple<float, float, float> get_velocity(int entity) const;
		// simulation step: advance every entity that has position + velocity
		void tick();
		int get_moved_count() const noexcept;

	protected:
		iris_entity_allocator_t<entity_t> allocator;
		iris_system_t<entity_t, block_allocator_t, std::allocator, position_t, velocity_t> system;
		int moved_count = 0;
	};
}
