#include "QCefWidgetImpl.h"
#include "QCefManager.h"
#include "QCefProtocol.h"
#include <QApplication>
#include <QDebug>
#include <QWindow>
#include <QDir>
#include <QTimer>
#include <QPainter>
#include <QDesktopWidget>
#include <include/base/cef_logging.h>
#include "Include/QCefWidget.h"
#include "Include/QCefOpenGLWidget.h"
#include "QCefGlobalSetting.h"
#include "CefBrowserApp/QCefRequestContextHandler.h"
#include "Win32DpiHelper.h"

namespace {
LPCWSTR kPreWndProc = L"CefPreWndProc";
LPCWSTR kDraggableRegion = L"CefDraggableRegion";
LPCWSTR kTopLevelHwnd = L"CefTopLevelHwnd";
LPCWSTR kTopLevelWidget = L"CefTopLevelWidget";
LPCWSTR kQCefWidgetHwnd = L"kQCefWidgetHwnd";
}  // namespace

QCefWidgetImpl::QCefWidgetImpl(WidgetType vt, QWidget* pWidget) :
    pWidget_(pWidget),
    pTopWidget_(nullptr),
    draggableRegion_(nullptr),
    vt_(vt),
    deviceScaleFactor_(1.f),
    browserCreated_(false),
    browserClosing_(false),
    pQCefViewHandler_(nullptr) {
  draggableRegion_ = ::CreateRectRgn(0, 0, 0, 0);
  QCefManager::getInstance().initializeCef();

  deviceScaleFactor_ = pWidget_->devicePixelRatioF();
  //
  int currentScreen = QApplication::desktop()->screenNumber(pWidget_); // ����
  QScreen* pScreen = nullptr;
  if (currentScreen >= 0 && currentScreen < QGuiApplication::screens().size()) {
      pScreen = QGuiApplication::screens().at(currentScreen);
  }
  else {
      pScreen = QGuiApplication::primaryScreen();
  }
  connect(pWidget_->window()->windowHandle(), &QWindow::screenChanged, this, &QCefWidgetImpl::onScreenChanged);
  connect(pScreen, &QScreen::logicalDotsPerInchChanged, this, &QCefWidgetImpl::onScreenLogicalDotsPerInchChanged);
  connect(this, &QCefWidgetImpl::sgBrowserCommand, this, &QCefWidgetImpl::onBrowserCommand);
}

QCefWidgetImpl::~QCefWidgetImpl() {
  qDebug().noquote() << "QCefWidgetImpl::~QCefWidgetImpl:" << this;
  ::DeleteObject(draggableRegion_);
  draggableRegion_ = nullptr;
  pQCefViewHandler_ = nullptr;
  if (pCefUIEventWin_)
    pCefUIEventWin_.reset();
  if (pCefUIEvent_)
      pCefUIEvent_.reset();
}

bool QCefWidgetImpl::createBrowser(const QString& url) {
  if (browserCreated_)
    return true;
  Q_ASSERT(pWidget_);
  CefWindowHandle hwnd = 0;
  if (!browserSetting_.osrQWidgetNoSysWnd)
  {
      hwnd = (CefWindowHandle)pWidget_->winId();
      Q_ASSERT(hwnd);
      if (!hwnd)
          return false;
  }
  else {
      if (pCefUIEvent_)
          pCefUIEvent_.reset();
      pCefUIEvent_ = std::make_shared<QCefWidgetUIEventHandler>(pWidget_);
      Q_ASSERT(pCefUIEvent_);
  }

#if (defined Q_OS_WIN32 || defined Q_OS_WIN64)
  if (hwnd)
  {
      RegisterTouchWindow(hwnd, 0);
  }
#endif

  QCefGlobalSetting::initializeInstance();
  QDir resourceDir = QString::fromStdWString(
      QCefGlobalSetting::resource_directory_path.ToWString());
  browserSetting_.devToolsResourceExist =
      QFile::exists(resourceDir.filePath("devtools_resources.pak"));

  CefWindowInfo window_info;
  CefBrowserSettings browserSettings;
  if (browserSetting_.osrEnabled) {
    window_info.SetAsWindowless(hwnd);

    // winsoft666:
    // Enable all plugins here.
    // If not set enabled, PDF will cannot be render correctly, even if add command lines in OnBeforeCommandLineProcessing function.
    browserSettings.plugins = STATE_ENABLED;
    browserSettings.windowless_frame_rate = browserSetting_.fps;
    browserSettings.background_color =
        CefColorSetARGB(browserSetting_.backgroundColor.alpha(),
                        browserSetting_.backgroundColor.red(),
                        browserSetting_.backgroundColor.green(),
                        browserSetting_.backgroundColor.blue());
  }
  else {
    // Don't use QWidget:rect() function, since qt's bug: https://bugreports.qt.io/browse/QTBUG-89646
    RECT rc = {0, 0, 0, 0};
    ::GetWindowRect(hwnd, &rc);

    window_info.SetAsChild(hwnd, {0, 0, rc.right - rc.left, rc.bottom - rc.left});

    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE) {
      // Don't activate the browser window on creation.
      window_info.ex_style |= WS_EX_NOACTIVATE;
    }

    // winsoft666:
    // Enable all plugins here.
    // If not set enabled, PDF will cannot be render correctly, even if add command lines in OnBeforeCommandLineProcessing function.
    browserSettings.plugins = STATE_ENABLED;
    browserSettings.background_color =
        CefColorSetARGB(browserSetting_.backgroundColor.alpha(),
                        browserSetting_.backgroundColor.red(),
                        browserSetting_.backgroundColor.green(),
                        browserSetting_.backgroundColor.blue());
  }

  pQCefViewHandler_ = new QCefBrowserHandler(this);
  for (auto& it : todoAddProviders_.keys()) {
    pQCefViewHandler_->addResourceProvider(todoAddProviders_[it], it);
  }

  todoAddProviders_.clear();

  CefRefPtr<CefRequestContext> requestContext =
      CefRequestContext::CreateContext(CefRequestContext::GetGlobalContext(),
                                       new RequestContextHandler);

  // This method can be called on any browser process thread and will not block.
