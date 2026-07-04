#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	MainWindow w;
	w.resize(1200, 700);
	w.show();

	return app.exec();
}
