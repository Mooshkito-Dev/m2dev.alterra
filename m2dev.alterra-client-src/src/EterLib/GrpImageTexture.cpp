#include "StdAfx.h"
#include "EterBase/MappedFile.h"
#include "EterPack/EterPackManager.h"
#include "GrpImageTexture.h"
#include "EterImageLib/DDSTextureLoader9.h"

#include <stb_image.h>

bool CGraphicImageTexture::Lock(int* pRetPitch, void** ppRetPixels, int level)
{
	D3DLOCKED_RECT lockedRect;
	if (FAILED(m_lpd3dTexture->LockRect(level, &lockedRect, NULL, 0)))
		return false;

	*pRetPitch = lockedRect.Pitch;
	*ppRetPixels = (void*)lockedRect.pBits;	
	return true;
}

void CGraphicImageTexture::Unlock(int level)
{
	assert(m_lpd3dTexture != NULL);
	m_lpd3dTexture->UnlockRect(level);
}

void CGraphicImageTexture::Initialize()
{
	CGraphicTexture::Initialize();

	m_stFileName = "";

	m_d3dFmt=D3DFMT_UNKNOWN;
	m_dwFilter=0;
}

void CGraphicImageTexture::Destroy()
{
	CGraphicTexture::Destroy();

	Initialize();
}

bool CGraphicImageTexture::CreateDeviceObjects()
{
	assert(ms_lpd3dDevice != NULL);
	assert(m_lpd3dTexture == NULL);

	if (m_stFileName.empty())
	{
		// 폰트 텍스쳐
		if (FAILED(ms_lpd3dDevice->CreateTexture(m_width, m_height, 1, 0, m_d3dFmt, D3DPOOL_MANAGED, &m_lpd3dTexture, nullptr)))
			return false;
	}
	else
	{
		CMappedFile	mappedFile;
		LPCVOID		c_pvMap;

		if (!CEterPackManager::Instance().Get(mappedFile, m_stFileName.c_str(), &c_pvMap))
			return false;

		return CreateFromMemoryFile(mappedFile.Size(), c_pvMap, m_d3dFmt, m_dwFilter);
	}

	m_bEmpty = false;
	return true;
}

bool CGraphicImageTexture::Create(UINT width, UINT height, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	assert(ms_lpd3dDevice != NULL);
	Destroy();

	m_width = width;
	m_height = height;
	m_d3dFmt = d3dFmt;
	m_dwFilter = dwFilter;

	return CreateDeviceObjects();
}

void CGraphicImageTexture::CreateFromTexturePointer(const CGraphicTexture* c_pSrcTexture)
{
	if (m_lpd3dTexture)
		m_lpd3dTexture->Release();

	m_width = c_pSrcTexture->GetWidth();
	m_height = c_pSrcTexture->GetHeight();
	m_lpd3dTexture = c_pSrcTexture->GetD3DTexture();

	if (m_lpd3dTexture)
		m_lpd3dTexture->AddRef();

	m_bEmpty = false;
}

bool CGraphicImageTexture::CreateFromDDSTexture(UINT bufSize, const void* c_pvBuf)
{
	if (FAILED(DirectX::CreateDDSTextureFromMemoryEx(ms_lpd3dDevice, reinterpret_cast<const uint8_t*>(c_pvBuf), bufSize, 0, D3DPOOL_MANAGED, false, &m_lpd3dTexture)))
		return false;

	D3DSURFACE_DESC desc;
	m_lpd3dTexture->GetLevelDesc(0, &desc);
	m_width = desc.Width;
	m_height = desc.Height;
	m_bEmpty = false;
	return true;
}

