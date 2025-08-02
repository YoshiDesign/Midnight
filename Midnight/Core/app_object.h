#pragma once

#include "aveng_model.h"
#include "AvengComponent.h"
#include <iostream>
#include <memory>
#include <unordered_map>

namespace aveng {

	class AvengAppObject 
	{

	public:
		using id_t = unsigned int;
		using Map = std::unordered_map<id_t, AvengAppObject>;

		static AvengAppObject createAppObject(int texture_id)
		{
			static id_t currentId = 0;
			return AvengAppObject{ currentId++, texture_id };
		}

		AvengAppObject(id_t objId, int texture_id) : id{ objId }, texture_id{ texture_id } {}
		AvengAppObject(const AvengAppObject&) = delete;				// Copy
		AvengAppObject& operator=(const AvengAppObject&) = delete;	// Copy Assignment 
		AvengAppObject(AvengAppObject&&) = default;					// Move
		AvengAppObject& operator=(AvengAppObject&&) = default;		// Move Assignment

		id_t getId() const { return id; }
		std::shared_ptr<AvengModel> model{};

		inline int get_texture() const { return texture_id; }
		inline void set_texture(int texture) { texture_id = texture; }

		glm::vec3 color{};

		MetaComponent meta{};
		TransformComponent transform{};
		VisualComponent visual{};

	private:

		int texture_id;
		id_t id;
		
	};
}