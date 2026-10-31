#include <sailfishapp.h>

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>

#include "chargemonitor.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    ChargeMonitor monitor;
    view->rootContext()->setContextProperty(QStringLiteral("chargescope"), &monitor);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}
