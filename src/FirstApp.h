#pragma once

#include "LveWindow.h"
#include "LveDevice.h"
#include "LveRenderer.h"
#include "LveCamera.h"
#include "systems/RenderSystem.h"
#include "systems/PointLightSystem.h"
#include "LveDescriptors.h"
#include "LveTexture.h"
#include "LveTextureManager.h"

#include <memory>
#include <vector>
#include <chrono>
#include <array>
#include <unordered_map>

namespace lve {

class FirstApp {
public:
	FirstApp(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name);
	~FirstApp();

	FirstApp(const FirstApp&) = delete;
    FirstApp& operator=(const FirstApp&) = delete;

	LveWindow* GetLveWindow() const { return m_lveWindow.get(); }

	void runFrame();
	void WaitIdle();
	/*交互接口*/
	void Orbit(float dxPixels, float dyPixels);	// 左键拖拽旋转
	void Pan(float dxPixels, float dyPixels);	// 中键平移
	void Dolly(float steps);					// 滚轮缩放
	void ResetView();

private:
	void InitLveComponants(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name);
	void LoadObjects();
	void UpdateCameraFromOrbit();
	void CreateSunLight();

private:
	struct OrbiState {
		glm::vec3 target{ 0.f, 0.f, 2.5f };	// 观察中心
		float distance{ 5.f };	// 与目标距离
		float yaw{ glm::pi<float>() };		// 绕Y轴旋转
		float pitch{ 0.f };		// 绕X轴旋转
	}m_orbit;

	std::unique_ptr<LveWindow> m_lveWindow;
	std::unique_ptr<LveDevice> m_lveDevice;
    std::unique_ptr<LveRenderer> m_lveRenderer;
	std::unique_ptr<LveCamera> m_lveCamera;
	std::unique_ptr<RenderSystem> m_renderSystem;
	std::unique_ptr<PointLightSystem> m_pointLightSystem;
	std::unique_ptr<LveDescriptorPool> m_globalPool;
	std::vector<std::unique_ptr<LveBuffer>> m_uboBuffers;
	std::unique_ptr<LveDescriptorSetLayout> m_globalSetLayout;
	std::vector<VkDescriptorSet> m_globalDescriptorSets;

	struct MaterialGPU {
		std::array<std::unique_ptr<lve::LveBuffer>, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT> ubos;
		std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT> sets{};
	};
	std::unique_ptr<LveTexture> m_texture;
	std::unique_ptr<LveDescriptorPool> m_materialPool;
	std::unique_ptr<LveDescriptorSetLayout> m_materialSetLayout;
	VkSampler m_sharedSampler = VK_NULL_HANDLE;
	std::unique_ptr<LveTextureManager> m_textureManager;
	std::unordered_map<uint32_t, VkDescriptorSet> m_objectMaterialSets;
	// ① set=2：材质参数用的描述符资源
	std::unique_ptr<lve::LveDescriptorPool>      m_materialParamPool;       // UBO 池
	std::unique_ptr<lve::LveDescriptorSetLayout> m_materialParamSetLayout;  // 仅 binding=0: UBO
	std::unordered_map<uint32_t, MaterialGPU> m_objectMaterialParams;
	std::unordered_map<uint32_t, std::vector<VkDescriptorSet>> m_submeshTextureSets;	// 材质贴图表，set = 1；[objId] = submeshId
	VkDescriptorSet m_defaultTextureSet{};	// 默认贴图，当某个obj的submesh没有配置贴图时，仍保证set=1一定被绑定

	std::unordered_map<uint32_t, std::vector<
		std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT>>> m_submeshMatSets;	// 材质参数UBO表，set = 2；
	std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT> m_dummyMatSets{};
	std::vector<std::unique_ptr<lve::LveBuffer>> m_materialParamBuffers;
	void CreateMaterialParamSetsForSubmesh(uint32_t objId, uint32_t submeshIndex, const MaterialUBO& u);

	void AssignMaterialParamsToObject(uint32_t objId, const MaterialUBO& init);
	void UpdateMaterialParamsPerFrame(uint32_t objId, int frameIndex, const MaterialUBO& data);

	LveObject::Map m_objects;

	glm::vec3 m_cameraTraget{ 0.f, 0.f, 1.f };
	float m_cameraDistance{ 0.1f };

	std::chrono::high_resolution_clock::time_point m_lastTick{};
	float m_frameTimeSec = 0.f;

	LveObject::id_t m_headlightId{};	// 跟随相机的点光源
	std::vector<LveObject::id_t> m_sunLightIds{};

};

}  // namespace lve