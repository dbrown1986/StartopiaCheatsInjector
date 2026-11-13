#pragma once
#include <windows.h>
#include <unknwn.h>

#define D3D_SDK_VERSION 220

typedef DWORD D3DCOLOR;

typedef enum _D3DFORMAT {
    D3DFMT_UNKNOWN = 0,
    D3DFMT_R8G8B8 = 20,
    D3DFMT_A8R8G8B8 = 21,
    D3DFMT_X8R8G8B8 = 22,
    D3DFMT_R5G6B5 = 23,
    D3DFMT_X1R5G5B5 = 24,
    D3DFMT_A1R5G5B5 = 25,
    D3DFMT_A4R4G4B4 = 26,
    D3DFMT_INDEX16 = 101,
    D3DFMT_INDEX32 = 102,
    D3DFMT_FORCE_DWORD = 0x7fffffff
} D3DFORMAT;

typedef enum _D3DPOOL {
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
    D3DPOOL_SCRATCH = 3,
    D3DPOOL_FORCE_DWORD = 0x7fffffff
} D3DPOOL;

typedef enum _D3DDEVTYPE {
    D3DDEVTYPE_HAL = 1,
    D3DDEVTYPE_REF = 2,
    D3DDEVTYPE_SW = 3,
    D3DDEVTYPE_FORCE_DWORD = 0x7fffffff
} D3DDEVTYPE;

typedef enum _D3DSWAPEFFECT {
    D3DSWAPEFFECT_DISCARD = 1,
    D3DSWAPEFFECT_FLIP = 2,
    D3DSWAPEFFECT_COPY = 3,
    D3DSWAPEFFECT_COPY_VSYNC = 4,
    D3DSWAPEFFECT_FORCE_DWORD = 0x7fffffff
} D3DSWAPEFFECT;

typedef enum _D3DPRIMITIVETYPE {
    D3DPT_POINTLIST = 1,
    D3DPT_LINELIST = 2,
    D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN = 6,
    D3DPT_FORCE_DWORD = 0x7fffffff
} D3DPRIMITIVETYPE;

typedef enum _D3DRENDERSTATETYPE {
    D3DRS_ZENABLE = 7,
    D3DRS_FILLMODE = 8,
    D3DRS_SHADEMODE = 9,
    D3DRS_ZWRITEENABLE = 14,
    D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19,
    D3DRS_DESTBLEND = 20,
    D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_FOGENABLE = 28,
    D3DRS_SPECULARENABLE = 29,
    D3DRS_STENCILENABLE = 52,
    D3DRS_CLIPPING = 136,
    D3DRS_LIGHTING = 137,
    D3DRS_COLORVERTEX = 141,
    D3DRS_FORCE_DWORD = 0x7fffffff
} D3DRENDERSTATETYPE;

typedef enum _D3DTEXTURESTAGESTATETYPE {
    D3DTSS_COLOROP = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_MINFILTER = 17,
    D3DTSS_MAGFILTER = 16,
    D3DTSS_MIPFILTER = 18,
    D3DTSS_FORCE_DWORD = 0x7fffffff
} D3DTEXTURESTAGESTATETYPE;

typedef enum _D3DTRANSFORMSTATETYPE {
    D3DTS_VIEW = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_WORLD = 256,
    D3DTS_FORCE_DWORD = 0x7fffffff
} D3DTRANSFORMSTATETYPE;

#define D3DCULL_NONE         1
#define D3DBLEND_SRCALPHA    5
#define D3DBLEND_INVSRCALPHA 6
#define D3DTOP_DISABLE       1
#define D3DTOP_MODULATE      4
#define D3DTA_DIFFUSE        0x00000000
#define D3DTA_TEXTURE        0x00000002
#define D3DTEXF_LINEAR       2

#define D3DUSAGE_WRITEONLY   0x00000008L
#define D3DUSAGE_DYNAMIC     0x00000200L
#define D3DLOCK_DISCARD      0x00002000L

#define D3DFVF_XYZ           0x002
#define D3DFVF_DIFFUSE       0x040
#define D3DFVF_TEX1          0x100

typedef struct _D3DMATRIX {
    float m[4][4];
} D3DMATRIX;

typedef struct _D3DVIEWPORT8 {
    DWORD X;
    DWORD Y;
    DWORD Width;
    DWORD Height;
    float MinZ;
    float MaxZ;
} D3DVIEWPORT8;

typedef struct _D3DLOCKED_RECT {
    INT   Pitch;
    void* pBits;
} D3DLOCKED_RECT;

typedef struct _D3DDEVICE_CREATION_PARAMETERS {
    UINT       AdapterOrdinal;
    D3DDEVTYPE DeviceType;
    HWND       hFocusWindow;
    DWORD      BehaviorFlags;
} D3DDEVICE_CREATION_PARAMETERS;

typedef struct _D3DPRESENT_PARAMETERS_ {
    UINT          BackBufferWidth;
    UINT          BackBufferHeight;
    D3DFORMAT     BackBufferFormat;
    UINT          BackBufferCount;
    DWORD         MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND          hDeviceWindow;
    BOOL          Windowed;
    BOOL          EnableAutoDepthStencil;
    D3DFORMAT     AutoDepthStencilFormat;
    DWORD         Flags;
    UINT          FullScreen_RefreshRateInHz;
    UINT          FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

typedef struct _D3DCAPS8 {
    BYTE RawData[1024];
} D3DCAPS8;

struct IDirect3DDevice8;

struct IDirect3DResource8 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, const void* pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) = 0;
    virtual DWORD   STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) = 0;
    virtual DWORD   STDMETHODCALLTYPE GetPriority() = 0;
    virtual void    STDMETHODCALLTYPE PreLoad() = 0;
    virtual DWORD   STDMETHODCALLTYPE GetType() = 0;
};