bool CGraphicImageTexture::CreateFromSTB(UINT bufSize, const void* c_pvBuf)
{
	int width, height, channels;
	unsigned char* data = stbi_load_from_memory((stbi_uc*)c_pvBuf, bufSize, &width, &height, &channels, 4); // force RGBA
	if (data) {
		LPDIRECT3DTEXTURE9 texture;
		if (SUCCEEDED(ms_lpd3dDevice->CreateTexture(width, height, 1, 0, channels == 4 ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr))) {
			D3DLOCKED_RECT rect;
			if (SUCCEEDED(texture->LockRect(0, &rect, nullptr, 0))) {
				uint8_t* dstData = (uint8_t*)rect.pBits;
				uint8_t* srcData = (uint8_t*)data;
				for (size_t i = 0; i < width * height; ++i, dstData += 4, srcData += 4) {
					dstData[0] = srcData[2];
					dstData[1] = srcData[1];
					dstData[2] = srcData[0];
					dstData[3] = srcData[3];
				}

				texture->UnlockRect(0);
				m_width = width;
				m_height = height;
				m_bEmpty = false;
				m_lpd3dTexture = texture;
			}
			else {
				texture->Release();
			}
		}
		stbi_image_free(data);
	}

	return !m_bEmpty;
}

bool CGraphicImageTexture::CreateFromMemoryFile(UINT bufSize, const void * c_pvBuf, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	assert(ms_lpd3dDevice != NULL);
	assert(m_lpd3dTexture == NULL);

	m_bEmpty = true;

	if (!CreateFromDDSTexture(bufSize, c_pvBuf)) {
		if (!CreateFromSTB(bufSize, c_pvBuf)) {

			D3DXIMAGE_INFO imageInfo;
			if (FAILED(D3DXCreateTextureFromFileInMemoryEx(ms_lpd3dDevice, c_pvBuf, bufSize
				, D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT, 0, d3dFmt, D3DPOOL_MANAGED
				, dwFilter, dwFilter, 0xffff00ff, &imageInfo, NULL, &m_lpd3dTexture))) {
				TraceError("CreateFromMemoryFile: Cannot create texture");
				return false;
			}

			m_width = imageInfo.Width;
			m_height = imageInfo.Height;

			D3DFORMAT format = imageInfo.Format;
			switch (imageInfo.Format) {
			case D3DFMT_A8R8G8B8:
				format = D3DFMT_A4R4G4B4;
				break;

			case D3DFMT_X8R8G8B8:
			case D3DFMT_R8G8B8:
				format = D3DFMT_A1R5G5B5;
				break;
			}

			UINT uTexBias = 0;

			extern bool GRAPHICS_CAPS_HALF_SIZE_IMAGE;
			if (GRAPHICS_CAPS_HALF_SIZE_IMAGE)
				uTexBias = 1;

			if (IsLowTextureMemory()) {
				if (uTexBias || format != imageInfo.Format) {
					IDirect3DTexture9* pkTexSrc = m_lpd3dTexture;
					IDirect3DTexture9* pkTexDst;


					if (SUCCEEDED(D3DXCreateTexture(ms_lpd3dDevice
						, imageInfo.Width >> uTexBias, imageInfo.Height >> uTexBias
						, imageInfo.MipLevels, 0, format, D3DPOOL_MANAGED, &pkTexDst))) {
						m_lpd3dTexture = pkTexDst;
						for (int i = 0; i < imageInfo.MipLevels; ++i) {

							IDirect3DSurface9* ppsSrc = NULL;
							IDirect3DSurface9* ppsDst = NULL;

							if (SUCCEEDED(pkTexSrc->GetSurfaceLevel(i, &ppsSrc))) {
								if (SUCCEEDED(pkTexDst->GetSurfaceLevel(i, &ppsDst))) {
									D3DXLoadSurfaceFromSurface(ppsDst, NULL, NULL, ppsSrc, NULL, NULL, D3DX_FILTER_LINEAR, 0);
									ppsDst->Release();
								}
								ppsSrc->Release();
							}
						}

						pkTexSrc->Release();
					}
				}
			}
		}
	}

	m_bEmpty = false;
	return true;
}

void CGraphicImageTexture::SetFileName(const char * c_szFileName)
{
	m_stFileName=c_szFileName;
}

bool CGraphicImageTexture::CreateFromDiskFile(const char * c_szFileName, D3DFORMAT d3dFmt, DWORD dwFilter)
{
	Destroy();

	SetFileName(c_szFileName);

	m_d3dFmt = d3dFmt;
	m_dwFilter = dwFilter;
	return CreateDeviceObjects();
}

CGraphicImageTexture::CGraphicImageTexture()
{
	Initialize();
}

CGraphicImageTexture::~CGraphicImageTexture()
{
	Destroy();
}
