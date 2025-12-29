#include "aveng_model.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/swapchain.h"
#include "Utils/glm_includes.h"
#include "Utils/aveng_utils.h"
#include "Utils/Logger.h"

#include <cmath>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader/tiny_obj_loader.h>

namespace std {

	// This function allows us to take a vertex struct instance and hash it, for use by an unordered map key
	// This allows us to create vertex buffers which only contain unique vertices
	template<>
	struct hash<aveng::VkVertex> {
		size_t operator()(aveng::VkVertex const& vertex) const {
	
			// for final hash value
			size_t seed = 0;
			aveng::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.boneNumber, vertex.boneWeight);
			return seed;
	
		}
	};
}

namespace aveng {

	struct Model {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
	};

	AvengModel::AvengModel(EngineDevice& device) 
		: engineDevice{ device },
		  mBoneParentMatrixBuffers(SwapChain::MAX_FRAMES_IN_FLIGHT),
		  mShaderBoneMatrixOffsetBuffers(SwapChain::MAX_FRAMES_IN_FLIGHT),
		  mMatrixMultPerModelDescriptorSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
	{ /*Keep the null model in mind if logic should find its way here - see <ModelLibrary> */ }

	AvengModel::~AvengModel() {}

	void AvengModel::drawInstancedV2(VkRenderData& renderData, VkPipelineLayout basicLayout, VkPipelineLayout animationLayout, uint32_t instanceCount, int frameIndex) {
		for (unsigned int i = 0; i < mModelMeshes.size(); ++i) {
			VkMesh& mesh = mModelMeshes.at(i);

			/*
			* @Note
			* For glTF imports, Assimp often maps glTF baseColorTexture -> aiTextureType_DIFFUSE 
			* for compatibility with older code paths. Later, Assimp added a dedicated texture 
			* type aiTextureType_BASE_COLOR specifically to represent glTF’s baseColor without pretending it’s "diffuse"
			*/

			// find diffuse texture by name
			VkTextureData diffuseTex{};
			auto diffuseTexName = mesh.textures.find(aiTextureType_DIFFUSE);
			if (diffuseTexName != mesh.textures.end()) {
				auto diffuseTexture = mTextures.find(diffuseTexName->second);
				if (diffuseTexture != mTextures.end()) {
					diffuseTex = diffuseTexture->second;
				}
			}

			/* switch between animated and non-animated pipeline layout */
			VkPipelineLayout renderLayout;
			if (hasAnimations()) {
				renderLayout = animationLayout;
			}
			else {
				renderLayout = basicLayout;
			}

			if (diffuseTex.image != VK_NULL_HANDLE) {
				
				vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
					renderLayout, 0, 1, &diffuseTex.descriptorSet, 0, nullptr);
			}
			else {
				if (mesh.usesPBRColors) {
					vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderLayout, 0, 1, &mWhiteTexture.descriptorSet, 0, nullptr);
				}
				else {
					vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[frameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderLayout, 0, 1, &mPlaceholderTexture.descriptorSet, 0, nullptr);
				}
			}

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(renderData.rdCommandBuffersGraphics[frameIndex], 0, 1, &mVertexBuffers.at(i).buffer, &offset);
			vkCmdBindIndexBuffer(renderData.rdCommandBuffersGraphics[frameIndex], mIndexBuffers.at(i).buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(renderData.rdCommandBuffersGraphics[frameIndex], static_cast<uint32_t>(mesh.indices.size()), instanceCount, 0, 0, 0);
		}
	}

	std::string AvengModel::getModelFileName() {
		return mModelFilename;
	}

	std::string AvengModel::getModelFileNamePath() {
		return mModelFilenamePath;
	}

	/*V2 of V2
	
		Big TODO: Clean this sh*t up.
		const VkRenderData& renderData (if possible) not VkRenderData& renderData!
		renderData has been factored out of many of the places it propagates through.
		It will receive an overhaul sooner than later
	
	*/
	bool AvengModel::loadModelV2(
		VkRenderData& renderData,
		const AssetKey& key,                        // keep for debug + extension hint
		std::span<const std::byte> bytes,           // data from IModelSource
		unsigned int extraImportFlags,
		const std::string& modelBaseDir,     // for model-owned refs
		const std::string& contentRoot      // Texture root. for engine-owned defaults
	) {

		std::cout 
			<< "LoadModelV2V2\nContent Root: " << contentRoot 
			<< "\nModel Base Dir: " << modelBaseDir << std::endl;

		Assimp::Importer importer;

		const unsigned int flags =
			aiProcess_Triangulate |
			aiProcess_GenNormals |
			aiProcess_ValidateDataStructure |
			aiProcess_FlipUVs |
			extraImportFlags;

		const aiScene* scene = nullptr;

		if (!bytes.empty()) {
			// Extension hint helps Assimp choose the correct importer.
			// Passing key.c_str() works well if key is a path-like string (it usually is today).
			scene = importer.ReadFileFromMemory(
				bytes.data(),
				bytes.size(),
				flags,
				key.c_str()
			);
		}
		else {
			// Transitional fallback only (optional)
			scene = importer.ReadFile(key, flags);
		}

		if (!scene || !scene->mRootNode) {
			std::printf("[AvengModel] Assimp load failed for %s: %s\n",
				key.c_str(),
				importer.GetErrorString()
			);
			return false;
		}

		// ... your existing pipeline:
		// - establish rootNode
		// - build vertex/index buffers
		// - bones/animations
		// - etc.

		unsigned int numMeshes = scene->mNumMeshes;
		std::printf("Loaded AssimpModel: Found %d mesh%s.\n", numMeshes, numMeshes == 1 ? "" : "es");

		// Count vertices and faces
		for (unsigned int i = 0; i < numMeshes; ++i) {
			unsigned int numVertices = scene->mMeshes[i]->mNumVertices;
			unsigned int numFaces = scene->mMeshes[i]->mNumFaces;

			mVertexCount += numVertices;
			mTriangleCount += numFaces;

			// std::printf("%s: mesh %i contains %i vertices and %i faces\n", __FUNCTION__, i, numVertices, numFaces);
		}
		std::printf("AssimpModel: Total %d vertices and %d faces\n", mVertexCount, mTriangleCount);

		aiNode* rootNode = scene->mRootNode;

		// Only for Embedded textures.
		if (scene->HasTextures()) {
			unsigned int numTextures = scene->mNumTextures;

			std::cout << "Model has an embedded texture!!" << std::endl;

			for (int i = 0; i < scene->mNumTextures; ++i) {
				std::string texName = scene->mTextures[i]->mFilename.C_Str(); // @warn: Your real key is the "*<index>", this is fine for logging, but don’t depend on it being meaningful/unique. For embedded textures it can be empty or weird depending on importer/exporter.

				int height = scene->mTextures[i]->mHeight;
				int width = scene->mTextures[i]->mWidth;
				aiTexel* data = scene->mTextures[i]->pcData;

				VkTextureData newTex{};
				if (!Texture::loadTexture(engineDevice, renderData, newTex, texName, data, width, height)) {
					return false;
				}

				std::string internalTexName = "*" + std::to_string(i);

				mTextures.insert({ internalTexName, newTex });
			}

			std::printf("%s: scene has %i embedded textures\n", __FUNCTION__, numTextures);
		}

		/* add a white texture in case there is no diffuse tex but colors */
		std::string whiteTexName = joinPath(contentRoot, "textures/white.png");
		if (!Texture::loadTexture(engineDevice, renderData, mWhiteTexture, whiteTexName)) {
			// std::printf("%s error: could not load white default texture '%s'\n", __FUNCTION__, whiteTexName.c_str());
			return false;
		}

		/* add a placeholder texture in case there is no diffuse tex */
		std::string placeholderTexName = joinPath(contentRoot, "textures/missing_tex.png");
		if (!Texture::loadTexture(engineDevice, renderData, mPlaceholderTexture, placeholderTexName)) {
			// std::printf("%s error: could not load placeholder texture '%s'\n", __FUNCTION__, placeholderTexName.c_str());
			return false;
		}

		/* the textures are stored directly or relative to the model file */
		//std::string assetDirectory = filepath.substr(0, filepath.find_last_of('/'));


		std::string rootNodeName = rootNode->mName.C_Str();
		mRootNode = AssimpNode::createNode(rootNodeName);
		std::printf("%s: root node name: '%s'\n", __FUNCTION__, rootNodeName.c_str());

		processNode(renderData, mRootNode, rootNode, scene, modelBaseDir, contentRoot);

		/**
		  * Check your work
		  */
		for (const auto& entry : mNodeList) {
			std::vector<std::shared_ptr<AssimpNode>> childNodes = entry->getChilds();

			std::string parentName = entry->getParentNodeName();
			// std::printf("%s: --- found node %s in node list, it has %i children, parent is %s\n", __FUNCTION__, entry->getNodeName().c_str(), childNodes.size(), parentName.c_str());

			for (const auto& node : childNodes) {
				// std::printf("%s: ---- child: %s\n", __FUNCTION__, node->getNodeName().c_str());
			}
		}

		std::vector<glm::mat4> boneOffsetMatricesList{};
		std::vector<int32_t> boneParentIndexList{};

		for (const auto& bone : mBoneList) {
			boneOffsetMatricesList.emplace_back(bone->getOffsetMatrix());

			std::string parentNodeName = mNodeMap.at(bone->getBoneName())->getParentNodeName();
			const auto boneIter = std::find_if(mBoneList.begin(), mBoneList.end(), [parentNodeName](std::shared_ptr<AssimpBone>& bone) { return bone->getBoneName() == parentNodeName; });
			if (boneIter == mBoneList.end()) {
				boneParentIndexList.emplace_back(-1); // root node gets a -1 to identify
			}
			else {
				boneParentIndexList.emplace_back(std::distance(mBoneList.begin(), boneIter));
			}
		}

		// std::printf("%s: -- bone parents --\n", __FUNCTION__);
		// for (unsigned int i = 0; i < mBoneList.size(); ++i) {
			// std::printf("%s: bone %i (%s) has parent %i (%s)\n", __FUNCTION__, i, mBoneList.at(i)->getBoneName().c_str(), boneParentIndexList.at(i),
				// boneParentIndexList.at(i) < 0 ? "invalid" : mBoneList.at(boneParentIndexList.at(i))->getBoneName().c_str());
		// }
		// std::printf("%s: -- bone parents --\n", __FUNCTION__);

		/* create vertex buffers for the meshes */
		for (const auto& mesh : mModelMeshes) {
			VkVertexBufferData vertexBuffer;
			VertexBuffer::init(engineDevice, vertexBuffer, mesh.vertices.size() * sizeof(VkVertex));
			VertexBuffer::uploadData(engineDevice, vertexBuffer, mesh);
			mVertexBuffers.emplace_back(vertexBuffer);

			VkIndexBufferData indexBuffer;
			IndexBuffer::init(engineDevice, indexBuffer, mesh.indices.size() * sizeof(uint32_t));
			IndexBuffer::uploadData(engineDevice, indexBuffer, mesh);
			mIndexBuffers.emplace_back(indexBuffer);
		}

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			size_t boneMatBufferSize = boneOffsetMatricesList.size() * sizeof(glm::mat4);
			size_t boneParentBufferSize = boneParentIndexList.size() * sizeof(int32_t);

			ShaderStorageBuffer::init(engineDevice, mShaderBoneMatrixOffsetBuffers[i], MapMode::OnDemand, ResidentMode::CPU, boneMatBufferSize);
			ShaderStorageBuffer::init(engineDevice, mBoneParentMatrixBuffers[i], MapMode::OnDemand, ResidentMode::CPU, boneParentBufferSize);

			if (ShaderStorageBuffer::uploadSsboData(engineDevice, mShaderBoneMatrixOffsetBuffers[i], boneOffsetMatricesList))
			{
				throw std::runtime_error("model buffer allocation size was incorrect");
			};

			if (ShaderStorageBuffer::uploadSsboData(engineDevice, mBoneParentMatrixBuffers[i], boneParentIndexList))
			{
				throw std::runtime_error("model buffer allocation size was incorrect");
			};

		}

		/* create descriptor set (for each available frame in flight) for per-model data */
		createDescriptorSet(renderData);

		/* animations */
		unsigned int numAnims = scene->mNumAnimations;
		for (unsigned int i = 0; i < numAnims; ++i) {
			aiAnimation* animation = scene->mAnimations[i];

			// std::printf("%s: -- animation clip %i has %i skeletal channels, %i mesh channels, and %i morph mesh channels\n",
				// __FUNCTION__, i, animation->mNumChannels, animation->mNumMeshChannels, animation->mNumMorphMeshChannels);

			std::shared_ptr<AssimpAnimClip> animClip = std::make_shared<AssimpAnimClip>();
			animClip->addChannels(animation, mBoneList);
			if (animClip->getClipName().empty()) {
				animClip->setClipName(std::to_string(i));
			}
			mAnimClips.emplace_back(animClip);
		}

		// mModelFilenamePath = baseDir;
		// mModelFilename = std::filesystem::path(filepath).filename().generic_string();

		/* get root transformation matrix from model's root node */
		glm::mat4 local_gltf = Tools::convertAiToGLM(rootNode->mTransformation);
		glm::mat4 local_engine =
			Tools::gltfToEngine * local_gltf * glm::inverse(Tools::gltfToEngine);

		mRootTransformMatrix = local_engine;

		Logger::log(1, "%s: - model has a total of %i texture%s\n", __FUNCTION__, mTextures.size(), mTextures.size() == 1 ? "" : "s");
		std::printf("%s: - model has a total of %zi bone%s\n", __FUNCTION__, mBoneList.size(), mBoneList.size() == 1 ? "" : "s");
		std::printf("%s: - model has a total of %zi animation%s\n", __FUNCTION__, numAnims, numAnims == 1 ? "" : "s");
		// std::printf("%s: successfully loaded model '%s' (%s)\n", __FUNCTION__, filepath.c_str(), mModelFilename.c_str());
		return true;

	}

	bool AvengModel::createDescriptorSet(VkRenderData& renderData) {

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++)
		{
			/* matrix multiplication, per-model data */
			VkDescriptorSetAllocateInfo computeMatrixMultPerModelDescriptorAllocateInfo{}; // ...This descriptor
			computeMatrixMultPerModelDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			computeMatrixMultPerModelDescriptorAllocateInfo.descriptorPool = renderData.avengDescriptorPool;
			computeMatrixMultPerModelDescriptorAllocateInfo.descriptorSetCount = 1;
			computeMatrixMultPerModelDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout; // ...is set upon this layout

			VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &computeMatrixMultPerModelDescriptorAllocateInfo,
				&mMatrixMultPerModelDescriptorSets[i]);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not allocate Assimp Matrix Mult Compute per-model descriptor set (error: %i)\n", __FUNCTION__, result);
				return false;
			}

			std::cout << "Index:\t"  << i << "\n" <<
				"[Creating Model (mShaderBoneMatrixOffsetBuffers) Buffers] Size:\t" <<
				mShaderBoneMatrixOffsetBuffers[i].bufferSize << std::endl;

			VkDescriptorBufferInfo parentNodeInfo{};
			parentNodeInfo.buffer = mBoneParentMatrixBuffers[i].buffer;	// ...attaching this data buffer
			parentNodeInfo.offset = 0;
			parentNodeInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo boneOffsetInfo{};
			boneOffsetInfo.buffer = mShaderBoneMatrixOffsetBuffers[i].buffer; // ...and this data buffer
			boneOffsetInfo.offset = 0;
			boneOffsetInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet parentNodeWriteDescriptorSet{};
			parentNodeWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			parentNodeWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			parentNodeWriteDescriptorSet.dstSet = mMatrixMultPerModelDescriptorSets[i];
			parentNodeWriteDescriptorSet.dstBinding = 0;
			parentNodeWriteDescriptorSet.descriptorCount = 1;
			parentNodeWriteDescriptorSet.pBufferInfo = &parentNodeInfo;

			VkWriteDescriptorSet boneOffsetWriteDescriptorSet{};
			boneOffsetWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			boneOffsetWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			boneOffsetWriteDescriptorSet.dstSet = mMatrixMultPerModelDescriptorSets[i];
			boneOffsetWriteDescriptorSet.dstBinding = 1;
			boneOffsetWriteDescriptorSet.descriptorCount = 1;
			boneOffsetWriteDescriptorSet.pBufferInfo = &boneOffsetInfo;

			std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
			{ parentNodeWriteDescriptorSet, boneOffsetWriteDescriptorSet };

			vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
				matrixMultWriteDescriptorSets.data(), 0, nullptr);

		}

		return true;

	}

	void AvengModel::processNode(
		VkRenderData& renderData, 
		std::shared_ptr<AssimpNode> node, 
		aiNode* aNode,
		const aiScene* scene, 
		/*std::string assetDirectory*/ 
		const std::string modelBaseDir, 
		const std::string contentRoot) 
	{
		std::string nodeName = aNode->mName.C_Str();
		// std::printf("%s: node name: '%s'\n", __FUNCTION__, nodeName.c_str());

		unsigned int numMeshes = aNode->mNumMeshes;
		if (numMeshes > 0) {
			// std::printf("%s: - node has %i meshes\n", __FUNCTION__, numMeshes);
			for (unsigned int i = 0; i < numMeshes; ++i) {
				aiMesh* modelMesh = scene->mMeshes[aNode->mMeshes[i]];

				AssimpMesh mesh;
				mesh.processMesh(renderData, engineDevice, modelMesh, scene, /*assetDirectory*/ modelBaseDir, contentRoot, mTextures);

				mModelMeshes.emplace_back(mesh.getMesh());

				/* avoid inserting duplicate bone Ids - meshes can reference the same bones */
				std::vector<std::shared_ptr<AssimpBone>> flatBones = mesh.getBoneList();
				for (const auto& bone : flatBones) {
					const auto iter = std::find_if(mBoneList.begin(), mBoneList.end(), [bone](std::shared_ptr<AssimpBone>& otherBone) { return bone->getBoneId() == otherBone->getBoneId(); });
					if (iter == mBoneList.end()) {
						mBoneList.emplace_back(bone);
					}
				}
			}
		}

		mNodeMap.insert({ nodeName, node });
		mNodeList.emplace_back(node);

		unsigned int numChildren = aNode->mNumChildren;
		// std::printf("%s: - node has %i children \n", __FUNCTION__, numChildren);

		for (unsigned int i = 0; i < numChildren; ++i) {
			std::string childName = aNode->mChildren[i]->mName.C_Str();
			// std::printf("%s: --- found child node '%s'\n", __FUNCTION__, childName.c_str());

			std::shared_ptr<AssimpNode> childNode = node->addChild(childName);
			processNode(renderData, childNode, aNode->mChildren[i], scene, /*assetDirectory*/ modelBaseDir, contentRoot);
		}
	}

	glm::mat4 AvengModel::getRootTranformationMatrix() {
		return mRootTransformMatrix;
	}

	const std::vector<std::shared_ptr<AssimpBone>>& AvengModel::getBoneList() {
		return mBoneList;
	}

	const std::vector<std::shared_ptr<AssimpAnimClip>>& AvengModel::getAnimClips() {
		return mAnimClips;
	}

	bool AvengModel::hasAnimations() {
		return !mAnimClips.empty();
	}

	unsigned int AvengModel::getTriangleCount() {
		return mTriangleCount;
	}

	unsigned int AvengModel::getTriangleCount() const {
		return mTriangleCount;
	}

	const std::vector<VkShaderStorageBufferData>& AvengModel::getBoneMatrixOffsetBuffers() const {
		return mShaderBoneMatrixOffsetBuffers;
	}

	const std::vector<VkShaderStorageBufferData>& AvengModel::getBoneParentBuffers() const {
		return mBoneParentMatrixBuffers;
	}

	std::vector<VkDescriptorSet>& AvengModel::getMatrixMultDescriptorSets() {
		return mMatrixMultPerModelDescriptorSets;
	}

	VkDescriptorSet& AvengModel::getMatrixMultDescriptorSet(int frameIndex) {
		return mMatrixMultPerModelDescriptorSets[frameIndex];
	}

	void AvengModel::cleanup(EngineDevice& engineDevice, VkRenderData& renderData, int frames) {

		VkDescriptorPool pool = renderData.avengDescriptorPool;

		for (int i = 0; i < frames; i++) {
			vkFreeDescriptorSets(engineDevice.device(), pool, 1, &mMatrixMultPerModelDescriptorSets[i]);
		}

		for (auto buffer : mVertexBuffers) {
			VertexBuffer::cleanup(engineDevice, buffer);
		}
		for (auto buffer : mIndexBuffers) {
			IndexBuffer::cleanup(engineDevice, buffer);
		}

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			ShaderStorageBuffer::cleanup(engineDevice, mShaderBoneMatrixOffsetBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mBoneParentMatrixBuffers[i]);

		}

		for (auto& tex : mTextures) {
			Texture::cleanup(engineDevice, renderData, tex.second);
		}

		Texture::cleanup(engineDevice, renderData, mPlaceholderTexture);
		Texture::cleanup(engineDevice, renderData, mWhiteTexture);
	}


}