#if CEF_VERSION_MAJOR == 72 || CEF_VERSION_MAJOR == 74
  if (!CefBrowserHost::CreateBrowser(window_info,
                                     pQCefViewHandler_,
                                     url.toStdWString(),
                                     browserSettings,
                                     requestContext)) {
    return false;
  }
#elif CEF_VERSION_MAJOR >= 76
  if (!CefBrowserHost::CreateBrowser(window_info,
                                     pQCefViewHandler_,
                                     url.toStdWString(),
                                     browserSettings,
                                     nullptr,
                                     requestContext)) {
    return false;
  }
#endif

  browserCreated_ = true;
  return true;
}

bool QCefWidgetImpl::createDevTools(CefRefPtr<CefBrowser> targetBrowser) {
  if (browserCreated_)
    return true;
  Q_ASSERT(pWidget_);
  CefWindowHandle hwnd = nullptr;
  if (pWidget_)
    hwnd = (CefWindowHandle)pWidget_->winId();
  Q_ASSERT(hwnd);
  if (!hwnd)
    return false;

#if (defined Q_OS_WIN32 || defined Q_OS_WIN64)
  RegisterTouchWindow(hwnd, 0);
#endif

  QCefGlobalSetting::initializeInstance();
  QDir resourceDir = QString::fromStdWString(
      QCefGlobalSetting::resource_directory_path.ToWString());
  browserSetting_.devToolsResourceExist =
      QFile::exists(resourceDir.filePath("devtools_resources.pak"));

  CefWindowInfo windowInfo;
  CefBrowserSettings browserSettings;
  if (browserSetting_.osrEnabled) {
    windowInfo.SetAsWindowless(hwnd);

    // winsoft666:
    // Enable all plugins here.
    // If not set enabled, PDF will cannot be render correctly, even if add command lines in OnBeforeCommandLineProcessing function.
    browserSettings.plugins = STATE_ENABLED;
    browserSettings.windowless_frame_rate = browserSetting_.fps;
    browserSettings.background_color =
        CefColorSetARGB(browserSetting_.backgroundColor.alpha(),
                        browserSetting_.backgroundColor.red(),
                        browserSetting_.backgroundColor.green(),
                        browserSetting_.backgroundColor.blue());
  }
  else {
    // // Don't use QWidget:rect() function, since qt's bug: https://bugreports.qt.io/browse/QTBUG-89646
    RECT rc = {0, 0, 0, 0};
    ::GetWindowRect((HWND)pWidget_->winId(), &rc);

    windowInfo.SetAsChild(hwnd, {0, 0, rc.right - rc.left, rc.bottom - rc.top});
    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE) {
      // Don't activate the browser window on creation.
      windowInfo.ex_style |= WS_EX_NOACTIVATE;
    }

    browserSettings.plugins = STATE_ENABLED;
    browserSettings.background_color =
        CefColorSetARGB(browserSetting_.backgroundColor.alpha(),
                        browserSetting_.backgroundColor.red(),
                        browserSetting_.backgroundColor.green(),
                        browserSetting_.backgroundColor.blue());
  }

  pQCefViewHandler_ = new QCefBrowserHandler(this);

  if (targetBrowser) {
    targetBrowser->GetHost()->ShowDevTools(
        windowInfo, pQCefViewHandler_, browserSettings, CefPoint());
  }

  browserCreated_ = true;
  return true;
}

void QCefWidgetImpl::browserCreatedNotify(CefRefPtr<CefBrowser> browser) {
  Q_ASSERT(pWidget_);
#if (defined Q_OS_WIN32 || defined Q_OS_WIN64)
  if (!browserSetting_.osrQWidgetNoSysWnd)
  {
      if (pCefUIEventWin_)
          pCefUIEventWin_.reset();
      if (browserSetting_.osrEnabled) {
          Q_ASSERT(pQCefViewHandler_);
          pCefUIEventWin_ = std::make_shared<QCefWidgetUIEventHandlerWin>(
              (HWND)pWidget_->winId(), browser, pQCefViewHandler_);
          Q_ASSERT(pCefUIEventWin_);
          if (pCefUIEventWin_) {
              pCefUIEventWin_->setDeviceScaleFactor(deviceScaleFactor_);
          }
      }

      // See QTBUG-89646
      QRect curRc = pWidget_->geometry();
      ::SetWindowPos((HWND)pWidget_->winId(), NULL,
          curRc.x() * deviceScaleFactor_,
          curRc.y() * deviceScaleFactor_,
          curRc.width() * deviceScaleFactor_,
          curRc.height() * deviceScaleFactor_,
          SWP_NOZORDER);

      CefWindowHandle cefhwnd = NULL;
      if (browser && browser->GetHost())
          cefhwnd = browser->GetHost()->GetWindowHandle();
      if (cefhwnd) {
          ::SetWindowPos(cefhwnd,
              NULL,
              0,
              0,
              curRc.width() * deviceScaleFactor_,
              curRc.height() * deviceScaleFactor_,
              SWP_NOZORDER);
      }
  }
  else {
      Q_ASSERT(pCefUIEvent_);
      if (pCefUIEvent_)
      {
          pCefUIEvent_->SetCefBrowser(browser, pQCefViewHandler_);
      }
  }
#else
#error("No implement")
#endif

  pTopWidget_ = QCefManager::getInstance().addBrowser(
      pWidget_, this, browser, &browserSetting_);
}

