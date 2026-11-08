#include "NetAvatar.h"
#include "NetCrypto.h"

#include <windows.h>
#include <wincodec.h>
#include <objidl.h>
#include <objbase.h>

#include "../Menu/stb_image.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
	constexpr int NET_AVATAR_MAX_SIDE = 64;
	constexpr int NET_AVATAR_MIN_SIDE = 48;
	constexpr size_t NET_AVATAR_MAX_PNG = 20000;
	constexpr size_t NET_AVATAR_MAX_DECODE = 512 * 512 * 4;

	struct WicScope_t
	{
		IWICImagingFactory* pFactory = nullptr;
		IWICBitmapDecoder* pDecoder = nullptr;
		IWICBitmapFrameDecode* pFrame = nullptr;
		IWICFormatConverter* pConverter = nullptr;
		IWICBitmapScaler* pScaler = nullptr;
		IWICStream* pWicStream = nullptr;
		IWICBitmapEncoder* pEncoder = nullptr;
		IWICBitmapFrameEncode* pFrameEncode = nullptr;
		IStream* pMemStream = nullptr;
		IWICBitmap* pBitmap = nullptr;

		~WicScope_t()
		{
			if (pFrameEncode)
				pFrameEncode->Release();
			if (pEncoder)
				pEncoder->Release();
			if (pWicStream)
				pWicStream->Release();
			if (pMemStream)
				pMemStream->Release();
			if (pScaler)
				pScaler->Release();
			if (pConverter)
				pConverter->Release();
			if (pFrame)
				pFrame->Release();
			if (pDecoder)
				pDecoder->Release();
			if (pBitmap)
				pBitmap->Release();
			if (pFactory)
				pFactory->Release();
		}
	};

	bool CreateFactory(IWICImagingFactory** ppFactory)
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(ppFactory)));
	}

	std::wstring ToWide(const std::string& strText)
	{
		const int nLen = MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), -1, nullptr, 0);
		std::wstring strOut;
		if (nLen > 0)
		{
			strOut.resize(static_cast<size_t>(nLen) - 1);
			MultiByteToWideChar(CP_UTF8, 0, strText.c_str(), -1, strOut.data(), nLen);
		}
		return strOut;
	}

	UINT ClampSide(UINT nSide)
	{
		if (nSide < 1)
			return 1;
		return nSide;
	}
}

