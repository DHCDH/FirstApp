#include "LvePipeline.h"
#include "LveModel.h"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <cassert>

namespace lve {

LvePipeline::LvePipeline(LveDevice& device, const std::string& vertFilepath, 
	const std::string& fragFilepath, const PipelineConfigInfo& configInfo)
	: m_lveDevice{device}
{
	std::cout << "Construct LvePipeline" << "\n";
	std::cout << "vertFilepath: " << vertFilepath << "\n";
	std::cout << "fragFilepath: " << fragFilepath << "\n";
	CreateGraphicsPipeline(vertFilepath, fragFilepath, configInfo);
}

LvePipeline::~LvePipeline()
{
	vkDestroyShaderModule(m_lveDevice.device(), m_vertShaderModule, nullptr);
	vkDestroyShaderModule(m_lveDevice.device(), m_fragShaderModule, nullptr);
	vkDestroyPipeline(m_lveDevice.device(), m_graphicsPipeline, nullptr);
}

/*读取文件*/
std::vector<char> LvePipeline::ReadFile(const std::string& filepath)
{
	std::cout << "Enter function: " << __FUNCTION__ << "\n";
	std::cout << "CWD: " << std::filesystem::current_path() << std::endl;
	std::cout << "filepath: " << filepath << std::endl;
	std::ifstream file(filepath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filepath);
	}

	size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

/*创建图形管线*/
void LvePipeline::CreateGraphicsPipeline(const std::string& vertFilepath, const std::string& fragFilepath, const PipelineConfigInfo& configInfo)
{
	assert(configInfo.pipelineLayout != VK_NULL_HANDLE && "Cannot create graphics pipeline: no pipelineLayout provided in configInfo");
	assert(configInfo.renderPass != VK_NULL_HANDLE && "Cannot create graphics pipeline: no renderPass provided in configInfo");

	auto vertCode = ReadFile(vertFilepath);
	auto fragCode = ReadFile(fragFilepath);

	CreateShaderModule(vertCode, &m_vertShaderModule);
	CreateShaderModule(fragCode, &m_fragShaderModule);

	/*两个着色器阶段：0 = 顶点， 1 = 片段*/
	VkPipelineShaderStageCreateInfo shaderStages[2] = {};
	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;	// 顶点着色器
	shaderStages[0].module = m_vertShaderModule;	// 绑定顶点着色器的VkShaderModule
	shaderStages[0].pName = "main";	// SPIR-V 的 entry point 名称（GLSL 默认 main，若编译时改过，这里也要一致）
	shaderStages[0].flags = 0;
    shaderStages[0].pNext = nullptr;
	shaderStages[0].pSpecializationInfo = nullptr;

	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].module = m_fragShaderModule;
	shaderStages[1].pName = "main";	// SPIR-V 的 entry point 名称（GLSL 默认 main，若编译时改过，这里也要一致）
	shaderStages[1].flags = 0;
	shaderStages[1].pNext = nullptr;
	shaderStages[1].pSpecializationInfo = nullptr;

	/*顶点输入*/
	auto& bindingDescription = configInfo.bindingDescriptions;
	auto& attributeDescriptions = configInfo.attributeDescriptions;

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	//不从顶点缓冲读取任何属性（attribute和binding均为0）
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescription.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data(); // 应用顶点属性描述
	vertexInputInfo.pVertexBindingDescriptions = bindingDescription.data();	// 应用顶点绑定描述

	/*大总管结构体：把所有固定功能状态+着色器阶段汇总*/
	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	/*指定两个shader（顶点+片段）*/
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &configInfo.inputAssemblyInfo;
    pipelineInfo.pViewportState = &configInfo.viewportInfo;
	pipelineInfo.pRasterizationState = &configInfo.rasterizationInfo;
	pipelineInfo.pMultisampleState = &configInfo.multisampleInfo;
	pipelineInfo.pColorBlendState = &configInfo.colorBlendInfo;
	pipelineInfo.pDepthStencilState = &configInfo.depthStencilInfo;
	pipelineInfo.pDynamicState = &configInfo.dynamicStateInfo;

	/*绑定管线布局*/
	pipelineInfo.layout = configInfo.pipelineLayout;
	pipelineInfo.renderPass = configInfo.renderPass;
	pipelineInfo.subpass = configInfo.subpass;

	/*不做派生管线*/
	pipelineInfo.basePipelineIndex = -1;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	if (vkCreateGraphicsPipelines(m_lveDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS) {
		throw std::runtime_error("failed to create graphics pipeline");
	}

}

void LvePipeline::CreateShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule)
{
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	if (vkCreateShaderModule(m_lveDevice.device(), &createInfo, nullptr, shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module");
	}

}

void LvePipeline::Bind(VkCommandBuffer commandBuffer)
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
}

