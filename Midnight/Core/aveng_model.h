#pragma once

#include "../CoreVK/EngineDevice.h"
#include "../CoreVK/aveng_buffer.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace aveng {

	class AvengModel 
	{

	public:

		struct Vertex {
			// These 4 items get packed into our vertex buffers
			glm::vec3 position{};		// Position of the vertex
			glm::vec3 color{};			// color at this vertex
			glm::vec3 normal{};			// surface norms
			glm::vec2 texCoord{};		// 2d texture coordinates

			/*
			* Required to communicate with the vertex shader.
			* Descriptions of our vertex buffers and how they are to be bound.
			*/
			static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
			static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

			// This is used with our hashing function to generate keys in our ordered map of vertices
			bool operator==(const Vertex& other) const 
			{
				return position == other.position && color == other.color && normal == other.normal && texCoord == other.texCoord;
			}

		};

		// Vertex and index information to be sent to the model's vertex and index buffer memory
		struct Builder {
			std::vector<Vertex> vertices{};
			std::vector<uint32_t> indices{};

			void loadModel(const std::string& filepath);
		};

		//AvengModel(EngineDevice& device, const AvengModel::Builder& builder);
		AvengModel(EngineDevice& device, std::vector<AvengModel::Vertex> vertices, std::vector<uint32_t> indices);
		~AvengModel();

		AvengModel(const AvengModel&) = delete;
		AvengModel& operator=(const AvengModel&) = delete;

		static std::unique_ptr<AvengModel> createModelFromFile(EngineDevice& device, const std::string& filepath);
		static std::unique_ptr<AvengModel> drawTriangle(EngineDevice& device, glm::vec3 pos);
		
		void bind(VkCommandBuffer commandBuffer);
		void draw(VkCommandBuffer commandBuffer);
	
	private:

		void createVertexBuffers(const std::vector<Vertex>& vertices);
		void createIndexBuffers(const std::vector<uint32_t>& indices);

		EngineDevice& engineDevice;
		uint32_t vertexCount;
		bool hasIndexBuffer = false;
		uint32_t indexCount;

		/*VkBuffer vertexBuffer;		OLD
		VkDeviceMemory vertexBufferMemory;*/
		// NEW
		std::unique_ptr<AvengBuffer> vertexBuffer;

		/*VkBuffer indexBuffer;			OLD
		VkDeviceMemory indexBufferMemory;*/
		// NEW
		std::unique_ptr<AvengBuffer> indexBuffer;

	};

} //