#ifndef QCEF_WIDGET_IMPLEMENT_H_
#define QCEF_WIDGET_IMPLEMENT_H_
#pragma once
#include "CefBrowserApp/QCefBrowserHandler.h"
#include "Include/QCefEvent.h"
#include "Include/QCefQuery.h"
#include "QCefWidgetUIEventHandlerWin.h"
#include <QString>
#include <QWidget>
#include <QScreen>
#include "QCefBrowserSetting.h"

enum WidgetType { WT_Widget = 0, WT_OpenGLWidget };

class QCefWidgetImpl : public QObject {
  Q_OBJECT
public:
  explicit QCefWidgetImpl(WidgetType vt, QWidget *pWidget);
  ~QCefWidgetImpl();

public:
  void navigateToUrl(const QString &url);

  bool canGoBack();
  bool canGoForward();
  void goBack();
  void goForward();
  bool isLoadingBrowser();
  void reloadBrowser(bool bIgnoreCache);
  void stopLoadBrowser();

  bool triggerEvent(const QString &name, const QCefEvent &event);
  bool responseCefQuery(const QCefQuery &query);
  void executeJavascript(const QString &javascript);

  bool createBrowser(const QString &url);
  bool createDevTools(CefRefPtr<CefBrowser> targetBrowser);

  void mainFrameLoadFinishedNotify();

  void browserClosingNotify(CefRefPtr<CefBrowser> browser);
  void browserCreatedNotify(CefRefPtr<CefBrowser> browser);
  void browserContextCreatedNotify(CefRefPtr<CefBrowser> browser);
  void browserDestoryedNotify(CefRefPtr<CefBrowser> browser);
  void draggableRegionsChangedNotify(CefRefPtr<CefBrowser> browser, const std::vector<CefDraggableRegion> &regions);
  void imeCompositionRangeChangedNotify(CefRefPtr<CefBrowser> browser, const CefRange &selection_range, const CefRenderHandler::RectList &character_bounds);

  QWidget *getWidget();
  WidgetType getWidgetType();
  QRect rect();

  bool nativeEvent(const QByteArray &eventType, void *message, long *result);
  bool paintEventHandle(QPaintEvent *event);
#ifndef QT_NO_OPENGL
  bool openGLPaintEventHandle(QPaintEvent *event);
#endif
  void visibleChangedNotify(bool visible);

  void setBrowserBackgroundColor(const QColor &color);
  bool setOsrEnabled(bool b);
  bool isOsrEnabled();
  void setContextMenuEnabled(bool b);
  void setAutoShowDevToolsContextMenu(bool b);
  void setAllowExecuteUnknownProtocolViaOS(bool b);
  void setAutoDestoryCefWhenCloseEvent(bool b);
  void setFPS(int fps);
  void setConsoleLogPath(const QString &path);
  const QCefBrowserSetting &browserSetting() const;

  void updateCefWidget(const QRect &region);

  void setBrowserClosing(bool b);

  CefRefPtr<CefBrowser> browser() const;

  float deviceScaleFactor();

  bool addResourceProvider(QCefResourceProvider *provider, const QString &identifier);
  bool removeResourceProvider(const QString &identifier);
  bool removeAllResourceProvider();

protected:
  bool sendEventNotifyMessage(const QString &name, const QCefEvent &event);
  CefRefPtr<CefBrowserHost> getCefBrowserHost();
  void simulateResizeEvent();

private:
    bool eventFilter(QObject *obj, QEvent *event) override;

  static LRESULT CALLBACK SubclassedWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
  static void subclassWindow(HWND hWnd, HWND hQCefWidgetHwnd, QWidget* pTopLevelWidget);
  static void unSubclassWindow(HWND hWnd);
  static BOOL CALLBACK EnumWindowsProc4Subclass(HWND hwnd, LPARAM lParam);
  static BOOL CALLBACK EnumWindowsProc4Unsubclass(HWND hwnd, LPARAM lParam);

private slots:
  void onScreenChanged(QScreen *screen);
  void onScreenLogicalDotsPerInchChanged();
private:
  WidgetType vt_;
  QWidget *pWidget_;
  QWidget *pTopWidget_;

  CefRefPtr<QCefBrowserHandler> pQCefViewHandler_;
  std::shared_ptr<QCefWidgetUIEventHandlerWin> pCefUIEventWin_;

  bool browserClosing_;
  bool browserCreated_;

  QCefBrowserSetting browserSetting_;

  HRGN draggableRegion_;

  float deviceScaleFactor_;

  QMap<QString, QCefResourceProvider*> todoAddProviders_;
};
#endif