struct IDirect3DBaseTexture8 : public IDirect3DResource8 {
    virtual DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) = 0;
    virtual DWORD STDMETHODCALLTYPE GetLOD() = 0;
    virtual DWORD STDMETHODCALLTYPE GetLevelCount() = 0;
};

struct IDirect3DTexture8 : public IDirect3DBaseTexture8 {
    virtual HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, void* pDesc) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, void** ppSurfaceLevel) = 0;
    virtual HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT* pDirtyRect) = 0;
};

struct IDirect3DVertexBuffer8 : public IDirect3DResource8 {
    virtual HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(void* pDesc) = 0;
};

struct IDirect3DIndexBuffer8 : public IDirect3DResource8 {
    virtual HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(void* pDesc) = 0;
};

struct IDirect3DDevice8 : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE TestCooperativeLevel() = 0;
    virtual UINT    STDMETHODCALLTYPE GetAvailableTextureMem() = 0;
    virtual HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD Bytes) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDirect3D(void** ppD3D8) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS8* pCaps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayMode(void* pMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, void* pCursorBitmap) = 0;
    virtual void    STDMETHODCALLTYPE SetCursorPosition(int X, int Y, DWORD Flags) = 0;
    virtual BOOL    STDMETHODCALLTYPE ShowCursor(BOOL bShow) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, void** pSwapChain) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) = 0;
    virtual HRESULT STDMETHODCALLTYPE Present(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT BackBuffer, DWORD Type, void** ppBackBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRasterStatus(void* pRasterStatus) = 0;
    virtual void    STDMETHODCALLTYPE SetGammaRamp(DWORD Flags, const void* pRamp) = 0;
    virtual void    STDMETHODCALLTYPE GetGammaRamp(void* pRamp) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, void** ppVolumeTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, void** ppCubeTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer8** ppVertexBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer8** ppIndexBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, DWORD MultiSample, BOOL Lockable, void** ppSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, DWORD MultiSample, void** ppSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, void** ppSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyRects(void* pSourceSurface, const RECT* pSourceRectsArray, UINT cRects, void* pDestinationSurface, const POINT* pDestPointsArray) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8* pSourceTexture, IDirect3DBaseTexture8* pDestinationTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrontBuffer(void* pDestSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRenderTarget(void* pRenderTarget, void* pNewZStencil) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRenderTarget(void** ppRenderTarget) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(void** ppZStencilSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE BeginScene() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndScene() = 0;
    virtual HRESULT STDMETHODCALLTYPE Clear(DWORD Count, const void* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) = 0;
    virtual HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT8* pViewport) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8* pViewport) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMaterial(const void* pMaterial) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMaterial(void* pMaterial) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, const void* pLight) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, void* pLight) = 0;
    virtual HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, const float* pPlane) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE BeginStateBlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD* pToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStateBlock(DWORD Type, DWORD* pToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClipStatus(const void* pClipStatus) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClipStatus(void* pClipStatus) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IDirect3DBaseTexture8** ppTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture8* pTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInfo(DWORD DevInfoID, void* pDevInfoStruct, DWORD DevInfoStructSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, const PALETTEENTRY* pEntries) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* PaletteNumber) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT MinIndex, UINT NumVertices, UINT StartIndex, UINT PrimitiveCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertexIndices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer8* pDestBuffer, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* pDeclaration, const DWORD* pFunction, DWORD* pHandle, DWORD Usage) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD Register, const void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD Register, void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD Handle, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD Handle, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT Stride) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8** ppStreamData, UINT* pStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer8** ppIndexData, UINT* pBaseVertexIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* pFunction, DWORD* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD Register, const void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD Register, void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD Handle, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, const float* pNumSegs, const void* pRectPatchInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, const float* pNumSegs, const void* pTriPatchInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) = 0;
};

struct IDirect3D8 : public IUnknown {
    virtual HRESULT  STDMETHODCALLTYPE RegisterSoftwareDevice(void* pInitializeFunction) = 0;
    virtual UINT     STDMETHODCALLTYPE GetAdapterCount() = 0;
    virtual HRESULT  STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, void* pIdentifier) = 0;
    virtual UINT     STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter) = 0;
    virtual HRESULT  STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, UINT Mode, void* pMode) = 0;
    virtual HRESULT  STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, void* pMode) = 0;
    virtual HRESULT  STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL Windowed) = 0;
    virtual HRESULT  STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, DWORD RType, D3DFORMAT CheckFormat) = 0;
    virtual HRESULT  STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, DWORD MultiSampleType) = 0;
    virtual HRESULT  STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
    virtual HRESULT  STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS8* pCaps) = 0;
    virtual HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) = 0;
    virtual HRESULT  STDMETHODCALLTYPE CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice8** ppReturnedDeviceInterface) = 0;
};