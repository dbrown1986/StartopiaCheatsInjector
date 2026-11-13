#include "pch.h" // Remove this line if your project does not use Precompiled Headers
#include "imgui.h"
#include "imgui_impl_dx8.h"
#include "d3d8.h"

struct CUSTOMVERTEX {
    float    pos[3];
    D3DCOLOR col;
    float    uv[2];
};
#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static IDirect3DDevice8* g_pd3dDevice = nullptr;
static IDirect3DVertexBuffer8* g_pVB = nullptr;
static IDirect3DIndexBuffer8* g_pIB = nullptr;
static IDirect3DTexture8* g_FontTexture = nullptr;
static int                     g_VertexBufferSize = 5000;
static int                     g_IndexBufferSize = 10000;

static void ImGui_ImplDX8_SetupRenderState(ImDrawData* draw_data) {
    D3DVIEWPORT8 vp;
    vp.X = vp.Y = 0;
    vp.Width = (DWORD)draw_data->DisplaySize.x;
    vp.Height = (DWORD)draw_data->DisplaySize.y;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_pd3dDevice->SetViewport(&vp);

    g_pd3dDevice->SetPixelShader(0);
    g_pd3dDevice->SetVertexShader(D3DFVF_CUSTOMVERTEX);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_CLIPPING, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_COLORVERTEX, TRUE);

    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    g_pd3dDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pd3dDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    float L = draw_data->DisplayPos.x + 0.5f;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x + 0.5f;
    float T = draw_data->DisplayPos.y + 0.5f;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y + 0.5f;
    D3DMATRIX mat_identity = { { { 1,0,0,0 }, { 0,1,0,0 }, { 0,0,1,0 }, { 0,0,0,1 } } };
    D3DMATRIX mat_projection = { {
        { 2.0f / (R - L),   0.0f,             0.0f, 0.0f },
        { 0.0f,             2.0f / (T - B),   0.0f, 0.0f },
        { 0.0f,             0.0f,             0.5f, 0.0f },
        { (L + R) / (L - R),(T + B) / (B - T),0.5f, 1.0f }
    } };
    g_pd3dDevice->SetTransform(D3DTS_WORLD, &mat_identity);
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &mat_identity);
    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &mat_projection);
}

