bool VkRenderer::createDescriptorLayouts() {
    VkResult result;
  
    {
      /* texture */
      VkDescriptorSetLayoutBinding assimpTextureBind{};
      assimpTextureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      assimpTextureBind.binding = 0;
      assimpTextureBind.descriptorCount = 1;
      assimpTextureBind.pImmutableSamplers = nullptr;
      assimpTextureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  
      std::vector<VkDescriptorSetLayoutBinding> assimpTexBindings = { assimpTextureBind };

      VkDescriptorSetLayoutCreateInfo assimpTextureCreateInfo{};
      assimpTextureCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      assimpTextureCreateInfo.bindingCount = static_cast<uint32_t>(assimpTexBindings.size());
      assimpTextureCreateInfo.pBindings = assimpTexBindings.data();
  
      result = vkCreateDescriptorSetLayout(mRenderData.rdVkbDevice.device, &assimpTextureCreateInfo,
        nullptr, &mRenderData.rdAssimpTextureDescriptorLayout);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not create Assimp texture descriptor set layout (error: %i)\n", __FUNCTION__, result);
        return false;
      }
    }
  
    {
      /* non-animated shader */
      VkDescriptorSetLayoutBinding assimpUboBind{};
      assimpUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      assimpUboBind.binding = 0;
      assimpUboBind.descriptorCount = 1;
      assimpUboBind.pImmutableSamplers = nullptr;
      assimpUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  
      VkDescriptorSetLayoutBinding assimpSsboBind{};
      assimpSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpSsboBind.binding = 1;
      assimpSsboBind.descriptorCount = 1;
      assimpSsboBind.pImmutableSamplers = nullptr;
      assimpSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  
      std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpUboBind, assimpSsboBind };
  
      VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
      assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
      assimpCreateInfo.pBindings = assimpBindings.data();
  
      result = vkCreateDescriptorSetLayout(mRenderData.rdVkbDevice.device, &assimpCreateInfo,
        nullptr, &mRenderData.rdAssimpDescriptorLayout);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not create Assimp buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
        return false;
      }
    }
  
    {
      /* animated shader */
      VkDescriptorSetLayoutBinding assimpUboBind{};
      assimpUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      assimpUboBind.binding = 0;
      assimpUboBind.descriptorCount = 1;
      assimpUboBind.pImmutableSamplers = nullptr;
      assimpUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  
      VkDescriptorSetLayoutBinding assimpSkinningSsboBind{};
      assimpSkinningSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpSkinningSsboBind.binding = 1;
      assimpSkinningSsboBind.descriptorCount = 1;
      assimpSkinningSsboBind.pImmutableSamplers = nullptr;
      assimpSkinningSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  
      VkDescriptorSetLayoutBinding assimpSkinningSsboBind2{};
      assimpSkinningSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpSkinningSsboBind2.binding = 2;
      assimpSkinningSsboBind2.descriptorCount = 1;
      assimpSkinningSsboBind2.pImmutableSamplers = nullptr;
      assimpSkinningSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  
      std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings = { assimpUboBind, assimpSkinningSsboBind, assimpSkinningSsboBind2 };
  
      VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
      assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
      assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();
  
      result = vkCreateDescriptorSetLayout(mRenderData.rdVkbDevice.device, &assimpSkinningCreateInfo,
        nullptr, &mRenderData.rdAssimpSkinningDescriptorLayout);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not create Assimp skinning buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
        return false;
      }
    }
  
    {
      /* compute transformation shader */
      VkDescriptorSetLayoutBinding assimpTransformSsboBind{};
      assimpTransformSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpTransformSsboBind.binding = 0;
      assimpTransformSsboBind.descriptorCount = 1;
      assimpTransformSsboBind.pImmutableSamplers = nullptr;
      assimpTransformSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  
      VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
      assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpTrsSsboBind.binding = 1;
      assimpTrsSsboBind.descriptorCount = 1;
      assimpTrsSsboBind.pImmutableSamplers = nullptr;
      assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  
      std::vector<VkDescriptorSetLayoutBinding> assimpTransformBindings = { assimpTransformSsboBind, assimpTrsSsboBind };
  
      VkDescriptorSetLayoutCreateInfo assimpTransformCreateInfo{};
      assimpTransformCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      assimpTransformCreateInfo.bindingCount = static_cast<uint32_t>(assimpTransformBindings.size());
      assimpTransformCreateInfo.pBindings = assimpTransformBindings.data();
  
      result = vkCreateDescriptorSetLayout(mRenderData.rdVkbDevice.device, &assimpTransformCreateInfo,
        nullptr, &mRenderData.rdAssimpComputeTransformDescriptorLayout);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not create Assimp transform compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
        return false;
      }
    }
  
    {
      /* compute matrix multiplication shader, global data */
      VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
      assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpTrsSsboBind.binding = 0;
      assimpTrsSsboBind.descriptorCount = 1;
      assimpTrsSsboBind.pImmutableSamplers = nullptr;
      assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  
      VkDescriptorSetLayoutBinding assimpNodeMatricesSsboBind{};
      assimpNodeMatricesSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpNodeMatricesSsboBind.binding = 1;
      assimpNodeMatricesSsboBind.descriptorCount = 1;
      assimpNodeMatricesSsboBind.pImmutableSamplers = nullptr;
      assimpNodeMatricesSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  
      std::vector<VkDescriptorSetLayoutBinding> assimpMatMultBindings =
        { assimpTrsSsboBind,assimpNodeMatricesSsboBind };
  
      VkDescriptorSetLayoutCreateInfo assimpMatrixMultCreateInfo{};
      assimpMatrixMultCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      assimpMatrixMultCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultBindings.size());
      assimpMatrixMultCreateInfo.pBindings = assimpMatMultBindings.data();
  
      result = vkCreateDescriptorSetLayout(mRenderData.rdVkbDevice.device, &assimpMatrixMultCreateInfo,
        nullptr, &mRenderData.rdAssimpComputeMatrixMultDescriptorLayout);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not create Assimp matrix multiplication global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
        return false;
      }
    }
  
    {
      /* compute matrix multiplication shader, per-model data */
      VkDescriptorSetLayoutBinding assimpParentMatrixSsboBind{};
      assimpParentMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpParentMatrixSsboBind.binding = 0;
      assimpParentMatrixSsboBind.descriptorCount = 1;
      assimpParentMatrixSsboBind.pImmutableSamplers = nullptr;
      assimpParentMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  
      VkDescriptorSetLayoutBinding assimpBoneOffsetSsboBind{};
      assimpBoneOffsetSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      assimpBoneOffsetSsboBind.binding = 1;
      assimpBoneOffsetSsboBind.descriptorCount = 1;
      assimpBoneOffsetSsboBind.pImmutableSamplers = nullptr;
      assimpBoneOffsetSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  
      std::vector<VkDescriptorSetLayoutBinding> assimpMatMultPerModelBindings =
        { assimpParentMatrixSsboBind, assimpBoneOffsetSsboBind};
  
      VkDescriptorSetLayoutCreateInfo assimpMatrixMultPerModelCreateInfo{};
      assimpMatrixMultPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      assimpMatrixMultPerModelCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultPerModelBindings.size());
      assimpMatrixMultPerModelCreateInfo.pBindings = assimpMatMultPerModelBindings.data();
  
      result = vkCreateDescriptorSetLayout(mRenderData.rdVkbDevice.device, &assimpMatrixMultPerModelCreateInfo,
        nullptr, &mRenderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not create Assimp matrix multiplication per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
        return false;
      }
    }
  
    return true;
  }
  
  bool VkRenderer::createDescriptorSets() {
    /* non-animated models */
    VkDescriptorSetAllocateInfo descriptorAllocateInfo{};
    descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocateInfo.descriptorPool = mRenderData.rdDescriptorPool;
    descriptorAllocateInfo.descriptorSetCount = 1;
    descriptorAllocateInfo.pSetLayouts = &mRenderData.rdAssimpDescriptorLayout;
  
    VkResult result = vkAllocateDescriptorSets(mRenderData.rdVkbDevice.device, &descriptorAllocateInfo,
        &mRenderData.rdAssimpDescriptorSet);
     if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  
    /* animated models */
    VkDescriptorSetAllocateInfo skinningDescriptorAllocateInfo{};
    skinningDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningDescriptorAllocateInfo.descriptorPool = mRenderData.rdDescriptorPool;
    skinningDescriptorAllocateInfo.descriptorSetCount = 1;
    skinningDescriptorAllocateInfo.pSetLayouts = &mRenderData.rdAssimpSkinningDescriptorLayout;
  
    result = vkAllocateDescriptorSets(mRenderData.rdVkbDevice.device, &skinningDescriptorAllocateInfo,
      &mRenderData.rdAssimpSkinningDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Skinning descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  
    /* compute transform */
    VkDescriptorSetAllocateInfo computeTransformDescriptorAllocateInfo{};
    computeTransformDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeTransformDescriptorAllocateInfo.descriptorPool = mRenderData.rdDescriptorPool;
    computeTransformDescriptorAllocateInfo.descriptorSetCount = 1;
    computeTransformDescriptorAllocateInfo.pSetLayouts = &mRenderData.rdAssimpComputeTransformDescriptorLayout;
  
    result = vkAllocateDescriptorSets(mRenderData.rdVkbDevice.device, &computeTransformDescriptorAllocateInfo,
      &mRenderData.rdAssimpComputeTransformDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Transform Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  
    /* matrix multiplication, global data */
    VkDescriptorSetAllocateInfo computeMatrixMultDescriptorAllocateInfo{};
    computeMatrixMultDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeMatrixMultDescriptorAllocateInfo.descriptorPool = mRenderData.rdDescriptorPool;
    computeMatrixMultDescriptorAllocateInfo.descriptorSetCount = 1;
    computeMatrixMultDescriptorAllocateInfo.pSetLayouts = &mRenderData.rdAssimpComputeMatrixMultDescriptorLayout;
  
    result = vkAllocateDescriptorSets(mRenderData.rdVkbDevice.device, &computeMatrixMultDescriptorAllocateInfo,
      &mRenderData.rdAssimpComputeMatrixMultDescriptorSet);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Matrix Mult Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  
    updateDescriptorSets();
    updateComputeDescriptorSets();
  
    return true;
  }
  
  void VkRenderer::updateDescriptorSets() {
    Logger::log(1, "%s: updating descriptor sets\n", __FUNCTION__);
    /* we must update the descriptor sets whenever the buffer size has changed */
    {
      /* non-animated shader */
      VkDescriptorBufferInfo matrixInfo{};
      matrixInfo.buffer = mPerspectiveViewMatrixUBO.buffer;
      matrixInfo.offset = 0;
      matrixInfo.range = VK_WHOLE_SIZE;
  
      VkDescriptorBufferInfo worldPosInfo{};
      worldPosInfo.buffer = mShaderModelRootMatrixBuffer.buffer;
      worldPosInfo.offset = 0;
      worldPosInfo.range = VK_WHOLE_SIZE;
  
      VkWriteDescriptorSet matrixWriteDescriptorSet{};
      matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      matrixWriteDescriptorSet.dstSet = mRenderData.rdAssimpDescriptorSet;
      matrixWriteDescriptorSet.dstBinding = 0;
      matrixWriteDescriptorSet.descriptorCount = 1;
      matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;
  
      VkWriteDescriptorSet posWriteDescriptorSet{};
      posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      posWriteDescriptorSet.dstSet = mRenderData.rdAssimpDescriptorSet;
      posWriteDescriptorSet.dstBinding = 1;
      posWriteDescriptorSet.descriptorCount = 1;
      posWriteDescriptorSet.pBufferInfo = &worldPosInfo;
  
      std::vector<VkWriteDescriptorSet> writeDescriptorSets =
         { matrixWriteDescriptorSet, posWriteDescriptorSet };
  
      vkUpdateDescriptorSets(mRenderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
         writeDescriptorSets.data(), 0, nullptr);
    }
  
    {
      /* animated shader */
      VkDescriptorBufferInfo matrixInfo{};
      matrixInfo.buffer = mPerspectiveViewMatrixUBO.buffer;
      matrixInfo.offset = 0;
      matrixInfo.range = VK_WHOLE_SIZE;
  
      VkDescriptorBufferInfo boneMatrixInfo{};
      boneMatrixInfo.buffer = mShaderBoneMatrixBuffer.buffer;
      boneMatrixInfo.offset = 0;
      boneMatrixInfo.range = VK_WHOLE_SIZE;
  
      VkDescriptorBufferInfo worldPosInfo{};
      worldPosInfo.buffer = mShaderModelRootMatrixBuffer.buffer;
      worldPosInfo.offset = 0;
      worldPosInfo.range = VK_WHOLE_SIZE;
  
      VkWriteDescriptorSet matrixWriteDescriptorSet{};
      matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      matrixWriteDescriptorSet.dstSet = mRenderData.rdAssimpSkinningDescriptorSet;
      matrixWriteDescriptorSet.dstBinding = 0;
      matrixWriteDescriptorSet.descriptorCount = 1;
      matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;
  
      VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
      boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      boneMatrixWriteDescriptorSet.dstSet = mRenderData.rdAssimpSkinningDescriptorSet;
      boneMatrixWriteDescriptorSet.dstBinding = 1;
      boneMatrixWriteDescriptorSet.descriptorCount = 1;
      boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;
  
      VkWriteDescriptorSet posWriteDescriptorSet{};
      posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      posWriteDescriptorSet.dstSet = mRenderData.rdAssimpSkinningDescriptorSet;
      posWriteDescriptorSet.dstBinding = 2;
      posWriteDescriptorSet.descriptorCount = 1;
      posWriteDescriptorSet.pBufferInfo = &worldPosInfo;
  
      std::vector<VkWriteDescriptorSet> skinningWriteDescriptorSets =
        { matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet, posWriteDescriptorSet };
  
      vkUpdateDescriptorSets(mRenderData.rdVkbDevice.device, static_cast<uint32_t>(skinningWriteDescriptorSets.size()),
         skinningWriteDescriptorSets.data(), 0, nullptr);
    }
  }
  
  void VkRenderer::updateComputeDescriptorSets() {
    Logger::log(1, "%s: updating compute descriptor sets\n", __FUNCTION__);
    {
      /* transform compute shader */
      VkDescriptorBufferInfo transformInfo{};
      transformInfo.buffer = mShaderNodeTransformBuffer.buffer;
      transformInfo.offset = 0;
      transformInfo.range = VK_WHOLE_SIZE;
  
      VkDescriptorBufferInfo trsInfo{};
      trsInfo.buffer = mShaderTRSMatrixBuffer.buffer;
      trsInfo.offset = 0;
      trsInfo.range = VK_WHOLE_SIZE;
  
      VkWriteDescriptorSet transformWriteDescriptorSet{};
      transformWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      transformWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      transformWriteDescriptorSet.dstSet = mRenderData.rdAssimpComputeTransformDescriptorSet;
      transformWriteDescriptorSet.dstBinding = 0;
      transformWriteDescriptorSet.descriptorCount = 1;
      transformWriteDescriptorSet.pBufferInfo = &transformInfo;
  
      VkWriteDescriptorSet trsWriteDescriptorSet{};
      trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      trsWriteDescriptorSet.dstSet = mRenderData.rdAssimpComputeTransformDescriptorSet;
      trsWriteDescriptorSet.dstBinding = 1;
      trsWriteDescriptorSet.descriptorCount = 1;
      trsWriteDescriptorSet.pBufferInfo = &trsInfo;
  
      std::vector<VkWriteDescriptorSet> transformWriteDescriptorSets =
        { transformWriteDescriptorSet, trsWriteDescriptorSet };
  
      vkUpdateDescriptorSets(mRenderData.rdVkbDevice.device, static_cast<uint32_t>(transformWriteDescriptorSets.size()),
         transformWriteDescriptorSets.data(), 0, nullptr);
    }
  
    {
      /* matrix multiplication compute shader, global data */
      VkDescriptorBufferInfo trsInfo{};
      trsInfo.buffer = mShaderTRSMatrixBuffer.buffer;
      trsInfo.offset = 0;
      trsInfo.range = VK_WHOLE_SIZE;
  
      VkDescriptorBufferInfo boneMatrixInfo{};
      boneMatrixInfo.buffer = mShaderBoneMatrixBuffer.buffer;
      boneMatrixInfo.offset = 0;
      boneMatrixInfo.range = VK_WHOLE_SIZE;
  
      VkWriteDescriptorSet trsWriteDescriptorSet{};
      trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      trsWriteDescriptorSet.dstSet = mRenderData.rdAssimpComputeMatrixMultDescriptorSet;
      trsWriteDescriptorSet.dstBinding = 0;
      trsWriteDescriptorSet.descriptorCount = 1;
      trsWriteDescriptorSet.pBufferInfo = &trsInfo;
  
      VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
      boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      boneMatrixWriteDescriptorSet.dstSet = mRenderData.rdAssimpComputeMatrixMultDescriptorSet;
      boneMatrixWriteDescriptorSet.dstBinding = 1;
      boneMatrixWriteDescriptorSet.descriptorCount = 1;
      boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;
  
      std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
        { trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };
  
      vkUpdateDescriptorSets(mRenderData.rdVkbDevice.device, static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
         matrixMultWriteDescriptorSets.data(), 0, nullptr);
    }
  }