void QCefWidgetImpl::browserContextCreatedNotify(CefRefPtr<CefBrowser> browser) {
  qDebug().noquote() << "QCefWidgetImpl::browserContextCreatedNotify:" << this;
  Q_ASSERT(browser && browser->GetHost());
  Q_ASSERT(pTopWidget_);
  if (!browserSetting_.osrQWidgetNoSysWnd) {
      if (browser && browser->GetHost()) {
          HWND hwnd = browser->GetHost()->GetWindowHandle();
          Q_ASSERT(hwnd);
          if (hwnd) {
              if (browserSetting_.osrEnabled) {
                  subclassWindow(hwnd,
                      (HWND)pWidget_->winId(),
                      pTopWidget_);
              }
              else {
                  ::EnumChildWindows(hwnd, EnumWindowsProc4Subclass, reinterpret_cast<LPARAM>(this));
              }
          }
      }
  }
}

void QCefWidgetImpl::browserClosingNotify(CefRefPtr<CefBrowser> browser) {
  qDebug().noquote() << "QCefWidgetImpl::browserClosingNotify:" << this;
  browserCreated_ = false;

  Q_ASSERT(browser && browser->GetHost());
  if (browser && browser->GetHost()) {
      if (!browserSetting_.osrQWidgetNoSysWnd)
      {
          HWND hwnd = browser->GetHost()->GetWindowHandle();
          Q_ASSERT(hwnd);
          if (hwnd) {
              if (browserSetting_.osrEnabled) {
                  unSubclassWindow(hwnd);
              }
              else {
                  ::EnumChildWindows(hwnd, EnumWindowsProc4Unsubclass, reinterpret_cast<LPARAM>(this));
              }
          }
      }
  }

  if (pCefUIEventWin_)
    pCefUIEventWin_.reset();
  if (pCefUIEvent_)
      pCefUIEvent_.reset();
  QCefManager::getInstance().setBrowserClosing(pWidget_);
}

void QCefWidgetImpl::browserDestoryedNotify(CefRefPtr<CefBrowser> browser) {
  qDebug().noquote() << "QCefWidgetImpl::browserDestoryedNotify:" << this;
  Q_ASSERT(!pCefUIEventWin_);

  QCefManager::getInstance().setBrowserClosed(pWidget_);

  if (QCefManager::getInstance().aliveBrowserCount(pTopWidget_) == 0) {
    QCefManager::getInstance().unhookTopWidget(pTopWidget_);
    QCefManager::getInstance().removeAllCefWidgets(pTopWidget_);
    if (pTopWidget_) {
#if (QT_VERSION > QT_VERSION_CHECK(5,12,0))
        QMetaObject::invokeMethod(
            pTopWidget_, [this]() {
                QTimer::singleShot(500, this, [this]() { // give enought time to release cef resource
                    if (pTopWidget_)
                        pTopWidget_->close();
                    });
            },
            Qt::QueuedConnection);
#else
        QVariantMap mapVar;
        mapVar["sCmdType"] = "TopWidgetClose";
        emit this->sgBrowserCommand(mapVar);
#endif
    }
  }
}

