#pragma once

#include "lve/LveWindow.h"
#include "lve/LveDevice.h"
#include "lve/LveRenderer.h"
#include "lve/LveCamera.h"
#include "lve/RenderSystem.h"

#include <memory>
#include <vector>

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
	std::vector<LveObject> m_objects;

	glm::vec3 m_cameraTraget{ 0.f, 0.f, 1.f };
	float m_cameraDistance{ 0.1f };

	void SetLveComponants(void* nativeWindowHandle, void* nativeInstanceHandle, int w, int h, std::string name);
	void LoadObjects();
	void UpdateCameraFromOrbit();

};

}  // namespace lve