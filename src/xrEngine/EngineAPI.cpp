// EngineAPI.cpp: implementation of the CEngineAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EngineAPI.h"
#include "XR_IOConsole.h"

#include "xrCore/ModuleLookup.hpp"

extern xr_vector<xr_token> vid_quality_token;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void __cdecl dummy(void) {}

CEngineAPI::CEngineAPI()
{
    hGame = nullptr;
    hTuner = nullptr;
    hRenderR1 = nullptr;
    hRenderR2 = nullptr;
    hRenderR3 = nullptr;
    hRenderR4 = nullptr;
    pCreate = nullptr;
    pDestroy = nullptr;
    tune_enabled = false;
    tune_pause = dummy;
    tune_resume = dummy;
}

CEngineAPI::~CEngineAPI()
{
    vid_quality_token.clear();
}

bool is_enough_address_space_available()
{
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    return (*(u32*)&system_info.lpMaximumApplicationAddress) > 0x90000000;
}

void CEngineAPI::SetupCurrentRenderer()
{
    GEnv.CurrentRenderer = -1;

    if (psDeviceFlags.test(rsR4))
    {
        if (hRenderR4->exist())
        {
            GEnv.CurrentRenderer = 4;
            GEnv.SetupCurrentRenderer = GEnv.SetupR4;
        }
        else
        {
            psDeviceFlags.set(rsR4, false);
            psDeviceFlags.set(rsR3, true);
        }
    }

    if (psDeviceFlags.test(rsR3))
    {
        if (hRenderR3->exist())
        {
            GEnv.CurrentRenderer = 3;
            GEnv.SetupCurrentRenderer = GEnv.SetupR3;
        }
        else
        {
            psDeviceFlags.set(rsR3, false);
            psDeviceFlags.set(rsR2, true);
        }
    }

    if (psDeviceFlags.test(rsR2))
    {
        if (hRenderR2->exist())
        {
            GEnv.CurrentRenderer = 2;
            GEnv.SetupCurrentRenderer = GEnv.SetupR2;
        }
        else
        {
            psDeviceFlags.set(rsR2, false);
            psDeviceFlags.set(rsR1, true);
        }
    }

    if (psDeviceFlags.test(rsR1))
    {
        if (hRenderR1->exist())
        {
            GEnv.CurrentRenderer = 1;
            GEnv.SetupCurrentRenderer = GEnv.SetupR1;
        }
        else
            psDeviceFlags.set(rsR1, false);
    }
}

void CEngineAPI::InitializeRenderers()
{
    SetupCurrentRenderer();

    if (GEnv.SetupCurrentRenderer == nullptr
        && vid_quality_token[0].id != -1)
    {
        // if engine failed to load renderer
        // but there is at least one available
        // then try again
        string32 buf;
        xr_sprintf(buf, "renderer %s", vid_quality_token[0].name);
        Console->Execute(buf);

        // Second attempt
        InitializeRenderers();
    }

    // ask current renderer to setup GlobalEnv
    R_ASSERT2(GEnv.SetupCurrentRenderer, "Can't setup renderer");
    GEnv.SetupCurrentRenderer();

    // Now unload unused renderers
    // XXX: Unloading disabled due to typeids invalidation
    /*if (GEnv.CurrentRenderer != 4)
        hRenderR4->close();

    if (GEnv.CurrentRenderer != 3)
        hRenderR3->close();

    if (GEnv.CurrentRenderer != 2)
        hRenderR2->close();

    if (GEnv.CurrentRenderer != 1)
        hRenderR1->close();*/
}

void CEngineAPI::Initialize(void)
{
    InitializeRenderers();

    hGame = XRay::LoadModule("xrGame");
    R_ASSERT2(hGame, "Game DLL raised exception during loading or there is no game DLL at all");

    pCreate = (Factory_Create*)hGame->getProcAddress("xrFactory_Create");
    R_ASSERT(pCreate);

    pDestroy = (Factory_Destroy*)hGame->getProcAddress("xrFactory_Destroy");
    R_ASSERT(pDestroy);

    //////////////////////////////////////////////////////////////////////////
    // vTune
    tune_enabled = false;
    if (strstr(Core.Params, "-tune"))
    {
        hTuner = XRay::LoadModule("vTuneAPI");
        tune_pause = (VTPause*)hTuner->getProcAddress("VTPause");
        tune_resume = (VTResume*)hTuner->getProcAddress("VTResume");

        if (!tune_pause || !tune_pause)
        {
            Log("Can't initialize Intel vTune");
            tune_pause = dummy;
            tune_resume = dummy;
            return;
        }

        tune_enabled = true;
    }
}

void CEngineAPI::Destroy(void)
{
    hGame = nullptr;
    hTuner = nullptr;
    hRenderR1 = nullptr;
    hRenderR2 = nullptr;
    hRenderR3 = nullptr;
    hRenderR4 = nullptr;
    pCreate = nullptr;
    pDestroy = nullptr;
    Engine.Event._destroy();
    XRC.r_clear_compact();
}

void CEngineAPI::CreateRendererList()
{
    hRenderR1 = XRay::LoadModule("xrRender_R1");

    xr_vector<xr_token> modes;
    if (GEnv.isDedicatedServer)
    {
        R_ASSERT2(hRenderR1->exist(), "Dedicated server needs xrRender_R1 to work");
        modes.push_back(xr_token("renderer_r1", 0));
        modes.push_back(xr_token(nullptr, -1));
        vid_quality_token = std::move(modes);
        return;
    }

    if (!vid_quality_token.empty())
        return;

    // Hide "d3d10.dll not found" message box for XP
    SetErrorMode(SEM_FAILCRITICALERRORS);

    hRenderR2 = XRay::LoadModule("xrRender_R2");
    hRenderR3 = XRay::LoadModule("xrRender_R3");
    hRenderR4 = XRay::LoadModule("xrRender_R4");

    // Restore error handling
    SetErrorMode(0);

    if (hRenderR1->exist())
    {
        modes.push_back(xr_token("renderer_r1", 0));
    }

    if (hRenderR2->exist())
    {
        modes.push_back(xr_token("renderer_r2a", 1));
        modes.push_back(xr_token("renderer_r2", 2));
        if (GEnv.CheckR2 && GEnv.CheckR2())
            modes.push_back(xr_token("renderer_r2.5", 3));
    }

    if (hRenderR3->exist())
    {
        if (GEnv.CheckR3 && GEnv.CheckR3())
            modes.push_back(xr_token("renderer_r3", 4));
        else
            hRenderR3->close();
    }

    if (hRenderR4->exist())
    {
        if (GEnv.CheckR4 && GEnv.CheckR4())
            modes.push_back(xr_token("renderer_r4", 5));
        else
            hRenderR4->close();
    }
    modes.push_back(xr_token(nullptr, -1));

    Msg("Available render modes[%d]:", modes.size());
    for (auto& mode : modes)
        if (mode.name)
            Log(mode.name);

    vid_quality_token = std::move(modes);
}
