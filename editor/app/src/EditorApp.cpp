#include <projectunity/editor/EditorApp.hpp>

#include <projectunity/core/Log.hpp>
#include <projectunity/editor/MainWindow.hpp>

#include <QApplication>
#include <QFile>
#include <QString>
#include <QStringList>

#include <iostream>

namespace projectunity::editor {

int runEditor(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName("ProjectUnity Editor");
    QApplication::setOrganizationName("ProjectUnity");
    QApplication::setOrganizationDomain("projectunity.local");

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

    mainWindow.show();

    core::logInfo(core::LogCategory::Editor, "Editor application started");
    return QApplication::exec();
}

} // namespace projectunity::editor
