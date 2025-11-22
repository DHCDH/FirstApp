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
#include "Global.h"
#include "entities/GrindingWheel.h"
#include "entities/Blank.h"

#include <memory>
#include <vector>
#include <chrono>
#include <array>
#include <unordered_map>

class FirstApp {
public:
	FirstApp(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name);
	~FirstApp();

	FirstApp(const FirstApp&) = delete;
    FirstApp& operator=(const FirstApp&) = delete;

	lve::LveWindow* GetLveWindow() const { return m_lveWindow.get(); }

	void runFrame();
	void WaitIdle();

	/*交互*/
	void Orbit(float dxPixels, float dyPixels);	// 左键拖拽旋转
	void Pan(float dxPixels, float dyPixels);	// 中键平移
	void Dolly(float steps);					// 滚轮缩放
	void ResetView();

	/*动画*/
	void SetGrindingWheelMotionEnable(const bool& enabled) {
		if (!m_grindingWheel) return; m_grindingWheel->SetMotionEnabled(enabled); }

	/*实例化*/
	void BuildGrindingWheelTrackInstances(float t1, float t2, int sampleCount);

private:
	void InitLveComponants(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name);
	void LoadObjects();
	void UpdateCameraFromOrbit();
	void CreateSunLight();
	void RenderGrindingWheelTrack(lve::FrameInfo& frameInfo);

private:
	struct OrbiState {
		glm::vec3 target{ 0.f, 0.f, 2.5f };	// 观察中心
		float distance{ 5.f };	// 与目标距离
		float yaw{ glm::pi<float>() };		// 绕Y轴旋转
		float pitch{ 0.f };		// 绕X轴旋转
	}m_orbit;

	std::unique_ptr<lve::LveWindow> m_lveWindow;
	std::unique_ptr<lve::LveDevice> m_lveDevice;
    std::unique_ptr<lve::LveRenderer> m_lveRenderer;
	std::unique_ptr<lve::LveCamera> m_lveCamera;
	std::unique_ptr<lve::RenderSystem> m_renderSystem;
	std::unique_ptr<lve::PointLightSystem> m_pointLightSystem;
	std::unique_ptr<lve::LveDescriptorPool> m_globalPool;
	std::vector<std::unique_ptr<lve::LveBuffer>> m_uboBuffers;
	std::unique_ptr<lve::LveDescriptorSetLayout> m_globalSetLayout;
	std::vector<VkDescriptorSet> m_globalDescriptorSets;

	std::unique_ptr<lve::LveTexture> m_texture;
	std::unique_ptr<lve::LveDescriptorPool> m_materialPool;
	std::unique_ptr<lve::LveDescriptorSetLayout> m_materialSetLayout;
	VkSampler m_sharedSampler = VK_NULL_HANDLE;
	std::unique_ptr<lve::LveTextureManager> m_textureManager;
	std::unordered_map<uint32_t, VkDescriptorSet> m_objectMaterialSets;
	// ① set=2：材质参数用的描述符资源
	std::unique_ptr<lve::LveDescriptorPool>      m_materialParamPool;       // UBO 池
	std::unique_ptr<lve::LveDescriptorSetLayout> m_materialParamSetLayout;  // 仅 binding=0: UBO
	std::unordered_map<uint32_t, lve::MaterialGPU> m_objectMaterialParams;
	std::unordered_map<uint32_t, std::vector<VkDescriptorSet>> m_submeshTextureSets;	// 材质贴图表，set = 1；[objId] = submeshId
	VkDescriptorSet m_defaultTextureSet{};	// 默认贴图，当某个obj的submesh没有配置贴图时，仍保证set=1一定被绑定

	std::unordered_map<uint32_t, std::vector<
		std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT>>> m_submeshMatSets;	// 材质参数UBO表，set = 2；
	std::array<VkDescriptorSet, lve::LveSwapChain::MAX_FRAMES_IN_FLIGHT> m_dummyMatSets{};
	std::vector<std::unique_ptr<lve::LveBuffer>> m_materialParamBuffers;

	void UpdateMaterialParamsPerFrame(uint32_t objId, int frameIndex, const lve::MaterialUBO& data);

	lve::LveObject::Map m_objects;

	glm::vec3 m_cameraTraget{ 0.f, 0.f, 1.f };
	float m_cameraDistance{ 0.1f };

	std::chrono::high_resolution_clock::time_point m_lastTick{};
	float m_frameTimeSec = 0.f;

	std::unique_ptr<RenderContext> m_renderContext;


private:
	/*objects*/
	lve::LveObject::id_t m_headlightId{};	// 跟随相机的点光源
	std::vector<lve::LveObject::id_t> m_sunLightIds{};	// 环境光

	/*entities*/
	std::unique_ptr<GrindingWheel> m_grindingWheel;
	lve::LveObject::id_t m_grindingWheelId{};

	std::unique_ptr<Blank> m_blank;
	lve::LveObject::id_t m_blankId{};

	/*实例数组*/
	std::vector<lve::InstanceData> m_grndWheelInstances;	// CPU侧实例数组
	std::unique_ptr<lve::LveBuffer> m_grndWheelInstanceBuffer; // GPU侧实例数组
	uint32_t m_grndWheelInstanceCount{};
};