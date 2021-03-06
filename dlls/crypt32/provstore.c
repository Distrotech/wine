/*
 * Copyright 2004-2007 Juan Lang
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#include "wine/debug.h"
#include "wine/list.h"
#include "crypt32_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypt);

typedef struct _WINE_PROVIDERSTORE
{
    WINECRYPT_CERTSTORE             hdr;
    DWORD                           dwStoreProvFlags;
    WINECRYPT_CERTSTORE            *memStore;
    HCERTSTOREPROV                  hStoreProv;
    PFN_CERT_STORE_PROV_CLOSE       provCloseStore;
    PFN_CERT_STORE_PROV_WRITE_CERT  provWriteCert;
    PFN_CERT_STORE_PROV_DELETE_CERT provDeleteCert;
    PFN_CERT_STORE_PROV_WRITE_CRL   provWriteCrl;
    PFN_CERT_STORE_PROV_DELETE_CRL  provDeleteCrl;
    PFN_CERT_STORE_PROV_WRITE_CTL   provWriteCtl;
    PFN_CERT_STORE_PROV_DELETE_CTL  provDeleteCtl;
    PFN_CERT_STORE_PROV_CONTROL     provControl;
} WINE_PROVIDERSTORE;

static void ProvStore_closeStore(WINECRYPT_CERTSTORE *cert_store, DWORD dwFlags)
{
    WINE_PROVIDERSTORE *store = (WINE_PROVIDERSTORE*)cert_store;

    TRACE("(%p, %08x)\n", store, dwFlags);

    if (store->provCloseStore)
        store->provCloseStore(store->hStoreProv, dwFlags);
    if (!(store->dwStoreProvFlags & CERT_STORE_PROV_EXTERNAL_FLAG))
        CertCloseStore(store->memStore, dwFlags);
    CRYPT_FreeStore((WINECRYPT_CERTSTORE*)store);
}

static BOOL CRYPT_ProvAddCert(WINECRYPT_CERTSTORE *store, void *cert,
 void *toReplace, const void **ppStoreContext)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    BOOL ret;

    TRACE("(%p, %p, %p, %p)\n", store, cert, toReplace, ppStoreContext);

    if (toReplace)
        ret = ps->memStore->certs.addContext(ps->memStore, cert, toReplace,
         ppStoreContext);
    else
    {
        ret = TRUE;
        if (ps->provWriteCert)
            ret = ps->provWriteCert(ps->hStoreProv, cert,
             CERT_STORE_PROV_WRITE_ADD_FLAG);
        if (ret)
            ret = ps->memStore->certs.addContext(ps->memStore, cert, NULL,
             ppStoreContext);
    }
    /* dirty trick: replace the returned context's hCertStore with
     * store.
     */
    if (ret && ppStoreContext)
        (*(PCERT_CONTEXT *)ppStoreContext)->hCertStore = store;
    return ret;
}

static void *CRYPT_ProvEnumCert(WINECRYPT_CERTSTORE *store, void *pPrev)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    void *ret;

    ret = ps->memStore->certs.enumContext(ps->memStore, pPrev);
    if (ret)
    {
        /* same dirty trick: replace the returned context's hCertStore with
         * store.
         */
        ((PCERT_CONTEXT)ret)->hCertStore = store;
    }
    return ret;
}

static BOOL CRYPT_ProvDeleteCert(WINECRYPT_CERTSTORE *store, void *cert)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    BOOL ret = TRUE;

    TRACE("(%p, %p)\n", store, cert);

    if (ps->provDeleteCert)
        ret = ps->provDeleteCert(ps->hStoreProv, cert, 0);
    if (ret)
        ret = ps->memStore->certs.deleteContext(ps->memStore, cert);
    return ret;
}

static BOOL CRYPT_ProvAddCRL(WINECRYPT_CERTSTORE *store, void *crl,
 void *toReplace, const void **ppStoreContext)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    BOOL ret;

    TRACE("(%p, %p, %p, %p)\n", store, crl, toReplace, ppStoreContext);

    if (toReplace)
        ret = ps->memStore->crls.addContext(ps->memStore, crl, toReplace,
         ppStoreContext);
    else
    {
        if (ps->hdr.dwOpenFlags & CERT_STORE_READONLY_FLAG)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            ret = FALSE;
        }
        else
        {
            ret = TRUE;
            if (ps->provWriteCrl)
                ret = ps->provWriteCrl(ps->hStoreProv, crl,
                 CERT_STORE_PROV_WRITE_ADD_FLAG);
            if (ret)
                ret = ps->memStore->crls.addContext(ps->memStore, crl, NULL,
                 ppStoreContext);
        }
    }
    /* dirty trick: replace the returned context's hCertStore with
     * store.
     */
    if (ret && ppStoreContext)
        (*(PCRL_CONTEXT *)ppStoreContext)->hCertStore = store;
    return ret;
}

