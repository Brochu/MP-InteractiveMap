//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "MapViewer.h"
#include "stdafx.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    MapViewer sample(800, 600, L"MP - Interactive Map");

#if _DEBUG
    AllocConsole();
    FILE *f;
    freopen_s(&f, "CONOUT$", "wb",
              stdout); // Redirect stdout to the newly created console
#endif

    return Win32Application::Run(&sample, hInstance, nCmdShow);
}
