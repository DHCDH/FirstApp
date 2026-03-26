#pragma once

#include <windows.h>
#include "renderdoc_app.h"
#include <iostream>

class RenderDocHelper
{
public:
	// 开始抓帧
    static void StartCapture()
    {
        RENDERDOC_API_1_1_2* rdoc = GetAPI();
        if (rdoc) {
            std::cout << "[RenderDoc] >>> Start Capture\n";
            rdoc->StartFrameCapture(nullptr, nullptr);
        }
    }

    // 结束抓帧
    static void EndCapture()
    {
        RENDERDOC_API_1_1_2* rdoc = GetAPI();
        if (rdoc) {
            rdoc->EndFrameCapture(nullptr, nullptr);
            std::cout << "[RenderDoc] <<< End Capture. Check RenderDoc UI!\n";
        }
    }

private:
    static RENDERDOC_API_1_1_2* GetAPI()
    {
        static RENDERDOC_API_1_1_2* rdoc_api = nullptr;
        if (!rdoc_api) {
            if (HMODULE mod = GetModuleHandleA("renderdoc.dll")) {
                pRENDERDOC_GetAPI RENDERDOC_GetAPI =
                    (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
                int ret =
                    RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
                if (ret != 1) rdoc_api = nullptr;
            }
        }
        return rdoc_api;
    }
};

#define RENDERDOC_START RenderDocHelper::StartCapture();
#define RENDERDOC_END RenderDocHelper::EndCapture();