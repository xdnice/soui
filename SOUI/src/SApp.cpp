#include "souistd.h"
#include "SApp.h"
#include "core/SimpleWnd.h"
#include "core/SWindowMgr.h"

#include "res.mgr/sfontpool.h"
#include "res.mgr/SNamedValue.h"
#include "res.mgr/SSkinPool.h"
#include "res.mgr/SStylePool.h"
#include "res.mgr/SObjDefAttr.h"

#include "helper/STimerEx.h"
#include "helper/SScriptTimer.h"
#include "helper/mybuffer.h"
#include "helper/SToolTip.h"
#include "helper/AppDir.h"
#include "helper/SwndFinder.h"

#include "control/SRichEdit.h"
#include "control/Smessagebox.h"
#include "updatelayeredwindow/SUpdateLayeredWindow.h"
#include "helper/splitstring.h"

namespace SOUI
{

class SNullTranslator : public TObjRefImpl<ITranslatorMgr>
{
public:
    BOOL CreateTranslator(ITranslator **pLang){return FALSE;}
    BOOL InstallTranslator(ITranslator * pLang){return FALSE;}
    BOOL UninstallTranslator(REFGUID id){return FALSE;}
    SStringW tr(const SStringW & strSrc,const SStringW & strCtx)
    {
        return strSrc;
    } 
};

class SDefToolTipFactory : public TObjRefImpl<IToolTipFactory>
{
public:
    /*virtual */IToolTip * CreateToolTip(HWND hHost)
    {
        STipCtrl *pTipCtrl = new STipCtrl;
        if(!pTipCtrl->Create())
        {
            delete pTipCtrl;
            return NULL;
        }
        return pTipCtrl;
    }

    /*virtual */void DestroyToolTip(IToolTip *pToolTip)
    {
        if(pToolTip)
        {
            STipCtrl *pTipCtrl= (STipCtrl *)pToolTip;
            pTipCtrl->DestroyWindow();
        }
    }
};

class SDefMsgLoopFactory : public TObjRefImpl<IMsgLoopFactory>
{
public:
    virtual SMessageLoop * CreateMsgLoop()
    {
        return new SMessageLoop;
    }

