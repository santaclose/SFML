////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2021 Laurent Gomila (laurent@sfml-dev.org)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Window/Win32/ClipboardImpl.hpp>
#include <SFML/System/String.hpp>
#include <iostream>
#include <windows.h>


namespace sf
{
namespace priv
{
////////////////////////////////////////////////////////////
String ClipboardImpl::getString()
{
    String text;

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
    {
        std::cerr << "Failed to get the clipboard data in Unicode format." << std::endl;
        return text;
    }

    if (!OpenClipboard(NULL))
    {
        std::cerr << "Failed to open the Win32 clipboard." << std::endl;
        return text;
    }

    HANDLE clipboard_handle = GetClipboardData(CF_UNICODETEXT);

    if (!clipboard_handle)
    {
        std::cerr << "Failed to get Win32 handle for clipboard content." << std::endl;
        CloseClipboard();
        return text;
    }

    text = String(static_cast<wchar_t*>(GlobalLock(clipboard_handle)));
    GlobalUnlock(clipboard_handle);

    CloseClipboard();
    return text;
}


////////////////////////////////////////////////////////////
void ClipboardImpl::setString(const String& text)
{
    if (!OpenClipboard(NULL))
    {
        std::cerr << "Failed to open the Win32 clipboard." << std::endl;
        return;
    }

    if (!EmptyClipboard())
    {
        std::cerr << "Failed to empty the Win32 clipboard." << std::endl;
        return;
    }

    // Create a Win32-compatible string
    size_t string_size = (text.getSize() + 1) * sizeof(WCHAR);
    HANDLE string_handle = GlobalAlloc(GMEM_MOVEABLE, string_size);

    if (string_handle)
    {
        memcpy(GlobalLock(string_handle), text.toWideString().data(), string_size);
        GlobalUnlock(string_handle);
        SetClipboardData(CF_UNICODETEXT, string_handle);
    }

    CloseClipboard();
}

void ClipboardImpl::setImage(unsigned int width, unsigned int height, const void* pointer)
{
    if (!OpenClipboard(NULL))
    {
        std::cerr << "Failed to open the Win32 clipboard." << std::endl;
        return;
    }

    if (!EmptyClipboard())
    {
        std::cerr << "Failed to empty the Win32 clipboard." << std::endl;
        return;
    }

    const sf::Uint8* originalBitmap = (const sf::Uint8*)pointer;
    sf::Uint8* channelsSwapped = new Uint8[width * height * 4u];
    for (unsigned int i = 0; i < width * height; i++)
    {
        channelsSwapped[i * 4u + 0u] = originalBitmap[i * 4u + 2u];
        channelsSwapped[i * 4u + 1u] = originalBitmap[i * 4u + 1u];
        channelsSwapped[i * 4u + 2u] = originalBitmap[i * 4u + 0u];
    }

    // https://www.codeproject.com/Questions/344732/copy-hbitmap-to-clipboard
    // https://docs.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbitmap

    HBITMAP hBM = CreateBitmap(width, height, 4, 8, channelsSwapped);

    BITMAP bm;
    GetObject(hBM, sizeof(bm), &bm);

    BITMAPINFOHEADER bi;
    ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bm.bmWidth;
    bi.biHeight = bm.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = bm.bmBitsPixel;
    bi.biCompression = BI_RGB;
    if (bi.biBitCount <= 1)	// make sure bits per pixel is valid
        bi.biBitCount = 1;
    else if (bi.biBitCount <= 4)
        bi.biBitCount = 4;
    else if (bi.biBitCount <= 8)
        bi.biBitCount = 8;
    else // if greater than 8-bit, force to 24-bit
        bi.biBitCount = 24;

    // Get size of color table.
    SIZE_T dwColTableLen = (bi.biBitCount <= 8) ? (1 << bi.biBitCount) * sizeof(RGBQUAD) : 0;

    // Create a device context with palette
    HDC hDC = GetDC(NULL);
    HPALETTE hPal = static_cast<HPALETTE>(GetStockObject(DEFAULT_PALETTE));
    HPALETTE hOldPal = SelectPalette(hDC, hPal, FALSE);
    RealizePalette(hDC);

    // Use GetDIBits to calculate the image size.
    GetDIBits(hDC, hBM, 0, static_cast<UINT>(bi.biHeight), NULL,
        reinterpret_cast<LPBITMAPINFO>(&bi), DIB_RGB_COLORS);
    // If the driver did not fill in the biSizeImage field, then compute it.
    // Each scan line of the image is aligned on a DWORD (32bit) boundary.
    if (0 == bi.biSizeImage)
        bi.biSizeImage = ((((bi.biWidth * bi.biBitCount) + 31) & ~31) / 8) * bi.biHeight;

    // Allocate memory
    HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + dwColTableLen + bi.biSizeImage);
    if (hDIB)
    {
        union tagHdr_u
        {
            LPVOID             p;
            LPBYTE             pByte;
            LPBITMAPINFOHEADER pHdr;
            LPBITMAPINFO       pInfo;
        } Hdr;

        Hdr.p = GlobalLock(hDIB);
        // Copy the header
        CopyMemory(Hdr.p, &bi, sizeof(BITMAPINFOHEADER));
        // Convert/copy the image bits and create the color table
        int nConv = GetDIBits(hDC, hBM, 0, static_cast<UINT>(bi.biHeight),
            Hdr.pByte + sizeof(BITMAPINFOHEADER) + dwColTableLen,
            Hdr.pInfo, DIB_RGB_COLORS);
        GlobalUnlock(hDIB);
        if (!nConv)
        {
            GlobalFree(hDIB);
            hDIB = NULL;
        }
    }
    if (hDIB)
        SetClipboardData(CF_DIB, hDIB);
    CloseClipboard();
    SelectPalette(hDC, hOldPal, FALSE);
    ReleaseDC(NULL, hDC);
    delete[] channelsSwapped;
}

} // namespace priv

} // namespace sf
