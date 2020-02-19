#pragma once

#include <string>
#include <stdexcept>
#include <windows.h>

template <typename T = IUnknown>
class UnknownImp : public T
{
public:
    virtual ~UnknownImp() {};

    virtual ULONG STDMETHODCALLTYPE AddRef()
    {
        return InterlockedIncrement(&_cRef);
    }

    virtual ULONG STDMETHODCALLTYPE Release()
    {
        ULONG ulRef = InterlockedDecrement(&_cRef);
        if (0 == ulRef)
        {
            delete this;
        }
        return ulRef;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID  riid,
        VOID** ppvInterface)
    {
        if (IID_IUnknown == riid)
        {
            AddRef();
            *ppvInterface = (IUnknown*)this;
        }
        else if (__uuidof(T) == riid)
        {
            AddRef();
            *ppvInterface = (T*)this;
        }
        else
        {
            *ppvInterface = NULL;
            return E_NOINTERFACE;
        }
        return S_OK;
    }

private:
    LONG _cRef = 1;
};

#define ThrowIfError(hr) \
    if (FAILED(hr)) { printf("hr = %x\n", hr); throw new std::runtime_error(""); }