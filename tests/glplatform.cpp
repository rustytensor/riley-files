// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// A window system for the tests that draw in 3D: no display and no GPU, yet real OpenGL. Windows
// are drawn by Mesa's software renderer (llvmpipe) through EGL's surfaceless platform, each into a
// buffer of its own size, so grabbing a window reads back what OpenGL drew. Everything else (keys,
// focus, the screen) works as on the "offscreen" platform the other tests use.
//
// A test picks it before it makes its application: qputenv("QT_QPA_PLATFORM", "rileytestgl").

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QWindow>
#include <QtGui/private/qeglconvenience_p.h>
#include <QtGui/private/qeglpbuffer_p.h>
#include <QtGui/private/qeglplatformcontext_p.h>
#include <QtGui/private/qgenericunixfontdatabase_p.h>
#include <QtGui/private/qunixeventdispatcher_qpa_p.h>
#include <qpa/qplatformbackingstore.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformintegrationplugin.h>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <memory>

namespace {

class Screen : public QPlatformScreen
{
public:
    QRect geometry() const override { return {0, 0, 1600, 1200}; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_ARGB32_Premultiplied; }
    QSizeF physicalSize() const override { return {423.0, 317.0}; } // 96 dots per inch
};

class Window : public QPlatformWindow
{
public:
    Window(QWindow *window, EGLDisplay display)
        : QPlatformWindow(window), m_display(display), m_id(++s_lastId)
    {
        QPlatformWindow::setGeometry(initialGeometry(window, window->geometry(), 640, 480));
    }

    ~Window() override
    {
        if (m_surface != EGL_NO_SURFACE)
            eglDestroySurface(m_display, m_surface);
    }

    WId winId() const override { return m_id; }

    void setGeometry(const QRect &rect) override
    {
        QPlatformWindow::setGeometry(rect);
        QWindowSystemInterface::handleGeometryChange(window(), rect);
        if (m_visible)
            expose();
    }

    void setVisible(bool visible) override
    {
        if (visible == m_visible)
            return;
        m_visible = visible;
        if (visible) {
            QWindowSystemInterface::handleGeometryChange(window(), geometry());
            expose();
        } else {
            QWindowSystemInterface::handleExposeEvent(window(), QRegion());
        }
    }

    void requestActivateWindow() override
    {
        if (m_visible)
            QWindowSystemInterface::handleFocusWindowChanged(window(), Qt::ActiveWindowFocusReason);
    }

    // The buffer OpenGL draws this window into, made again when the window changes size.
    EGLSurface surface(EGLConfig config)
    {
        const QSize size = geometry().size().expandedTo(QSize(1, 1)) * window()->devicePixelRatio();
        if (m_surface != EGL_NO_SURFACE && size == m_size)
            return m_surface;
        if (m_surface != EGL_NO_SURFACE)
            eglDestroySurface(m_display, m_surface);
        const EGLint attributes[] = {EGL_WIDTH, size.width(), EGL_HEIGHT, size.height(), EGL_NONE};
        m_surface = eglCreatePbufferSurface(m_display, config, attributes);
        m_size = size;
        return m_surface;
    }

private:
    void expose()
    {
        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
    }

    static inline WId s_lastId = 0;
    EGLDisplay m_display;
    WId m_id;
    bool m_visible = false;
    EGLSurface m_surface = EGL_NO_SURFACE;
    QSize m_size;
};

class Context : public QEGLPlatformContext
{
public:
    using QEGLPlatformContext::QEGLPlatformContext;

protected:
    EGLSurface eglSurfaceForPlatformSurface(QPlatformSurface *surface) override
    {
        if (surface->surface()->surfaceClass() == QSurface::Window)
            return static_cast<Window *>(surface)->surface(eglConfig());
        return static_cast<QEGLPbuffer *>(surface)->pbuffer();
    }
};

// Raster windows (none in the tests that use this, but Qt may ask) draw into an image.
class BackingStore : public QPlatformBackingStore
{
public:
    using QPlatformBackingStore::QPlatformBackingStore;
    QPaintDevice *paintDevice() override { return &m_image; }
    void flush(QWindow *, const QRegion &, const QPoint &) override {}
    void resize(const QSize &size, const QRegion &) override
    {
        const qreal ratio = window()->devicePixelRatio();
        if (m_image.size() != size * ratio) {
            m_image = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
            m_image.setDevicePixelRatio(ratio);
        }
    }
    QImage toImage() const override { return m_image; }

private:
    QImage m_image;
};

class Integration : public QPlatformIntegration
{
public:
    Integration()
    {
        const auto platformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (platformDisplay)
            m_display = platformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        EGLint major = 0;
        EGLint minor = 0;
        if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, &major, &minor))
            qFatal("rileytestgl: EGL's surfaceless platform cannot start");
        m_screen = new Screen;
        QWindowSystemInterface::handleScreenAdded(m_screen);
    }

    ~Integration() override
    {
        QWindowSystemInterface::handleScreenRemoved(m_screen);
        eglTerminate(m_display);
    }

    bool hasCapability(Capability cap) const override
    {
        switch (cap) {
        case OpenGL:
        case RhiBasedRendering:
        case ThreadedPixmaps:
        case MultipleWindows:
        case WindowActivation:
        case NonFullScreenWindows:
            return true;
        default:
            return false;
        }
    }

    QPlatformWindow *createPlatformWindow(QWindow *window) const override
    {
        return new Window(window, m_display);
    }

    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override
    {
        return new BackingStore(window);
    }

    QPlatformOpenGLContext *createPlatformOpenGLContext(QOpenGLContext *context) const override
    {
        QSurfaceFormat format = context->format();
        EGLConfig config = q_configFromGLFormat(m_display, format, false, EGL_PBUFFER_BIT);
        return new Context(format, context->shareHandle(), m_display, &config);
    }

    QPlatformOffscreenSurface *createPlatformOffscreenSurface(QOffscreenSurface *surface) const override
    {
        return new QEGLPbuffer(m_display, surface->requestedFormat(), surface);
    }

    QAbstractEventDispatcher *createEventDispatcher() const override
    {
        return new QUnixEventDispatcherQPA;
    }

    QPlatformFontDatabase *fontDatabase() const override { return &m_fonts; }
    // The font database asks it about the screen's antialiasing; it has nothing to add.
    QPlatformNativeInterface *nativeInterface() const override { return &m_native; }

private:
    EGLDisplay m_display = EGL_NO_DISPLAY;
    Screen *m_screen = nullptr;
    mutable QGenericUnixFontDatabase m_fonts;
    mutable QPlatformNativeInterface m_native;
};

} // namespace

class RileyTestGlPlugin : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "glplatform.json")

public:
    QPlatformIntegration *create(const QString &key, const QStringList &) override
    {
        if (key.compare(QLatin1String("rileytestgl"), Qt::CaseInsensitive) == 0)
            return new Integration;
        return nullptr;
    }
};

#include "glplatform.moc"