static void *CRYPT_ProvEnumCRL(WINECRYPT_CERTSTORE *store, void *pPrev)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    void *ret;

    ret = ps->memStore->crls.enumContext(ps->memStore, pPrev);
    if (ret)
    {
        /* same dirty trick: replace the returned context's hCertStore with
         * store.
         */
        ((PCRL_CONTEXT)ret)->hCertStore = store;
    }
    return ret;
}

static BOOL CRYPT_ProvDeleteCRL(WINECRYPT_CERTSTORE *store, void *crl)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    BOOL ret = TRUE;

    TRACE("(%p, %p)\n", store, crl);

    if (ps->provDeleteCrl)
        ret = ps->provDeleteCrl(ps->hStoreProv, crl, 0);
    if (ret)
        ret = ps->memStore->crls.deleteContext(ps->memStore, crl);
    return ret;
}

static BOOL CRYPT_ProvAddCTL(WINECRYPT_CERTSTORE *store, void *ctl,
 void *toReplace, const void **ppStoreContext)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    BOOL ret;

    TRACE("(%p, %p, %p, %p)\n", store, ctl, toReplace, ppStoreContext);

    if (toReplace)
        ret = ps->memStore->ctls.addContext(ps->memStore, ctl, toReplace,
         ppStoreContext);
    else
    {
        if (ps->hdr.dwOpenFlags & CERT_STORE_READONLY_FLAG)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            ret = FALSE;
        }
        else
        {
            ret = TRUE;
            if (ps->provWriteCtl)
                ret = ps->provWriteCtl(ps->hStoreProv, ctl,
                 CERT_STORE_PROV_WRITE_ADD_FLAG);
            if (ret)
                ret = ps->memStore->ctls.addContext(ps->memStore, ctl, NULL,
                 ppStoreContext);
        }
    }
    /* dirty trick: replace the returned context's hCertStore with
     * store.
     */
    if (ret && ppStoreContext)
        (*(PCTL_CONTEXT *)ppStoreContext)->hCertStore = store;
    return ret;
}

static void *CRYPT_ProvEnumCTL(WINECRYPT_CERTSTORE *store, void *pPrev)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    void *ret;

    ret = ps->memStore->ctls.enumContext(ps->memStore, pPrev);
    if (ret)
    {
        /* same dirty trick: replace the returned context's hCertStore with
         * store.
         */
        ((PCTL_CONTEXT)ret)->hCertStore = store;
    }
    return ret;
}

static BOOL CRYPT_ProvDeleteCTL(WINECRYPT_CERTSTORE *store, void *ctl)
{
    WINE_PROVIDERSTORE *ps = (WINE_PROVIDERSTORE*)store;
    BOOL ret = TRUE;

    TRACE("(%p, %p)\n", store, ctl);

    if (ps->provDeleteCtl)
        ret = ps->provDeleteCtl(ps->hStoreProv, ctl, 0);
    if (ret)
        ret = ps->memStore->ctls.deleteContext(ps->memStore, ctl);
    return ret;
}

static BOOL ProvStore_control(WINECRYPT_CERTSTORE *cert_store, DWORD dwFlags, DWORD dwCtrlType, void const *pvCtrlPara)
{
    WINE_PROVIDERSTORE *store = (WINE_PROVIDERSTORE*)cert_store;
    BOOL ret = TRUE;

    TRACE("(%p, %08x, %d, %p)\n", store, dwFlags, dwCtrlType,
     pvCtrlPara);

    if (store->provControl)
        ret = store->provControl(store->hStoreProv, dwFlags, dwCtrlType,
         pvCtrlPara);
    return ret;
}

static const store_vtbl_t ProvStoreVtbl = {
    ProvStore_closeStore,
    ProvStore_control
};