void ImGui_ImplDX8_RenderDrawData(ImDrawData* draw_data) {
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    if (!g_pVB || g_VertexBufferSize < draw_data->TotalVtxCount) {
        if (g_pVB) { g_pVB->Release(); g_pVB = nullptr; }
        g_VertexBufferSize = draw_data->TotalVtxCount + 5000;
        if (FAILED(g_pd3dDevice->CreateVertexBuffer(g_VertexBufferSize * sizeof(CUSTOMVERTEX), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX, D3DPOOL_DEFAULT, &g_pVB)))
            return;
    }
    if (!g_pIB || g_IndexBufferSize < draw_data->TotalIdxCount) {
        if (g_pIB) { g_pIB->Release(); g_pIB = nullptr; }
        g_IndexBufferSize = draw_data->TotalIdxCount + 10000;
        if (FAILED(g_pd3dDevice->CreateIndexBuffer(g_IndexBufferSize * sizeof(ImDrawIdx), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, D3DPOOL_DEFAULT, &g_pIB)))
            return;
    }

    CUSTOMVERTEX* vtx_dst = nullptr;
    ImDrawIdx* idx_dst = nullptr;
    if (FAILED(g_pVB->Lock(0, (UINT)(draw_data->TotalVtxCount * sizeof(CUSTOMVERTEX)), (BYTE**)&vtx_dst, D3DLOCK_DISCARD)))
        return;
    if (FAILED(g_pIB->Lock(0, (UINT)(draw_data->TotalIdxCount * sizeof(ImDrawIdx)), (BYTE**)&idx_dst, D3DLOCK_DISCARD))) {
        g_pVB->Unlock();
        return;
    }

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_src = cmd_list->VtxBuffer.Data;
        for (int i = 0; i < cmd_list->VtxBuffer.Size; i++) {
            vtx_dst->pos[0] = vtx_src->pos.x;
            vtx_dst->pos[1] = vtx_src->pos.y;
            vtx_dst->pos[2] = 0.0f;
            vtx_dst->col = (vtx_src->col & 0xFF00FF00) | ((vtx_src->col & 0xFF0000) >> 16) | ((vtx_src->col & 0xFF) << 16);
            vtx_dst->uv[0] = vtx_src->uv.x;
            vtx_dst->uv[1] = vtx_src->uv.y;
            vtx_dst++;
            vtx_src++;
        }
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    g_pVB->Unlock();
    g_pIB->Unlock();

    g_pd3dDevice->SetStreamSource(0, g_pVB, sizeof(CUSTOMVERTEX));
    ImGui_ImplDX8_SetupRenderState(draw_data);

    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        g_pd3dDevice->SetIndices(g_pIB, (UINT)global_vtx_offset);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplDX8_SetupRenderState(draw_data);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else {
                float x1 = pcmd->ClipRect.x - clip_off.x;
                float y1 = pcmd->ClipRect.y - clip_off.y;
                float x2 = pcmd->ClipRect.z - clip_off.x;
                float y2 = pcmd->ClipRect.w - clip_off.y;
                if (x1 < 0.0f) x1 = 0.0f;
                if (y1 < 0.0f) y1 = 0.0f;
                if (x2 > draw_data->DisplaySize.x) x2 = draw_data->DisplaySize.x;
                if (y2 > draw_data->DisplaySize.y) y2 = draw_data->DisplaySize.y;

                if (x2 > x1 && y2 > y1) {
                    D3DVIEWPORT8 vp = { (DWORD)x1, (DWORD)y1, (DWORD)(x2 - x1), (DWORD)(y2 - y1), 0.0f, 1.0f };
                    g_pd3dDevice->SetViewport(&vp);

                    float L = x1 + clip_off.x + 0.5f;
                    float R = x2 + clip_off.x + 0.5f;
                    float T = y1 + clip_off.y + 0.5f;
                    float B = y2 + clip_off.y + 0.5f;
                    D3DMATRIX mat_proj = { {
                        { 2.0f / (R - L),    0.0f,              0.0f, 0.0f },
                        { 0.0f,              2.0f / (T - B),    0.0f, 0.0f },
                        { 0.0f,              0.0f,              0.5f, 0.0f },
                        { (L + R) / (L - R), (T + B) / (B - T), 0.5f, 1.0f }
                    } };
                    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &mat_proj);
                    g_pd3dDevice->SetTexture(0, (IDirect3DBaseTexture8*)pcmd->GetTexID());
                    g_pd3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, (UINT)cmd_list->VtxBuffer.Size, global_idx_offset + pcmd->IdxOffset, pcmd->ElemCount / 3);
                }
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }
}

bool ImGui_ImplDX8_Init(IDirect3DDevice8* device) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_dx8";
    g_pd3dDevice = device;
    g_pd3dDevice->AddRef();
    return true;
}

void ImGui_ImplDX8_Shutdown() {
    ImGui_ImplDX8_InvalidateDeviceObjects();
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static bool ImGui_ImplDX8_CreateFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0, bytes_per_pixel = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);

    g_FontTexture = nullptr;
    if (FAILED(g_pd3dDevice->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_FontTexture)))
        return false;

    D3DLOCKED_RECT tex_locked_rect;
    if (FAILED(g_FontTexture->LockRect(0, &tex_locked_rect, nullptr, 0)))
        return false;

    for (int y = 0; y < height; y++)
        memcpy((unsigned char*)tex_locked_rect.pBits + tex_locked_rect.Pitch * y, pixels + (width * bytes_per_pixel) * y, (width * bytes_per_pixel));
    g_FontTexture->UnlockRect(0);

    io.Fonts->SetTexID((ImTextureID)g_FontTexture);
    return true;
}

bool ImGui_ImplDX8_CreateDeviceObjects() {
    if (!g_pd3dDevice) return false;
    return ImGui_ImplDX8_CreateFontsTexture();
}

void ImGui_ImplDX8_InvalidateDeviceObjects() {
    if (!g_pd3dDevice) return;
    if (g_pVB) { g_pVB->Release(); g_pVB = nullptr; }
    if (g_pIB) { g_pIB->Release(); g_pIB = nullptr; }
    if (g_FontTexture) { g_FontTexture->Release(); g_FontTexture = nullptr; ImGui::GetIO().Fonts->SetTexID(0); }
}

void ImGui_ImplDX8_NewFrame() {
    if (!g_FontTexture)
        ImGui_ImplDX8_CreateDeviceObjects();
}