bool CNetAvatar::EncodeFromFile(const std::string& strPath, std::string& strOutB64, std::string& strError)
{
	strOutB64.clear();
	strError.clear();

	if (strPath.empty())
	{
		strError = "no file selected";
		return false;
	}

	WicScope_t wic;
	if (!CreateFactory(&wic.pFactory))
	{
		strError = "image system unavailable";
		return false;
	}

	const std::wstring strWide = ToWide(strPath);
	if (FAILED(wic.pFactory->CreateDecoderFromFilename(strWide.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &wic.pDecoder)))
	{
		strError = "could not read that image";
		return false;
	}

	if (FAILED(wic.pDecoder->GetFrame(0, &wic.pFrame)))
	{
		strError = "could not read that image";
		return false;
	}

	if (FAILED(wic.pFactory->CreateFormatConverter(&wic.pConverter)))
	{
		strError = "could not read that image";
		return false;
	}

	if (FAILED(wic.pConverter->Initialize(wic.pFrame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
	{
		strError = "unsupported image format";
		return false;
	}

	UINT nSrcW = 0, nSrcH = 0;
	if (FAILED(wic.pConverter->GetSize(&nSrcW, &nSrcH)) || nSrcW == 0 || nSrcH == 0)
	{
		strError = "image is empty";
		return false;
	}

	if (nSrcW > 8192 || nSrcH > 8192)
	{
		strError = "image is too large";
		return false;
	}

	UINT nFitW = nSrcW, nFitH = nSrcH;
	const UINT nLargest = nSrcW > nSrcH ? nSrcW : nSrcH;

	if (nLargest > static_cast<UINT>(NET_AVATAR_MAX_SIDE))
	{
		nFitW = (nSrcW * NET_AVATAR_MAX_SIDE + nLargest - 1) / nLargest;
		nFitH = (nSrcH * NET_AVATAR_MAX_SIDE + nLargest - 1) / nLargest;
	}

	static const int nSizes[] = { NET_AVATAR_MAX_SIDE, 64, NET_AVATAR_MIN_SIDE };

	for (const int nTarget : nSizes)
	{
		UINT nOutW = nFitW, nOutH = nFitH;

		if (nTarget != NET_AVATAR_MAX_SIDE)
		{
			const UINT nScaleNum = static_cast<UINT>(nTarget);
			nOutW = (nFitW * nScaleNum + NET_AVATAR_MAX_SIDE - 1) / NET_AVATAR_MAX_SIDE;
			nOutH = (nFitH * nScaleNum + NET_AVATAR_MAX_SIDE - 1) / NET_AVATAR_MAX_SIDE;
		}

		nOutW = ClampSide(nOutW);
		nOutH = ClampSide(nOutH);

		IWICBitmapSource* pSource = wic.pConverter;

		if (nOutW != nSrcW || nOutH != nSrcH)
		{
			if (FAILED(wic.pFactory->CreateBitmapScaler(&wic.pScaler)))
			{
				strError = "could not process the image";
				return false;
			}

			if (FAILED(wic.pScaler->Initialize(wic.pConverter, nOutW, nOutH, WICBitmapInterpolationModeFant)))
			{
				strError = "could not process the image";
				return false;
			}

			pSource = wic.pScaler;
		}

		if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &wic.pMemStream)))
		{
			strError = "could not process the image";
			return false;
		}

		if (FAILED(wic.pFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &wic.pEncoder)))
		{
			strError = "could not process the image";
			return false;
		}

		if (FAILED(wic.pEncoder->Initialize(wic.pMemStream, WICBitmapEncoderNoCache)))
		{
			strError = "could not process the image";
			return false;
		}

		if (FAILED(wic.pEncoder->CreateNewFrame(&wic.pFrameEncode, nullptr)))
		{
			strError = "could not process the image";
			return false;
		}

		if (FAILED(wic.pFrameEncode->Initialize(nullptr)) || FAILED(wic.pFrameEncode->SetSize(nOutW, nOutH)))
		{
			strError = "could not process the image";
			return false;
		}

		if (FAILED(wic.pFrameEncode->WriteSource(pSource, nullptr)))
		{
			strError = "could not process the image";
			return false;
		}

		if (FAILED(wic.pFrameEncode->Commit()) || FAILED(wic.pEncoder->Commit()))
		{
			strError = "could not process the image";
			return false;
		}

		HGLOBAL hMem = nullptr;
		if (FAILED(GetHGlobalFromStream(wic.pMemStream, &hMem)))
		{
			strError = "could not process the image";
			return false;
		}

		const SIZE_T nPngSize = GlobalSize(hMem);
		if (nPngSize == 0 || nPngSize > NET_AVATAR_MAX_PNG)
		{
			wic.pFrameEncode->Release();
			wic.pFrameEncode = nullptr;
			wic.pEncoder->Release();
			wic.pEncoder = nullptr;
			wic.pMemStream->Release();
			wic.pMemStream = nullptr;
			wic.pScaler->Release();
			wic.pScaler = nullptr;
			continue;
		}

		const void* pBytes = GlobalLock(hMem);
		if (!pBytes)
		{
			strError = "could not process the image";
			return false;
		}

		strOutB64 = CNetCrypto::Base64UrlEncode(pBytes, nPngSize);
		GlobalUnlock(hMem);

		if (strOutB64.empty())
		{
			strError = "could not process the image";
			return false;
		}

		return true;
	}

	strError = "image is too large, pick a smaller one";
	return false;
}

