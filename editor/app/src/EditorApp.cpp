#include <projectunity/editor/EditorApp.hpp>

#include <projectunity/core/Log.hpp>
#include <projectunity/editor/MainWindow.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QPalette>
#include <QString>
#include <QStringList>

#include <iostream>

namespace projectunity::editor {
namespace {

void applyDarkWindowFrame(MainWindow& window)
{
#ifdef _WIN32
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    constexpr DWORD useImmersiveDarkMode = 20;
    constexpr DWORD useImmersiveDarkModeBefore20H1 = 19;

    const auto module = LoadLibraryW(L"dwmapi.dll");
    if (module == nullptr) {
        return;
    }

    const auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(module, "DwmSetWindowAttribute"));
    if (setWindowAttribute != nullptr) {
        const BOOL enabled = TRUE;
        auto* handle = reinterpret_cast<HWND>(window.winId());
        if (setWindowAttribute(handle, useImmersiveDarkMode, &enabled, sizeof(enabled)) != S_OK) {
            (void)setWindowAttribute(handle, useImmersiveDarkModeBefore20H1, &enabled, sizeof(enabled));
        }
    }

    FreeLibrary(module);
#else
    (void)window;
#endif
}

} // namespace

int runEditor(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName("ProjectUnity Editor");
    QApplication::setOrganizationName("ProjectUnity");
    QApplication::setOrganizationDomain("projectunity.local");

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(26, 29, 34));
    palette.setColor(QPalette::WindowText, QColor(226, 231, 240));
    palette.setColor(QPalette::Base, QColor(20, 23, 28));
    palette.setColor(QPalette::AlternateBase, QColor(29, 33, 39));
    palette.setColor(QPalette::ToolTipBase, QColor(38, 44, 52));
    palette.setColor(QPalette::ToolTipText, QColor(238, 242, 248));
    palette.setColor(QPalette::Text, QColor(226, 231, 240));
    palette.setColor(QPalette::Button, QColor(42, 47, 56));
    palette.setColor(QPalette::ButtonText, QColor(238, 242, 248));
    palette.setColor(QPalette::BrightText, QColor(255, 118, 118));
    palette.setColor(QPalette::Highlight, QColor(45, 126, 214));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::Link, QColor(99, 164, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(130, 140, 154));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(105, 113, 126));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(105, 113, 126));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(105, 113, 126));
    app.setPalette(palette);

    QFile themeFile(":/themes/unity_dark.qss");
    if (themeFile.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(QString::fromUtf8(themeFile.readAll()));
    } else {
        core::logWarning(core::LogCategory::Editor, "Unable to load editor theme resource");
    }

    MainWindow mainWindow;
    mainWindow.resize(1440, 900);

    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        mainWindow.show();
        applyDarkWindowFrame(mainWindow);
        QApplication::processEvents();

        QString errorMessage;
        const bool passed = mainWindow.runSmokeChecks(&errorMessage);
        mainWindow.close();
        QApplication::processEvents();

        if (!passed) {
            core::logError(core::LogCategory::Editor, errorMessage.toStdString());
            std::cerr << errorMessage.toStdString() << '\n';
            return 2;
        }

        core::logInfo(core::LogCategory::Editor, "Editor smoke test passed");
        return 0;
    }

    if (QCoreApplication::arguments().contains(QStringLiteral("--phase6-visual-smoke"))) {
        mainWindow.show();
        applyDarkWindowFrame(mainWindow);
        QApplication::processEvents();

        QString errorMessage;
        const bool passed = mainWindow.runPhase6VisualChecks(&errorMessage);
        mainWindow.close();
        QApplication::processEvents();

        if (!passed) {
            core::logError(core::LogCategory::Editor, errorMessage.toStdString());
            std::cerr << errorMessage.toStdString() << '\n';
            return 2;
        }

        core::logInfo(core::LogCategory::Editor, "Phase 6 visual smoke passed");
        return 0;
    }

    if (QCoreApplication::arguments().contains(QStringLiteral("--phase6-culling-profile"))) {
        mainWindow.show();
        applyDarkWindowFrame(mainWindow);
        QApplication::processEvents();

        QString errorMessage;
        const bool passed = mainWindow.runPhase6CullingProfile(&errorMessage);
        mainWindow.close();
        QApplication::processEvents();

        if (!passed) {
            core::logError(core::LogCategory::Editor, errorMessage.toStdString());
            std::cerr << errorMessage.toStdString() << '\n';
            return 2;
        }

        core::logInfo(core::LogCategory::Editor, "Phase 6 culling profile passed");
        return 0;
    }

    mainWindow.show();
    applyDarkWindowFrame(mainWindow);

    core::logInfo(core::LogCategory::Editor, "Editor application started");
    return QApplication::exec();
}

} // namespace projectunity::editor