LRESULT CALLBACK QCefWidgetImpl::SubclassedWindowProc(HWND hWnd,
                                                      UINT message,
                                                      WPARAM wParam,
                                                      LPARAM lParam) {
  WNDPROC hPreWndProc = reinterpret_cast<WNDPROC>(::GetPropW(hWnd, kPreWndProc));
  if (!hPreWndProc)
    return 0;
  HWND hQCefWidgetHwnd = reinterpret_cast<HWND>(::GetPropW(hWnd, kQCefWidgetHwnd));
  HRGN hRegion = reinterpret_cast<HRGN>(::GetPropW(hQCefWidgetHwnd, kDraggableRegion));
  HWND hTopLevelWnd = reinterpret_cast<HWND>(::GetPropW(hWnd, kTopLevelHwnd));

  if (message == WM_LBUTTONDOWN) {
    if (hRegion) {
      POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      if (::PtInRegion(hRegion, point.x, point.y)) {
        ::ClientToScreen(hWnd, &point);
        if (hTopLevelWnd) {
          ::PostMessage(hTopLevelWnd,
                        WM_NCLBUTTONDOWN,
                        HTCAPTION,
                        MAKELPARAM(point.x, point.y));
        }
        return 0;
      }
    }
  }
#if 0
  else if (message == WM_MOUSEMOVE) {
    QWidget* pTopWidget = reinterpret_cast<QWidget*>(::GetPropW(hWnd, kTopLevelWidget));
    if (pTopWidget) {
      QMetaObject::invokeMethod(pTopWidget, [pTopWidget, lParam]() {
        QPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        QMouseEvent mouseEvent((QEvent::MouseMove), pt, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(pTopWidget, &mouseEvent);
      });
    }
  }
#endif

  Q_ASSERT(hPreWndProc);
  return CallWindowProc(hPreWndProc, hWnd, message, wParam, lParam);
}

void QCefWidgetImpl::subclassWindow(HWND hWnd, HWND hQCefWidgetHwnd, QWidget* pTopLevelWidget) {
  if (GetWindowLongPtr(hWnd, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(SubclassedWindowProc))
    return;  // Has been subclassed

  SetLastError(ERROR_SUCCESS);
  LONG_PTR hOldWndProc = SetWindowLongPtr(
      hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubclassedWindowProc));
  if (hOldWndProc == NULL && GetLastError() != ERROR_SUCCESS) {
    return;
  }

  ::SetPropW(hWnd, kPreWndProc, reinterpret_cast<HANDLE>(hOldWndProc));
  if (pTopLevelWidget) {
    ::SetPropW(hWnd, kTopLevelHwnd, (HWND)pTopLevelWidget->winId());
    ::SetPropW(hWnd, kTopLevelWidget, (HANDLE)pTopLevelWidget);
  }
  ::SetPropW(hWnd, kQCefWidgetHwnd, hQCefWidgetHwnd);
}

void QCefWidgetImpl::unSubclassWindow(HWND hWnd) {
  LONG_PTR hPreWndProc =
      reinterpret_cast<LONG_PTR>(::GetPropW(hWnd, kPreWndProc));
  if (hPreWndProc) {
    LONG_PTR hPreviousWndProc =
        SetWindowLongPtr(hWnd, GWLP_WNDPROC, hPreWndProc);
    ALLOW_UNUSED_LOCAL(hPreviousWndProc);
    DCHECK_EQ(hPreviousWndProc,
              reinterpret_cast<LONG_PTR>(SubclassedWindowProc));
  }

  ::RemovePropW(hWnd, kPreWndProc);
  ::RemovePropW(hWnd, kTopLevelHwnd);
  ::RemovePropW(hWnd, kTopLevelWidget);
  ::RemovePropW(hWnd, kQCefWidgetHwnd);
}

BOOL CALLBACK QCefWidgetImpl::EnumWindowsProc4Subclass(HWND hwnd, LPARAM lParam) {
  QCefWidgetImpl* pImpl = (QCefWidgetImpl*)lParam;
  subclassWindow(hwnd, (HWND)pImpl->pWidget_->winId(), pImpl->pTopWidget_);
  return TRUE;
}

BOOL CALLBACK QCefWidgetImpl::EnumWindowsProc4Unsubclass(HWND hwnd, LPARAM lParam) {
  unSubclassWindow(hwnd);
  return TRUE;
}

void QCefWidgetImpl::onScreenLogicalDotsPerInchChanged() {
  qDebug().noquote() << "onScreenLogicalDotsPerInchChanged";
  
  if (!browserSetting_.osrQWidgetNoSysWnd)
  {
      deviceScaleFactor_ = pWidget_->devicePixelRatioF();
      if (pCefUIEventWin_)
        pCefUIEventWin_->setDeviceScaleFactor(deviceScaleFactor_);

      // See QTBUG-89646
      QRectF curRc = pWidget_->geometry();
      ::SetWindowPos((HWND)pWidget_->winId(), NULL,
                     curRc.x() * deviceScaleFactor_,
                     curRc.y() * deviceScaleFactor_,
                     curRc.width() * deviceScaleFactor_,
                     curRc.height() * deviceScaleFactor_,
                     SWP_NOZORDER);
  }

  if (browserSetting_.osrEnabled) {
    // For simply, always notify screen info changed thought screen not changed.
    if (this->browser() && this->browser()->GetHost())
      this->browser()->GetHost()->NotifyScreenInfoChanged();
  }
}

void QCefWidgetImpl::onBrowserCommand(QVariantMap mapVar)
{
    QString sCmd = mapVar["sCmdType"].toString();
    if (sCmd == "setCursor")
    {
        Qt::CursorShape shape = (Qt::CursorShape)mapVar["nQtCursor"].toInt();
        if (pWidget_)
        {
            pWidget_->setCursor(shape);
        }
    }
    else if (sCmd == "CreateBrowser")
    {
        QString sUrl = mapVar["sUrl"].toString();
        if (!createBrowser(sUrl)) {
            Q_ASSERT(false);
        }
    }
    else if (sCmd == "TopWidgetClose")
    {
        QTimer::singleShot(500, this, [this]() { // give enought time to release cef resource
            if (pTopWidget_)
                pTopWidget_->close();
            });
    }
}

void QCefWidgetImpl::onScreenChanged(QScreen* screen) {
  qDebug().noquote() << "onScreenChanged";
  connect(screen, &QScreen::logicalDotsPerInchChanged, this, &QCefWidgetImpl::onScreenLogicalDotsPerInchChanged);
  if (!browserSetting_.osrQWidgetNoSysWnd)
  {
      deviceScaleFactor_ = pWidget_->devicePixelRatioF();
      if (pCefUIEventWin_)
          pCefUIEventWin_->setDeviceScaleFactor(deviceScaleFactor_);

      // See QTBUG-89646
      QRectF curRc = pWidget_->geometry();
      ::SetWindowPos((HWND)pWidget_->winId(), NULL,
          curRc.x() * deviceScaleFactor_,
          curRc.y() * deviceScaleFactor_,
          curRc.width() * deviceScaleFactor_,
          curRc.height() * deviceScaleFactor_,
          SWP_NOZORDER);
  }

  if (browserSetting_.osrEnabled) {
    // For simply, always notify screen info changed thought screen not changed.
    if (this->browser() && this->browser()->GetHost())
      this->browser()->GetHost()->NotifyScreenInfoChanged();
  }
}

void QCefWidgetImpl::draggableRegionsChangedNotify(
    CefRefPtr<CefBrowser> browser,
    const std::vector<CefDraggableRegion>& regions) {

    if (!browserSetting_.osrQWidgetNoSysWnd)
    {
        ::SetRectRgn(draggableRegion_, 0, 0, 0, 0);

        float dpiScale = deviceScaleFactor_;

        std::vector<CefDraggableRegion>::const_iterator it = regions.begin();
        for (; it != regions.end(); ++it) {
            cef_rect_t rc = it->bounds;
            rc.x = (float)rc.x * dpiScale;
            rc.y = (float)rc.y * dpiScale;
            rc.width = (float)rc.width * dpiScale;
            rc.height = (float)rc.height * dpiScale;
            HRGN region =
                ::CreateRectRgn(rc.x, rc.y, rc.x + rc.width, rc.y + rc.height);
            ::CombineRgn(draggableRegion_,
                draggableRegion_,
                region,
                it->draggable ? RGN_OR : RGN_DIFF);
            ::DeleteObject(region);
        }

        if (regions.empty())
            ::RemovePropW((HWND)pWidget_->winId(), kDraggableRegion);
        else
            ::SetPropW((HWND)pWidget_->winId(), kDraggableRegion, reinterpret_cast<HANDLE>(draggableRegion_));
    }
}

void QCefWidgetImpl::imeCompositionRangeChangedNotify(
    CefRefPtr<CefBrowser> browser,
    const CefRange& selection_range,
    const CefRenderHandler::RectList& character_bounds) {
  if (pCefUIEventWin_) {
    pCefUIEventWin_->OnImeCompositionRangeChanged(
        browser, selection_range, character_bounds);
  }
}

void QCefWidgetImpl::browserCursorChange(CefRefPtr<CefBrowser> browser, int nQtCursor)
{
    QVariantMap mapVar;
    mapVar["sCmdType"] = "setCursor";
    mapVar["nQtCursor"] = nQtCursor;
    emit this->sgBrowserCommand(mapVar);
}

void QCefWidgetImpl::navigateToUrl(const QString& url) {
  if (!browserCreated_) {

#if (QT_VERSION > QT_VERSION_CHECK(5,12,0))
      QMetaObject::invokeMethod(
          pWidget_,
          [this, url]() {
              if (!createBrowser(url)) {
                  Q_ASSERT(false);
              }
  },
          Qt::QueuedConnection);
#else
      QVariantMap mapVar;
      mapVar["sCmdType"] = "CreateBrowser";
      mapVar["sUrl"] = url;
      emit this->sgBrowserCommand(mapVar);
#endif
    return;
  }

  CefString strUrl;
  strUrl.FromWString(url.toStdWString());
  Q_ASSERT(pQCefViewHandler_);
  if (pQCefViewHandler_ && pQCefViewHandler_->browser() &&
      pQCefViewHandler_->browser()->GetMainFrame()) {
    pQCefViewHandler_->browser()->GetMainFrame()->LoadURL(strUrl);
  }
}

bool QCefWidgetImpl::canGoBack() {
  if (pQCefViewHandler_ && pQCefViewHandler_->browser())
    return pQCefViewHandler_->browser()->CanGoBack();

  return false;
}

bool QCefWidgetImpl::canGoForward() {
  if (pQCefViewHandler_ && pQCefViewHandler_->browser())
    return pQCefViewHandler_->browser()->CanGoForward();

  return false;
}

void QCefWidgetImpl::goBack() {
  if (pQCefViewHandler_ && pQCefViewHandler_->browser())
    pQCefViewHandler_->browser()->GoBack();
}

void QCefWidgetImpl::goForward() {
  if (pQCefViewHandler_ && pQCefViewHandler_->browser())
    pQCefViewHandler_->browser()->GoForward();
}

bool QCefWidgetImpl::isLoadingBrowser() {
  if (pQCefViewHandler_ && pQCefViewHandler_->browser())
    return pQCefViewHandler_->browser()->IsLoading();

  return false;
}

void QCefWidgetImpl::reloadBrowser(bool bIgnoreCache) {
  if (pQCefViewHandler_ && pQCefViewHandler_->browser())
    if (bIgnoreCache)
      pQCefViewHandler_->browser()->Reload();
    else
      pQCefViewHandler_->browser()->ReloadIgnoreCache();
}

void QCefWidgetImpl::stopLoadBrowser() {
  if (pQCefViewHandler_ && pQCefViewHandler_->browser())
    pQCefViewHandler_->browser()->StopLoad();
}

bool QCefWidgetImpl::triggerEvent(const QString& name, const QCefEvent& event) {
  if (!name.isEmpty()) {
    return sendEventNotifyMessage(name, event);
  }

  return false;
}

bool QCefWidgetImpl::responseCefQuery(const QCefQuery& query) {
  if (pQCefViewHandler_) {
    CefString res;
    res.FromString(query.response().toStdString());
    return pQCefViewHandler_->responseQuery(
        query.id(), query.result(), res, query.error());
  }
  return false;
}

void QCefWidgetImpl::executeJavascript(const QString& javascript) {
  CefString strJavascript;
  strJavascript.FromWString(javascript.toStdWString());

  if (pQCefViewHandler_ && pQCefViewHandler_->browser() &&
      pQCefViewHandler_->browser()->GetMainFrame()) {
    pQCefViewHandler_->browser()->GetMainFrame()->ExecuteJavaScript(
        strJavascript, pQCefViewHandler_->browser()->GetMainFrame()->GetURL(), 0);
  }
}

void QCefWidgetImpl::mainFrameLoadFinishedNotify() {
  //visibleChangedNotify(pWidget_->isVisible());
}

bool QCefWidgetImpl::sendEventNotifyMessage(const QString& name,
                                            const QCefEvent& event) {
  if (!pQCefViewHandler_)
    return false;
  CefRefPtr<CefProcessMessage> msg =
      CefProcessMessage::Create(TRIGGEREVENT_NOTIFY_MESSAGE);
  CefRefPtr<CefListValue> arguments = msg->GetArgumentList();

  int idx = 0;
  CefString eventName = name.toStdWString();
  arguments->SetString(idx++, eventName);

  CefRefPtr<CefDictionaryValue> dict = CefDictionaryValue::Create();

  CefString cefStr;
  cefStr.FromWString(event.objectName().toStdWString());
  dict->SetString("eventname", cefStr);

  QList<QByteArray> keys = event.dynamicPropertyNames();
  for (QByteArray& key : keys) {
    QVariant value = event.property(key.data());
    if (value.type() == QMetaType::Bool)
      dict->SetBool(key.data(), value.toBool());
    else if (value.type() == QMetaType::Int || value.type() == QMetaType::UInt)
      dict->SetInt(key.data(), value.toInt());
    else if (value.type() == QMetaType::Double)
      dict->SetDouble(key.data(), value.toDouble());
    else if (value.type() == QMetaType::QString) {
      cefStr.FromWString(value.toString().toStdWString());
      dict->SetString(key.data(), cefStr);
    }
    else {
      Q_ASSERT(false);
    }
  }

  arguments->SetDictionary(idx++, dict);

  return pQCefViewHandler_->triggerEvent(msg);
}

QWidget* QCefWidgetImpl::getWidget() {
  Q_ASSERT(pWidget_);
  return pWidget_;
}

WidgetType QCefWidgetImpl::getWidgetType() {
  return vt_;
}

QRect QCefWidgetImpl::rect() {
  QRect rc;
  if (pWidget_)
    rc = pWidget_->rect();
  return rc;
}

bool QCefWidgetImpl::nativeEvent(const QByteArray& eventType,
                                 void* message,
                                 long* result) {
#if (defined Q_OS_WIN32 || defined Q_OS_WIN64)
  if (eventType == "windows_generic_MSG") {
    MSG* pMsg = (MSG*)message;
    if (!pMsg || !pWidget_)
      return false;

    if (pMsg->message == WM_SYSCHAR || pMsg->message == WM_SYSKEYDOWN ||
        pMsg->message == WM_SYSKEYUP || pMsg->message == WM_KEYDOWN ||
        pMsg->message == WM_KEYUP || pMsg->message == WM_CHAR) {
      if (pWidget_->isActiveWindow() && pWidget_->hasFocus()) {
        if (pCefUIEventWin_)
          pCefUIEventWin_->OnKeyboardEvent(
              pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
      }
    }
    else if (pMsg->message == WM_MOUSEMOVE || pMsg->message == WM_MOUSEWHEEL ||
             pMsg->message == WM_MOUSELEAVE ||
             pMsg->message == WM_LBUTTONDOWN ||
             pMsg->message == WM_RBUTTONDOWN ||
             pMsg->message == WM_MBUTTONDOWN || pMsg->message == WM_LBUTTONUP ||
             pMsg->message == WM_RBUTTONUP || pMsg->message == WM_MBUTTONUP) {
      if (pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_RBUTTONDOWN ||
          pMsg->message == WM_MBUTTONDOWN) {
        pWidget_->setFocus();
      }
      else if (pMsg->message == WM_MOUSELEAVE) {
        // winsoft666:
        // WM_MOUSELEAVE message will be triggered when using third-party input method input,
        // at this time you can not release the focus, otherwise you can not enter.
        // pCefView_->clearFocus();
        //
        if (::GetCapture() == pMsg->hwnd)
          ReleaseCapture();

        // winsoft666:
        //::SetCursor(NULL);
        SetClassLongPtr((HWND)pWidget_->winId(), GCLP_HCURSOR, NULL);
      }

      if (pWidget_->isActiveWindow()) {
        if (pCefUIEventWin_)
          pCefUIEventWin_->OnMouseEvent(
              pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
      }
    }
    else if (pMsg->message == WM_SIZE) {
      if (browserSetting_.osrEnabled) {
        // winsoft666:
        // Old cef version maybe has some bugs about resize with dpi scale.
        // https://bitbucket.org/chromiumembedded/cef/issues/2823/osr-on-a-monitor-at-125x-scale-onpaint
        // https://bitbucket.org/chromiumembedded/cef/issues/2733/viz-osr-might-be-causing-some-graphic
        // https://bitbucket.org/chromiumembedded/cef/issues/2833/osr-gpu-consume-cpu-and-may-not-draw
        if (pCefUIEventWin_) {
          pCefUIEventWin_->OnSize(pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
        }
      }
      else {
        CefWindowHandle cefhwnd = NULL;
        if (this->browser() && this->browser()->GetHost())
          cefhwnd = this->browser()->GetHost()->GetWindowHandle();
        if (cefhwnd) {
          // Don't directly use QWidget:rect() function, since qt's bug: https://bugreports.qt.io/browse/QTBUG-89646
          RECT rc = {0, 0, 0, 0};
          if (::GetWindowRect((HWND)pWidget_->winId(), &rc)) {
            ::SetWindowPos(cefhwnd,
                           NULL,
                           0,
                           0,
                           rc.right - rc.left,
                           rc.bottom - rc.top,
                           SWP_NOZORDER);
            qDebug().noquote() << "Cef render:" << cefhwnd << ", size:" << rc.right - rc.left << "," << rc.bottom - rc.top;
          }
        }
      }
    }
    else if (pMsg->message == WM_TOUCH) {
      if (pCefUIEventWin_)
        pCefUIEventWin_->OnTouchEvent(
            pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
    }
    else if (pMsg->message == WM_SETFOCUS || pMsg->message == WM_KILLFOCUS) {
      if (pCefUIEventWin_)
        pCefUIEventWin_->OnFocusEvent(
            pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
    }
    else if (pMsg->message == WM_CAPTURECHANGED ||
             pMsg->message == WM_CANCELMODE) {
      if (pCefUIEventWin_)
        pCefUIEventWin_->OnCaptureLostEvent(
            pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
    }
    else if (pMsg->message == WM_IME_SETCONTEXT ||
             pMsg->message == WM_IME_STARTCOMPOSITION ||
             pMsg->message == WM_IME_COMPOSITION ||
             pMsg->message == WM_IME_ENDCOMPOSITION) {
      if (pWidget_->isActiveWindow() && pWidget_->hasFocus()) {
        if (pCefUIEventWin_)
          pCefUIEventWin_->OnIMEEvent(
              pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
        if (pMsg->message != WM_IME_ENDCOMPOSITION)
          return true;
      }
    }
  }
#elif __APPLE__
#if TARGET_IPHONE_SIMULATOR
#elif TARGET_OS_IPHONE
#elif TARGET_OS_MAC
#else
#error "Unknown Apple platform"
#endif
#elif __linux__
#elif __unix__
#elif defined(_POSIX_VERSION)
#else
#error "Unknown compiler"
#endif
  return false;
}

CefRefPtr<CefBrowserHost> QCefWidgetImpl::getCefBrowserHost() {
  if (!pQCefViewHandler_)
    return nullptr;

  CefRefPtr<CefBrowser> browser = pQCefViewHandler_->browser();
  if (!browser)
    return nullptr;

  return browser->GetHost();
}

void QCefWidgetImpl::simulateResizeEvent() {
  // Simulate a resize event
  //
  QSize curSize = pWidget_->size();
  QSize s = curSize;
  int minStep = 2;
  if (curSize.width() - minStep <= 0)
    s.setWidth(curSize.width() + minStep);
  else
    s.setWidth(curSize.width() - minStep);

  if (curSize.height() - minStep <= 0)
    s.setHeight(curSize.height() + minStep);
  else
    s.setHeight(curSize.height() - minStep);

  pWidget_->resize(s);
  pWidget_->resize(curSize);
}

bool QCefWidgetImpl::paintEventHandle(QPaintEvent* event) {
  if (!pQCefViewHandler_ || !pWidget_ || browserClosing_)
    return false;

  Q_ASSERT(vt_ == WT_Widget);
  float scale = deviceScaleFactor_;
  QPainter paint(pWidget_);

  QRectF updateRect = event->rect();
  QRectF srcRect = {updateRect.x() * scale,
                    updateRect.y() * scale,
                    updateRect.width() * scale,
                    updateRect.height() * scale};

  CefRenderBuffer* pRenderBuffer = pQCefViewHandler_->lockViewBuffer();
  if (pRenderBuffer) {
    QImage image(pRenderBuffer->buffer.get(),
                 pRenderBuffer->width,
                 pRenderBuffer->height,
                 QImage::Format_ARGB32);
    paint.drawImage(updateRect, image, srcRect);
  }
  pQCefViewHandler_->unlockViewBuffer();

  if (pQCefViewHandler_->isPopupShow()) {
    CefRenderBuffer* pPopupImageParam = pQCefViewHandler_->lockPopupBuffer();
    if (pPopupImageParam) {
      QImage image(pPopupImageParam->buffer.get(),
                   pPopupImageParam->width,
                   pPopupImageParam->height,
                   QImage::Format_ARGB32);
      paint.drawImage(updateRect, image, srcRect);
    }
    pQCefViewHandler_->unlockPopupBuffer();
  }

  return true;
}
#ifndef QT_NO_OPENGL
bool QCefWidgetImpl::openGLPaintEventHandle(QPaintEvent* event) {
  if (!pQCefViewHandler_ || browserClosing_)
    return false;

  if (!pWidget_)
    return false;

  Q_ASSERT(vt_ == WT_OpenGLWidget);
  QOpenGLWidget* pCefWidget = qobject_cast<QCefOpenGLWidget*>(pWidget_);
  if (!pCefWidget)
    return false;

  float scale = deviceScaleFactor_;
  QPainter paint(pCefWidget);

  CefRenderBuffer* pDrawImageParam = pQCefViewHandler_->lockViewBuffer();
  if (pDrawImageParam) {
    QImage image(pDrawImageParam->buffer.get(),
                 pDrawImageParam->width,
                 pDrawImageParam->height,
                 QImage::Format_ARGB32);

    QRect destRect(pDrawImageParam->x,
                   pDrawImageParam->y,
                   pDrawImageParam->width / scale,
                   pDrawImageParam->height / scale);
    paint.drawImage(destRect, image);
  }
  pQCefViewHandler_->unlockViewBuffer();

  if (pQCefViewHandler_->isPopupShow()) {
    CefRenderBuffer* pPopupImageParam = pQCefViewHandler_->lockPopupBuffer();
    if (pPopupImageParam) {
      QImage image(pPopupImageParam->buffer.get(),
                   pPopupImageParam->width,
                   pPopupImageParam->height,
                   QImage::Format_ARGB32);

      QRect destRect(pPopupImageParam->x,
                     pPopupImageParam->y,
                     pPopupImageParam->width / scale,
                     pPopupImageParam->height / scale);
      paint.drawImage(destRect, image);
    }
    pQCefViewHandler_->unlockPopupBuffer();
  }

  return true;
}
#endif

void QCefWidgetImpl::visibleChangedNotify(bool visible) {
  if (browserClosing_)
    return;
  if (!browserSetting_.osrEnabled)
    return;
  CefRefPtr<CefBrowserHost> host = getCefBrowserHost();
  if (!host)
    return;
  if (!browserSetting_.osrQWidgetNoSysWnd) {
      if (visible) {
          host->WasHidden(false);
          host->SendFocusEvent(true);
      }
      else {
          host->SendFocusEvent(false);
          host->WasHidden(true);
      }
  }
  else {
      if (visible) {
          host->SendFocusEvent(true);
          host->WasResized();
      }
      else {
          host->SendFocusEvent(false);
      }
  }
}

bool QCefWidgetImpl::setOsrNoSysWndEnabled(bool b)
{
    if (browserCreated_)
        return false;
    browserSetting_.osrQWidgetNoSysWnd = b;
    return true;
}

bool QCefWidgetImpl::isOsrNoSysWndEnabled()
{
    return browserSetting_.osrQWidgetNoSysWnd;
}

bool QCefWidgetImpl::setOsrEnabled(bool b) {
  if (browserCreated_)
    return false;
  browserSetting_.osrEnabled = b;
  if (!browserSetting_.osrEnabled)
  {
      setOsrNoSysWndEnabled(false);
  }
  return true;
}

bool QCefWidgetImpl::isOsrEnabled() {
  return browserSetting_.osrEnabled;
}

void QCefWidgetImpl::setContextMenuEnabled(bool b) {
  browserSetting_.contextMenuEnabled = b;
}

void QCefWidgetImpl::setAutoShowDevToolsContextMenu(bool b) {
  browserSetting_.autoShowDevToolsContextMenu = b;
}

void QCefWidgetImpl::setAllowExecuteUnknownProtocolViaOS(bool b) {
  browserSetting_.executeUnknownProtocolViaOS = b;
}

void QCefWidgetImpl::setAutoDestoryCefWhenCloseEvent(bool b) {
  browserSetting_.autoDestroyCefWhenCloseEvent = b;
}

void QCefWidgetImpl::setFPS(int fps) {
  browserSetting_.fps = fps;
  CefRefPtr<CefBrowser> b = browser();
  if (b && b->GetHost()) {
    b->GetHost()->SetWindowlessFrameRate(fps);
  }
}

void QCefWidgetImpl::setConsoleLogPath(const QString& path) {
  browserSetting_.consoleLogPath = path;
}

const QCefBrowserSetting& QCefWidgetImpl::browserSetting() const {
  return browserSetting_;
}

void QCefWidgetImpl::setBrowserBackgroundColor(const QColor& color) {
  browserSetting_.backgroundColor = color;
}

void QCefWidgetImpl::updateCefWidget(const QRect& region) {
  if (pWidget_) {
    pWidget_->update(region);
  }
}

void QCefWidgetImpl::setBrowserClosing(bool b) {
  browserClosing_ = b;
}

CefRefPtr<CefBrowser> QCefWidgetImpl::browser() const {
  if (!pQCefViewHandler_) {
    return nullptr;
  }
  return pQCefViewHandler_->browser();
}

float QCefWidgetImpl::deviceScaleFactor() {
  return deviceScaleFactor_;
}

bool QCefWidgetImpl::addResourceProvider(QCefResourceProvider* provider,
                                         const QString& identifier) {
  if (pQCefViewHandler_) {
    return pQCefViewHandler_->addResourceProvider(provider, identifier);
  }

  todoAddProviders_[identifier] = provider;
  return true;
}

bool QCefWidgetImpl::removeResourceProvider(const QString& identifier) {
  if (pQCefViewHandler_) {
    return pQCefViewHandler_->removeResourceProvider(identifier);
  }

  auto it = todoAddProviders_.find(identifier);
  if (it == todoAddProviders_.end())
    return false;

  todoAddProviders_.erase(it);

  return true;
}

bool QCefWidgetImpl::removeAllResourceProvider() {
  if (pQCefViewHandler_) {
    return pQCefViewHandler_->removeAllResourceProvider();
  }

  todoAddProviders_.clear();
  return true;
}