bool CNetAvatar::EncodeFromRGBA(const unsigned char* pData, int nWidth, int nHeight, std::string& strOutB64, std::string& strError)
{
	strOutB64.clear();
	strError.clear();

	if (!pData || nWidth <= 0 || nHeight <= 0 || nWidth > 512 || nHeight > 512)
	{
		strError = "bad image data";
		return false;
	}

	WicScope_t wic;
	if (!CreateFactory(&wic.pFactory))
	{
		strError = "image system unavailable";
		return false;
	}

	const UINT nStride = static_cast<UINT>(nWidth) * 4;
	if (FAILED(wic.pFactory->CreateBitmapFromMemory(static_cast<UINT>(nWidth), static_cast<UINT>(nHeight), GUID_WICPixelFormat32bppRGBA, nStride, nStride * static_cast<UINT>(nHeight), const_cast<unsigned char*>(pData), &wic.pBitmap)))
	{
		strError = "could not process the image";
		return false;
	}

	IWICBitmapSource* pSource = wic.pBitmap;
	bool bScaled = false;
	UINT nSrcW = 0, nSrcH = 0;
	wic.pBitmap->GetSize(&nSrcW, &nSrcH);

	const UINT nLargest = nSrcW > nSrcH ? nSrcW : nSrcH;
	if (nLargest > static_cast<UINT>(NET_AVATAR_MAX_SIDE))
	{
		const UINT nFitW = (nSrcW * NET_AVATAR_MAX_SIDE + nLargest - 1) / nLargest;
		const UINT nFitH = (nSrcH * NET_AVATAR_MAX_SIDE + nLargest - 1) / nLargest;
		if (FAILED(wic.pFactory->CreateBitmapScaler(&wic.pScaler)) || FAILED(wic.pScaler->Initialize(pSource, ClampSide(nFitW), ClampSide(nFitH), WICBitmapInterpolationModeFant)))
		{
			strError = "could not process the image";
			return false;
		}
		pSource = wic.pScaler;
		bScaled = true;
	}

	if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &wic.pMemStream)) || FAILED(wic.pFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &wic.pEncoder)) || FAILED(wic.pEncoder->Initialize(wic.pMemStream, WICBitmapEncoderNoCache)) || FAILED(wic.pEncoder->CreateNewFrame(&wic.pFrameEncode, nullptr)))
	{
		strError = "could not process the image";
		return false;
	}

	UINT nOutW = nSrcW, nOutH = nSrcH;
	if (bScaled)
		pSource->GetSize(&nOutW, &nOutH);

	if (FAILED(wic.pFrameEncode->Initialize(nullptr)) || FAILED(wic.pFrameEncode->SetSize(nOutW, nOutH)))
	{
		strError = "could not process the image";
		return false;
	}

	if (FAILED(wic.pFrameEncode->WriteSource(pSource, nullptr)) || FAILED(wic.pFrameEncode->Commit()) || FAILED(wic.pEncoder->Commit()))
	{
		strError = "could not process the image";
		return false;
	}

	HGLOBAL hMem = nullptr;
	if (FAILED(GetHGlobalFromStream(wic.pMemStream, &hMem)))
	{
		strError = "could not process the image";
		return false;
	}

	const SIZE_T nPngSize = GlobalSize(hMem);
	if (nPngSize == 0 || nPngSize > NET_AVATAR_MAX_PNG)
	{
		strError = "image is too large, pick a smaller one";
		return false;
	}

	const void* pBytes = GlobalLock(hMem);
	if (!pBytes)
	{
		strError = "could not process the image";
		return false;
	}

	strOutB64 = CNetCrypto::Base64UrlEncode(pBytes, nPngSize);
	GlobalUnlock(hMem);

	if (strOutB64.empty())
	{
		strError = "could not process the image";
		return false;
	}

	return true;
}

bool CNetAvatar::DecodeToRGBA(const std::string& strB64, std::vector<unsigned char>& vecOut, int& nWidth, int& nHeight)
{
	vecOut.clear();
	nWidth = 0;
	nHeight = 0;

	std::string strBytes;
	if (strB64.empty() || !CNetCrypto::Base64UrlDecode(strB64, strBytes) || strBytes.size() < 16)
		return false;

	int nInfoW = 0, nInfoH = 0, nInfoComp = 0;
	if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(strBytes.data()), static_cast<int>(strBytes.size()), &nInfoW, &nInfoH, &nInfoComp))
		return false;

	if (nInfoW <= 0 || nInfoH <= 0 || nInfoW > 1024 || nInfoH > 1024)
		return false;

	if (static_cast<size_t>(nInfoW) * static_cast<size_t>(nInfoH) * 4 > NET_AVATAR_MAX_DECODE)
		return false;

	stbi_uc* pPixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(strBytes.data()), static_cast<int>(strBytes.size()), &nWidth, &nHeight, nullptr, 4);
	if (!pPixels)
		return false;

	if (nWidth <= 0 || nHeight <= 0 || nWidth > 1024 || nHeight > 1024)
	{
		stbi_image_free(pPixels);
		nWidth = 0;
		nHeight = 0;
		return false;
	}

	const size_t nStride = static_cast<size_t>(nWidth) * 4;
	vecOut.assign(pPixels, pPixels + nStride * static_cast<size_t>(nHeight));
	stbi_image_free(pPixels);
	return true;
}