WINECRYPT_CERTSTORE *CRYPT_ProvCreateStore(DWORD dwFlags,
 WINECRYPT_CERTSTORE *memStore, const CERT_STORE_PROV_INFO *pProvInfo)
{
    WINE_PROVIDERSTORE *ret = CryptMemAlloc(sizeof(WINE_PROVIDERSTORE));

    if (ret)
    {
        CRYPT_InitStore(&ret->hdr, dwFlags, StoreTypeProvider, &ProvStoreVtbl);
        ret->dwStoreProvFlags = pProvInfo->dwStoreProvFlags;
        if (ret->dwStoreProvFlags & CERT_STORE_PROV_EXTERNAL_FLAG)
        {
            CertCloseStore(memStore, 0);
            ret->memStore = NULL;
        }
        else
            ret->memStore = memStore;
        ret->hStoreProv = pProvInfo->hStoreProv;
        ret->hdr.certs.addContext = CRYPT_ProvAddCert;
        ret->hdr.certs.enumContext = CRYPT_ProvEnumCert;
        ret->hdr.certs.deleteContext = CRYPT_ProvDeleteCert;
        ret->hdr.crls.addContext = CRYPT_ProvAddCRL;
        ret->hdr.crls.enumContext = CRYPT_ProvEnumCRL;
        ret->hdr.crls.deleteContext = CRYPT_ProvDeleteCRL;
        ret->hdr.ctls.addContext = CRYPT_ProvAddCTL;
        ret->hdr.ctls.enumContext = CRYPT_ProvEnumCTL;
        ret->hdr.ctls.deleteContext = CRYPT_ProvDeleteCTL;
        if (pProvInfo->cStoreProvFunc > CERT_STORE_PROV_CLOSE_FUNC)
            ret->provCloseStore =
             pProvInfo->rgpvStoreProvFunc[CERT_STORE_PROV_CLOSE_FUNC];
        else
            ret->provCloseStore = NULL;
        if (pProvInfo->cStoreProvFunc >
         CERT_STORE_PROV_WRITE_CERT_FUNC)
            ret->provWriteCert = pProvInfo->rgpvStoreProvFunc[
             CERT_STORE_PROV_WRITE_CERT_FUNC];
        else
            ret->provWriteCert = NULL;
        if (pProvInfo->cStoreProvFunc >
         CERT_STORE_PROV_DELETE_CERT_FUNC)
            ret->provDeleteCert = pProvInfo->rgpvStoreProvFunc[
             CERT_STORE_PROV_DELETE_CERT_FUNC];
        else
            ret->provDeleteCert = NULL;
        if (pProvInfo->cStoreProvFunc >
         CERT_STORE_PROV_WRITE_CRL_FUNC)
            ret->provWriteCrl = pProvInfo->rgpvStoreProvFunc[
             CERT_STORE_PROV_WRITE_CRL_FUNC];
        else
            ret->provWriteCrl = NULL;
        if (pProvInfo->cStoreProvFunc >
         CERT_STORE_PROV_DELETE_CRL_FUNC)
            ret->provDeleteCrl = pProvInfo->rgpvStoreProvFunc[
             CERT_STORE_PROV_DELETE_CRL_FUNC];
        else
            ret->provDeleteCrl = NULL;
        if (pProvInfo->cStoreProvFunc >
         CERT_STORE_PROV_WRITE_CTL_FUNC)
            ret->provWriteCtl = pProvInfo->rgpvStoreProvFunc[
             CERT_STORE_PROV_WRITE_CTL_FUNC];
        else
            ret->provWriteCtl = NULL;
        if (pProvInfo->cStoreProvFunc >
         CERT_STORE_PROV_DELETE_CTL_FUNC)
            ret->provDeleteCtl = pProvInfo->rgpvStoreProvFunc[
             CERT_STORE_PROV_DELETE_CTL_FUNC];
        else
            ret->provDeleteCtl = NULL;
        if (pProvInfo->cStoreProvFunc >
         CERT_STORE_PROV_CONTROL_FUNC)
            ret->provControl = pProvInfo->rgpvStoreProvFunc[
             CERT_STORE_PROV_CONTROL_FUNC];
        else
            ret->provControl = NULL;
    }
    return (WINECRYPT_CERTSTORE*)ret;
}

WINECRYPT_CERTSTORE *CRYPT_ProvOpenStore(LPCSTR lpszStoreProvider,
 DWORD dwEncodingType, HCRYPTPROV hCryptProv, DWORD dwFlags, const void *pvPara)
{
    static HCRYPTOIDFUNCSET set = NULL;
    PFN_CERT_DLL_OPEN_STORE_PROV_FUNC provOpenFunc;
    HCRYPTOIDFUNCADDR hFunc;
    WINECRYPT_CERTSTORE *ret = NULL;

    if (!set)
        set = CryptInitOIDFunctionSet(CRYPT_OID_OPEN_STORE_PROV_FUNC, 0);
    CryptGetOIDFunctionAddress(set, dwEncodingType, lpszStoreProvider, 0,
     (void **)&provOpenFunc, &hFunc);
    if (provOpenFunc)
    {
        CERT_STORE_PROV_INFO provInfo = { 0 };

        provInfo.cbSize = sizeof(provInfo);
        if (dwFlags & CERT_STORE_DELETE_FLAG)
            provOpenFunc(lpszStoreProvider, dwEncodingType, hCryptProv,
             dwFlags, pvPara, NULL, &provInfo);
        else
        {
            HCERTSTORE memStore;

            memStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0,
             CERT_STORE_CREATE_NEW_FLAG, NULL);
            if (memStore)
            {
                if (provOpenFunc(lpszStoreProvider, dwEncodingType, hCryptProv,
                 dwFlags, pvPara, memStore, &provInfo))
                    ret = CRYPT_ProvCreateStore(dwFlags, memStore, &provInfo);
                else
                    CertCloseStore(memStore, 0);
            }
        }
        CryptFreeOIDFunctionAddress(hFunc, 0);
    }
    else
        SetLastError(ERROR_FILE_NOT_FOUND);
    return ret;
}