void LvePipeline::DefaultPipelineConfigInfo(PipelineConfigInfo& configInfo)
{
	/*图元装配*/
	configInfo.inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; // 标明结构体是用于描述图元装配阶段配置的
	configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; //每三个顶点构成一个独立三角形
	configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;	// 关闭图元重启

	configInfo.viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	configInfo.viewportInfo.viewportCount = 1;
	configInfo.viewportInfo.pViewports = nullptr;
	configInfo.viewportInfo.scissorCount = 1;
	configInfo.viewportInfo.pScissors = nullptr;

	/*光栅化*/
	configInfo.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; //标明结构体是描述光栅化阶段配置的
	configInfo.rasterizationInfo.depthClampEnable = VK_FALSE;	// 关闭深度钳制（如果开启，超出近平远平面的片元会被钳到边界，而不是被裁剪掉
	configInfo.rasterizationInfo.rasterizerDiscardEnable = VK_FALSE;	// 不丢弃光栅化（如果开启，将不产生任何片元输出）
	configInfo.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;	// 三角形以填充方式绘制（而不是线框、点，非FILL需要GPU支持）
	configInfo.rasterizationInfo.lineWidth = 1.0f;	// 线宽1，大于1需要线宽特性？
	configInfo.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;	// 关闭背面剔除。性能略低，但不易出现正反面反了导致看不见的坑
	configInfo.rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;	// 规定顺时针顶点顺序为正面
	configInfo.rasterizationInfo.depthBiasEnable = VK_FALSE;	// 关闭深度偏移
	/*深度偏移的三个参数，未开启时无效*/
	configInfo.rasterizationInfo.depthBiasConstantFactor = 0.0f;  // Optional
	configInfo.rasterizationInfo.depthBiasClamp = 0.0f;           // Optional
	configInfo.rasterizationInfo.depthBiasSlopeFactor = 0.0f;     // Optional

	/*多重采样*/
	configInfo.multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; // 标明结构体是描述多重采样阶段配置的
	configInfo.multisampleInfo.sampleShadingEnable = VK_FALSE;	// 关闭每样本着色
	configInfo.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;	// 1个样本（无MSAA），与渲染通道/附件的样本数必须匹配
	configInfo.multisampleInfo.minSampleShading = 1.0f;           // Optional
	configInfo.multisampleInfo.pSampleMask = nullptr;             // Optional
	configInfo.multisampleInfo.alphaToCoverageEnable = VK_FALSE;  // Optional
	configInfo.multisampleInfo.alphaToOneEnable = VK_FALSE;       // Optional

	/*颜色混合*/
	configInfo.colorBlendAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;	// 允许写出RGBA四个通道
	configInfo.colorBlendAttachment.blendEnable = VK_FALSE;	// 关闭颜色混合（新片元颜色直接覆盖目标颜色）
	configInfo.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
	configInfo.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
	configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
	configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
	configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
	configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional

	configInfo.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;	// 颜色混合状态
	configInfo.colorBlendInfo.logicOpEnable = VK_FALSE;	// 关闭逻辑运算，按位运算式混色方式
	configInfo.colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;  // Optional
	configInfo.colorBlendInfo.attachmentCount = 1;	// 只有1个颜色附件需要配置混合
	configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;	// 指向colorBlendAttachment的颜色混合
	configInfo.colorBlendInfo.blendConstants[0] = 0.0f;  // Optional
	configInfo.colorBlendInfo.blendConstants[1] = 0.0f;  // Optional
	configInfo.colorBlendInfo.blendConstants[2] = 0.0f;  // Optional
	configInfo.colorBlendInfo.blendConstants[3] = 0.0f;  // Optional

	configInfo.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;	// 深度/模板状态
	configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;	// 开启深度测试
	configInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;	// 通过深度测试的片元会写入深度缓冲
	configInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;	// 片元深度小于当前深度才通过（更近覆盖更远）
	configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;	// 关闭深度区间测试（只有在做特殊裁剪时会用到）
	configInfo.depthStencilInfo.minDepthBounds = 0.0f;  // Optional
	configInfo.depthStencilInfo.maxDepthBounds = 1.0f;  // Optional
	configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;	// 关闭模板测试
	configInfo.depthStencilInfo.front = {};  // Optional
	configInfo.depthStencilInfo.back = {};   // Optional

	configInfo.dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	configInfo.dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
	configInfo.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
	configInfo.dynamicStateInfo.flags = 0;

	configInfo.bindingDescriptions = LveModel::Vertex::GetBindingDescriptions();
	configInfo.attributeDescriptions = LveModel::Vertex::GetAttributeDescriptions();
}

void LvePipeline::EnableAlphaBlending(PipelineConfigInfo& configInfo)
{
	configInfo.colorBlendAttachment.blendEnable = VK_TRUE;	// 启用会有性能成本，只有需要使用alpha混合的对象才启用

	configInfo.colorBlendAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;	// 允许写出RGBA四个通道
	
	configInfo.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;  
	configInfo.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; 
	configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;             
	configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  
	configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; 
	configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;             
}

}