    virtual void DestoryMsgLoop(SMessageLoop * pMsgLoop)
    {
        delete pMsgLoop;
    }
};

//////////////////////////////////////////////////////////////////////////
// SApplication

template<> SApplication* SSingleton<SApplication>::ms_Singleton = 0;

SApplication::SApplication(IRenderFactory *pRendFactory,HINSTANCE hInst,LPCTSTR pszHostClassName)
    :m_hInst(hInst)
    ,m_RenderFactory(pRendFactory)
    ,m_hMainWnd(NULL)
{
    SWndSurface::Init();
    _CreateSingletons();
    CSimpleWndHelper::Init(m_hInst,pszHostClassName);
    STextServiceHelper::Init();
    SRicheditMenuDef::Init();
    m_translator.Attach(new SNullTranslator);
    m_tooltipFactory.Attach(new SDefToolTipFactory);
    m_msgLoopFactory.Attach(new SDefMsgLoopFactory);
    
    SAppDir appDir(hInst);
    m_strAppDir = appDir.AppDir();
    
    m_pMsgLoop = GetMsgLoopFactory()->CreateMsgLoop();
}

SApplication::~SApplication(void)
{
    GetMsgLoopFactory()->DestoryMsgLoop(m_pMsgLoop);
    
    _DestroySingletons();
    CSimpleWndHelper::Destroy();
    STextServiceHelper::Destroy();
    SRicheditMenuDef::Destroy();
}

void SApplication::_CreateSingletons()
{
    new SWindowMgr();
    new STimer2();
    new SScriptTimer();
    new SFontPool(m_RenderFactory);
    new SNamedString();
    new SNamedID();
    new SNamedColor();
    new SSkinPoolMgr();
    new SStylePoolMgr();
    new SObjDefAttr();
    new SWindowFinder();
}

void SApplication::_DestroySingletons()
{
    delete SWindowFinder::getSingletonPtr();
    delete SObjDefAttr::getSingletonPtr();
    delete SStylePoolMgr::getSingletonPtr();
    delete SSkinPoolMgr::getSingletonPtr();
    delete SNamedID::getSingletonPtr();
    delete SNamedString::getSingletonPtr();
    delete SNamedColor::getSingletonPtr();
    delete SFontPool::getSingletonPtr();
    delete SScriptTimer::getSingletonPtr();
    delete STimer2::getSingletonPtr();
    delete SWindowMgr::getSingletonPtr();
}

BOOL SApplication::_LoadXmlDocment( LPCTSTR pszXmlName ,LPCTSTR pszType ,pugi::xml_document & xmlDoc)
{
    DWORD dwSize=GetRawBufferSize(pszType,pszXmlName);
    if(dwSize==0) return FALSE;

    CMyBuffer<char> strXml;
    strXml.Allocate(dwSize);
    GetRawBuffer(pszType,pszXmlName,strXml,dwSize);

    pugi::xml_parse_result result= xmlDoc.load_buffer(strXml,strXml.size(),pugi::parse_default,pugi::encoding_utf8);
    SASSERT_FMTW(result,L"parse xml error! xmlName=%s,desc=%s,offset=%d",pszXmlName,result.description(),result.offset);
    return result;
}

BOOL SApplication::LoadXmlDocment( pugi::xml_document & xmlDoc,LPCTSTR pszXmlName ,LPCTSTR pszType )
{
    return _LoadXmlDocment(pszXmlName,pszType,xmlDoc);
}

BOOL SApplication::LoadXmlDocment(pugi::xml_document & xmlDoc, const SStringT & strXmlTypeName)
{
    SStringTList strLst;
    if(2!=ParseResID(strXmlTypeName,strLst)) return FALSE;
    return LoadXmlDocment(xmlDoc,strLst[1],strLst[0]);
}

BOOL SApplication::Init( LPCTSTR pszName ,LPCTSTR pszType)
{
    SASSERT(m_RenderFactory);

    pugi::xml_document xmlDoc;
    if(!LOADXML(xmlDoc,pszName,pszType)) return FALSE;
    pugi::xml_node root=xmlDoc.child(L"UIDEF");
    if(!root) return FALSE;

    //set default font
    pugi::xml_node xmlFont;
    xmlFont=root.child(L"font");
    if(xmlFont)
    {
        int nSize=xmlFont.attribute(L"size").as_int(12);
        BYTE byCharset=(BYTE)xmlFont.attribute(L"charset").as_int(DEFAULT_CHARSET);
        SFontPool::getSingleton().SetDefaultFont(S_CW2T(xmlFont.attribute(L"face").value()),nSize,byCharset);
    }
    
    pugi::xml_node xmlString = root.child(L"string");
    if(xmlString)
    {//load string table
        pugi::xml_document xmlStrDoc;
        if(xmlString.attribute(L"src"))
        {
            LoadXmlDocment(xmlStrDoc,xmlString.attribute(L"src").value());
            xmlString = xmlStrDoc.child(L"string");
        }
        SNamedString::getSingleton().Init(xmlString);        
    }
    pugi::xml_node xmlColor = root.child(L"color");
    if(xmlColor)
    {//load color table
        pugi::xml_document xmlColorDoc;
        if(xmlColor.attribute(L"src"))
        {
            LoadXmlDocment(xmlColorDoc,xmlColor.attribute(L"src").value());
            xmlColor = xmlColorDoc.child(L"color");
        }
        SNamedColor::getSingleton().Init(xmlColor);
    }
    
    pugi::xml_node xmlSkin=root.child(L"skin");
    pugi::xml_document xmlSkinDoc;
    if(xmlSkin.attribute(L"src"))
    {
        LoadXmlDocment(xmlSkinDoc,xmlSkin.attribute(L"src").value());
        xmlSkin = xmlSkinDoc.child(L"skin");
    }
    if(xmlSkin)
    {
        SSkinPool *pSkinPool = new SSkinPool;
        pSkinPool->LoadSkins(xmlSkin);
        SSkinPoolMgr::getSingletonPtr()->PushSkinPool(pSkinPool);
        pSkinPool->Release();
    }
    
    SStylePool *pStylePool = new SStylePool;
    pStylePool->Init(root.child(L"style"));
    SStylePoolMgr::getSingleton().PushStylePool(pStylePool);
    pStylePool->Release();
    
    SObjDefAttr::getSingleton().Init(root.child(L"objattr"));
    return TRUE;
}

UINT SApplication::LoadSystemNamedResource( IResProvider *pResProvider )
{
    UINT uRet=0;
    AddResProvider(pResProvider);
    //load system skins
    {
        pugi::xml_document xmlDoc;
        if(_LoadXmlDocment(_T("SYS_XML_SKIN"),_T("XML"),xmlDoc))
        {
            SSkinPool * p= SSkinPoolMgr::getSingletonPtr()->GetBuiltinSkinPool();
            p->LoadSkins(xmlDoc.child(L"skin"));
        }else
        {
            uRet |= 0x01;
        }
    }
    //load edit context menu
    {
        pugi::xml_document xmlDoc;
        if(_LoadXmlDocment(_T("SYS_XML_EDITMENU"),_T("XML"),xmlDoc))
        {
            SRicheditMenuDef::getSingleton().SetMenuXml(xmlDoc.child(L"editmenu"));
        }else
        {
            uRet |= 0x02;
        }
    }
    //load messagebox template
    {
        pugi::xml_document xmlDoc;
        if(!_LoadXmlDocment(_T("SYS_XML_MSGBOX"),_T("XML"),xmlDoc)
        || !SetMsgTemplate(xmlDoc.child(L"SOUI")))
        {
            uRet |= 0x04;
        }
    }
    RemoveResProvider(pResProvider);
    return uRet;
}

int SApplication::Run( HWND hMainWnd )
{
    m_hMainWnd = hMainWnd;
    return m_pMsgLoop->Run();
}

HINSTANCE SApplication::GetInstance()
{
	return m_hInst;
}

void SApplication::SetTranslator(ITranslatorMgr * pTrans)
{
	m_translator = pTrans;
}

ITranslatorMgr * SApplication::GetTranslator()
{
	return m_translator;
}

void SApplication::SetScriptFactory(IScriptFactory *pScriptFactory)
{
	m_pScriptFactory = pScriptFactory;
}


HRESULT SApplication::CreateScriptModule( IScriptModule **ppScriptModule )
{
    if(!m_pScriptFactory) return E_FAIL;
    return m_pScriptFactory->CreateScriptModule(ppScriptModule);
}

IRenderFactory * SApplication::GetRenderFactory()
{
	return m_RenderFactory;
}

void SApplication::SetRealWndHandler( IRealWndHandler *pRealHandler )
{
    m_pRealWndHandler = pRealHandler;
}

IRealWndHandler * SApplication::GetRealWndHander()
{
    return m_pRealWndHandler;
}

IToolTipFactory * SApplication::GetToolTipFactory()
{
    return m_tooltipFactory;
}

void SApplication::SetToolTipFactory( IToolTipFactory* pToolTipFac )
{
    m_tooltipFactory = pToolTipFac;
}

HWND SApplication::GetMainWnd()
{
    return m_hMainWnd;
}

BOOL SApplication::SetMsgLoopFactory(IMsgLoopFactory *pMsgLoopFac)
{
    if(m_pMsgLoop->IsRunning()) return FALSE;
    m_msgLoopFactory->DestoryMsgLoop(m_pMsgLoop);
    m_msgLoopFactory = pMsgLoopFac;
    m_pMsgLoop = m_msgLoopFactory->CreateMsgLoop();
    return TRUE;
}

IMsgLoopFactory * SApplication::GetMsgLoopFactory()
{
    return m_msgLoopFactory;
}

void SApplication::InitXmlNamedID(const SNamedID::NAMEDVALUE *pNamedValue,int nCount,BOOL bSorted)
{
    SNamedID::getSingleton().Init2(pNamedValue,nCount,bSorted);
}

}//namespace SOUI